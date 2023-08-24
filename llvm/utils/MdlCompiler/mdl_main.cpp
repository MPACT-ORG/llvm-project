//===- mdl_main.cpp - Top level program for MDL compiler ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Architecture Machine Description Compiler.
// Read in a machine description for an architecture, parse it, do
// semantic error checking, build instruction database, and write it out.
//
//===----------------------------------------------------------------------===//

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "mdl_generate.h"
#include "mdl_visitor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

using namespace llvm;

//-------------------------------------------------------------------------
// Command line flags.
//-------------------------------------------------------------------------
cl::opt<std::string> input_file(cl::Positional, cl::desc("<input file>"));

cl::OptionCategory MdlOutput("Output options");
cl::opt<std::string> output_dir("output_dir", cl::desc("Output directory"),
                                cl::init(""), cl::value_desc("dir"),
                                cl::cat(MdlOutput));
cl::opt<std::string> import_dir("import_dir", cl::desc("Import directory"),
                                cl::init(""), cl::value_desc("dir"),
                                cl::cat(MdlOutput));
cl::opt<bool>
    gen_missing_info("gen_missing_info",
                     cl::desc("Generate missing info for instructions"),
                     cl::cat(MdlOutput), cl::init(false));

cl::OptionCategory MdlDiags("Diagnostic options");
cl::opt<bool> warnings("warnings", cl::desc("Print warnings"),
                       cl::cat(MdlDiags), cl::init(false));
cl::opt<bool> fatal_warnings("fatal_warnings",
                             cl::desc("Treat warnings as errors"),
                             cl::cat(MdlDiags), cl::init(false));
cl::opt<bool>
    check_usage("check_usage",
                cl::desc("Check subunit, reference, and resource usage"),
                cl::cat(MdlDiags), cl::init(false));
cl::opt<bool> check_all_operands(
    "check_all_operands",
    cl::desc("Check references to all operands - not just registers"),
    cl::cat(MdlDiags), cl::init(false));

cl::OptionCategory MdlDebug("Debugging options");
cl::opt<bool> dump_resources("dump_resources", cl::desc("Dump resource ids"),
                             cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> dump_fus("dump_fus",
                       cl::desc("Dump functional unit instantiations"),
                       cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> dump_sus("dump_sus", cl::desc("Dump subunit instantiations"),
                       cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> dump_spec("dump_spec", cl::desc("Dump entire mdl specification"),
                        cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> dump_instr("dump_instr", cl::desc("Dump instruction information"),
                         cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> dump_preds("dump_preds", cl::desc("Dump user-defined predicates"),
                         cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> generate_llvm_defs("gen_llvm_defs",
                                 cl::desc("Generate LLVM definitions"),
                                 cl::init(false), cl::cat(MdlDebug));

//-------------------------------------------------------------------------
// Process command lines and do some cursory error checking.
//-------------------------------------------------------------------------
static void usage(int argc, char **argv) {
  if (argc < 2) {
    llvm::errs() << "Usage: mdl [flags] <input-file>\n"
                    "    --help: print program options\n";
    exit(EXIT_FAILURE);
  }

  // If user specifies check_all_options, do some other checking too.
  if (check_all_operands)
    check_usage = true;

  // Disable some flags we don't particularly want to see.
  cl::getRegisteredOptions()["help-list"]->setHiddenFlag(cl::ReallyHidden);
  cl::getRegisteredOptions()["version"]->setHiddenFlag(cl::Hidden);
  cl::getRegisteredOptions()["color"]->setHiddenFlag(cl::ReallyHidden);
  cl::ParseCommandLineOptions(argc, argv, "MDL Compiler");

  if (input_file.empty()) {
    llvm::errs() << "Error: no input file\n";
    exit(EXIT_FAILURE);
  }
}

//-------------------------------------------------------------------------
// Parse the input machine description, error check it, build a database
// of all instruction information, and write it out to a C file.
//-------------------------------------------------------------------------
int main(int argc, char **argv) {
  // Process command line options.
  usage(argc, argv);

  // Create object which collects all the information from the input files.
  mpact::mdl::MdlSpec spec(warnings, fatal_warnings);

  //--------------------------------------------------------------------------
  // First Pass: Parse the input file, and build a representation of the
  // entire machine description. Abort if syntax errors found.
  //--------------------------------------------------------------------------
  mpact::mdl::MdlVisitor visitor(spec, import_dir);
  if (!visitor.ProcessInputFile(input_file))
    mpact::mdl::Abort();

  //--------------------------------------------------------------------------
  // Second Pass: Perform semantic checking on the specification, and clean
  // up the representation so that later passes don't have to look things up.
  //--------------------------------------------------------------------------
  // Build dictionaries for functional unit, subunit, and latency templates.
  spec.BuildDictionaries();
  // Create templates for implicitly defined functional units.
  spec.FindImplicitFuncUnitTemplates();
  // Check for duplicate definitions, and for valid pipe phase references.
  spec.CheckForDuplicateDefs();
  // Check resource definitions for correctness.
  spec.CheckResourceDefs();
  // Check subunit references in instructions.
  spec.CheckPipeReferences();
  // Add globally defined resources to each defined CPU.
  spec.PromoteGlobalResources();
  // Promote resource group members to regular resource definitions.
  spec.PromoteResourceGroups();
  // Check that base templates exist and have compatible parameters.
  // Explicitly link templates (fu, su, latency) to their bases.
  spec.CheckTemplateBases();
  // Check that each instantiation refers to a valid template, and they have
  // compatible parameters/arguments.
  spec.CheckInstantiations();

  // Check operand references in instructions, operands, and latencies.
  spec.CheckInstructions();
  spec.CheckOperands();
  // Make sure all instruction have subunits.  If they don't, add a default.
  if (gen_missing_info)
    spec.CheckInstructionSubunits();

  // Check references in latency templates for correctness.
  spec.CheckReferences();
  // Determine if we need to explicitly manage issue slots.
  spec.CheckIssueSlots();
  // Scan predicate table and do logical simplification on predicates.
  spec.SimplifyPredicates();

  // If we've seen any semantic errors, abort.
  if (spec.ErrorsSeen())
    mpact::mdl::Abort();

  // Scan latencies for functional unit specifiers. For each specifier
  // add implicit subunit instances to any CPUs which instantiate the FU.
  spec.TieSubUnitsToFunctionalUnits();

  // A derived subunit should be added to any instruction which is tied to
  // any of the subunit's base subunits.
  spec.TieDerivedSubUnitsToInstructions();

  // Check that the input spec has some basic required components.
  spec.CheckInputStructure();

  //--------------------------------------------------------------------------
  // Third Pass: Build the internal representation of the processor database.
  // This process has several steps:
  //--------------------------------------------------------------------------
  // For each CPU definition, perform the instantiation of each functional
  // unit, which recursively expands subunits and latency instances.
  spec.InstantiateFunctionalUnits();

  // For each CPU, build a dictionary of instances for each used functional
  // unit template.
  spec.BuildFuncUnitInstancesMap();

  // For each instruction, create instruction behaviors for each processor
  // and functional unit that it can run on.
  mpact::mdl::InstructionDatabase instruction_info(output_dir, input_file,
                                                   gen_missing_info, spec);
  // Assign ids to every defined resource.
  spec.AssignResourceIds();
  // Assign pool ids to each pooled resource.
  spec.AssignPoolIds();

  //--------------------------------------------------------------------------
  // Fourth Pass: do consistency checking, dump requested debug information.
  //--------------------------------------------------------------------------
  if (check_usage) {
    // Check for operands that never match a reference.
    instruction_info.CheckUnreferencedOperands(check_all_operands);
    // Check for latency referenced that never match instructions.
    spec.CheckReferenceUse();
    // Also check for subunits that are never instantiated.
    spec.CheckSubunitUse();
    // Look for unreferenced resources.
    spec.CheckResourceUse();
  }

  // If we encountered any errors during database generation, abort.
  if (spec.ErrorsSeen())
    mpact::mdl::Abort();

  // Debug stuff - write out what we know about the machine.
  if (dump_resources)
    spec.DumpResourceIds();
  if (dump_fus)
    spec.DumpFuncUnitInstantiations();
  if (dump_sus)
    spec.DumpSubUnitInstantiations();
  if (dump_spec)
    std::cout << spec.ToString();
  if (dump_instr)
    instruction_info.DumpInstructions();
  if (dump_preds)
    spec.DumpPredicates();

  //--------------------------------------------------------------------------
  // Output Pass: Generate the output files.
  //--------------------------------------------------------------------------
  instruction_info.Write(generate_llvm_defs);
  return EXIT_SUCCESS;
}

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

#include "mdl_parser.h"
#include "mdl_generate.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

using namespace llvm;

//-------------------------------------------------------------------------
// Command line flags.
//-------------------------------------------------------------------------
cl::opt<std::string> InputFile(cl::Positional, cl::desc("<input file>"));

cl::OptionCategory MdlOutput("Output options");
cl::opt<std::string> OutputDir("output-dir", cl::desc("Output directory"),
                                cl::init(""), cl::value_desc("dir"),
                                cl::cat(MdlOutput));
cl::opt<std::string> ImportDir("import-dir", cl::desc("Import directory"),
                                cl::init(""), cl::value_desc("dir"),
                                cl::cat(MdlOutput));
cl::opt<bool>
    GenMissingInfo("gen-missing-info",
                     cl::desc("Generate missing info for instructions"),
                     cl::cat(MdlOutput), cl::init(false));

cl::OptionCategory MdlDiags("Diagnostic options");
cl::opt<bool> Warnings("warnings", cl::desc("Print warnings"),
                       cl::cat(MdlDiags), cl::init(false));
cl::opt<bool> FatalWarnings("fatal-warnings",
                             cl::desc("Treat warnings as errors"),
                             cl::cat(MdlDiags), cl::init(false));
cl::opt<bool>
    CheckUsage("check-usage",
                cl::desc("Check subunit, reference, and resource usage"),
                cl::cat(MdlDiags), cl::init(false));
cl::opt<bool> CheckAllOperands(
    "check-all-operands",
    cl::desc("Check references to all operands - not just registers"),
    cl::cat(MdlDiags), cl::init(false));

cl::OptionCategory MdlDebug("Debugging options");
cl::opt<bool> DumpResources("dump-resources", cl::desc("Dump resource ids"),
                             cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> DumpFus("dump-fus",
                       cl::desc("Dump functional unit instantiations"),
                       cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> DumpSus("dump-sus", cl::desc("Dump subunit instantiations"),
                       cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> DumpSpec("dump-spec", cl::desc("Dump entire mdl specification"),
                        cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> DumpInstr("dump-instr", cl::desc("Dump instruction information"),
                         cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> DumpPreds("dump-preds", cl::desc("Dump user-defined predicates"),
                         cl::init(false), cl::cat(MdlDebug));
cl::opt<bool> GenerateLLVMDefs("gen-llvm-defs",
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
  if (CheckAllOperands)
    CheckUsage = true;

  // Disable some flags we don't particularly want to see.
  cl::getRegisteredOptions()["help-list"]->setHiddenFlag(cl::ReallyHidden);
  cl::getRegisteredOptions()["version"]->setHiddenFlag(cl::Hidden);
  cl::getRegisteredOptions()["color"]->setHiddenFlag(cl::ReallyHidden);
  cl::ParseCommandLineOptions(argc, argv, "MDL Compiler");

  if (InputFile.empty()) {
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
  mpact::mdl::MdlSpec Spec(Warnings, FatalWarnings);

  //--------------------------------------------------------------------------
  // First Pass: Parse the input file, and build a representation of the
  // entire machine description. Abort if syntax errors found.
  //--------------------------------------------------------------------------
  if (!ProcessInputFile(Spec, ImportDir, InputFile))
    mpact::mdl::Abort();

  //--------------------------------------------------------------------------
  // Second Pass: Perform semantic checking on the specification, and clean
  // up the representation so that later passes don't have to look things up.
  //--------------------------------------------------------------------------
  // Build dictionaries for functional unit, subunit, and latency templates.
  Spec.buildDictionaries();
  // Create templates for implicitly defined functional units.
  Spec.findImplicitFuncUnitTemplates();
  // Check for duplicate definitions, and for valid pipe phase references.
  Spec.checkForDuplicateDefs();
  // Check resource definitions for correctness.
  Spec.checkResourceDefs();
  // Check subunit references in instructions.
  Spec.checkPipeReferences();
  // Add globally defined resources to each defined CPU.
  Spec.promoteGlobalResources();
  // Promote resource group members to regular resource definitions.
  Spec.promoteResourceGroups();
  // Check that base templates exist and have compatible parameters.
  // Explicitly link templates (fu, su, latency) to their bases.
  Spec.checkTemplateBases();
  // Check that each instantiation refers to a valid template, and they have
  // compatible parameters/arguments.
  Spec.checkInstantiations();

  // Check operand references in instructions, operands, and latencies.
  Spec.checkInstructions();
  Spec.checkOperands();
  // Make sure all instruction have subunits.  If they don't, add a default.
  if (GenMissingInfo)
    Spec.checkInstructionSubunits();

  // Check references in latency templates for correctness.
  Spec.checkReferences();
  // Determine if we need to explicitly manage issue slots.
  Spec.checkIssueSlots();
  // Scan predicate table and do logical simplification on predicates.
  Spec.simplifyPredicates();

  // If we've seen any semantic errors, abort.
  if (Spec.ErrorsSeen())
    mpact::mdl::Abort();

  // Scan latencies for functional unit specifiers. For each specifier
  // add implicit subunit instances to any CPUs which instantiate the FU.
  Spec.tieSubUnitsToFunctionalUnits();

  // A derived subunit should be added to any instruction which is tied to
  // any of the subunit's base subunits.
  Spec.tieDerivedSubUnitsToInstructions();

  // Check that the input spec has some basic required components.
  Spec.checkInputStructure();

  //--------------------------------------------------------------------------
  // Third Pass: Build the internal representation of the processor database.
  // This process has several steps:
  //--------------------------------------------------------------------------
  // For each CPU definition, perform the instantiation of each functional
  // unit, which recursively expands subunits and latency instances.
  Spec.instantiateFunctionalUnits();

  // For each CPU, build a dictionary of instances for each used functional
  // unit template.
  Spec.buildFuncUnitInstancesMap();

  // For each instruction, create instruction behaviors for each processor
  // and functional unit that it can run on.
  mpact::mdl::InstructionDatabase InstructionInfo(OutputDir, InputFile,
                                                   GenMissingInfo, Spec);
  // Assign ids to every defined resource.
  Spec.assignResourceIds();
  // Assign pool ids to each pooled resource.
  Spec.assignPoolIds();

  //--------------------------------------------------------------------------
  // Fourth Pass: do consistency checking, dump requested debug information.
  //--------------------------------------------------------------------------
  if (CheckUsage) {
    // Check for operands that never match a reference.
    InstructionInfo.checkUnreferencedOperands(CheckAllOperands);
    // Check for latency referenced that never match instructions.
    Spec.checkReferenceUse();
    // Also check for subunits that are never instantiated.
    Spec.checkSubunitUse();
    // Look for unreferenced resources.
    Spec.checkResourceUse();
  }

  // If we encountered any errors during database generation, abort.
  if (Spec.ErrorsSeen())
    mpact::mdl::Abort();

  // Debug stuff - write out what we know about the machine.
  if (DumpResources)
    Spec.dumpResourceIds();
  if (DumpFus)
    Spec.dumpFuncUnitInstantiations();
  if (DumpSus)
    Spec.dumpSubUnitInstantiations();
  if (DumpSpec)
    std::cout << Spec.ToString();
  if (DumpInstr)
    InstructionInfo.dumpInstructions();
  if (DumpPreds)
    Spec.dumpPredicates();

  //--------------------------------------------------------------------------
  // Output Pass: Generate the output files.
  //--------------------------------------------------------------------------
  InstructionInfo.write(GenerateLLVMDefs);
  return EXIT_SUCCESS;
}

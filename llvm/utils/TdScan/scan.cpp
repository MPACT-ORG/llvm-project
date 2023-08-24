//===- scan.cpp - Read in tablegen records and generate and MDL -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Read through an llvm table-gen-generated "reports.txt" input file, and
// compile a table of instructions and their attributes (operands, syntax,
// etc), operand definitions, and register/register class definitions. The MDL
// compiler will use these definitions to tie the architecture description
// back to llvm-defined instructions, registers, and register classes.  We
// write these definitions out in the MDL language (we can optionally generate
// a CSV file that contains all the instruction information).
//
// Since the input file is machine-generated, we assume the file is (very)
// well-formed, but still do some simple sanity checks to avoid crashes.
//
// For each instruction definition, we collect:
//    - its name.
//    - its output operand names and types.
//    - its input operand names and types.
//    - its assembly formatting rule.
//    - its llvm matching pattern.
//    - its parent instruction (if present).
//    - its subunit identifier(s).
//
// For each operand definition, we collect its set of sub-operands, and type.
// We also look for register definitions, and register class definitions.
//
// Based on a command-line switch `gen_arch_spec`, we collect the information
// related to CPU variants, the functional units supported by each variant, and
// the subunits supported by each functional unit.  When using this switch, we
// create a complete MDL description for the architecture that captures all the
// subtargets, functional units, instruction latencies, resource usage, and
// parallel issue rules.
//
//===----------------------------------------------------------------------===//

#include "scan.h"

#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

//-------------------------------------------------------------------------
// Command line options.
//-------------------------------------------------------------------------
cl::opt<std::string> input_file(cl::Positional, cl::desc("<input-file>"));

cl::opt<std::string>
    family_name("family_name", cl::init(""), cl::value_desc("family"),
                cl::desc("Processor family name (default=<input file>)."));
cl::opt<std::string> output_name(
    "output_name", cl::init(""), cl::value_desc("output"),
    cl::desc("Output file name prefix (default=<input file name>)"));

cl::opt<bool> ignore_subunits("ignore_subunits", cl::init(false),
                              cl::desc("Ignore subunits."));
cl::opt<bool>
    gen_arch_spec("gen_arch_spec", cl::init(false),
                  cl::desc("Auto generate the mdl specification. (-g)"));
cl::alias gen_arch_specA("g", cl::desc("Alias for --gen_arch_spec"),
                         cl::aliasopt(gen_arch_spec));

cl::opt<bool> generate_base_subunits(
    "gen_base_subunits", cl::init(false),
    cl::desc("Generate instruction bases for each subunit"));
cl::opt<bool> generate_forwarding_info(
    "gen_forwarding", cl::init(false),
    cl::desc("Generate forwarding information each CPU"));
cl::opt<bool>
    generate_operand_indexes("gen_indexes", cl::init(false),
                             cl::desc("Generate operand indexes (vs names)"));

cl::opt<std::string> output_dir("output_dir", cl::init(""),
                                cl::value_desc("dir"),
                                cl::desc("Output path for generated files."));
cl::opt<bool> no_warnings("nowarnings", cl::init(false),
                          cl::desc("Suppress warnings. (-q)"));
cl::alias quiet_mode("q", cl::desc("Alias for --nowarnings"),
                     cl::aliasopt(no_warnings));

cl::OptionCategory TdDebug("Debugging Options");
cl::opt<bool> generate_csv("dump_csv", cl::init(false), cl::cat(TdDebug),
                           cl::desc("Dump instructions to a CSV file."));
cl::opt<bool> dump_debug("dump_debug", cl::init(false), cl::cat(TdDebug),
                         cl::desc("Dump lots of debug info."));
cl::opt<bool> dump_instr_forwarding("dump_instr_forwarding", cl::init(false),
                                    cl::cat(TdDebug),
                                    cl::desc("Dump forwarding info."));
cl::opt<bool> dump_forwarding("dump_forwarding", cl::init(false),
                              cl::cat(TdDebug),
                              cl::desc("Dump forwarding info."));
cl::opt<bool> dump_rw("dump_rw", cl::init(false), cl::cat(TdDebug),
                      cl::desc("Dump ReadWrite Records"));
cl::alias dump_debugA("d", cl::desc("Alias for --dump_debug"),
                      cl::aliasopt(dump_debug));

// Check command line arguments and manage help option messages.
static void process_cmd_line(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: scan <input-file>\n"
                 "    --help: print program options\n";
    exit(EXIT_FAILURE);
  }

  cl::getRegisteredOptions()["version"]->setHiddenFlag(cl::Hidden);
  cl::getRegisteredOptions()["color"]->setHiddenFlag(cl::ReallyHidden);
  cl::ParseCommandLineOptions(argc, argv, "Tablegen Scan Utility");

  if (input_file.empty()) {
    std::cerr << "Error: no input file\n";
    exit(EXIT_FAILURE);
  }

  if (gen_arch_spec)
    ignore_subunits = true;

  // If a family name isn't specified, extract it from the input file name.
  if (family_name.empty())
    family_name = std::filesystem::path(input_file.c_str()).stem();
  if (family_name.empty()) {
    std::cerr << "Please specify a family name\n";
    exit(EXIT_FAILURE);
  }
}

//-------------------------------------------------------------------------
// Scan a Tablegen-generated records file, find definitions we're
// interested in and add to the machine description.
//-------------------------------------------------------------------------
int main(int argc, char **argv) {
  // Process command-line options.
  process_cmd_line(argc, argv);

  // Create an object that will contain the entire machine description.
  mpact::mdl::scan::MachineDescription md_info(
      family_name, output_name, ignore_subunits, no_warnings,
      generate_base_subunits, generate_forwarding_info,
      generate_operand_indexes);

  // Read the file and collect instruction info
  md_info.ScanFile(input_file.c_str());

  // Augment an instructions's implicit input & output operands with that of its
  // base instruction.
  md_info.AddImplicitOperands();

  // Note in each operand definition whether its been referenced or not.
  md_info.FindReferencedOperands();

  if (generate_csv) {
    md_info.WriteInstructionsCsv();
    exit(EXIT_SUCCESS);
  }

  // Generate an architecture description from tablegen info, if requested.
  if (gen_arch_spec) {
    md_info.PopulateCPUsWithFUs();
    md_info.ProcessItinRW();
    md_info.ProcessInstRW();
    md_info.ProcessItineraries();
    md_info.CheckSchedulingInfo();
    if (dump_debug)
      md_info.DumpTableGenInfo();
    if (dump_forwarding)
      md_info.DumpForwardingGraph();
    if (dump_instr_forwarding)
      md_info.DumpInstrForwardingInfo();
  }

  // Debug - write out each SchedReadWrite.
  if (dump_rw)
    md_info.DumpSchedReadWrite(false);

  // Write out the MDL file.
  md_info.WriteMDL(input_file, gen_arch_spec, output_dir);
  exit(EXIT_SUCCESS);
}

namespace mpact {
namespace mdl {
namespace scan {

//-------------------------------------------------------------------------
// Read in an input line, skip over leading spaces.
//-------------------------------------------------------------------------
char *MachineDescription::GetLine(std::ifstream &in, std::string &input) {
  std::getline(in, input);
  if (input == "}")
    return nullptr;

  std::size_t found = input.find_first_not_of(" \t");
  if (found != std::string::npos)
    return &input[found];
  return &input[0];
}

//-------------------------------------------------------------------------
// Skip over the unused part of a Def record.
//-------------------------------------------------------------------------
void MachineDescription::SkipRecord(std::ifstream &in) {
  std::string input;
  while (in && GetLine(in, input) != nullptr) {
  }
}

//-------------------------------------------------------------------------
// Scan a register class definition, save off class.
// Note that we cannot populate the class until we're read in all the
// classes, since classes can be composed of subclasses which we haven't
// seen yet.
//-------------------------------------------------------------------------
void MachineDescription::ScanRegisterClass(std::ifstream &in,
                                           const std::string &name) {
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto MemberList = "dag MemberList = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, MemberList, strlen(MemberList))) {
      register_class_list_.emplace(
          name, new RegisterClass(name, lstart + strlen(MemberList)));
      SkipRecord(in);
      return;
    }
  }
}

//-------------------------------------------------------------------------
// Scan an "unknown" register class, and create an empty register class.
//-------------------------------------------------------------------------
void MachineDescription::ScanUnknownRegisterClass(std::ifstream &in,
                                                  const std::string &name) {
  register_class_list_.emplace(name, new RegisterClass(name, ""));
  SkipRecord(in);
}

//-------------------------------------------------------------------------
// Scan a SDPatternOperator operator.
//-------------------------------------------------------------------------
void MachineDescription::ScanPatternOperator(std::ifstream &in,
                                             const std::string &name) {
  std::vector<std::string> ops;
  operands_[name] = new Operand(name, ops, "i32");
  SkipRecord(in);
}

//-------------------------------------------------------------------------
// Scan a ValueType record.
// We're really just interested in saving these names, since they are
// used to define operands.
//-------------------------------------------------------------------------
void MachineDescription::ScanValueType(std::ifstream &in,
                                       const std::string &name) {
  value_type_set_.insert(name);
  SkipRecord(in);
}

//-------------------------------------------------------------------------
// Scan a PointerLikeRegClass record.
// We're really just interested in saving these names, since they are
// used to define operands.
//-------------------------------------------------------------------------
void MachineDescription::ScanPointerLikeRegClass(std::ifstream &in,
                                                 const std::string &name) {
  register_class_list_.emplace(name, new RegisterClass(name, ""));
  SkipRecord(in);
}
//-------------------------------------------------------------------------
// Scan a register definition, save off register.
//-------------------------------------------------------------------------
void MachineDescription::ScanRegister(std::ifstream &in,
                                      const std::string &name) {
  std::string name_space; // Namespace attribute.
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto namespace__str = "string Namespace = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, namespace__str, strlen(namespace__str)))
      name_space = ScanName(lstart + strlen(namespace__str));
  }

  // If definition wasn't part of the current family, ignore.
  if (name_space != family())
    return;

  registers_.insert(RegDefinition(name));
  register_dict_.insert(name);
}

//-------------------------------------------------------------------------
// Read in a subunit record.
// Lines are of the form: list<string> = ["name" (',' "name")* ];
// Input points just past the first bracket.
// Return the vector of subunit names.
//-------------------------------------------------------------------------
std::vector<std::string> *MachineDescription::ScanInstructSubunit(char *input) {
  auto *result = new std::vector<std::string>;

  while (char *name = strtok_r(input, " ,];", &input))
    result->push_back(name + 3); // Skip over the prefix.

  if (result->empty()) {
    delete result;
    return nullptr;
  }
  return result;
}

//-------------------------------------------------------------------------
// Read in the entire contents of a record's field interpreted as a list.
//-------------------------------------------------------------------------
StringVec MachineDescription::ScanList(char *input) {
  std::vector<std::string> result;

  while (char *name = strtok_r(input, " ,]);", &input))
    result.push_back(name);
  return result;
}

//-------------------------------------------------------------------------
// Read in the entire contents of a record's field interpreted as a set.
//-------------------------------------------------------------------------
StringSet MachineDescription::ScanSet(char *input) {
  StringSet result;

  while (char *name = strtok_r(input, " ,]);", &input))
    result.insert(name);
  return result;
}

//-------------------------------------------------------------------------
// Read in a list of quoted strings.
//-------------------------------------------------------------------------
StringVec MachineDescription::ScanStrings(char *input) {
  StringVec result;

  while (*input == '"') {
    char *name = input + 1;
    input = strchr(name, '"');
    *input++ = 0;
    result.push_back(name);
    if (*input == ',')
      input++;
    while (isspace(*input))
      input++;
  }
  return result;
}

//-------------------------------------------------------------------------
// Read in the entire contents of a record's field as is.
//-------------------------------------------------------------------------
std::string MachineDescription::ScanMember(char *input) {
  char *end = strchr(input, ';'); // Get rid of any ';'.
  if (end)
    *end = 0;
  if (strcmp(input, "\"\"") == 0)
    return "";

  std::string name = input;
  return name;
}

//-------------------------------------------------------------------------
// Read in a field that might contain a "empty" string, in which case we
// just return the empty string.
//-------------------------------------------------------------------------
std::string MachineDescription::ScanFixedMember(char *input) {
  return FixAttribute(ScanMember(input));
}

//-------------------------------------------------------------------------
// Read in the entire contents of a record's field interpreted as code.
// We handle three representations of code:
//     - code in quotes: "a + b + c"
//     - code delineated by [{ ... }]
//     - multi-line code delineated by [{ ... }]
//-------------------------------------------------------------------------
std::string MachineDescription::ScanCode(std::ifstream &in, std::string code) {
  char *lstart;
  std::string input;

  // Handle quoted code.
  if (code[0] == '"') {
    return code.substr(1, code.find("\";") - 1);
  }

  // Handle single-line code fragments.
  auto it = code.find("}]");
  if (it != std::string::npos)
    return code.substr(2, it - 2);

  // Handle multi-line code fragments.
  code = code.substr(2);
  while (in && (lstart = GetLine(in, input))) {
    auto first_non_ws = input.find_first_not_of(" \t");
    if (first_non_ws != std::string::npos)
      input = " " + input.substr(first_non_ws);

    auto it = input.find("}]");

    if (it != std::string::npos) {
      code += input.substr(0, it);
      return code;
    } else {
      code += input;
    }
  }
  // If we never found the terminator, abort.
  assert(0 && "Something went wrong while parsing predicate code!");
  return code;
}

//-------------------------------------------------------------------------
// Read in an instruction parent record.
// Lines are of the form: Instruction Unbundled = ...
// Input points to the beginning of the parent name.
//-------------------------------------------------------------------------
std::string MachineDescription::ScanInstructParent(char *input) {
  char *end = strchr(input, ';'); // Get rid of any ';'.
  if (end)
    *end = 0;

  std::string name = input;
  return name;
}

//-------------------------------------------------------------------------
// Read in a pattern record.
// Lines are of the form: list<dag> Pattern = [(...)];
//-------------------------------------------------------------------------
std::string MachineDescription::ScanInstructPattern(char *input) {
  char *start = strchr(input, '(');
  if (start == nullptr)
    return {};

  char *end = strrchr(start + 1, ']'); // Find last ].
  if (end == nullptr)
    return {};

  *end = 0;
  return start;
}

//-------------------------------------------------------------------------
// Read in a string field.
// The input is of the form: "name";
// Returns the string with quotes stripped off.
//-------------------------------------------------------------------------
std::string MachineDescription::ScanName(char *input) {
  char *end = strchr(input + 1, '"'); // Get rid of any trailing quote.
  if (end)
    *end = 0;
  return input + 1; // Don't include the first quote.
}

//-------------------------------------------------------------------------
// Read in assembly syntax records.
// Lines are of the form: string AsmString = "..."
//-------------------------------------------------------------------------
std::string MachineDescription::ScanInstructAssy(char *input) {
  // Find beginning/end of the assembly string, terminate (include quotes).
  char *start = strchr(input, '"');
  if (start == nullptr)
    return {};
  char *end = strrchr(start + 1, '"'); // Find last ".
  if (end == nullptr)
    return {};
  end[1] = 0; // terminate the string

  // Convert all the tabs to spaces (for export to csv files).
  for (char *tab = start; *tab; tab++)
    if (*tab == '\t')
      *tab = ' ';

  return start; // Return the string with the quotes.
}

//-------------------------------------------------------------------------
// Return the type of an operand.
//-------------------------------------------------------------------------
std::string MachineDescription::ScanType(char *input) {
  char *end = strchr(input, ';');
  if (end)
    *end = 0;
  return input;
}

//-------------------------------------------------------------------------
// Scan ins or outs operand records.
// Lines are of the form: dag InOperandList = (ins  opnd, opnd, opnd, ...);
//                        dag OutOperandList = (outs opnd, opnd, opnd, ...);
//                        dag MIOperandInfo = (ops opnd, opnd, opnd, ...);
// "input" points to right after the "ins" / "outs" / "ops".
//-------------------------------------------------------------------------
std::vector<std::string> MachineDescription::ScanInstructOpnds(char *input) {
  std::vector<std::string> opnds;

  // Grab comma-separated strings and append to the opnds vector.
  // There are three types of operand strings:
  //    - regular: <operand-type> ':$' <operand-name>
  //    - "complex": '(' <operand-type> ('?:$' <name>)+ ')' ':$" <operand_name>
  //    - ellipsis operands: "variable_ops"
  while (char *opnd = strtok_r(input, " ,);", &input)) {
    // Look for ellipsis operands.
    if (!strcmp(opnd, "variable_ops")) {
      opnds.push_back("...");
      continue;
    }
    // Parse complex operands.  We don't need the operand names.
    // (If we did need the names, we could create a specialized operand using
    // the names provided, but this isn't necessary).
    if (opnd[0] == '(') {
      auto complex = std::string(opnd + 1);     // grab the operand type
      opnd = strtok_r(input, ")", &input);      // skip rest of complex opnd
      opnd = strtok_r(input, ":$ ,);", &input); // grab the operand name
      complex += "  " + std::string(opnd);      // (yes, two spaces needed...)
      opnds.push_back(complex);
      continue;
    }
    // Parse regular operands.  Just replace the : and $ with two spaces.
    if (char *colon = strchr(opnd, ':'))
      *colon = ' ';
    if (char *dollar = strchr(opnd, '$'))
      *dollar = ' ';
    opnds.push_back(opnd);
  }
  return opnds;
}

//-------------------------------------------------------------------------
// Read the entries associated with a register operand definition, looking
// for the only field "RegisterClass RegClass" we're interested in.
//-------------------------------------------------------------------------
void MachineDescription::ScanRegisterOperand(std::ifstream &in,
                                             const std::string &name) {
  std::vector<std::string> ops; // Operand records we find.
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto regClassString = "RegisterClass RegClass = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, regClassString, strlen(regClassString))) {
      char *input = lstart + strlen(regClassString);
      char *regClass = strtok_r(input, ";", &input);
      ops.push_back(std::string(regClass) + " reg");
    }
  }

  operands_[name] = new Operand(name, ops, "i32");
}

//-------------------------------------------------------------------------
// Read the entries associated with an operand definition, looking for
// fields we're interested in:
//    - the "ops"
//    - the "type"
//-------------------------------------------------------------------------
void MachineDescription::ScanOperand(std::ifstream &in,
                                     const std::string &name) {
  std::vector<std::string> ops; // Operand records we find.
  std::string type;             // Type of operand (ie i32).
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto typeString = "ValueType Type = ";
  constexpr auto opsString = "dag MIOperandInfo = (ops";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, typeString, strlen(typeString)))
      type = ScanType(lstart + strlen(typeString));
    else if (!strncmp(lstart, opsString, strlen(opsString)))
      ops = ScanInstructOpnds(lstart + strlen(opsString));
  }

  //---------------------------------------------------------------------
  // Inexplicably, SystemZ has a pc-relative operand called pcrel32 that
  // has a single suboperand named pcrel32. Recursively-defined operands
  // are considered (by this tool) to be nonsensical, so we look for this
  // case and delete the suboperand.
  //---------------------------------------------------------------------
  if (ops.size() == 1 && ops[0] == name)
    ops.clear();

  // Create an operand description object.
  operands_[name] = new Operand(name, ops, type);
}

//-------------------------------------------------------------------------
// Read the entries associated with a CPU definition, looking for
// fields we're interested in:
//    - the "Name"
//    - the "SchedModel"
//    - the "processor itineraries
//-------------------------------------------------------------------------
void MachineDescription::ScanProcessorModel(std::ifstream &in) {
  std::string name;
  std::string sched_model;
  std::string input;
  std::string itineraries;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto name_str = "string Name = ";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";
  constexpr auto itineraries_str = "ProcessorItineraries ProcItin = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, name_str, strlen(name_str)))
      name = ScanName(lstart + strlen(name_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
    else if (!strncmp(lstart, itineraries_str, strlen(itineraries_str)))
      itineraries = ScanMember(lstart + strlen(itineraries_str));
  }

  if (sched_model.empty())
    std::cerr << "No schedule model for CPU " << name << "\n";

  // Promote processor itineraries to a new SchedModel that uses the
  // itineraries, and use that SchedModel to initialize the ProcessorModel.
  if (!itineraries.empty() && itineraries != "NoItineraries") {
    if (sched_model != "NoSchedModel")
      std::cerr << formatv("Warning: {0} has a SchedModel and Itineraries\n",
                           sched_model);
    if (sched_models_.count(itineraries) == 0)
      sched_models_[itineraries] =
          new SchedMachineModel(itineraries, itineraries, family());
    sched_model = itineraries;
  }

  cpus_[name] = new ProcessorModel(name, sched_model);
}

//-------------------------------------------------------------------------
// Read the entries associated with a ProcResource definition, looking for
// fields we're interested in:
//    - the number of instances for this unit (defaults to 1).
//    - the size of the input buffer.
//    - which SchedModel this unit is associated with.
//    - its ProcResource group (optionally).
//    - Its parent functional unit (optionally)
//-------------------------------------------------------------------------
void MachineDescription::ScanProcResource(std::ifstream &in,
                                          const std::string &name,
                                          const char *paren) {
  // Fields scraped from the definition.
  std::string count = "1";
  std::string buffer_size = "-1";
  std::string sched_model;
  std::vector<std::string> func_units;
  std::string super("");
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto num_unit_str = "int NumUnits = ";
  constexpr auto buffer_size_str = "int BufferSize = ";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";
  constexpr auto func_units_str = "list<ProcResource> Resources = [";
  constexpr auto super_str = "ProcResourceKind Super = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, num_unit_str, strlen(num_unit_str)))
      count = ScanMember(lstart + strlen(num_unit_str));
    if (!strncmp(lstart, buffer_size_str, strlen(buffer_size_str)))
      buffer_size = ScanMember(lstart + strlen(buffer_size_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
    else if (!strncmp(lstart, func_units_str, strlen(func_units_str)))
      func_units = ScanList(lstart + strlen(func_units_str));
    else if (!strncmp(lstart, super_str, strlen(super_str)))
      super = ScanFixedMember(lstart + strlen(super_str));
  }

  proc_resources_[name] =
      new ProcResource(name, count, buffer_size, sched_model, func_units, super,
                       strstr(paren, " ProcResGroup"));
}

//-------------------------------------------------------------------------
// Read the entries associated with a ComboFuncData resource.  This maps a
// name to several functional units that are used together.
//-------------------------------------------------------------------------
void MachineDescription::ScanComboFuncData(std::ifstream &in) {
  std::string name;
  std::vector<std::string> func_units;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto the_combo_func_str = "FuncUnit TheComboFunc = ";
  constexpr auto func_list_str = "list<FuncUnit> FuncList = [";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, the_combo_func_str, strlen(the_combo_func_str)))
      name = ScanMember(lstart + strlen(the_combo_func_str));
    if (!strncmp(lstart, func_list_str, strlen(func_list_str)))
      func_units = ScanList(lstart + strlen(func_list_str));
  }

  // Add an entry to the combo_units table.
  combo_units_.insert({name, func_units});
}

//-------------------------------------------------------------------------
// Scan SchedReadWrite definitions.
// Fields we're interested in: (for SchedWrites)
//    - the "WriteType"
//    - the "SchedModel"
//    - the "ProcResources"
//    - the "Latency"
//    - the "NumMicroOps"
//    - the "ResourceCycles"  (or "ReleaseAtCycles")
//    - the "StartAtCycle"    (or "AcquireAtCycles")
//    - flags: BeginGroup, EndGroup, SingleIssue, RetireOOO
// Fields we're interest in: (for SchedReads)
//    - the "WriteType"
//    - the "SchedModel"
//    - the "ValidWrites"
//    - the "Latency"
//-------------------------------------------------------------------------
void MachineDescription::ScanSchedReadWrite(std::ifstream &in,
                                            const std::string &def_name,
                                            const char *paren, bool is_write) {
  std::string name;
  std::string model;
  std::string latency;
  std::string micro_ops = "0";
  std::vector<std::string> func_units;
  std::vector<std::string> res_cycles;
  std::vector<std::string> start_cycles;
  std::string input;
  bool is_begin_group = false;
  bool is_end_group = false;
  bool is_single_issue = false;
  bool retire_ooo = false;

  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto schedwrite_str = "SchedWrite WriteType = ";
  constexpr auto schedread_str = "SchedRead ReadType = ";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";
  constexpr auto func_units_str = "list<ProcResourceKind> ProcResources = [";
  constexpr auto res_cycles_str = "list<int> ResourceCycles = [";
  constexpr auto release_cycles_str = "list<int> ReleaseAtCycles = [";
  constexpr auto start_cycle_str = "list<int> StartAtCycle = [";
  constexpr auto acq_cycle_str = "list<int> AcquireAtCycle = [";
  constexpr auto valid_writes_str = "list<SchedWrite> ValidWrites = [";
  constexpr auto latency_str = "int Latency = ";
  constexpr auto micro_ops_str = "int NumMicroOps = ";
  constexpr auto cycles_str = "int Cycles = ";
  constexpr auto begin_str = "bit BeginGroup = ";
  constexpr auto end_str = "bit EndGroup = ";
  constexpr auto single_str = "bit SingleIssue = ";
  constexpr auto retire_str = "bit RetireOOO = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, schedwrite_str, strlen(schedwrite_str)))
      name = ScanMember(lstart + strlen(schedwrite_str));
    if (!strncmp(lstart, schedread_str, strlen(schedread_str)))
      name = ScanMember(lstart + strlen(schedread_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      model = ScanFixedMember(lstart + strlen(sched_model_str));
    else if (!strncmp(lstart, func_units_str, strlen(func_units_str)))
      func_units = ScanList(lstart + strlen(func_units_str));
    else if (!strncmp(lstart, res_cycles_str, strlen(res_cycles_str)))
      res_cycles = ScanList(lstart + strlen(res_cycles_str));
    else if (!strncmp(lstart, release_cycles_str, strlen(release_cycles_str)))
      res_cycles = ScanList(lstart + strlen(release_cycles_str));
    else if (!strncmp(lstart, start_cycle_str, strlen(start_cycle_str)))
      start_cycles = ScanList(lstart + strlen(start_cycle_str));
    else if (!strncmp(lstart, acq_cycle_str, strlen(acq_cycle_str)))
      start_cycles = ScanList(lstart + strlen(acq_cycle_str));
    else if (!strncmp(lstart, valid_writes_str, strlen(valid_writes_str)))
      func_units = ScanList(lstart + strlen(valid_writes_str));
    else if (!strncmp(lstart, latency_str, strlen(latency_str)))
      latency = ScanMember(lstart + strlen(latency_str));
    else if (!strncmp(lstart, micro_ops_str, strlen(micro_ops_str)))
      micro_ops = ScanMember(lstart + strlen(micro_ops_str));
    else if (!strncmp(lstart, cycles_str, strlen(cycles_str)))
      latency = ScanMember(lstart + strlen(cycles_str));
    else if (!strncmp(lstart, begin_str, strlen(begin_str)))
      is_begin_group = ScanMember(lstart + strlen(begin_str)) == "1";
    else if (!strncmp(lstart, end_str, strlen(end_str)))
      is_end_group = ScanMember(lstart + strlen(end_str)) == "1";
    else if (!strncmp(lstart, single_str, strlen(single_str)))
      is_single_issue = ScanMember(lstart + strlen(single_str)) == "1";
    else if (!strncmp(lstart, retire_str, strlen(retire_str)))
      retire_ooo = ScanMember(lstart + strlen(retire_str)) == "1";
  }

  // If this is a useless write resource, ignore it.
  if (is_write && (model.empty() || model == "?") &&
      (latency.empty() || latency == "0") && func_units.empty())
    return;

  // Add it to the ReadWrite table.
  if (!strstr(paren, " WriteRes") && !strstr(paren, " ReadAdvance"))
    name = def_name;
  AddSchedReadWrite(name, model, func_units, res_cycles, start_cycles, latency,
                    micro_ops, is_write, is_begin_group, is_end_group,
                    is_single_issue, retire_ooo);
}

//-------------------------------------------------------------------------
// Read the entries associated with a machine model definition, looking for
// fields we're interested in:
//    - the "IssueWidth"
//    - the "LoadLatency"
//    - the "HighLatency"
//    - the micro-op reorder buffer size
//    - the mispredicted branch penalty
//    - Itineraries used in this model
//-------------------------------------------------------------------------
void MachineDescription::ScanSchedMachineModel(std::ifstream &in,
                                               const std::string &name) {
  // Use defaults defined in the MCSchedModel.
  auto issue_width = std::to_string(MCSchedModel::DefaultIssueWidth);
  auto load_latency =std::to_string(MCSchedModel::DefaultLoadLatency);
  auto high_latency = std::to_string(MCSchedModel::DefaultHighLatency);
  int mispredict = MCSchedModel::DefaultMispredictPenalty;
  int micro_op_size = MCSchedModel::DefaultMicroOpBufferSize;
  std::string itineraries;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto issue_width_str = "int IssueWidth = ";
  constexpr auto load_latency_str = "int LoadLatency = ";
  constexpr auto high_latency_str = "int HighLatency = ";
  constexpr auto itineraries_str = "ProcessorItineraries Itineraries = ";
  constexpr auto micro_op_str = "int MicroOpBufferSize = ";
  constexpr auto mispredict_str = "int MispredictPenalty = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, issue_width_str, strlen(issue_width_str)))
      issue_width = ScanMember(lstart + strlen(issue_width_str));
    else if (!strncmp(lstart, load_latency_str, strlen(load_latency_str)))
      load_latency = ScanMember(lstart + strlen(load_latency_str));
    else if (!strncmp(lstart, high_latency_str, strlen(high_latency_str)))
      high_latency = ScanMember(lstart + strlen(high_latency_str));
    else if (!strncmp(lstart, micro_op_str, strlen(micro_op_str)))
      micro_op_size = std::stoi(ScanMember(lstart + strlen(micro_op_str)));
    else if (!strncmp(lstart, mispredict_str, strlen(mispredict_str)))
      mispredict = std::stoi(ScanMember(lstart + strlen(mispredict_str)));
    else if (!strncmp(lstart, itineraries_str, strlen(itineraries_str)))
      itineraries = ScanMember(lstart + strlen(itineraries_str));
  }

  sched_models_[name] =
      new SchedMachineModel(name, issue_width, load_latency, high_latency,
                            micro_op_size, mispredict, itineraries, family());
}

//-------------------------------------------------------------------------
// Process InstRW Instrs parameters records. These lines are of the form:
// "dag Instrs = (instrs <parameters>);", and we need to parse the
// parameters here.  Each comma-separated parameter is either a plain
// identifier which represents an instruction name, or a regular expression
// list of the form: (instregex <string>+).
// Add the instruction names to the "instrs" vector, and add the regular
// expressions to the "regex" vector.
//-------------------------------------------------------------------------
void MachineDescription::ScanInstrs(char *input, StringVec &instrs,
                                    StringVec &regex) {

  for (char end = ','; end == ','; end = *input) {
    // Skip over delimiters
    input += strspn(input, " ,)");
    // Handle a list of regular expressions.
    if (strncmp(input, "(instregex", strlen("(instregex")) == 0) {
      for (char qend = ','; qend == ','; qend = *input) {
        char *quote = strchr(input, '"');
        char *endquote = strchr(quote + 1, '"');
        regex.push_back(std::string(quote + 1, endquote - quote - 1));
        input = endquote + 1;
      }
      input++; // skip trailing ')'
    } else {
      // Handle a name.
      char *sep = strpbrk(input, ",)");
      instrs.push_back(std::string(input, sep - input));
      input = sep;
    }
  }
}

//-------------------------------------------------------------------------
// Read the entries associated with a InstRW definition, looking for
// fields we're interested in:
//    - the "OperandReadWrites"
//    - the "Instrs"
//      - the "(instrregex "<regex>");"
//      - the "instrs Opcode1, Opcode2)"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanInstRW(std::ifstream &in,
                                    const std::string &name) {
  std::vector<std::string> rw_units;
  std::string sched_model;
  StringVec instregex;
  StringVec instrs;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto rw_units_str = "list<SchedReadWrite> OperandReadWrites = [";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";
  constexpr auto instregex_str = "dag Instrs = (instregex ";
  constexpr auto instrs_str = "dag Instrs = (instrs ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, rw_units_str, strlen(rw_units_str)))
      rw_units = ScanList(lstart + strlen(rw_units_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
    else if (!strncmp(lstart, instregex_str, strlen(instregex_str)))
      instregex = ScanStrings(lstart + strlen(instregex_str));
    else if (!strncmp(lstart, instrs_str, strlen(instrs_str)))
      ScanInstrs(lstart + strlen(instrs_str), instrs, instregex);
  }

  instrw_info_.push_back(
      new InstRW(name, rw_units, sched_model, instregex, instrs));
}

//-------------------------------------------------------------------------
// Read the entries associated with a ItinRW definition, looking for
// fields we're interested in:
//    - the "OperandReadWrites"
//    - the "Matched Itinerary Classes"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanItinRW(std::ifstream &in,
                                    const std::string &name) {
  std::vector<std::string> rw_units;
  std::vector<std::string> itin_names;
  std::string sched_model;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto rw_units_str = "list<SchedReadWrite> OperandReadWrites = [";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";
  constexpr auto itins_str = "list<InstrItinClass> MatchedItinClasses = [";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, rw_units_str, strlen(rw_units_str)))
      rw_units = ScanList(lstart + strlen(rw_units_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
    else if (!strncmp(lstart, itins_str, strlen(itins_str)))
      itin_names = ScanList(lstart + strlen(itins_str));
  }

  itinrw_info_.push_back(new ItinRW(name, itin_names, rw_units, sched_model));
}

//-------------------------------------------------------------------------
// Read the entries associated with a SchedVariant definition, looking for
// fields we're interested in:
//    - the "variants"
//     - Variants composed of a predicate function and a list of rw-units.
//    - the "SchedModel"
//    - the "Variadic" attribute
//-------------------------------------------------------------------------
void MachineDescription::ScanSchedVariant(std::ifstream &in,
                                          const std::string &name,
                                          const char *paren) {
  std::vector<std::string> variants;
  std::string sched_model;
  std::string input;
  bool is_variadic = false;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";
  constexpr auto variants_str = "list<SchedVar> Variants = [";
  constexpr auto is_variadic_str = "bit Variadic = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
    else if (!strncmp(lstart, variants_str, strlen(variants_str)))
      variants = ScanList(lstart + strlen(variants_str));
    else if (!strncmp(lstart, is_variadic_str, strlen(is_variadic_str)))
      is_variadic = ScanMember(lstart + strlen(is_variadic_str)) != "0";
  }

  assert(!sched_variants_.count(name) && "Duplicate SchedVariant Record!");
  sched_variants_[name] =
      new SchedVariant(name, sched_model, variants,
                       strstr(paren, " SchedWriteVariant"), is_variadic);
}

//-------------------------------------------------------------------------
// Read the entries associated with a SchedVar definition, looking for
// fields we're interested in:
//    - the "predicate"
//    - the "Selected"
//-------------------------------------------------------------------------
void MachineDescription::ScanSchedVar(std::ifstream &in,
                                      const std::string &name) {
  std::vector<std::string> selected;
  std::string predicate("");
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto predicate_str = "SchedPredicateBase Predicate = ";
  constexpr auto selected_str = "list<SchedReadWrite> Selected = [";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, predicate_str, strlen(predicate_str)))
      predicate = ScanMember(lstart + strlen(predicate_str));
    else if (!strncmp(lstart, selected_str, strlen(selected_str)))
      selected = ScanList(lstart + strlen(selected_str));
  }

  assert(!sched_vars_.count(name) && "Duplicate SchedVar Record!");
  sched_vars_[name] = new SchedVar(name, predicate, selected);
}

//-------------------------------------------------------------------------
// Read the entries associated with a MCSchedPredicate definition, looking for
// fields we're interested in:
//    - the "Pred"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanMCSchedPredicate(std::ifstream &in,
                                              const std::string &name) {
  std::string mcinst_predicate;
  std::string sched_model;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto mcinst_predicate_str = "MCInstPredicate Pred = ";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, mcinst_predicate_str, strlen(mcinst_predicate_str)))
      mcinst_predicate = ScanMember(lstart + strlen(mcinst_predicate_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
  }

  assert(!IsMCSchedPredicate(name) && "Duplicate MCSchedPredicate Record!");
  mc_sched_predicates_[name] =
      new MCSchedPredicate(name, sched_model, mcinst_predicate);
}

//-------------------------------------------------------------------------
// Read the entries associated with a SchedVar definition, looking for
// fields we're interested in:
//    - the "FunctionName"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanMCInstPredicate(std::ifstream &in,
                                             const std::string &name,
                                             const char *paren) {
  std::string function_name;
  std::string function_mapper;
  std::string opindex;
  std::string immval;
  std::vector<std::string> predicates;
  std::vector<std::string> valid_opcodes;
  std::string register_name;
  std::vector<std::string> attributes;
  std::string statement;

  std::string input;
  char *lstart;

  std::string pred_attributes[] = {kCheckAny,
                                   kCheckAll,
                                   kCheckNot,
                                   kCheckOpcode,
                                   kCheckIsRegOperand,
                                   kCheckRegOperand,
                                   kCheckSameRegOperand,
                                   kCheckInvalidRegOperand,
                                   kCheckIsImmOperand,
                                   kCheckImmOperand,
                                   kCheckZeroOperand,
                                   kCheckFunctionPredicateWithTII,
                                   kCheckFunctionPredicate,
                                   kCheckNumOperands};

  // These strings correspond to subfields we are interested in.
  constexpr auto function_name_str = "string FunctionName = ";
  constexpr auto instr_fn_name_str = "string MachineInstrFnName = ";
  constexpr auto mc_ins_fn_str = "string MCInstFnName = ";
  constexpr auto tii_ptr_name_str = "string TIIPtrName = ";
  constexpr auto function_mapper_str = "string FunctionMapper = ";
  constexpr auto opindex_str = "int OpIndex = ";
  constexpr auto immval_str = "int ImmVal = ";
  constexpr auto immval_str_s = "string ImmVal = ";
  constexpr auto predicates_str = "list<MCInstPredicate> Predicates = [";
  constexpr auto predicate_str = "MCInstPredicate Pred = ";
  constexpr auto valid_opcodes_str = "list<Instruction> ValidOpcodes = [";
  constexpr auto register_str = "Register Reg = ";
  constexpr auto statement_str = "MCStatement Body = ";
  constexpr auto first_index_str = "int FirstIndex = ";
  constexpr auto second_index_str = "int SecondIndex = ";
  constexpr auto numops_str = "int NumOps = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, function_name_str, strlen(function_name_str)))
      function_name = ScanName(lstart + strlen(function_name_str));
    if (!strncmp(lstart, instr_fn_name_str, strlen(instr_fn_name_str)))
      function_mapper = ScanMember(lstart + strlen(instr_fn_name_str));
    else if (!strncmp(lstart, mc_ins_fn_str, strlen(mc_ins_fn_str)))
      function_name = ScanMember(lstart + strlen(mc_ins_fn_str));
    else if (!strncmp(lstart, tii_ptr_name_str, strlen(tii_ptr_name_str)))
      immval = ScanMember(lstart + strlen(tii_ptr_name_str));
    else if (!strncmp(lstart, function_mapper_str, strlen(function_mapper_str)))
      function_mapper = ScanMember(lstart + strlen(function_mapper_str));
    else if (!strncmp(lstart, opindex_str, strlen(opindex_str)))
      opindex = ScanMember(lstart + strlen(opindex_str));
    else if (!strncmp(lstart, immval_str, strlen(immval_str)))
      immval = ScanMember(lstart + strlen(immval_str));
    else if (!strncmp(lstart, numops_str, strlen(numops_str)))
      immval = ScanMember(lstart + strlen(numops_str));
    else if (!strncmp(lstart, immval_str_s, strlen(immval_str_s)))
      immval = ScanMember(lstart + strlen(immval_str_s));
    else if (!strncmp(lstart, first_index_str, strlen(first_index_str)))
      opindex = ScanMember(lstart + strlen(first_index_str));
    else if (!strncmp(lstart, second_index_str, strlen(second_index_str)))
      immval = ScanMember(lstart + strlen(second_index_str));
    else if (!strncmp(lstart, predicates_str, strlen(predicates_str)))
      predicates = ScanList(lstart + strlen(predicates_str));
    else if (!strncmp(lstart, predicate_str, strlen(predicate_str)))
      predicates.push_back(ScanMember(lstart + strlen(predicate_str)));
    else if (!strncmp(lstart, valid_opcodes_str, strlen(valid_opcodes_str)))
      valid_opcodes = ScanList(lstart + strlen(valid_opcodes_str));
    else if (!strncmp(lstart, register_str, strlen(register_str)))
      register_name = ScanMember(lstart + strlen(register_str));
    else if (!strncmp(lstart, statement_str, strlen(statement_str)))
      statement = ScanMember(lstart + strlen(statement_str));
  }

  // Process predicate attributes on predicate definition line.
  for (auto &attr : pred_attributes)
    if (strstr(paren, attr.c_str()))
      attributes.push_back(attr);

  assert(!mc_inst_predicates_.count(name) && "Unknown MCInstPredicate Record!");
  mc_inst_predicates_[name] = new MCInstPredicate(
      name, function_name, function_mapper, opindex, immval, predicates,
      valid_opcodes, register_name, attributes, statement);
}

//-------------------------------------------------------------------------
// Read in entries associated with MCStatement objects.
//-------------------------------------------------------------------------
void MachineDescription::ScanMCStatement(std::ifstream &in,
                                         const std::string &name,
                                         const char *paren) {
  std::string attribute;
  std::string predicate;
  std::vector<std::string> cases;
  std::string default_case;

  std::string input;
  char *lstart;

  constexpr auto predicate_str = "MCInstPredicate Pred = ";
  constexpr auto cases_str = "list<MCOpcodeSwitchCase> Cases = [";
  constexpr auto default_case_str = "MCStatement DefaultCase = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, predicate_str, strlen(predicate_str)))
      predicate = ScanMember(lstart + strlen(predicate_str));
    else if (!strncmp(lstart, cases_str, strlen(cases_str)))
      cases = ScanList(lstart + strlen(cases_str));
    if (!strncmp(lstart, default_case_str, strlen(default_case_str)))
      default_case = ScanMember(lstart + strlen(default_case_str));
  }

  // Note attributes on definition line.
  if (strstr(paren, "MCReturnStatement"))
    attribute = "ReturnStatement";
  if (strstr(paren, "MCOpcodeSwitchStatement"))
    attribute = "OpcodeSwitch";

  statements_[name] =
      new MCStatement(name, attribute, predicate, cases, default_case);
}

//-------------------------------------------------------------------------
// Read in entries associated with MCOpcodeSwitchCase objects.
//-------------------------------------------------------------------------
void MachineDescription::ScanMCOpcodeSwitchCase(std::ifstream &in,
                                                const std::string &name) {
  std::string attribute;
  std::vector<std::string> cases;
  std::string case_stmt;

  std::string input;
  char *lstart;

  constexpr auto cases_str = "list<Instruction> Opcodes = [";
  constexpr auto default_case_str = "MCStatement CaseStmt = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, cases_str, strlen(cases_str)))
      cases = ScanList(lstart + strlen(cases_str));
    if (!strncmp(lstart, default_case_str, strlen(default_case_str)))
      case_stmt = ScanMember(lstart + strlen(default_case_str));
  }

  switch_cases_[name] = new MCOpcodeSwitchCase(name, cases, case_stmt);
}

//-------------------------------------------------------------------------
// Read the entries associated with a SchedPredicate definition, looking for
// fields we're interested in:
//    - the "Predicate"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanSchedPredicate(std::ifstream &in,
                                            const std::string &name) {
  std::string predicate;
  std::string sched_model;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto predicate_str = "code Predicate = ";
  constexpr auto predicate_str_s = "string Predicate = ";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, predicate_str, strlen(predicate_str)))
      predicate = ScanCode(in, lstart + strlen(predicate_str));
    if (!strncmp(lstart, predicate_str_s, strlen(predicate_str_s)))
      predicate = ScanCode(in, lstart + strlen(predicate_str_s));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
  }

  assert(!IsSchedPredicate(name) && "Duplicate SchedPredicate Record!");
  sched_predicates_[name] = new SchedPredicate(name, sched_model, predicate);
}

//-------------------------------------------------------------------------
// Read the entries associated with a SchedAlias definition, looking for
// fields we're interested in:
//    - the "MatchRW"
//    - the "AliasRW"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanSchedAlias(std::ifstream &in,
                                        const std::string &name) {
  std::string match_rw;
  std::string alias_rw;
  std::string sched_model;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto match_rw_str = "SchedReadWrite MatchRW = ";
  constexpr auto alias_rw_str = "SchedReadWrite AliasRW = ";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, match_rw_str, strlen(match_rw_str)))
      match_rw = ScanMember(lstart + strlen(match_rw_str));
    if (!strncmp(lstart, alias_rw_str, strlen(alias_rw_str)))
      alias_rw = ScanMember(lstart + strlen(alias_rw_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
  }

  auto *item = new SchedAlias(name, sched_model, match_rw, alias_rw);
  sched_alias_[name] = item;
  sched_alias_matches_[match_rw][sched_model] = alias_rw;
}

//-------------------------------------------------------------------------
// Read the entries associated with a WriteSequence definition, looking for
// fields we're interested in:
//    - the "Writes"
//    - the "Repeat"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanWriteSequence(std::ifstream &in,
                                           const std::string &name) {
  std::vector<std::string> writes;
  std::string repeat;
  std::string sched_model;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto writes_str = "list<SchedWrite> Writes = [";
  constexpr auto repeat_str = "int Repeat = ";
  constexpr auto sched_model_str = "SchedMachineModel SchedModel = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, writes_str, strlen(writes_str)))
      writes = ScanList(lstart + strlen(writes_str));
    if (!strncmp(lstart, repeat_str, strlen(repeat_str)))
      repeat = ScanMember(lstart + strlen(repeat_str));
    else if (!strncmp(lstart, sched_model_str, strlen(sched_model_str)))
      sched_model = ScanFixedMember(lstart + strlen(sched_model_str));
  }

  assert(!write_sequences_.count(name) && "Duplicate WriteSequence Record!");
  write_sequences_[name] =
      new WriteSequence(name, sched_model, writes, std::stoi(repeat));
}

//-------------------------------------------------------------------------
// Read the entries associated with a ProcessorItineraries definition, looking
// for fields we're interested in:
//    - the "FU"
//    - the "BP"
//    - the "IID"
//-------------------------------------------------------------------------
void MachineDescription::ScanProcessorItineraries(std::ifstream &in,
                                                  const std::string &name) {
  StringSet resources;
  std::vector<std::string> bypass;
  StringSet iid;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto func_units_str = "list<FuncUnit> FU = [";
  constexpr auto bypass_str = "list<Bypass> BP = [";
  constexpr auto iid_str = "list<InstrItinData> IID = [";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, func_units_str, strlen(func_units_str)))
      resources = ScanSet(lstart + strlen(func_units_str));
    if (!strncmp(lstart, bypass_str, strlen(bypass_str)))
      bypass = ScanList(lstart + strlen(bypass_str));
    else if (!strncmp(lstart, iid_str, strlen(iid_str)))
      iid = ScanSet(lstart + strlen(iid_str));
  }

  assert(!processor_itineraries_.count(name) &&
         "Duplicate ProcessorItineraries Record!");
  processor_itineraries_[name] =
      new ProcessorItineraries(name, resources, bypass, iid);
}

//-------------------------------------------------------------------------
// Read the entries associated with a InstrItinData definition, looking
// for fields we're interested in:
//    - the "TheClass"
//    - the "NumMicroOps"
//    - the "Stages"
//    - the "OperandCycles"
//    - the "Bypasses"
//-------------------------------------------------------------------------
void MachineDescription::ScanInstrItineraryData(std::ifstream &in,
                                                const std::string &name) {
  std::string class_name("");
  std::string num_micro_ops("");
  std::vector<std::string> stages;
  std::vector<std::string> operand_cycles;
  std::vector<std::string> bypasses;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto instr_itinerary_class_str = "InstrItinClass TheClass = ";
  constexpr auto num_micro_op_str = "int NumMicroOps = ";
  constexpr auto stages_str = "list<InstrStage> Stages = [";
  constexpr auto operand_cycles_str = "list<int> OperandCycles = [";
  constexpr auto bypasses_str = "list<Bypass> Bypasses = [";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, instr_itinerary_class_str,
                 strlen(instr_itinerary_class_str)))
      class_name = ScanMember(lstart + strlen(instr_itinerary_class_str));
    if (!strncmp(lstart, num_micro_op_str, strlen(num_micro_op_str)))
      num_micro_ops = ScanMember(lstart + strlen(num_micro_op_str));
    else if (!strncmp(lstart, stages_str, strlen(stages_str)))
      stages = ScanList(lstart + strlen(stages_str));
    else if (!strncmp(lstart, operand_cycles_str, strlen(operand_cycles_str)))
      operand_cycles = ScanList(lstart + strlen(operand_cycles_str));
    else if (!strncmp(lstart, bypasses_str, strlen(bypasses_str)))
      bypasses = ScanList(lstart + strlen(bypasses_str));
  }

  auto *item =
      new InstrItineraryData(name, class_name, std::stoi(num_micro_ops), stages,
                             operand_cycles, bypasses);
  instr_itinerary_data_[name] = item;
  instr_itinerary_class_[class_name].push_back(item);
}

//-------------------------------------------------------------------------
// Read the entries associated with a InstrStage definition, looking
// for fields we're interested in:
//    - the "Writes"
//    - the "Repeat"
//    - the "SchedModel"
//-------------------------------------------------------------------------
void MachineDescription::ScanInstrStage(std::ifstream &in,
                                        const std::string &name) {
  std::string cycles("");
  StringVec resources;
  std::string timeinc("");
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto cycles_str = "int Cycles = ";
  constexpr auto func_units_str = "list<FuncUnit> Units = [";
  constexpr auto timeinc_str = "int TimeInc = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, cycles_str, strlen(cycles_str)))
      cycles = ScanMember(lstart + strlen(cycles_str));
    if (!strncmp(lstart, func_units_str, strlen(func_units_str)))
      resources = ScanList(lstart + strlen(func_units_str));
    if (!strncmp(lstart, timeinc_str, strlen(timeinc_str)))
      timeinc = ScanMember(lstart + strlen(timeinc_str));
  }

  assert(!instr_stages_.count(name) && "Duplicate InstrStage Record!");
  instr_stages_[name] =
      new InstrStage(name, std::stoi(cycles), resources, std::stoi(timeinc));
}

//-------------------------------------------------------------------------
// Read the entries associated with a InstrItinClasses definition.
//-------------------------------------------------------------------------
void MachineDescription::ScanInstrItineraryClasses(std::ifstream &in,
                                                   const std::string &name) {
  assert(!instr_itinerary_classes_.count(name) &&
         "Duplicate InstrItinClasses Record!");
  instr_itinerary_classes_.insert(name);
}

//-------------------------------------------------------------------------
// Read the entries associated with a Bypass definition.
//-------------------------------------------------------------------------
void MachineDescription::ScanBypasses(std::ifstream &in,
                                      const std::string &name) {
  assert(!bypasses_.count(name) && "Duplicate Bypass Record!");
  bypasses_.insert(name);
}

//-------------------------------------------------------------------------
// Read the entries associated with an instruction definition looking for
// fields we're interested in:
//    - the "outs".
//    - the "ins".
//    - the assembly syntax.
//    - the dag matching pattern.
//    - the instruction's "parent".
//-------------------------------------------------------------------------
void MachineDescription::ScanInstruction(std::ifstream &in,
                                         const std::string &name,
                                         const char *paren) {
  std::vector<std::string> outs;     // Output operands.
  std::vector<std::string> ins;      // Input operands.
  std::string assy;                  // Assembly syntax.
  std::string pat;                   // Matching pattern.
  StringVec *subunit = nullptr;      // Subunit identifier.
  StringVec implicit_uses;           // Implicit Uses.
  StringVec implicit_defs;           // Implicit Defs.
  std::string parent;                // Parent instruction.
  std::string base_instr;            // Base Instruction.
  std::vector<std::string> rw_units; // SchedRW Subunits.
  std::string name_space;            // Namespace attribute.
  std::string itinerary;             // InstrItinClass attribute.
  bool pseudo = false;
  bool load = false;
  std::string input;
  char *lstart;

  // These strings correspond to subfields we are interested in.
  constexpr auto out_str = "dag OutOperandList = (outs";
  constexpr auto in_str = "dag InOperandList = (ins";
  constexpr auto assy_str = "string AsmString = ";
  constexpr auto pat_str = "list<dag> Pattern = ";
  constexpr auto parent_str = "Instruction Unbundled = ";
  constexpr auto base_instr_str = "Instruction base_instr = ";
  constexpr auto isPseudo = "bit isPseudo = 1";
  constexpr auto mayLoad = "bit mayLoad = 1";
  constexpr auto subunit_str = "list<SubUnitEncoding> SubUnits = [";
  constexpr auto namespace__str = "string Namespace = ";
  constexpr auto implicit_uses_str = "list<Register> Uses = [";
  constexpr auto implicit_defs_str = "list<Register> Defs = [";
  constexpr auto derived_subunits_str = "list<SchedReadWrite> SchedRW = [";
  constexpr auto itinerary_str = "InstrItinClass Itinerary = ";

  // Read subfield definitions and process the ones we're interested in.
  while (in && (lstart = GetLine(in, input))) {
    if (!strncmp(lstart, out_str, strlen(out_str)))
      outs = ScanInstructOpnds(lstart + strlen(out_str));
    else if (!strncmp(lstart, in_str, strlen(in_str)))
      ins = ScanInstructOpnds(lstart + strlen(in_str));
    else if (!strncmp(lstart, assy_str, strlen(assy_str)))
      assy = ScanInstructAssy(lstart + strlen(assy_str));
    else if (!strncmp(lstart, pat_str, strlen(pat_str)))
      pat = ScanInstructPattern(lstart + strlen(pat_str));
    else if (!strncmp(lstart, parent_str, strlen(parent_str)))
      parent = ScanInstructParent(lstart + strlen(parent_str));
    else if (!strncmp(lstart, base_instr_str, strlen(base_instr_str)))
      base_instr = ScanInstructParent(lstart + strlen(base_instr_str));
    else if (!strncmp(lstart, subunit_str, strlen(subunit_str)))
      subunit = ScanInstructSubunit(lstart + strlen(subunit_str));
    else if (!strncmp(lstart, implicit_uses_str, strlen(implicit_uses_str)))
      implicit_uses = ScanList(lstart + strlen(implicit_uses_str));
    else if (!strncmp(lstart, implicit_defs_str, strlen(implicit_defs_str)))
      implicit_defs = ScanList(lstart + strlen(implicit_defs_str));
    else if (!strncmp(lstart, namespace__str, strlen(namespace__str)))
      name_space = ScanName(lstart + strlen(namespace__str));
    else if (!strncmp(lstart, isPseudo, strlen(isPseudo)))
      pseudo = true;
    else if (!strncmp(lstart, mayLoad, strlen(mayLoad)))
      load = true;
    else if (!strncmp(lstart, derived_subunits_str,
                      strlen(derived_subunits_str)))
      rw_units = ScanList(lstart + strlen(derived_subunits_str));
    else if (!strncmp(lstart, itinerary_str, strlen(itinerary_str)))
      itinerary = ScanMember(lstart + strlen(itinerary_str));
  }

  // If instruction wasn't part of the current family, ignore.
  // We also read all the StandardPseudoInstructions.
  if (name_space != family() && name_space != "TargetOpcode")
    return;

  //---------------------------------------------------------------------
  // Create an instruction description object.
  // If it's a base instruction, add to the table.  If it'a child, add
  // it to its parent's list of children.
  //---------------------------------------------------------------------
  auto inst =
      new Instruction(name, outs, ins, assy, pat, parent, subunit, rw_units,
                      implicit_uses, implicit_defs, base_instr, itinerary, load,
                      pseudo, strstr(paren, "GenericInstruction"));
  if (parent.empty())
    instructions_[name] = inst; // Duplicates ought to be impossible
  else
    instructions_[parent]->children_.push_back(inst);
}

//-------------------------------------------------------------------------
// Just skip over class definitions (for now).
//-------------------------------------------------------------------------
void MachineDescription::ScanClass(std::ifstream &in, char *input) {
  SkipRecord(in);
}

//-------------------------------------------------------------------------
// In a def record, look for fields of interest.
//---------------------------------------------------------------------
// Process lines of the form:
//     def <name> { <attributes ...>
//---------------------------------------------------------------------
// We're looking for instruction definitions which:
//    - have an attribute of "instruction" "<family>Inst"
//    - have a non-empty attribute AsmString
//    - have "ins" and "outs"
// For instructions, we want to collect:
//    - the instruction name
//    - the "outs"
//    - the "ins"
//    - the assembly syntax
//    - the subunit identifier
//    - ... and maybe some other stuff
// We're also looking for register definitions, which
//    - have an attribute of "Register" and "<family>Reg"
// We're also looking for non-trivial "operand" definitions, which:
//    - have an attribute of "OperandDAG"
//    - have a non-trivial OperandInfo entry
//-------------------------------------------------------------------------
void MachineDescription::ScanDef(std::ifstream &in, char *input) {
  int name_start = 0;
  int name_end = 0;

  //  Isolate the name of the object being defined.
  for (name_start = 4; isspace(input[name_start]); name_start++) {
  }
  for (name_end = name_start; input[name_end] != ' '; name_end++) {
  }
  input[name_end] = 0; // Terminate the name.

  // Find the open paren - if it's not there, we're confused.
  char *paren = nullptr;
  if (!(paren = strchr(&input[name_end + 1], '{'))) {
    SkipRecord(in);
    return;
  }

  //  Is this is a processor family instruction record?
  if (strstr(paren, " Instruction "))
    ScanInstruction(in, &input[name_start], paren);

  else if (strstr(paren, " PointerLikeRegClass"))
    ScanPointerLikeRegClass(in, &input[name_start]);

  //  Is this is a operand record?
  else if (strstr(paren, " DAGOperand Operand"))
    ScanOperand(in, &input[name_start]);

  // Scan Various register definitions.  Note: Scan plain "registers" last
  // since they may only have the "Register" qualify (and at the EOL).
  //  Is this is an register-operand record?
  else if (strstr(paren, " DAGOperand RegisterOperand"))
    ScanRegisterOperand(in, &input[name_start]);

  else if (strstr(paren, " RegisterClass"))
    ScanRegisterClass(in, &input[name_start]);

  else if (strstr(paren, " unknown_class"))
    ScanUnknownRegisterClass(in, &input[name_start]);

  //  Is this is a register record for our processor family?
  else if (strstr(paren, " Register"))
    ScanRegister(in, &input[name_start]);

  else if (strstr(paren, " SDPatternOperator SDNode"))
    ScanPatternOperator(in, &input[name_start]);

  else if (strstr(paren, " ValueType"))
    ScanValueType(in, &input[name_start]);

  //----------------------------------------------------------------------
  // If we're not extracting the entire architecture spec, skip other
  // records.
  //----------------------------------------------------------------------
  else if (!gen_arch_spec)
    SkipRecord(in);

  else if (strstr(paren, " Processor Proc") ||
           (strstr(paren, " Processor") &&
            !strstr(paren, " ProcessorItineraries")))
    ScanProcessorModel(in);

  else if (strstr(paren, " SchedMachineModel"))
    ScanSchedMachineModel(in, &input[name_start]);

  else if (strstr(paren, " ProcResourceKind Proc"))
    ScanProcResource(in, &input[name_start], paren);

  else if (strstr(paren, " FuncUnit"))
    ScanProcResource(in, &input[name_start], paren);

  else if (strstr(paren, " ComboFuncData"))
    ScanComboFuncData(in);

  else if (strstr(paren, " InstRW"))
    ScanInstRW(in, &input[name_start]);

  else if (strstr(paren, " ItinRW"))
    ScanItinRW(in, &input[name_start]);

  //---------------------------------------------------------------------
  // Process all the SchedReadWrite resource definitions.  Process
  // SchedRead and SchedWrite records last, to catch all the qualified
  // versions first.
  //---------------------------------------------------------------------
  else if (strstr(paren, " ProcWriteResources"))
    ScanSchedReadWrite(in, &input[name_start], paren, true);

  else if (strstr(paren, " ProcReadAdvance"))
    ScanSchedReadWrite(in, &input[name_start], paren, false);

  else if (strstr(paren, " WriteSequence"))
    ScanWriteSequence(in, &input[name_start]);

  else if (strstr(paren, " SchedVariant"))
    ScanSchedVariant(in, &input[name_start], paren);

  else if (strstr(paren, " SchedVar"))
    ScanSchedVar(in, &input[name_start]);

  else if (strstr(paren, " SchedReadWrite SchedWrite"))
    ScanSchedReadWrite(in, &input[name_start], paren, true);

  else if (strstr(paren, " SchedReadWrite SchedRead"))
    ScanSchedReadWrite(in, &input[name_start], paren, false);

  else if (strstr(paren, " SchedAlias"))
    ScanSchedAlias(in, &input[name_start]);

  //---------------------------------------------------------------------
  // Process predicate-related records.
  //---------------------------------------------------------------------
  else if (strstr(paren, " SchedPredicateBase SchedPredicate"))
    ScanSchedPredicate(in, &input[name_start]);

  else if (strstr(paren, " MCSchedPredicate"))
    ScanMCSchedPredicate(in, &input[name_start]);

  else if (strstr(paren, " MCInstPredicate"))
    ScanMCInstPredicate(in, &input[name_start], paren);

  else if (strstr(paren, " MCStatement"))
    ScanMCStatement(in, &input[name_start], paren);

  else if (strstr(paren, " MCOpcodeSwitchCase"))
    ScanMCOpcodeSwitchCase(in, &input[name_start]);

  //---------------------------------------------------------------------
  // Process Itinerary records.
  //---------------------------------------------------------------------
  else if (strstr(paren, " ProcessorItineraries"))
    ScanProcessorItineraries(in, &input[name_start]);

  else if (strstr(paren, " InstrItinData"))
    ScanInstrItineraryData(in, &input[name_start]);

  else if (strstr(paren, " InstrStage"))
    ScanInstrStage(in, &input[name_start]);

  else if (strstr(paren, " Bypass"))
    ScanBypasses(in, &input[name_start]);

  else if (strstr(paren, " InstrItinClass"))
    ScanInstrItineraryClasses(in, &input[name_start]);

  else
    SkipRecord(in); // Nothing interesting, skip rest of record.
}

//-------------------------------------------------------------------------
// Open the file, scan all the lines and add to machine description.
//
// The "records" file is a flattened version of the td file for a cpu, and
// contains records for each instruction and register (and other things).
//
// The file contains "class" descriptors and "def" descriptors:
//        ------------- Classes ----------------
//        class <name> {
//        ...
//        }
//        ...
//        ------------- Defs ----------------
//        def <name> {
//        ...
//        }
//        ...
//-------------------------------------------------------------------------
void MachineDescription::ScanFile(const char *file) {
  std::ifstream in(file);
  if (!in) {
    std::cerr << "Error: input file \"" << file << "\" not found" << std::endl;
    exit(EXIT_FAILURE);
  }

  while (in) {
    std::string line;
    char *input = GetLine(in, line);
    if (input == nullptr)
      continue;
    if (!in)
      return;

    if (strncmp(input, "def", strlen("def")) == 0)
      ScanDef(in, input);
    else if (strncmp(input, "class", strlen("class")) == 0)
      ScanClass(in, input);
    else if (strncmp(input, "--", 2) == 0) // Skip comment lines.
      continue;
    else
      std::cerr << "Unrecognized line:" << input << std::endl;
  }

  in.close();
}

} // namespace scan
} // namespace mdl
} // namespace mpact

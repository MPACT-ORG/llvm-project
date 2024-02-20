//===- mdl_parser.cpp - Parse the file and process the parse tree --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Parser for the machine description language.
//
// This file contains the recursive descent parser for the MDL language. The
// parser is completely context-free - there is no semantic analysis done on
// the input. If we don't find any syntax errors, we perform all the semantic
// checking in the next pass.
//
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "llvm/Support/Error.h"

#include "mdl.h"
#include "mdl_parser.h"

namespace mpact {
namespace mdl {

//-----------------------------------------------------------------------------
// Top level parser for the MDL language.
//-----------------------------------------------------------------------------
bool ProcessInputFile(MdlSpec &spec, std::string import_path,
                      std::string file_name) {
  // If we've already seen this file name, there's no point in parsing it again,
  // so just return "success".
  static std::vector<std::string *> imports;
  for (auto *name : imports)
    if (*name == file_name) return true;

  // Attempt to open the file.
  std::fstream *mdl_stream = new std::fstream(file_name, std::fstream::in);
  if (!mdl_stream->is_open()) {
    llvm::errs() << formatv("File not found: \"{0}\"\n", file_name);
    return false;
  }

  // Save the name of the input file (so we don't parse it twice).
  std::string *saved_file_name = new std::string(file_name);
  imports.push_back(saved_file_name);

  // Create a new parser and parse the file.
  MdlParser mdl_parser(spec, import_path, saved_file_name, mdl_stream);
  return mdl_parser.ParseArchitectureSpec();
}

//-----------------------------------------------------------------------------
// Process architecture_spec and architecture_item rules:
//     architecture_spec  : architectural_item+
//     architectural_item : cpu_def | register_def | resource_def |
//                          pipe_def | func_unit_template | subunit_template |
//                          latency_template | import_file;
//
// This is the top-level production.  Here we create an object (MdlSpec)
// which contains all of the information in the input description.
//-----------------------------------------------------------------------------
bool MdlParser::ParseArchitectureSpec() {
  bool success = true;
  while (!Token.EndOfFile()) {
    switch (Token.getType()) {
      case TK_FAMILY:
          if (auto *family = ParseFamilySpec())
            spec().set_family_name(family);
          break;
      case TK_PIPE_PHASES: case TK_PROTECTED: case TK_UNPROTECTED: case TK_HARD:
          if (auto *pipe = ParsePipeDef())
            spec().pipe_phases().push_back(pipe);
          break;
      case TK_REGISTER:
          if (auto *regs = ParseRegisterDef())
             spec().registers().insert(spec().registers().end(),
                                      regs->begin(), regs->end());
          break;
      case TK_REGCLASS:
          if (auto *regclass = ParseRegisterClass())
            spec().reg_classes().push_back(regclass);
          break;
      case TK_RESOURCE:
          if (auto *resdef = ParseResourceDef())
            spec().resources().insert(spec().resources().end(), resdef->begin(),
                                      resdef->end());
          break;
      case TK_CPU:
            if (auto *cpu = ParseCpuDef())
              spec().cpus().push_back(cpu);
          break;
      case TK_FUNCUNIT:
          if (auto *func = ParseFuncUnitTemplate())
            spec().func_units().push_back(func);
          break;
      case TK_FUNCGROUP:
          if (auto *func = ParseFuncUnitGroup())
            spec().func_unit_groups().push_back(func);
          break;
      case TK_SUBUNIT:
          if (auto *subunit = ParseSubunitTemplate())
            spec().subunits().push_back(subunit);
          break;
      case TK_LATENCY:
          if (auto *latency = ParseLatencyTemplate())
            spec().latencies().push_back(latency);
          break;
      case TK_INSTRUCT:
          if (auto *instruction = ParseInstructionDef())
            spec().instructions().push_back(instruction);
          break;
      case TK_OPERAND:
          if (auto *operand = ParseOperandDef())
            spec().operands().push_back(operand);
          break;
      case TK_IMPORT:
          if (bool included = ParseImportFile())
            success &= included;
          break;
      case TK_PREDICATE:
          ParsePredicateDef();
          break;
      default:
          Error("Invalid MDL statement: family, cpu, func_unit, func_group,\n"
                "  subunit, latency, instruction, operand, register,\n"
                "  register class, resource, pipe, or predicate"
                " definition expected");
          if (spec().ErrorsSeen() > 30) {
            Error("Too many syntax errors, aborting");
            return false;
          }
          Token.SkipUntil(TK_FAMILY, TK_CPU, TK_FUNCUNIT, TK_FUNCGROUP,
                          TK_SUBUNIT, TK_LATENCY, TK_INSTRUCT, TK_OPERAND,
                          TK_REGISTER, TK_REGCLASS, TK_RESOURCE,
                          TK_PIPE_PHASES, TK_PROTECTED, TK_UNPROTECTED, TK_HARD,
                          TK_IMPORT, TK_PREDICATE, TK_ENDOFFILE);
          break;
    }
  }
  return success;
}

//-----------------------------------------------------------------------------
// Process a family name specification.
//-----------------------------------------------------------------------------
Identifier *MdlParser::ParseFamilySpec() {
  if (!Token.accept(TK_FAMILY)) return nullptr;
  auto *family = ParseIdent("Family name");
  Token.expect(TK_SEMI);
  return family;
}

//-----------------------------------------------------------------------------
//  Process an import file.
//  Return true if successful, false if any errors are found.
//-----------------------------------------------------------------------------
bool MdlParser::ParseImportFile() {
  if (!Token.accept(TK_IMPORT)) return false;

  // Fetch the filename.
  std::filesystem::path import_name = ParseString("Filename");
  if (import_name.empty()) return false;

  // Get directory names for the current source file and the import file name.
  auto current_file_name = Token.getText();
  auto current_dir = std::filesystem::path(current_file_name).parent_path();
  auto import_dir = import_name.parent_path();

  auto AddSlash = [](std::string path_name) {
    if (!path_name.empty() && path_name.back() != '/')
      path_name += "/";
    return path_name;
  };

  // If the import name has directory information, use it.
  if (!import_dir.empty()) {
    if (!current_dir.empty() && !import_dir.is_absolute())
      return ProcessInputFile(spec_, import_path_,
                              formatv("{0}{1}{2}", AddSlash(current_dir),
                                      AddSlash(import_dir), import_name));
    return ProcessInputFile(spec_, import_path_, import_name);
  }

  // If the import name doesn't have directory info, see if its in the
  // including file's directory.
  if (!current_dir.empty()) {
    auto name = formatv("{0}{1}", AddSlash(current_dir), import_name);
    if (std::filesystem::exists(name))
      return ProcessInputFile(spec_, import_path_, name);
  }

  // If both the import dir and current directory are empty, check the current
  // directory.
  if (std::filesystem::exists(import_name))
    return ProcessInputFile(spec_, import_path_, import_name);

  // If not found in the current directory, look in the import path.
  if (!import_path_.empty()) {
    auto name = formatv("{0}{1}", AddSlash(import_path_), import_name);
    if (std::filesystem::exists(name))
      return ProcessInputFile(spec_, import_path_, name);
  }

  // Otherwise, just use the name verbatim.
  return ProcessInputFile(spec_, import_path_, import_name);
}

//-----------------------------------------------------------------------------
// Process cpu_def and cpu_stmt rules:
//      cpu_def : CPU|CORE ident cpu_def_names? '{' cpu_stmt+ '}' ';'
//      cpu_stmt: pipe_def | register_def | resource_def | issue_statement |
//                cluster_instantiation | func_unit_instantiation ;
//-----------------------------------------------------------------------------
CpuInstance *MdlParser::ParseCpuDef() {
  MdlItem item(Token());
  if (!Token.accept(TK_CPU)) return nullptr;

  Identifier *name = ParseIdent("Cpu name");
  if (name == nullptr) return nullptr;

  auto *pipes = new PipeDefList;
  auto *issues = new ResourceDefList;
  auto *resources = new ResourceDefList;
  int reorder_buffer_size = 0;
  auto *func_units = new FuncUnitInstList;
  auto *forward_stmts = new ForwardStmtList;
  auto *clusters = new ClusterList;

  // Fetch optional names and strip off quotes. Note that we need at least
  // one name, so use the CPU name by default.
  std::vector<std::string> aliases = ParseCpuDefNames();
  if (aliases.empty())
    aliases.push_back(name->name());

  if (!Token.expect(TK_LBRACE)) return nullptr;

  // for each non-terminal in cpu_stmt, collect information.
  while (!Token.accept(TK_RBRACE)) {
    switch (Token.getType()) {
      case TK_PIPE_PHASES: case TK_PROTECTED: case TK_UNPROTECTED: case TK_HARD:
          if (auto *pipe = ParsePipeDef())
            pipes->push_back(pipe);
          break;
      case TK_RESOURCE:
          if (auto *res = ParseResourceDef())
            resources->insert(resources->end(), res->begin(), res->end());
          break;
      case TK_FUNCUNIT:
          if (auto *func = ParseFuncUnitInstantiation(resources))
            func_units->push_back(func);
          break;
      case TK_FORWARD:
          if (auto *forward = ParseForwardStmt())
            forward_stmts->push_back(forward);
          break;
      case TK_ISSUE:
          if (auto *slots = ParseIssueStatement())
            issues->insert(issues->end(), slots->begin(), slots->end());
          break;
      case TK_CLUSTER:
          if (auto *cluster = ParseClusterInstantiation())
            clusters->push_back(cluster);
          break;
      case TK_REORDER_BUFFER:
          if (int size = ParseReorder(); size != 0)
            reorder_buffer_size = size;
          break;
      default:
          Error("Pipe definition, cluster, resource, func_unit, forward, "
                "reorder, or issue statement expected");
          if (spec().ErrorsSeen() > 30) {
            Error("Too many syntax errors, aborting");
            return nullptr;
          }
          Token.SkipUntil(TK_RESOURCE, TK_FUNCUNIT, TK_FORWARD, TK_RBRACE,
                          TK_PIPE_PHASES, TK_PROTECTED, TK_UNPROTECTED,
                          TK_HARD, TK_CLUSTER, TK_ISSUE, TK_REORDER_BUFFER);
          break;
    }
  }
  Token.accept(TK_SEMI);      // Skip optional semicolon

  // If we found functional unit instantiations at the cpu level, create a
  // cluster which contains those functional units. (This simplifies things
  // downstream.) Resources can remain global, but any issue resources must
  // also be associated with the generated cluster.
  // If we (still) didn't see any clusters, add an empty one.
  // NOTE: Currently, by design clusters are defined as having their own
  // issue slots. This enforces that rule. If we want clusters to access
  // CPU-level issue slots we'll have to redesign how this works.
  if (clusters->empty() || !func_units->empty()) {
    clusters->push_back(new ClusterInstance(item, new Identifier(item, "__"),
                                            issues, new ResourceDefList,
                                            func_units, new ForwardStmtList));
    issues = new ResourceDefList; // reset the issues list.
  }

  // Note: forward statements defined at the CPU level stay at the CPU level.
  return new CpuInstance(item, name, pipes, issues, resources,
                         reorder_buffer_size, clusters, forward_stmts, aliases);
}

//-----------------------------------------------------------------------------
// Process the optional cpu_def_names part of a CPU definition:
//      cpu_def_names: '(' STRING (',' STRING) ')'
// Return the set of quote-stripped strings.
//-----------------------------------------------------------------------------
std::vector<std::string> MdlParser::ParseCpuDefNames() {
  std::vector<std::string> aliases;
  if (Token.accept(TK_LPAREN)) {
    do {
      auto name = ParseString("Cpu name");
      if (name.empty()) return aliases;
      aliases.push_back(name);
    } while (Token.accept(TK_COMMA));
    Token.expect(TK_RPAREN);                // Eat the token
  }
  return aliases;
}

//-----------------------------------------------------------------------------
// Process cluster definition rules:
//      cluster_instantiation: CLUSTER ident '{' cluster_stmt+ '}' ';'
//      cluster_stmt: register_def | resource_def | issue_statement |
//                    func_unit_instantiation ;
// Return a ClusterInstance object that contains all the information.
//-----------------------------------------------------------------------------
ClusterInstance *MdlParser::ParseClusterInstantiation() {
  MdlItem item(Token());
  if (!Token.accept(TK_CLUSTER)) return nullptr;

  Identifier *name = ParseIdent("Cluster name");
  if (name == nullptr) return nullptr;

  if (!Token.expect(TK_LBRACE)) return nullptr;

  auto *issues = new ResourceDefList;
  auto *resources = new ResourceDefList;
  auto *func_units = new FuncUnitInstList;
  auto *forward_stmts = new ForwardStmtList;

  // For each rule in each cluster_stmt, collect and save information.
  while (!Token.accept(TK_RBRACE)) {
    switch (Token.getType()) {
      case TK_RESOURCE:
          if (ResourceDefList *res = ParseResourceDef())
            resources->insert(resources->end(), res->begin(), res->end());
          break;
      case TK_FUNCUNIT:
          if (auto *func = ParseFuncUnitInstantiation(resources))
            func_units->push_back(func);
          break;
      case TK_FORWARD:
          if (auto *forward = ParseForwardStmt())
            forward_stmts->push_back(forward);
          break;
      case TK_ISSUE:
          if (auto *slots = ParseIssueStatement())
            issues->insert(issues->end(), slots->begin(), slots->end());
          break;
      default:
          Error("Resource, func_unit, forward, or issue statement expected");
          if (spec().ErrorsSeen() > 30) {
            Error("Too many syntax errors, aborting");
            return nullptr;
          }
          Token.SkipUntil(TK_RBRACE, TK_RESOURCE, TK_FUNCUNIT, TK_FORWARD,
                          TK_ISSUE);
          break;
    }
  }
  Token.accept(TK_SEMI);      // Skip optional semicolon

  return new ClusterInstance(item, name, issues, resources, func_units,
                             forward_stmts);
}

//-----------------------------------------------------------------------------
// Process functional unit instance name:
//     func_unit_instance: ident ('<>' | '<' number '>')?
//-----------------------------------------------------------------------------
std::tuple<Identifier *, int, bool>
MdlParser::ParseFuncUnitInstType(std::string type) {
  Identifier *name = ParseIdent(type);
  if (name == nullptr) return { nullptr, 0, 0 };

  long buffer_size = -1;
  bool unreserved = false;

  // Parse optional buffer_size/unreserved clause.
  if (Token.accept(TK_LT)) {
    if (Token.check(TK_NUMBER)) {
      ParseNumber(buffer_size, "Buffer size");
    }
    if (!Token.expect(TK_GT)) return { nullptr, 0, 0 };
    if (buffer_size == -1)
      unreserved = true;
  }

  return { name, buffer_size, unreserved };
}

//-----------------------------------------------------------------------------
// Parse functional unit pinning clauses:
//     pin_any : '->' ident ('|' ident)+
//     pin_all : '->' ident ('&' ident)+
// Returns "any_list" and "all_list".
//-----------------------------------------------------------------------------
std::pair<IdList *, IdList *> MdlParser::ParsePins() {
  if (!Token.accept(TK_ARROW)) return { nullptr, nullptr };

  auto *issue = ParseIdent("Issue slot name");
  if (issue == nullptr) return { nullptr, nullptr };
  IdList *units = new IdList;
  units->push_back(issue);
  if (!Token.check(TK_AND, TK_OR)) return { nullptr, units };
  auto type = Token.getType();

  while (Token.accept(type)) {
    auto *issue = ParseIdent("Issue slot name");
    if (issue == nullptr) return { nullptr, nullptr };
    units->push_back(issue);
  }

  if (type == TK_AND) return { nullptr, units };
  return { units, nullptr };
}

//-----------------------------------------------------------------------------
// Process func_unit_instantiation rules:
//     func_unit_instantiation:
//                       FUNCUNIT func_unit_instance (':' func_unit_instance)*
//                       ident '(' resource refs ')'
//                       ('-> (pin_one | pin_any | pin_all))? ';' ;
// Return a FuncUnitInstance object that contains all the information.
//-----------------------------------------------------------------------------
FuncUnitInstance *
MdlParser::ParseFuncUnitInstantiation(ResourceDefList *resources) {
  MdlItem item(Token());
  if (!Token.expect(TK_FUNCUNIT)) return nullptr;

  // Parse the functional unit type.  Handle base types.
  auto [type, buffer_size, unreserved] =
                          ParseFuncUnitInstType("Functional unit type");
  if (type == nullptr) return nullptr;

  // Maintain a set of buffer sizes for each functional unit and base.
  std::map<std::string, int> buffer_sizes;
  buffer_sizes.emplace(type->name(), buffer_size);

  // Read any base units.  This will produce an implicitely defined template.
  IdList *bases = new IdList;
  while (Token.accept(TK_COLON)) {
    auto [btype, buffer_size, bunreserved] =
                          ParseFuncUnitInstType("Base functional unit type");
    if (btype == nullptr) return nullptr;
    buffer_sizes.emplace(btype->name(), buffer_size);
    bases->push_back(btype);
  }

  // Parse the functional unit instance name.
  auto *name = ParseIdent("Functional unit instance name");
  if (name == nullptr) return nullptr;

  // Parse the parameters to the instance.
  // Note: we don't currently allow implicitly defined instances to have
  // arguments passed to them.  If you want that, then define the template!
  if (!Token.expect(TK_LPAREN)) return nullptr;
  ResourceRefList *refs = nullptr;
  if (bases->empty())
    refs = ParseResourceRefs(resources);
  if (!Token.expect(TK_RPAREN)) return nullptr;

  // Parse any functional unit instance pinning.
  auto [pin_any, pin_all] = ParsePins();

  if (!Token.expect(TK_SEMI)) return nullptr;

  // If we see a list of bases, create a functional unit template for it.
  if (!bases->empty()) {
    // Create an aggregate functional unit template, including the top-level
    // type in the aggregate.
    bases->insert(bases->begin(), type);
    auto derived = based_fu_instance(bases);
    type = new Identifier(*type, derived);
    if (FindItem(spec().func_units(), derived) == nullptr)
      spec().func_units().push_back(new FuncUnitTemplate(item, type, bases));
  }

  if (refs == nullptr) refs = new ResourceRefList;
  return new FuncUnitInstance(item, type, name, unreserved, buffer_sizes, refs,
                              pin_any, pin_all);
}

//-----------------------------------------------------------------------------
// Process a single CPU forward statement:
//     forward_stmt : FORWARD ident '->' forward_to_unit (',' forward_to_unit)?
//     forward_to_unit : ident ('(' snumber ')')?
//-----------------------------------------------------------------------------
ForwardStmt *MdlParser::ParseForwardStmt() {
  MdlItem item(Token());
  if (!Token.expect(TK_FORWARD)) return nullptr;

  ForwardToSet to_units;
  Identifier *from_unit = ParseIdent("Functional unit name");
  if (from_unit == nullptr) return nullptr;
  if (!Token.expect(TK_ARROW)) return nullptr;

  do {
    auto *to_unit = ParseIdent("Functional unit name");
    if (to_unit == nullptr) return nullptr;
    long cycles = 0;
    if (Token.accept(TK_LPAREN)) {
      if (!ParseSignedNumber(cycles, "Number of cycles")) return nullptr;
      if (!Token.expect(TK_RPAREN)) return nullptr;
    }
    to_units.emplace_back(to_unit, cycles);
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);

  return new ForwardStmt(item, from_unit, to_units);
}

//-----------------------------------------------------------------------------
// Process function_unit_template and func_unit_template_stmt rules:
//  func_unit_template     : FUNCUNIT ident (':' base)* func_unit_params
//                           '{' func_unit_template_stmt* '}' ';'
//  func_unit_template_stmt: resource_def | port_def |
//                              connect_stmt | subunit_instantiation ;
// Return a FuncUnitTemplate object that contains all the information.
//-----------------------------------------------------------------------------
FuncUnitTemplate *MdlParser::ParseFuncUnitTemplate() {
  MdlItem item(Token());
  auto *ports = new IdList;
  auto *resources = new ResourceDefList;
  auto *connects = new ConnectList;
  auto *subunits = new SubUnitInstList;
  if (!Token.accept(TK_FUNCUNIT)) return nullptr;

  Identifier *type = ParseIdent("Functional unit name");
  IdList *bases = nullptr;
  if (type == nullptr) return nullptr;

  if (Token.check(TK_COLON)) {
    bases = ParseBaseList("Functional unit base name");
    if (bases == nullptr) return nullptr;
  }
  ParamsList *params = ParseFuncUnitParams();

  if (!Token.expect(TK_LBRACE)) return nullptr;

  // Process each rule matched in template statements, save off info.
  do {
    switch (Token.getType()) {
      case TK_RESOURCE:
        if (auto *res = ParseResourceDef())
          resources->insert(resources->end(), res->begin(), res->end());
        break;
      case TK_PORT:
        if (auto *defs = ParsePortDef(connects))
          ports->insert(ports->end(), defs->begin(), defs->end());
        break;
      case TK_CONNECT:
        if (auto *conn = ParseConnectStmt())
          connects->push_back(conn);
        break;
      case TK_IDENT:       // could be a predicated subunit.
      case TK_SUBUNIT:
        if (auto *items = ParseSubunitInstantiation(resources))
          subunits->insert(subunits->end(), items->begin(), items->end());
        break;
      case TK_RBRACE:
        break;
      default:
        Error("Resource, port, connect, or subunit statement expected");
        if (spec().ErrorsSeen() > 30) {
          Error("Too many syntax errors, aborting");
          return nullptr;
        }
        Token.SkipUntil(TK_RBRACE, TK_RESOURCE, TK_PORT, TK_CONNECT,
                        TK_SUBUNIT);
        break;
    }
  } while (!Token.accept(TK_RBRACE));

  Token.accept(TK_SEMI);     // Skip optional semicolon

  return new FuncUnitTemplate(item, type, bases, params, ports, resources,
                              connects, subunits);
}

//-----------------------------------------------------------------------------
// Process a functional unit group definition:
//     func_unit_group : FUNCGROUP ident ('<' number '>')? ':' name_list ';'
//-----------------------------------------------------------------------------
FuncUnitGroup *MdlParser::ParseFuncUnitGroup() {
  MdlItem item(Token());
  if (!Token.expect(TK_FUNCGROUP)) return nullptr;

  Identifier *name = ParseIdent("Group name");
  if (name == nullptr) return nullptr;

  long buffer_size = -1;
  if (Token.accept(TK_LT)) {
    if (!ParseNumber(buffer_size, "Buffer size")) return nullptr;
    if (!Token.expect(TK_GT)) return nullptr;
  }
  if (!Token.expect(TK_COLON)) return nullptr;

  IdList *members = ParseNameList("Functional unit name", TK_COMMA);
  if (members == nullptr) return nullptr;
  Token.expect(TK_SEMI);

  return new FuncUnitGroup(item, name, buffer_size, members);
}

//-----------------------------------------------------------------------------
// Process func_unit_params rules: (arguments to a functional unit template).
//      func_unit_params : '(' (fu_decl_item (';' fu_decl_item)*)? ')'
// Return a vector of parameters.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::ParseFuncUnitParams() {
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *params = new ParamsList;
  if (Token.accept(TK_RPAREN)) return params;        // Empty params list

  do {
    ParamsList *param = ParseFuDeclItem();
    if (param == nullptr) return nullptr;
    params->insert(params->end(), param->begin(), param->end());
  } while (Token.accept(TK_SEMI));

  if (!Token.expect(TK_RPAREN)) return nullptr;
  return params;  // return the list of parameters.
}

//-----------------------------------------------------------------------------
// Process fu_decl_item rules:
//      func_decl_item : RESOURCE name_list | CLASS name_list
// Each namelist can define a list of parameters. We want to flatten those
// lists to a single list of resources and classes, and return a single list
// of class and resource definitions.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::ParseFuDeclItem() {
  ParamType type;
  if (Token.accept(TK_RESOURCE)) type = kParamResource;
  else if (Token.accept(TK_CLASS)) type = kParamClass;
  else return Error("Register or Class declaration expected"), nullptr;
  auto kind = (type == kParamResource) ? "Resource name" : "Class name";

  IdList *names = ParseNameList(kind, TK_COMMA);
  if (names == nullptr) return nullptr;

  auto *params = new ParamsList;
  for (auto *name : *names)
    params->push_back(new Params(*name, name, type));
  return params;
}

//-----------------------------------------------------------------------------
// Process the port_def rules: (part of a functional unit template definition).
//      PORT port_decl (',' port_decl )* ';'
// Return a list of port definitions.
//-----------------------------------------------------------------------------
IdList *MdlParser::ParsePortDef(ConnectList *connects) {
  if (!Token.accept(TK_PORT)) return nullptr;
  auto *names = new IdList;

  do {
    auto *port = ParsePortDecl(connects);
    if (port == nullptr) return nullptr;
    names->push_back(port);
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);
  return names;
}

//-----------------------------------------------------------------------------
// Process a single port definition.  The definition may optionally include
// a register class and a list of resource references.
//     ident ('<' reg_class=ident '>')? ('(' ref=resource_ref ')')?
// If a declaration contains connection information, create CONNECT records.
//-----------------------------------------------------------------------------
Identifier *MdlParser::ParsePortDecl(ConnectList *connects) {
  auto *name = ParseIdent("Port name");
  if (name == nullptr) return nullptr;
  Identifier *reg_class = nullptr;
  ResourceRef *ref = nullptr;

  if (Token.accept(TK_LT)) {               // Parse a class name
    reg_class = ParseIdent("Register class name");
    if (reg_class == nullptr || !Token.expect(TK_GT)) return nullptr;
  }

  if (Token.accept(TK_LPAREN)) {           // parse a resource reference
    ref = ParseResourceRef();
    if (ref == nullptr || !Token.expect(TK_RPAREN)) return nullptr;
  }

  if (reg_class != nullptr || ref != nullptr)
    connects->push_back(new Connect(*name, name, reg_class, ref));
  return name;
}

//-----------------------------------------------------------------------------
// Process connect_stmt rules: (part of a functional unit template definition).
//      CONNECT ident ('to' ident)? ('via' resource_ref)? ';' ;
// Return a Connect object that contains all the information.
//-----------------------------------------------------------------------------
Connect *MdlParser::ParseConnectStmt() {
  MdlItem item(Token());
  if (!Token.accept(TK_CONNECT)) return nullptr;

  ResourceRef *ref = nullptr;
  Identifier *reg_class = nullptr;
  Identifier *port = ParseIdent("Port name");
  if (port == nullptr) return nullptr;

  if (Token.accept(TK_TO)) {
    reg_class = ParseIdent("Register class name");
    if (reg_class == nullptr) return nullptr;
  }
  if (Token.accept(TK_VIA)) {
    ref = ParseResourceRef();
    if (ref == nullptr) return nullptr;
  }
  return new Connect(item, port, reg_class, ref);
}

//-----------------------------------------------------------------------------
// Process subunit_instantiation rules: (also part of func unit definitions).
//   subunit_instantiation:
//                     (predicate=name_list ':')? subunit_statement
//                   | (predicate=name_list ':' '{' subunit_statement* '}' ';'
// Return a SubUnitInstance object that contains the information.
//-----------------------------------------------------------------------------
SubUnitInstList *
MdlParser::ParseSubunitInstantiation(ResourceDefList *resources) {
  // Parse an optional leading predicate.
  IdList *predicate = nullptr;
  if (Token.check(TK_IDENT))
    if ((predicate = ParsePredicateList()) == nullptr) return nullptr;

  auto *stmts = new SubUnitInstList;
  // Parse a predicated list of subunit statements.
  if (predicate != nullptr && Token.accept(TK_LBRACE)) {
    do {
      auto subunits = ParseSubunitStatement(predicate, resources);
      if (subunits != nullptr)
        stmts->insert(stmts->end(), subunits->begin(), subunits->end());
    } while (!Token.accept(TK_RBRACE));
    Token.accept(TK_SEMI);            // Skip optional semicolon
    return stmts;
  }

  // Parse a single statement (predicated or not)
  auto *subunits = ParseSubunitStatement(predicate, resources);
  if (subunits != nullptr)
    stmts->insert(stmts->end(), subunits->begin(), subunits->end());

  return stmts;
}

//-----------------------------------------------------------------------------
// Process subunit_statement rules:
//     subunit_statement: SUBUNIT subunit_instance (',' subunit_instance)* ';'
//     subunit_instance:  ident '(' resource_refs ')'
//-----------------------------------------------------------------------------
SubUnitInstList *MdlParser::ParseSubunitStatement(IdList *predicate,
                                                  ResourceDefList *resources) {
  MdlItem item(Token());
  if (!Token.expect(TK_SUBUNIT)) return nullptr;
  auto *subunits = new SubUnitInstList;

  do {
    auto *name = ParseIdent("Subunit type");
    if (name == nullptr) return nullptr;
    if (!Token.expect(TK_LPAREN)) return nullptr;
    auto *args = ParseResourceRefs(resources);
    if (args == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;

    subunits->push_back(new SubUnitInstance(item, name, args, predicate));
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);
  return subunits;
}

//-----------------------------------------------------------------------------
// Process subunit_template rules:
//     subunit_template: SUBUNIT ident (':' ident)? '(' su_decl_items ')'
//                 (('{' latency_instance* '}') | ('{{' latency_items? '}}') );
// Return a SubUnitTemplate object that contains all the information.
//-----------------------------------------------------------------------------
SubUnitTemplate *MdlParser::ParseSubunitTemplate() {
  MdlItem item(Token());
  if (!Token.accept(TK_SUBUNIT)) return nullptr;
  Identifier *name = ParseIdent("Subunit template name");
  if (name == nullptr) return nullptr;

  StringList *regex = nullptr;
  IdList *bases = nullptr;
  ParseSuBaseList(bases, regex);

  ParamsList *params = ParseSuDeclItems();
  if (params == nullptr) return nullptr;

  auto *latencies = new LatencyInstList;

  // If the body of the subunit is a list of subunit statements, return a list
  // of those instantiations.
  if (Token.accept(TK_LBRACE)) {
    while (!Token.accept(TK_RBRACE)) {
      auto *lats = ParseLatencyInstance();
      if (lats == nullptr) return nullptr;
      latencies->insert(latencies->end(), lats->begin(), lats->end());
    }
    Token.accept(TK_SEMI);    // Skip optional semicolon
    return new SubUnitTemplate(item, name, bases, regex, params, latencies,
                               nullptr);
  }

  // If the body of the subunit is an inlined latency template, create a
  // new latency template (with the same name as the subunit), and create a
  // latency instance which refers to that latency template. The created
  // latency template will be returned as part of the subunit.
  LatencyTemplate *inline_lat = nullptr;
  if (Token.accept(TK_2LBRACE)) {
    auto *refs = new ReferenceList;
    while (!Token.accept(TK_2RBRACE)) {
      auto *items = ParseLatencyItems(nullptr);
      if (items == nullptr) return nullptr;
      refs->insert(refs->end(), items->begin(), items->end());
      delete items;
    }
    Token.accept(TK_SEMI);    // Skip optional semicolon

    // Create a new latency template, add it to spec.
    Identifier *tname = new Identifier(name->name());
    ParamsList *tparams = CopySuDeclItems(params);
    inline_lat = new LatencyTemplate(item, tname, nullptr, tparams, refs);
    spec().latencies().push_back(inline_lat);

    // Create an instance for the new latency template for this subunit.
    Identifier *instname = new Identifier(name->name());
    ResourceRefList *args = new ResourceRefList;
    for (auto *param : *params)
      args->push_back(new ResourceRef(*param->id(), param->id()));
    latencies->push_back(new LatencyInstance(item, instname, args, nullptr));

    return new SubUnitTemplate(item, name, bases, regex, params, latencies,
                               inline_lat);
  }
  return Error("Invalid subunit template definition"), nullptr;
}

//-----------------------------------------------------------------------------
// Process su_decl_items rules: (part of a subunit template definition).
//      su_decl_items: '(' su_decl_item (';' su_decl_item)* ')'
// Return a list of subunit parameters.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::ParseSuDeclItems() {
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *params = new ParamsList;

  // If there are no parameters, we're done.
  if (Token.accept(TK_RPAREN)) return params;

  // Parse and append the lists of resources or ports together.
  do {
    auto *param = ParseSuDeclItem();
    if (param == nullptr) return nullptr;
    params->insert(params->end(), param->begin(), param->end());
  } while (Token.accept(TK_SEMI));
  if (!Token.expect(TK_RPAREN)) return nullptr;
  return params; // return the list of declared items.
}

//-----------------------------------------------------------------------------
// Process su_decl_item rules: (part of a subunit template definition).
//      su_decl_item: RESOURCE name_list | PORT name_list ;
// Return a list of resource or port parameter definitions.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::ParseSuDeclItem() {
  ParamType type;
  if (Token.accept(TK_RESOURCE)) type = kParamResource;
  else if (Token.accept(TK_PORT)) type = kParamPort;
  else return Error("Resource or Port declaration expected"), nullptr;
  auto kind = (type == kParamResource) ? "Resource name" : "Port name";

  IdList *names = ParseNameList(kind, TK_COMMA);
  if (names == nullptr) return nullptr;

  auto *params = new ParamsList;
  for (auto *name : *names)
    params->push_back(new Params(*name, name, type));

  delete names;
  return params; // return the list of resources or ports.
}

//-----------------------------------------------------------------------------
// Make a copy of a Subunit parameter declaration list.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::CopySuDeclItems(ParamsList *params) {
  auto *copy = new ParamsList;

  for (auto *param : *params)
    copy->push_back(
        new Params(*param, new Identifier(param->id(), 0), param->type()));

  return copy;
}


//-----------------------------------------------------------------------------
// Process latency_instance rules: (part of a subunit template definition).
//   latency_instance: (predicate=name_list ':')? latency_statement
//                   | (predicate=name_list ':' '{' latency_statement* '}' ';'?
// Return a LatencyInstance object that contains all the information.
//-----------------------------------------------------------------------------
LatencyInstList *MdlParser::ParseLatencyInstance() {
  IdList *predicate = nullptr;
  // Parse an optional leading predicate.
  if (Token.check(TK_IDENT))
    if ((predicate = ParsePredicateList()) == nullptr) return nullptr;

  auto *stmts = new LatencyInstList;
  if (predicate != nullptr && Token.accept(TK_LBRACE)) {
    do {
      auto *stmt = ParseLatencyStatement(predicate);
      if (stmt == nullptr) return nullptr;
      stmts->push_back(stmt);
    } while (!Token.expect(TK_RBRACE));

    Token.accept(TK_SEMI);        // Skip optional semi
    return stmts;
  }

  // Otherwise parse just a single latency statement.
  auto stmt = ParseLatencyStatement(predicate);
  if (stmt == nullptr) return nullptr;
  stmts->push_back(stmt);
  return stmts;
}

//-----------------------------------------------------------------------------
// Process latency_statement rules:
//     latency_statement: LATENCY ident '(' resource_refs? ')' ';'
// Return a LatencyInstance object that contains all the information.
//-----------------------------------------------------------------------------
LatencyInstance *MdlParser::ParseLatencyStatement(IdList *predicates) {
  MdlItem item(Token());
  if (!Token.expect(TK_LATENCY)) return nullptr;

  auto *name = ParseIdent("Latency template name");
  if (name == nullptr) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *args = ParseResourceRefs();
  if (!Token.expect(TK_RPAREN)) return nullptr;
  if (!Token.expect(TK_SEMI)) return nullptr;

  return new LatencyInstance(item, name, args, predicates);
}

//-----------------------------------------------------------------------------
// Process latency_template rules.
//     latency_template: LATENCY ident (':' ident)* su_decl_items
//                                     latency_block
// Return a LatencyTemplate object that contains all the information.
//-----------------------------------------------------------------------------
LatencyTemplate *MdlParser::ParseLatencyTemplate() {
  MdlItem item(Token());
  if (!Token.accept(TK_LATENCY)) return nullptr;

  IdList *bases = nullptr;
  Identifier *name = ParseIdent("Latency template name");
  if (name == nullptr) return nullptr;
  if (Token.check(TK_COLON)) {
    bases = ParseBaseList("Latency template base name");
    if (bases == nullptr) return nullptr;
  }
  ParamsList *params = ParseSuDeclItems();
  if (params == nullptr) return nullptr;
  ReferenceList *refs = ParseLatencyBlock(nullptr);
  if (refs == nullptr) return nullptr;

  return new LatencyTemplate(item, name, bases, params, refs);
}

//-----------------------------------------------------------------------------
// Process a block of latency items:
//      latency_block: '{' latency_items* '}'
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::ParseLatencyBlock(IdList *predicates) {
  if (!Token.expect(TK_LBRACE)) return nullptr;
  auto *references = new ReferenceList;
  while (!Token.accept(TK_RBRACE)) {
    auto *refs = ParseLatencyItems(predicates);
    if (refs == nullptr) return nullptr;
    references->insert(references->end(), refs->begin(), refs->end());
    delete refs;
  };
  Token.accept(TK_SEMI);
  return references;
}

//-----------------------------------------------------------------------------
// Process latency_items rules: (part of a latency template definition).
//      latency_items: (name_list ':')? (latency_item | ('{' latency_item* '}')
// Return a list of Reference objects.
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::ParseLatencyItems(IdList *predicates) {
  if (predicates && Token.check(TK_IDENT))
    return Error("Nested predicates not allowed"), nullptr;

  if (Token.check(TK_IDENT)) {
    predicates = ParsePredicateList();
    if (predicates == nullptr) return nullptr;
  }
  if (Token.check(TK_LBRACE))
    return ParseLatencyBlock(predicates);

  // Parse a reference. This could return multiple references.
  return ParseLatencyItem(predicates);
}

//-----------------------------------------------------------------------------
// Process latency_item rules: (part of a latency template definition).
//    latency_item : latency_ref
//                 | conditional_ref
//                 | fu_statement ;
// Return a single Reference object that describes a single latency.
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::ParseLatencyItem(IdList *predicates) {
  if (auto *lat = ParseLatencyRef(predicates)) {
    auto *refs = new ReferenceList;
    refs->push_back(lat);
    return refs;
  } else if (auto *cref = ParseConditionalRef(predicates)) {
    auto *refs = new ReferenceList;
    refs->push_back(new Reference(*cref, predicates, cref));
    return refs;
  } else if (auto *refs = ParseFusStatement(predicates)) {
    return refs;
  }

  Token.SkipUntil(TK_SEMI, TK_USE, TK_DEF, TK_KILL, TK_USEDEF, TK_RES, TK_HOLD,
            TK_FUS, TK_IF, TK_ELSE);
  return Error("Invalid latency statement"), nullptr;
}

//-----------------------------------------------------------------------------
// Process a conditional reference statement.
//   conditional_ref : 'if' ident '{' latency_item* '}'
//                                   (conditional_elseif | conditional_else)?
//   conditional_elseif : 'else' 'if' ident '{' latency_item* '}'
//                                   (conditional_elseif | conditional_else)?
//   conditional_else : 'else' '{' latency_item* '}'
//
// Note: predicates are not passed into conditional reference blocks.  This is
// actually a feature - the block can have its own predicates.
//-----------------------------------------------------------------------------
ConditionalRef *MdlParser::ParseConditionalRef(IdList *predicates) {
  MdlItem item(Token());
  if (!Token.accept(TK_IF)) return nullptr;
  auto *pred = ParseIdent("Predicate name");
  if (pred == nullptr) return nullptr;
  auto *refs = ParseLatencyBlock(nullptr);   //  '{' latency_items* '}'
  if (refs == nullptr) return nullptr;

  ConditionalRef *else_clause = nullptr;
  if (Token.accept(TK_ELSE)) {
    if (Token.check(TK_IF)) {
      else_clause = ParseConditionalRef(nullptr);  // recur!
      if (else_clause == nullptr) return nullptr;
    }
    else if (Token.check(TK_LBRACE)) {
      auto *block = ParseLatencyBlock(nullptr);    //  '{' latency_items* '}'
      if (block == nullptr) return nullptr;
      else_clause = new ConditionalRef(item, nullptr, block, nullptr);
    }
    else return Error("Invalid if/then/else construct"), nullptr;
  }

  return new ConditionalRef(item, pred, refs, else_clause);
}

//-----------------------------------------------------------------------------
// Process latency_ref rules: (part of a latency template definition).
//     latency_ref: ref_type '(' latency_spec ')' ';'
//     latency_spec: phase_expr (':' cycles=number)? ',' latency_resource_refs
//                 | phase_expr ('[' repeat=number (',' delay=number)? ']')?
//                              ',' operand
//                 | phase_expr ',' operand
//                 | phase_expr ',' latency_resource_refs
//                 | phase_expr ',' operand ',' latency_resource_refs
// Return a single Reference object that describes a single latency.
//-----------------------------------------------------------------------------
Reference *MdlParser::ParseLatencyRef(IdList *predicates) {
  MdlItem item(Token());
  auto ref_type = ParseRefType();
  if (ref_type == RefTypes::kNull) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;

  // Default values for different components of the latency statement.
  OperandRef *opnd = nullptr;
  ResourceRefList *refs = nullptr;
  long cycles = 1;
  long repeat = 1;
  long delay = 1;

  auto *phase = ParseExpr();
  if (phase == nullptr) return nullptr;

  // Parse a resource refs statement, which may include a cycle count.
  if (Token.accept(TK_COLON)) {
    if (!ParseNumber(cycles, "Cycles")) return nullptr;
    if (!Token.expect(TK_COMMA)) return nullptr;
    if ((refs = ParseLatencyResourceRefs()) == nullptr) return nullptr;
  }

  // Parse an operand ref that may include a repeat/delay clause.
  else if (Token.accept(TK_LBRACKET)) {
    if (!ParseNumber(repeat, "Cycles")) return nullptr;
    if (Token.accept(TK_COMMA) && !ParseNumber(delay, "Delay")) return nullptr;
    if (!Token.expect(TK_RBRACKET)) return nullptr;
    if (!Token.expect(TK_COMMA)) return nullptr;
    if (!isOperand() || (opnd = ParseOperand()) == nullptr) return nullptr;
  }

  // Parse a combined operand/resource reference (no modifiers allowed).
  else {
    if (!Token.expect(TK_COMMA)) return nullptr;
    if (isOperand()) {
      if ((opnd = ParseOperand()) == nullptr) return nullptr;
      if (Token.accept(TK_COMMA))
        if ((refs = ParseLatencyResourceRefs()) == nullptr) return nullptr;
    }
    else
      if ((refs = ParseLatencyResourceRefs()) == nullptr) return nullptr;
  }

  if (!Token.expect(TK_RPAREN)) return nullptr;
  Token.accept(TK_SEMI);
  if (refs == nullptr) refs = new ResourceRefList;
  return new Reference(item, predicates, ref_type, phase, repeat, delay, cycles,
                       opnd, refs);
}

//-----------------------------------------------------------------------------
// Parse the type of a latency reference.
//-----------------------------------------------------------------------------
RefType MdlParser::ParseRefType() {
  if (!Token.check(TK_USE, TK_DEF, TK_USEDEF, TK_KILL,
                   TK_HOLD, TK_RES, TK_PREDICATE)) return RefTypes::kNull;
  auto ref_type = StringToRefType(Token.getText());
  ++Token;
  return ref_type;
}

//-----------------------------------------------------------------------------
// Process latency resource_refs rules.
//     latency_resource_refs: latency_resource_ref (',' latency_resource_ref)*
// Return a list of resource references.
//-----------------------------------------------------------------------------
ResourceRefList *MdlParser::ParseLatencyResourceRefs() {
  auto *refs = new ResourceRefList;
  do {
    auto *ref = ParseLatencyResourceRef();
    if (ref == nullptr) return nullptr;
    refs->push_back(ref);
  } while (Token.accept(TK_COMMA));

  return refs;
}

//-----------------------------------------------------------------------------
// Process resource_ref rules.  Handle a single resource reference.
//   latency_resource_ref: resource_ref ':' number (':' ident)?
//                       | resource_ref ':' ident (':' ident)?
//                       | resource_ref ':' ':' ident
//                       | resource_ref ':' '*'
//                       | resource_ref
// Return a single resource reference object.
//-----------------------------------------------------------------------------
ResourceRef *MdlParser::ParseLatencyResourceRef() {
  ResourceRef *res = ParseResourceRef();
  if (res == nullptr) return nullptr;
  long count = -1;

  if (Token.accept(TK_COLON)) {                       // ref : *
    if (Token.accept(TK_MUL)) {
      res->set_use_all_members();
      return res;
    }
    if (Token.accept(TK_COLON)) {                     // ref : : mask
      auto *value_mask = ParseIdent("Value mask");
      if (value_mask == nullptr) return nullptr;
      res->set_value_name(value_mask);
      return res;
    }

    if (Token.check(TK_NUMBER)) {                    // ref : number
      if (!ParseNumber(count, "Resource count")) return nullptr;
      res->set_pool_count(count);
    }
    else if (isIdent()) {                             // ref : ident
      auto *countname = ParseIdent("Value mask");
      if (countname == nullptr) return nullptr;
      res->set_pool_count_name(countname);
    }

    if (Token.accept(TK_COLON)) {                     // ref : ... : value
      auto *value_mask = ParseIdent("Value mask");
      if (value_mask == nullptr) return nullptr;
      res->set_value_name(value_mask);
    }
  }
  return res;
}

//-----------------------------------------------------------------------------
// Process resource_refs rules.  Return a list of resource references.
//     resource_refs: resource_ref (',' resource_ref)*
// Return a list of resource references.
//-----------------------------------------------------------------------------
ResourceRefList *
MdlParser::ParseResourceRefs(ResourceDefList *resources /* = nullptr */) {
  auto *refs = new ResourceRefList;
  if (Token.check(TK_RPAREN)) return refs;
  do {
    auto *ref = ParseResourceRef(resources);
    if (ref == nullptr) return nullptr;
    refs->push_back(ref);
  } while (Token.accept(TK_COMMA));

  return refs;
}

//-----------------------------------------------------------------------------
// Process resource_ref rules.  Handle a single resource reference.
//   resource_ref : ident '.' ident           // specify which member
//                | ident '[' range ']'       // specify a range of members
//                | ident '[' number ']'      // specify a single member
//                | ident ('|' ident)+        // implicitly defined group
//                | ident ('&' ident)+        // implicitly defined group
//                | ident ;
// Return a single resource reference object.
//-----------------------------------------------------------------------------
ResourceRef *
MdlParser::ParseResourceRef(ResourceDefList *resources /* = nullptr */) {
  MdlItem item(Token());
  Identifier *name = ParseIdent("Resource name");
  if (name == nullptr) return nullptr;

  if (Token.accept(TK_DOT)) {                     // ident '.' member
    Identifier *id = ParseIdent("Member name");
    if (id == nullptr) return nullptr;
    return new ResourceRef(*id, name, id);
  }
  if (Token.accept(TK_LBRACKET)) {                // ident '[' range|number ']'
    long first = -1, last = -1;
    std::string type = Token.peek(TK_DOTDOT) ? "Range start" : "Index";
    if (!ParseNumber(first, type)) return nullptr;
    if (Token.accept(TK_DOTDOT)) {
      if (!ParseNumber(last, "Range end")) return nullptr;
    }
    if (!Token.expect(TK_RBRACKET)) return nullptr;
    if (last != -1)
      return new ResourceRef(item, name, first, last);
    return new ResourceRef(item, name, first, first);
  }

  if (!Token.check(TK_OR, TK_AND))            // just an ident.
    return new ResourceRef(item, name);

  // Handle implicitly defined resource groups.
  int index = 0;
  auto *members = new IdList;
  members->push_back(name);
  name->set_index(index++);
  auto group_type = Token.getType();             // save off first separator

  while (Token.accept(group_type)) {             // ident | ident | ident ...
    auto *member = ParseIdent("Member name");
    if (member == nullptr) return nullptr;
    member->set_index(index++);
    members->push_back(member);
  }

  if (resources == nullptr) {
    spec().ErrorLog(&item, "Implicit group definition not allowed");
    return new ResourceRef(item, name);
  }

  // Create a resource group in the scope surrounding the reference.
  // Register it in the parent's resource list.
  static int implicit_id = 0;
  name = new Identifier(item, formatv("<group_{0}>", implicit_id++));
  auto *def = new ResourceDef(item, name, -1, members, nullptr, nullptr);
  def->set_implicit_group();
  def->set_group_type(group_type == TK_AND ? GroupType::kUseAll
                                           : GroupType::kUseSingle);
  resources->push_back(def);
  return new ResourceRef(item, name);
}

//-----------------------------------------------------------------------------
// Process a functional unit usage statement.
//    fus_statement: FUS '(' (fus_item ('&' fus_item)* ',') snumber
//                                  (',' fus_attribute)* ')' ';'
//    fus_attribute : BEGINGROUP | ENDGROUP | SINGLEISSUE  | RETIREOOO
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::ParseFusStatement(IdList *predicates) {
  MdlItem item(Token());
  if (!Token.accept(TK_FUS)) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;

  auto *refs = new ReferenceList;

  // Parse the list of optional fus_items.
  if (isIdent()) {
    do {
      auto *ref = ParseFusItem(predicates, item);
      if (ref == nullptr) return nullptr;
      refs->push_back(ref);
    } while (Token.accept(TK_AND));
    if (!Token.expect(TK_COMMA)) return nullptr;
  }

  // Parse the micro_ops field.  Record it in the first reference (only)
  long micro_ops;
  if (!ParseSignedNumber(micro_ops, "Micro-ops")) return nullptr;
  if (!refs->empty())
    refs->front()->set_micro_ops(micro_ops);

  // If we see functional unit attributes, collect them.
  RefFlags::Item flags = RefFlags::kNone;
  while (Token.accept(TK_COMMA)) {
    if (Token.accept(TK_BEGINGROUP))
      flags |= RefFlags::kBeginGroup;
    else if (Token.accept(TK_ENDGROUP))
      flags |= RefFlags::kEndGroup;
    else if (Token.accept(TK_SINGLEISSUE))
      flags |= RefFlags::kSingleIssue;
    else if (Token.accept(TK_RETIREOOO))
      flags |= RefFlags::kRetireOOO;
    else return Error("Unrecognized functional unit attribute"), nullptr;
  }
  if (!Token.expect(TK_RPAREN)) return nullptr;
  Token.accept(TK_SEMI);

  // If there weren't any functional units specified, generate a single
  // micro_ops reference.
  if (refs->empty()) {
    refs->push_back(new Reference(item, predicates, micro_ops, flags));
    return refs;
  }

  // If we found flags, annotate all the fu references.
  if (flags != RefFlags::kNone)
    for (auto *ref : *refs)
      ref->set_fu_flags(flags);

  // Note that we've seen an Fus reference.  If none seen, we can avoid looking
  // for them later.
  spec().set_explicit_fu_refs();
  return refs;
}

//-----------------------------------------------------------------------------
// Process an fus_item:
//    fus_item: ident ('<' (expr ':')? number '>')?
//-----------------------------------------------------------------------------
Reference *MdlParser::ParseFusItem(IdList *predicates, MdlItem &item) {
  auto *fu = ParseIdent("Functional unit name");
  if (fu == nullptr) return nullptr;

  PhaseExpr *expr = nullptr;
  long cycles = 1;

  // Parse the optional Phase/Cycles expressions. Note that the expression
  // is optional as well, so we check for the number first.
  if (Token.accept(TK_LT)) {
    if (Token.check(TK_NUMBER) && Token.peek(TK_GT)) {
      ParseNumber(cycles, "Cycle count");
    } else {
      if ((expr = ParseExpr()) == nullptr) return nullptr;
      if (!Token.expect(TK_COLON)) return nullptr;
      if (!ParseNumber(cycles, "Cycle count")) return nullptr;
    }
    if (!Token.expect(TK_GT)) return nullptr;
  }

  return new Reference(item, predicates, RefTypes::kFus, expr,
                    cycles, 0, RefFlags::kNone, new ResourceRef(*fu, fu));
}

//-----------------------------------------------------------------------------
// Process expr rules.  These are part of latency specs, and are a limited
// set of operations for calculating pipeline latencies:
//     expr : (term '+' term) | (term  '-' term)
// Return a single expression tree root.
//-----------------------------------------------------------------------------
PhaseExpr *MdlParser::ParseExpr() {
  MdlItem item(Token());
  PhaseExpr *lhs = nullptr, *rhs = nullptr;
  if ((lhs = ParseTerm()) == nullptr) return nullptr;
  while (auto op = Token.accept(TK_PLUS, TK_MINUS)) {
    if ((rhs = ParseTerm()) == nullptr) return nullptr;
    lhs = new PhaseExpr(item, (op == TK_PLUS) ? kPlus : kMinus, lhs, rhs);
  }
  return lhs;
}

//-----------------------------------------------------------------------------
//  Parse a "term" expression component
//     term : (factor '*' factor) | (factor '/' factor)
//-----------------------------------------------------------------------------
PhaseExpr *MdlParser::ParseTerm() {
  MdlItem item(Token());
  PhaseExpr *lhs = nullptr, *rhs = nullptr;
  if ((lhs = ParseFactor()) == nullptr) return nullptr;
  while (auto op = Token.accept(TK_MUL, TK_DIV)) {
    if ((rhs = ParseFactor()) == nullptr) return nullptr;
    lhs = new PhaseExpr(item, (op == TK_MUL) ? kMult : kDiv , lhs, rhs);
  }
  return lhs;
}

//-----------------------------------------------------------------------------
//  Parse a "factor" expression component
//    factor : '(' expr ')'         (subexpression)
//           | '{' expr '}'         (floor of 0)
//           | '-' expr             (unary negate)
//           | ident                (phase name)
//           | operand              (operand reference)
//           | number
//-----------------------------------------------------------------------------
PhaseExpr *MdlParser::ParseFactor() {
  MdlItem item(Token());
  PhaseExpr *expr;
  if (Token.accept(TK_LPAREN)) {
    if ((expr = ParseExpr()) == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;
    return expr;
  }
  if (Token.accept(TK_LBRACE)) {
    if ((expr = ParseExpr()) == nullptr) return nullptr;
    if (!Token.expect(TK_RBRACE)) return nullptr;
    return new PhaseExpr(item, kPositive, expr, nullptr);
  }
  if (Token.accept(TK_MINUS)) {
    if ((expr = ParseExpr()) == nullptr) return nullptr;
    return new PhaseExpr(item, kNeg, expr, nullptr);
  }

  // Parse an operand reference ( (ident ':')? '$' ident, etc)
  if (isOperand()) {
    auto *opnd = ParseOperand();
    if (opnd) return new PhaseExpr(item, kOpnd, opnd);
  }
  if (isIdent()) {
    return new PhaseExpr(item, kPhase, ParseIdent("Phase name"));
  }
  if (Token.check(TK_NUMBER)) {
    long number;
    ParseNumber(number, "Number");
    return new PhaseExpr(item, kInt, number);
  }

  // If we get here, we have a bogus expression.
  return Error("Error in expression"), nullptr;
}

//-----------------------------------------------------------------------------
// Look ahead and see if there is an explicit operand ref.
// Return true if it is an operand reference.
//-----------------------------------------------------------------------------
bool MdlParser::isOperand() {
  if (Token.check(TK_DOLLAR, TK_2DOLLAR)) return true;
  if (isIdent() && Token.peek(TK_COLON) && Token.peek(TK_DOLLAR, TK_2DOLLAR))
      return true;
  return false;
}

//-----------------------------------------------------------------------------
// Process operand references.  Operands are part of latency expressions, and
// refer to operands in the target instructions.
//      operand: (ident ':')? '$' ident ('.' operand_ref)*
//             | (ident ':')? '$' number
//             | (ident ':')? '$$' number
// This syntax corresponds closely to operands in llvm td files.
// A "$number" operand refers directly to an operand by index.
// A "$$number" operand refers to a variadic operand, by index (1,2,3...).
// Return an OperandRef, which references an operand of an instruction.
//-----------------------------------------------------------------------------
OperandRef *MdlParser::ParseOperand() {
  MdlItem item(Token());
  Identifier *type = nullptr;
  Identifier *opnd = nullptr;
  long number = -1;

  // We've already determined (via isOperand()) that the next tokens form the
  // prefix of an operand, so we don't need to check again.
  // If there's an operand type (ident ':'), parse it.
  if (isIdent()) {
    type = ParseIdent("Operand type");
    Token.expect(TK_COLON);
  }

  // Parse the operand, starting with "$" or "$$".
  bool variadic = Token.check(TK_2DOLLAR);
  ++Token;    // Consume the "$" or "$$"

  MdlItem name_item(Token());
  if (!variadic && isIdent()) {
    if ((opnd = ParseIdent("Operand name")) == nullptr)
      return nullptr;
  } else {
    std::string type = variadic ? "Variadic operand index" : "Operand index";
    if (!ParseNumber(number, type)) return nullptr;
  }

  // Handle the normal case of an operand name. We allow suboperands to be
  // specified either by name or by index.
  auto *operand = new IdList;
  if (opnd) {
    operand->push_back(opnd);

    // If there are operand qualifiers (suboperands), add them to the list.
    while (Token.accept(TK_DOT)) {
      MdlItem subitem(Token());
      if (Token.check(TK_NUMBER)) {
        ParseNumber(number, "Operand number");
        operand->push_back(new Identifier(subitem, std::to_string(number)));
      }
      else if (isIdent())
        operand->push_back(ParseIdent("Sub-operand name"));
      else Error("Invalid operand specification");
    }
    return new OperandRef(item, type, operand);
  }

  // Handle an operand index reference ($<number>, or $$<number>).
  if (number != -1) {
    auto name = formatv("{0}{1}", variadic ? "$" : "", number);
    operand->push_back(new Identifier(name_item, name));
    return new OperandRef(item, type, operand);
  }
  return Error("Panic, shouldn't get here..."), nullptr;
}

//-----------------------------------------------------------------------------
// Process pipe_def rules.  This is a top-level description of the
// processor's pipeline phases:
//    pipe_def: protection? PIPE_PHASE ident '{' pipe_phases '}' ';' ;
// Return a PipePhases object which contains the names of pipeline
// phases of a CPU, and the protected attribute of the pipeline.
//-----------------------------------------------------------------------------
PipePhases *MdlParser::ParsePipeDef() {
  MdlItem item(Token());
  // Determine if this is a protected or unprotected pipeine. If you don't
  // specify it, we assume its protected.  Note if we didn't see any.
  bool is_protected = true;
  bool is_hard = false;
  bool qualifiers = true;
  if (Token.accept(TK_PROTECTED)) is_protected = true;
  else if (Token.accept(TK_UNPROTECTED)) is_protected = false;
  else if (Token.accept(TK_HARD)) is_hard = true;
  else qualifiers = false;

  // If we've seen qualifiers, we must see a PIPE_PHASE keyword.
  if (qualifiers && !Token.expect(TK_PIPE_PHASES)) return nullptr;
  if (!qualifiers && !Token.accept(TK_PIPE_PHASES)) return nullptr;

  Identifier *name = ParseIdent("Pipeline name");
  if (name == nullptr) return nullptr;

  if (!Token.expect(TK_LBRACE)) return nullptr;
  PhaseName *exe_phase = nullptr;
  auto *phases = ParsePipePhases(is_protected, is_hard, exe_phase);
  if (phases == nullptr) return nullptr;
  if (!Token.expect(TK_RBRACE)) return nullptr;
  Token.accept(TK_SEMI);         // Skip optional semicolon

  return new PipePhases(item, name, phases, exe_phase, is_protected, is_hard);
}

//-----------------------------------------------------------------------------
// Process pipe_phases rules.  These correspond to a set of pipeline phases.
//     pipe_phases: phase_id (',' phase_id)* ;
// Return a list of pipeline phase names.
//-----------------------------------------------------------------------------
PhaseNameList *MdlParser::ParsePipePhases(bool is_protected, bool is_hard,
                                          PhaseName *&exe_phase) {
  // Each item in the list can return a list of phases.
  auto *phases = new PhaseNameList;
  bool is_first;
  do {
    auto *phase_set = ParsePhaseId(is_protected, is_hard, is_first);
    if (phase_set == nullptr) return nullptr;
    if (is_first && exe_phase == nullptr)
      exe_phase = phase_set->front();
    phases->insert(phases->end(), phase_set->begin(), phase_set->end());
  } while (Token.accept(TK_COMMA));

  // After we've seen all the phases, assign phase indexes to each:
  // If a value was provided (e.g. name=23), use that value as the next id
  // to use. If not, just assign the next sequential id.  NOTE that this
  // explicitly allows duplicates!!
  int phase_index = 0;
  for (auto *phase : *phases) {
    if (phase->index() == -1)
      phase->set_index(phase_index++);
    else
      phase_index = phase->index() + 1;
  }

  return phases;
}

//-----------------------------------------------------------------------------
// Process phase_id rules. These return a single name or a set of names.
//     phase_id: '#'? ident ('[' range ']')? ('=' number)? ;
// Return a list of (possibly one) pineline phase names.
//-----------------------------------------------------------------------------
PhaseNameList *MdlParser::ParsePhaseId(
                      bool is_protected, bool is_hard, bool &is_first) {
  MdlItem item(Token());
  long first = -1, last = -1;
  long cycle = -1;

  // Check for the "first execute stage" indicator.
  is_first = Token.accept(TK_SHARP);
  Identifier *name = ParseIdent("Pipe phase name");
  if (name == nullptr) return nullptr;

  auto *phases = new PhaseNameList;

  // Parse the optional range
  if (Token.accept(TK_LBRACKET)) {
    if (!ParseRange(first, last)) return nullptr;
    if (!Token.expect(TK_RBRACKET)) return nullptr;
  }
  if (Token.accept(TK_EQUAL) && !ParseNumber(cycle, "Cycle")) return nullptr;

  // If a range was not specified, just return the name we found.
  // Set its pipe phase (its index) if the cycle is specified.
  if (first == -1) {
    auto *phase = new PhaseName(item, name->name(), is_protected, is_hard);
    if (cycle != -1) phase->set_index(cycle);
    phases->push_back(phase);
  } else {
    // If a range was specified, create a vector of names (range inclusive).
    for (int id = first; id <= last; id++) {
      auto *phase = new PhaseName(
          item, formatv("{0}{1}", name->name(), id), is_protected, is_hard);
      if (cycle != -1) phase->set_index(cycle++);
      phases->push_back(phase);
    }
  }
  return phases;
}


//-----------------------------------------------------------------------------
// Process resource_def rules:
//     resource_def : RESOURCE ('(' phase ('..' phase)? ')')
//                             resource_decl (',' resource_decl)* ';'
// Return a list of resource definitions.
//-----------------------------------------------------------------------------
ResourceDefList *MdlParser::ParseResourceDef() {
  if (!Token.accept(TK_RESOURCE)) return nullptr;
  Identifier *start = nullptr;
  Identifier *end = nullptr;

  if (Token.check(TK_LPAREN) && !ParsePhaseRange(start, end)) return nullptr;

  auto *defs = new ResourceDefList;
  do {
    auto *def = ParseResourceDecl(start, end);
    if (def == nullptr) return nullptr;
    defs->push_back(def);
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);
  return defs;
}

//-----------------------------------------------------------------------------
// Process resource_decl rules, which define a single resource.
//     resource_decl: ident (':' number)? ('[' number ']')?
//                  | ident (':' number)? '{' name_list '}' ;
//                  | ident (':' number)? '{' group_list '}' ;
// This handles resource definitions of the forms:
//     resource name;       // a single named resource.
//     resource name:4;     // a resource with 4 bits of data.
//     resource name[6];    // a pool of 6 resources.
//     resource name:31[5]; // a pool of 5 31-bit resources.
//     resource name:6 { member1, member2 }; // a pool of two named resources.
// Return a ResourceDef object that describes a single resource.
//-----------------------------------------------------------------------------
ResourceDef *MdlParser::ParseResourceDecl(Identifier *start, Identifier *end) {
  MdlItem item(Token());
  Identifier *name = ParseIdent("Resource name");
  if (name == nullptr) return nullptr;

  long bits = -1;
  long count = -1;;
  if (Token.accept(TK_COLON)) {                  // ':' number
    if (!ParseNumber(bits, "Bit width")) return nullptr;
  }

  if (Token.accept(TK_LBRACE)) {            // '{' group_list '}'
    GroupType group_type;
    IdList *members = ParseGroupList(group_type);
    if (!Token.expect(TK_RBRACE)) return nullptr;
    auto *group = new ResourceDef(item, name, bits, members, start, end);
    group->set_group_type(group_type);
    return group;
  }

  if (Token.accept(TK_LBRACKET)) {               // '[' number ']'
    if (!ParseNumber(count, "Resource count")) return nullptr;
    if (!Token.expect(TK_RBRACKET)) return nullptr;
  }

  return new ResourceDef(item, name, bits, count, start, end);
}

//-----------------------------------------------------------------------------
// Process an issue_statement rule:
//     issue_statement : ISSUE ('(' phase ('..' phase)? ')'? name_list ';'
// Return a list of resource definitions.
//-----------------------------------------------------------------------------
ResourceDefList *MdlParser::ParseIssueStatement() {
  Identifier *start = nullptr;
  Identifier *end = nullptr;

  if (!Token.accept(TK_ISSUE)) return nullptr;
  if (Token.check(TK_LPAREN) && !ParsePhaseRange(start, end)) return nullptr;

  auto *slots = ParseNameList("Issue slot name", TK_COMMA);
  if (slots == nullptr) return nullptr;
  Token.expect(TK_SEMI);

  auto *defs = new ResourceDefList;
  for (auto *id : *slots)
    defs->push_back(new ResourceDef(*id, id, -1, -1, start, end));

  return defs;
}

//-----------------------------------------------------------------------------
// Process a reorder_buffer_def rule:
//     reorder_buffer_def: REORDER_BUFFER '<' number '>' ';'
//-----------------------------------------------------------------------------
int MdlParser::ParseReorder() {
  if (!Token.accept(TK_REORDER_BUFFER)) return 0;
  if (!Token.expect(TK_LT)) return 0;
  if (!Token.check(TK_NUMBER)) return 0;
  long number;
  ParseNumber(number, "Reorder buffer size");
  if (!Token.expect(TK_GT)) return 0;
  Token.expect(TK_SEMI);
  return number;
}

//-----------------------------------------------------------------------------
// Process a range rule.
//     range: number ".." number
//-----------------------------------------------------------------------------
bool MdlParser::ParseRange(long &first, long &last) {
  first = last = 0;
  if (!ParseNumber(first, "Range start")) return false;
  if (!Token.expect(TK_DOTDOT)) return false;
  if (!ParseNumber(last, "Range end")) return false;
  if (first > last) { Error("Invalid range"); return false; }
  return true;
}

//-----------------------------------------------------------------------------
// Parse a phase range:
//    '(' phase ('..' phase)? ')'
//-----------------------------------------------------------------------------
bool MdlParser::ParsePhaseRange(Identifier *&start, Identifier *&end) {
  start = end = nullptr;
  if (Token.accept(TK_LPAREN)) {
    start = ParseIdent("Phase name");
    if (start == nullptr) return false;
    if (Token.accept(TK_DOTDOT) && (end = ParseIdent("Phase name")) == nullptr)
      return false;
    if (!Token.expect(TK_RPAREN)) return false;
  }
  return true;
}

//-----------------------------------------------------------------------------
// Process a name_list rule.
//       name_list: ident (',' ident)* ;
// Return a list of names.
//-----------------------------------------------------------------------------
IdList *MdlParser::ParseNameList(std::string type, TokenType separator) {
  auto *names = new IdList;
  int index = 0;
  do {
    auto *name = ParseIdent(type);
    if (name == nullptr) return nullptr;
    name->set_index(index++);
    names->push_back(name);
  } while (Token.accept(separator));
  return names;
}

//-----------------------------------------------------------------------------
// Parse a predicate list for subunits and latency statements.
//      predicate_list:  name_list ':'
// Note: predicates cannot be reserved keywords.
//-----------------------------------------------------------------------------
IdList *MdlParser::ParsePredicateList() {
  IdList *predicates = nullptr;
  if (Token.check(TK_IDENT)) {
    predicates = ParseNameList("Predicate name", TK_COMMA);
    if (!Token.expect(TK_COLON)) return nullptr;
  }

  return predicates;
}

//-----------------------------------------------------------------------------
// Process a group_list rule.
//       group_list: ident ('|' ident)*
//                 | ident ('&' ident)*
// Return a list of names.
//-----------------------------------------------------------------------------
IdList *MdlParser::ParseGroupList(GroupType &type) {
  auto *names = new IdList;
  Identifier *member = nullptr;
  type = GroupType::kUseAll;

  // Get the first member.  Note the type of separator.
  if ((member = ParseIdent("Member name")) == nullptr) return nullptr;
  names->push_back(member);
  if (!Token.check(TK_AND, TK_OR, TK_COMMA)) return names;
  auto separator = Token.getType();
  if (separator != TK_AND) type = GroupType::kUseSingle;

  while (Token.accept(separator)) {
    if ((member = ParseIdent("Member name")) == nullptr) return nullptr;
    names->push_back(member);
  } while (Token.accept(separator));

  // Set indexes for each member.
  int index = 0;
  for (auto *name : *names)
    name->set_index(index++);
  return names;
}

//-----------------------------------------------------------------------------
// Process a list of base templates.
//       base_list: (':' ident)* ;
//-----------------------------------------------------------------------------
IdList *MdlParser::ParseBaseList(std::string type) {
  if (!Token.check(TK_COLON)) return nullptr;
  auto *names = new IdList;
  while (Token.accept(TK_COLON)) {
    auto *base = ParseIdent(type);
    if (base == nullptr) return nullptr;
    names->push_back(base);
  }
  return names;
}

//-----------------------------------------------------------------------------
// Process a list of subunit bases, which can be subunit names or strings
// representing regular expressions of instruction names.
//       su_base_list: (':' (ident|STRING_LITERAL))* ;
//-----------------------------------------------------------------------------
void MdlParser::ParseSuBaseList(IdList *&bases, StringList *&regex) {
  if (!Token.check(TK_COLON)) return;

  while (Token.accept(TK_COLON)) {
    if (isIdent()) {
      if (bases == nullptr) bases = new IdList;
      bases->push_back(ParseIdent("Subunit base name"));
    }
    if (Token.check(TK_STRING)) {
      auto pattern = ParseString("RegEx pattern");
      if (pattern.empty()) continue;
      if (regex == nullptr) regex = new StringList;
      regex->push_back(pattern);
    }
  }
}

//-----------------------------------------------------------------------------
// Process a register_def rule:
//      register_def : REGISTER register_decl (',' register_decl)* ';'
// A register declaration can declare a set of registers, each of which
// can be a set of registers. Expand them all to a single vector, and return it.
//-----------------------------------------------------------------------------
RegisterDefList *MdlParser::ParseRegisterDef() {
  if (!Token.accept(TK_REGISTER)) return nullptr;

  auto *regs = ParseRegisterList();
  if (regs == nullptr) return nullptr;
  Token.expect(TK_SEMI);
  return regs;
}

//-----------------------------------------------------------------------------
// Process a register_class rule:
//    register_class : REGCLASS ident '{'
//                             register_decl (',' register_decl)* '}' ';' ;
// A register class declaration creates a collection of register definitions.
//-----------------------------------------------------------------------------
RegisterClass *MdlParser::ParseRegisterClass() {
  MdlItem item(Token());
  if (!Token.accept(TK_REGCLASS)) return nullptr;
  Identifier *name = ParseIdent("Register class name");
  if (name == nullptr) return nullptr;
  if (!Token.expect(TK_LBRACE)) return nullptr;

  RegisterDefList *regs = nullptr;
  // Parse the registers in this class.
  if (!Token.check(TK_RBRACE))
    regs = ParseRegisterList();
  if (!Token.expect(TK_RBRACE)) return nullptr;

  if (regs == nullptr) regs = new RegisterDefList;
  Token.accept(TK_SEMI);     // Skip optional semicolon
  return new RegisterClass(item, name, regs);
}

//-----------------------------------------------------------------------------
// Parse a register definition list:
//-----------------------------------------------------------------------------
RegisterDefList *MdlParser::ParseRegisterList() {
  // Each register declaration can return a list, so append them together.
  auto *regs = new RegisterDefList;
  do {
    RegisterDefList *regset;
    if ((regset = ParseRegisterDecl()) == nullptr) return nullptr;
    regs->insert(regs->end(), regset->begin(), regset->end());
    delete regset;
  } while (Token.accept(TK_COMMA));
  return regs;
}

//-----------------------------------------------------------------------------
// Process a register_decl rule:
//     register_decl: ident | ident '[' range ']' ;
// If a range of registers is specified, expand to a list.
//-----------------------------------------------------------------------------
RegisterDefList *MdlParser::ParseRegisterDecl() {
  MdlItem item(Token());
  auto *reg = ParseIdent("Register name");
  if (reg == nullptr) return nullptr;

  auto *regs = new RegisterDefList;
  // If no range was specified, just return a single register.
  if (!Token.accept(TK_LBRACKET)) {
    regs->push_back(new RegisterDef(item, reg));
    return regs; // return the single definition in a vector.
  }

  // If a range was specified, create a vector of register names.
  long first, last;
  ParseRange(first, last);
  for (int id = first; id <= last; id++) {
    auto *def = new Identifier(item, reg->name() + std::to_string(id));
    regs->push_back(new RegisterDef(item, def));
  }
  Token.expect(TK_RBRACKET);
  return regs;
}

//-----------------------------------------------------------------------------
// Check to see if the next token is an identifier, or any of the keywords
// that we accept as identifiers.
//-----------------------------------------------------------------------------
bool MdlParser::isIdent() {
  return Token.check(TK_IDENT, TK_USE, TK_DEF, TK_KILL, TK_USEDEF, TK_HOLD,
                     TK_RES, TK_PORT, TK_TO, TK_VIA, TK_CPU, TK_ISSUE, TK_CLASS,
                     TK_TYPE, TK_HARD, TK_IF, TK_FAMILY, TK_FUS, TK_BEGINGROUP,
                     TK_ENDGROUP, TK_RETIREOOO, TK_REGISTER);
}

//-----------------------------------------------------------------------------
// Process an ident rule:
//     ident : IDENT ;
// Every identifier creates an Identifier object, with context.
//-----------------------------------------------------------------------------
Identifier *MdlParser::ParseIdent(std::string type) {
  // First check for keywords that we allow as identifiers.
  if (!isIdent())
    return Error("{0} expected", type), nullptr;

  auto *ident = new Identifier(Token(), Token.getText());
  ++Token;
  return ident;
}

//-----------------------------------------------------------------------------
// Process a required string token.
// Return an empty string if non-string encountered.
//-----------------------------------------------------------------------------
std::string MdlParser::ParseString(std::string type) {
  if (!Token.check(TK_STRING)) return Error("{0} expected", type), "";
  auto text = Token.getText();
  ++Token;
  return text;
}

//-----------------------------------------------------------------------------
// Process a required positive number token.
// Return true if number not found.
//-----------------------------------------------------------------------------
bool MdlParser::ParseNumber(long &value, std::string type) {
  if (!Token.check(TK_NUMBER))
    return Error("{0} expected", type), false;
  value = Token.getValue();
  ++Token;
  return true;
}

//-----------------------------------------------------------------------------
// Process a required possibly signed number token.
//-----------------------------------------------------------------------------
bool MdlParser::ParseSignedNumber(long &value, std::string type) {
  bool neg = Token.accept(TK_MINUS);
  if (!Token.check(TK_NUMBER))
     return Error("{0} expected", type), false;
  value = Token.getValue();
  if (neg) value = -value;
  ++Token;
  return true;
}

//-----------------------------------------------------------------------------
// Process an instruction definition rule:
// instruction_def : INSTRUCT ident
//                       '(' operand_decl (',' operand_decl)* ');
//                       '{' (SUBUNIT '(' name_list ')' ';')?
//                           (DERIVED '(' name_list ')' ';')? '}' ';'?
//-----------------------------------------------------------------------------
const int kOpndNameRequired = -1;

InstructionDef *MdlParser::ParseInstructionDef() {
  MdlItem item(Token());
  if (!Token.accept(TK_INSTRUCT)) return nullptr;
  Identifier *name = ParseIdent("Instruction name");
  if (name == nullptr) return nullptr;

  // Parse the instructions operands.
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *operands = new OperandDeclList;
  bool ellipsis = false;

  if (!Token.check(TK_RPAREN))
  do {
    auto *operand = ParseOperandDecl(kOpndNameRequired);
    if (operand == nullptr) return nullptr;
    if (operand->is_ellipsis()) {
      if (Token.check(TK_COMMA))
        spec().ErrorLog(operand, "Ellipsis must be last declared operand");
      else ellipsis = true;
    } else operands->push_back(operand);
  } while (Token.accept(TK_COMMA));

  if (!Token.expect(TK_RPAREN)) return nullptr;
  if (!Token.expect(TK_LBRACE)) return nullptr;

  IdList *subunits = nullptr;
  if (Token.accept(TK_SUBUNIT)) {
    if (!Token.expect(TK_LPAREN)) return nullptr;
    subunits = ParseNameList("Subunit name", TK_COMMA);
    if (subunits == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;
    Token.expect(TK_SEMI);
  }
  IdList *derived = nullptr;
  if (Token.accept(TK_DERIVED)) {
    if (!Token.expect(TK_LPAREN)) return nullptr;
    derived = ParseNameList("Base instruction name", TK_COMMA);
    if (derived == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;
    Token.expect(TK_SEMI);
  }
  if (!Token.expect(TK_RBRACE)) return nullptr;
  Token.accept(TK_SEMI);      // Skip optional semicolon

  return new InstructionDef(item, name, operands, subunits, derived, ellipsis);
}

//-----------------------------------------------------------------------------
// Process an operand declaration rule (used in instructions and operands) :
//     operand_decl : (type=ident (name=ident)? ('(I)' | '(O)')?) | '...'
//-----------------------------------------------------------------------------
OperandDecl *MdlParser::ParseOperandDecl(int opnd_id) {
  MdlItem item(Token());
  Identifier *type = nullptr;
  Identifier *name = nullptr;

  // We only allow ellipses operands for instructions.
  if (Token.accept(TK_ELLIPSIS)) {
    if (opnd_id != kOpndNameRequired) {
      spec().ErrorLog(&item, "Ellipsis not allowed in operand definitions");
      name = type = new Identifier(item, "...");
    } else name = new Identifier(item, "");
    return new OperandDecl(item, type, name, true, false, false);
  }

  // Parse the operand type, followed by the sometimes optional operand name.
  if ((type = ParseIdent("Operand type")) == nullptr) return nullptr;
  if (isIdent())
    if ((name = ParseIdent("Operand name")) == nullptr) return nullptr;

  bool is_input = false;
  bool is_output = false;
  if (!ParseInputOutput(is_input, is_output)) return nullptr;

  // If an operand name is not provided, we sythesize a name based
  // on the component index.
  if (name == nullptr && opnd_id != kOpndNameRequired)
    name = new Identifier(item, std::to_string(opnd_id));
  if (name == nullptr)
    name = new Identifier(item, "");

  return new OperandDecl(item, type, name, false, is_input, is_output);
}

//-----------------------------------------------------------------------------
// Process the operand input output qualifiers:
//     '(I)' | '(O)'
// Return true if no errors found.
//-----------------------------------------------------------------------------
bool MdlParser::ParseInputOutput(bool &is_input, bool &is_output) {
  if (!Token.accept(TK_LPAREN)) return true;
  auto *type = ParseIdent("Input/Output attribute");
  if (type == nullptr) return false;
  if (!Token.expect(TK_RPAREN)) return false;

  if (type->name() == "I") is_input = true;
  else if (type->name() == "O") is_output = true;
  else return Error("Invalid I/O attribute"), false;
  return true;
}

//-----------------------------------------------------------------------------
// Process an operand or derived operand definition rule:
// operand_def : OPERAND name=ident '(' (operand_decl (',' operand_decl)*)? ')'
//                          '{' (operand_type | operand_attribute)* '}' ';'?
// derived_operand_def : OPERAND name=ident ':' base=ident ('(' ')')?
//                          '{' (operand_type | operand_attribute)* '}' ';'?
//-----------------------------------------------------------------------------
OperandDef *MdlParser::ParseOperandDef() {
  MdlItem item(Token());
  if (!Token.accept(TK_OPERAND)) return nullptr;
  auto *name = ParseIdent("Operand name");
  if (name == nullptr) return nullptr;
  Identifier *type = nullptr;
  auto *operands = new OperandDeclList;
  auto *attributes = new OperandAttributeList;

  // Parse the base operands of a derived operand definition.
  IdList *bases = nullptr;
  if (Token.check(TK_COLON))  {
    bases = ParseBaseList("Operand base name");
    if (bases == nullptr) return nullptr;
    if (Token.accept(TK_LPAREN)) Token.expect(TK_RPAREN);
  }
  // Parse the suboperands of a regular operand definition.
  else if (Token.expect(TK_LPAREN)) {
    int opnd_id = 0;
    if (!Token.accept(TK_RPAREN)) {
      do {
        auto *operand = ParseOperandDecl(opnd_id++);
        if (operand == nullptr) return nullptr;
        operands->push_back(operand);
      } while (Token.accept(TK_COMMA));
      if (!Token.expect(TK_RPAREN)) return nullptr;
    }
  }

  // Parse the operand body (for both operands and derived operands).
  if (!Token.expect(TK_LBRACE)) return nullptr;
  do {
    type = ParseOperandType(type);
    if (auto *attr = ParseOperandAttribute())
      attributes->insert(attributes->end(), attr->begin(), attr->end());
  } while (!Token.accept(TK_RBRACE));
  Token.accept(TK_SEMI);             // Skip optional semicolon

  return new OperandDef(item, name, operands, type, attributes, bases);
}

//-----------------------------------------------------------------------------
// Process an operand_type rule:
//    operand_type: TYPE '(' ident ')' ';'
//-----------------------------------------------------------------------------
Identifier *MdlParser::ParseOperandType(Identifier *last_type) {
  MdlItem item(Token());
  if (!Token.accept(TK_TYPE)) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *type = ParseIdent("Operand type");
  if (type == nullptr) return nullptr;
  if (!Token.expect(TK_RPAREN)) return nullptr;
  if (last_type)
    spec().ErrorLog(&item, "Only one type specification allowed");
  Token.expect(TK_SEMI);
  return type;
}

//-----------------------------------------------------------------------------
// Process an operand attribute definition rule:
// operand_attribute :
//        (predicate=name_list ':')? operand_attribute_stmt
//      | predicate=name_list ':' '{' operand_attribute_stmt* '}' ';'?
//-----------------------------------------------------------------------------
OperandAttributeList *
MdlParser::ParseOperandAttribute() {
  // Parse an optional predicate set.
  IdList *predicates = nullptr;
  if (Token.check(TK_IDENT)) {
    predicates = ParsePredicateList();
    if (predicates == nullptr) return nullptr;
  }

  // Parse a block of attributes, or just a single attribute.
  auto *attributes = new OperandAttributeList;
  bool is_block = Token.accept(TK_LBRACE);
  do {
    auto *attr = ParseOperandAttributeStmt(predicates);
    if (attr == nullptr) return nullptr;
    attributes->push_back(attr);
  } while(is_block && !Token.accept(TK_RBRACE));
  if (is_block) Token.accept(TK_SEMI);

  return attributes;
}

//-----------------------------------------------------------------------------
// Process an operand attribute definition rule:
//  operand_attribute_stmt : ATTRIBUTE ident '=' (snumber | tuple)
//                           (IF type '[' pred_value (',' pred_value)* ']')? ';'
//  tuple : '[' snumber (',' snumber)* ']'
//-----------------------------------------------------------------------------
OperandAttribute *
MdlParser::ParseOperandAttributeStmt(IdList *predicate) {
  MdlItem item(Token());
  if (!Token.accept(TK_ATTRIBUTE)) return nullptr;
  auto *name = ParseIdent("Operand attribute name");
  if (name == nullptr) return nullptr;
  if (!Token.expect(TK_EQUAL)) return nullptr;
  long number = 0;

  // Parse either a single value, or a tuple of values.
  auto *values = new std::vector<int>;
  auto *pred_values = new PredValueList;
  bool is_tuple = Token.accept(TK_LBRACKET);
  do {
    if (!ParseSignedNumber(number, "Number")) return nullptr;
    values->push_back(number);
  } while (is_tuple && Token.accept(TK_COMMA));

  if (is_tuple && !Token.expect(TK_RBRACKET)) return nullptr;
  std::string type;

  // Parse optional IF clause.
  if (Token.accept(TK_IF)) {
    auto *ident = ParseIdent("Attribute value type");
    if (ident == nullptr) return nullptr;
    if (ident->name() != "label" && ident->name() != "address" &&
        ident->name() != "lit") {
      spec().ErrorLog(&item, "Invalid predicate type: {0}", ident->name());
      Token.accept(TK_SEMI);
      return nullptr;
    }
    type = ident->name();

    // Parse the optional predicate value list.
    if (Token.accept(TK_LBRACKET)) {
      do {
        auto *pred_value = ParsePredValue();
        if (pred_value == nullptr) return nullptr;
        pred_values->push_back(pred_value);
      } while (Token.accept(TK_COMMA));
      Token.expect(TK_RBRACKET);
    }
  }
  Token.expect(TK_SEMI);

  return new OperandAttribute(item, name, values, type, pred_values, predicate);
}

//-----------------------------------------------------------------------------
// Process a predicate value:
//     value=snumber | low=snumber '..' high=snumber | '{' mask=number '}'
//-----------------------------------------------------------------------------
PredValue *MdlParser::ParsePredValue() {
  MdlItem item(Token());

  // Parse and return a "mask" value.
  if (Token.accept(TK_LBRACE)) {
    long number;
    if (!ParseSignedNumber(number, "Mask value")) return nullptr;
    if (!Token.expect(TK_RBRACE)) return nullptr;
    return new PredValue(item, number);
  }

  // Parse either a single number, or a range.
  long low, high;
  if (!ParseSignedNumber(low, "Low range number")) return nullptr;
  if (Token.accept(TK_DOTDOT)) {                      // snumber .. snumber
    if (!ParseSignedNumber(high, "High range number")) return nullptr;
    if (low <= high) return new PredValue(item, low, high);
    spec().ErrorLog(&item, "Invalid value range: {0}..{1}", low, high);
    return new PredValue(item, 0, 0); // dummy value
  }
  // Return a single signed number;
  return new PredValue(item, low, low);
}

//-----------------------------------------------------------------------------
// Convert a predicate expression string to an internal expression type.
//-----------------------------------------------------------------------------
static PredOp NameToOp(std::string name) {
  static auto *predicate_ops = new std::unordered_map<std::string, PredOp>(
      {{kTrue, PredOp::kTrue},
       {kFalse, PredOp::kFalse},
       {kCheckAny, PredOp::kCheckAny},
       {kCheckAll, PredOp::kCheckAll},
       {kCheckNot, PredOp::kCheckNot},
       {kCheckOpcode, PredOp::kCheckOpcode},
       {kCheckIsRegOperand, PredOp::kCheckIsRegOperand},
       {kCheckIsImmOperand, PredOp::kCheckIsImmOperand},
       {kCheckZeroOperand, PredOp::kCheckZeroOperand},
       {kCheckFunctionPredicate, PredOp::kCheckFunctionPredicate},
       {kCheckFunctionPredicateWithTII, PredOp::kCheckFunctionPredicateWithTII},
       {kCheckNumOperands, PredOp::kCheckNumOperands},
       {kCheckRegOperand, PredOp::kCheckRegOperand},
       {kCheckInvalidRegOperand, PredOp::kCheckInvalidRegOperand},
       {kCheckImmOperand, PredOp::kCheckImmOperand},
       {kCheckSameRegOperand, PredOp::kCheckSameRegOperand},
       {kOpcodeSwitchStmt, PredOp::kOpcodeSwitchStmt},
       {kOpcodeSwitchCase, PredOp::kOpcodeSwitchCase},
       {kReturnStatement, PredOp::kReturnStatement}});

  if (predicate_ops->count(name))
    return (*predicate_ops)[name];
  return PredOp::kEmpty;
}

//-----------------------------------------------------------------------------
// Process a predicate definition:
//    predicate_def : PREDICATE ident ':' predicate_op? ';'
// Return true if it appeared to be a predicate statement (even if errors).
//-----------------------------------------------------------------------------
bool MdlParser::ParsePredicateDef() {
  if (!Token.accept(TK_PREDICATE)) return false;

  auto *name = ParseIdent("Predicate name");
  if (name == nullptr) return true;
  if (!Token.expect(TK_COLON)) return true;

  auto *expr = ParsePredicateOp();
  if (expr == nullptr) return true;
  Token.expect(TK_SEMI);

  // If we see a predicate expression, add it to the table.
  spec().EnterPredicate(name->name(), expr);
  return true;
}

//-----------------------------------------------------------------------------
// Process a predicate expression:
//    predicate_op : pred_opcode '<' pred_opnd (',' pred_opnd)* '>'
//                 | CODE_ESCAPE | ident
// We do some moderate sanity checking here so that we don't create predicate
// objects that have obvious errors.  But we don't do any semantic checking
// because we haven't processed the entire input spec yet, and don't have
// dictionaries built yet, so can't easily check predicate names, register
// names, operand names, instruction names, etc.
//-----------------------------------------------------------------------------
PredExpr *MdlParser::ParsePredicateOp() {
  MdlItem item(Token());

  auto code_escape = ParseCodeEscape();
  if (!code_escape.empty())
    return new PredExpr(item, PredOp::kCode, code_escape);

  // Parse the predicate opcode name, and look it up.
  auto *name = ParseIdent("Predicate name");
  if (name == nullptr) return nullptr;
  auto opcode = NameToOp(name->name());
  if (opcode == PredOp::kEmpty)
    return new PredExpr(item, PredOp::kName, name->name());

  // Handle parameterless definitions.
  if (!Token.accept(TK_LT)) return new PredExpr(item, opcode);

  // Read in parameters and create the overall expression.
  std::vector<PredExpr *> opnds;
  do {
    if (Token.check(TK_GT)) break;           // Skip if '<' '>'
    auto *opnd = ParsePredicateOpnd();
    if (opnd == nullptr) return nullptr;
    opnds.push_back(opnd);
  } while (Token.accept(TK_COMMA));
  if (!Token.expect(TK_GT)) return nullptr;

  // Error check the number and general type of operands. Each predicate type
  // has its own expected operands.  We check the form of these operands now,
  // and we check the semantics after the entire spec has been visited.
  unsigned min_opnds, max_opnds;
  switch (opcode) {
  default:
    return new PredExpr(item, opcode, opnds);

  // All operands are names (we'll check that they're instructions later).
  case PredOp::kCheckOpcode:
    for (auto *opnd : opnds)
      if (!opnd->IsName())
        spec().ErrorLog(opnd, "Instruction name expected");
    return new PredExpr(item, opcode, opnds);

  // All operands should be predicate expressions.
  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kCheckAll:
  case PredOp::kCheckAny:
    for (auto *opnd : opnds)
      if (!opnds.empty() && !opnd->IsPred())
        spec().ErrorLog(opnd, "Predicate expression expected");
    return new PredExpr(item, opcode, opnds);

  // The second operand should be a predicate expression.
  case PredOp::kOpcodeSwitchCase:
    if (opnds.size() > 1 && !opnds[1]->IsPred())
      spec().ErrorLog(opnds[1], "Predicate expression expected");
    min_opnds = max_opnds = 2;
    break;

  // The single operand should be a predicate expression.
  case PredOp::kReturnStatement:
  case PredOp::kCheckNot:
    if (!opnds.empty() && !opnds[0]->IsPred())
      spec().ErrorLog(opnds[0], "Predicate expression expected");
    min_opnds = max_opnds = 1;
    break;

  // The single operand should be an operand index.
  case PredOp::kCheckIsRegOperand:
  case PredOp::kCheckIsImmOperand:
  case PredOp::kCheckInvalidRegOperand:
  case PredOp::kCheckZeroOperand:
    if (!opnds.empty() && !opnds[0]->IsIntegerOrOperand())
      spec().ErrorLog(opnds[0], "Operand index expected");
    min_opnds = max_opnds = 1;
    break;

  case PredOp::kCheckSameRegOperand:
    if (!opnds.empty() && !opnds[0]->IsIntegerOrOperand())
      spec().ErrorLog(opnds[0], "Operand index expected");
    if (opnds.size() > 1 && !opnds[1]->IsIntegerOrOperand())
      spec().ErrorLog(opnds[1], "Operand index expected");
    min_opnds = max_opnds = 2;
    break;

  // The single operand should be an operand count.
  case PredOp::kCheckNumOperands:
    if (!opnds.empty() && !opnds[0]->IsInteger())
      spec().ErrorLog(opnds[0], "Operand count expected");
    min_opnds = max_opnds = 1;
    break;

  // Function predicates have strings representing MI and MC functions.
  case PredOp::kCheckFunctionPredicateWithTII:
  case PredOp::kCheckFunctionPredicate:
    if (!opnds.empty() && !opnds[0]->IsString())
      spec().ErrorLog(opnds[0], "MC function expected");
    if (opnds.size() > 1 && !opnds[1]->IsString())
      spec().ErrorLog(opnds[1], "MI function expected");
    if (opcode == PredOp::kCheckFunctionPredicate) {
      min_opnds = max_opnds = 2;
      break;
    }
    if (opnds.size() > 2 && !opnds[1]->IsString())
      spec().ErrorLog(opnds[1], "TII prefix expected");
    min_opnds = 2;
    max_opnds = 3; // TII operand is optional
    break;

  // Register operand predicates have an index, a register name, and an
  // optional function.
  case PredOp::kCheckRegOperand:
    if (!opnds.empty() && !opnds[0]->IsIntegerOrOperand())
      spec().ErrorLog(opnds[0], "Operand index expected");
    if (opnds.size() > 1 && !opnds[1]->IsName())
      spec().ErrorLog(opnds[1], "Register name expected");
    if (opnds.size() > 2 && !opnds[2]->IsString())
      spec().ErrorLog(opnds[2], "Predicate function name expected");
    min_opnds = 2;
    max_opnds = 3;
    break;

  // Immediate operand predicates have an index, an integer or symbolic name,
  // and an optional function.
  case PredOp::kCheckImmOperand:
    if (!opnds.empty() && !opnds[0]->IsIntegerOrOperand())
      spec().ErrorLog(opnds[0], "Operand index expected");
    if (opnds.size() > 1 && !opnds[1]->IsInteger() && !opnds[1]->IsString())
      spec().ErrorLog(opnds[1], "Immediate value expected");
    if (opnds.size() > 2 && !opnds[2]->IsString())
      spec().ErrorLog(opnds[2], "Predicate function name expected");
    min_opnds = 1;
    max_opnds = 3;
    break;
  }

  if (opnds.size() < min_opnds)
    spec().ErrorLog(&item, "Missing operands ({0} expected)", min_opnds);
  if (opnds.size() > max_opnds)
    spec().ErrorLog(&item, "Extra operands ({0} expected)", max_opnds);

  return new PredExpr(item, opcode, opnds);
}

//-----------------------------------------------------------------------------
// Parse a predicate code escape:
//      '[' '{' <c code> '}' ']'
//-----------------------------------------------------------------------------
std::string MdlParser::ParseCodeEscape() {
  if (!Token.check(TK_LBRACKET) || !Token.peek(TK_LBRACE)) return "";

  // Parse and concatenate tokens until a '}' and ']' are found.
  if (!Token.getCodeEscape()) return "";
  auto code = "[{ " + Token.getText() + " }]";
  ++Token;
  return code;
}

//-----------------------------------------------------------------------------
// Process a predicate operand:
//    pred_opnd : name=ident | snumber | STRING_LITERAL | predicate_op |
//                '[' opcode=ident (',' ident)* ']' | operand
//-----------------------------------------------------------------------------
PredExpr *MdlParser::ParsePredicateOpnd() {
  MdlItem item(Token());
  // Parse string parameters.
  if (Token.check(TK_STRING))
    return new PredExpr(item, PredOp::kString, ParseString("String"));

  // Parse signed and unsigned numbers
  if (Token.check(TK_NUMBER)) {
    long number;
    ParseSignedNumber(number, "Number");
    return new PredExpr(item, PredOp::kNumber, std::to_string(number));
  }

  // Parse an opcode list.
  if (Token.accept(TK_LBRACKET)) {
    auto *names = ParseNameList("Instruction name", TK_COMMA);
    Token.expect(TK_RBRACKET);
    if (names == nullptr) return nullptr;
    std::vector<PredExpr *> opcodes;
    for (auto *name : *names)
      opcodes.push_back(new PredExpr(*name, PredOp::kName, name->name()));
    delete names;
    return new PredExpr(item, PredOp::kCheckOpcode, opcodes);
  }

  // Parse an operand reference ( (ident ':')? '$' ident, etc)
  if (isOperand()) {
    auto *opnd = ParseOperand();
    if (opnd) return new PredExpr(item, PredOp::kOperandRef, opnd);
  }

  // If we get here, it must be a predicate_op.
  return ParsePredicateOp();
}

} // namespace mdl
} // namespace mpact

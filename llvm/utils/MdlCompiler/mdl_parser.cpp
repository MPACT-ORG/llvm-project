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
bool ProcessInputFile(MdlSpec &Spec, std::string ImportPath,
                      std::string FileName) {
  // If we've already seen this file name, there's no point in parsing it again,
  // so just return "success".
  static std::vector<std::string *> ImportFiles;
  for (auto *Name : ImportFiles)
    if (*Name == FileName) return true;

  // Attempt to open the file.
  std::fstream *MdlStream = new std::fstream(FileName, std::fstream::in);
  if (!MdlStream->is_open()) {
    llvm::errs() << formatv("File not found: \"{0}\"\n", FileName);
    return false;
  }

  // Save a copy of the name of the input file (so we don't parse it twice).
  std::string *SavedFileName = new std::string(FileName);
  ImportFiles.push_back(SavedFileName);

  // Create a new parser and parse the file.
  MdlParser Parser(Spec, ImportPath, SavedFileName, MdlStream);
  return Parser.parseArchitectureSpec();
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
bool MdlParser::parseArchitectureSpec() {
  bool Success = true;
  while (!Token.EndOfFile()) {
    switch (Token.getType()) {
      case TK_FAMILY:
          if (auto *Family = parseFamilySpec())
            Spec.setFamilyName(Family);
          break;
      case TK_PIPE_PHASES: case TK_PROTECTED: case TK_UNPROTECTED: case TK_HARD:
          if (auto *Pipe = parsePipeDef())
            Spec.getPipePhases().push_back(Pipe);
          break;
      case TK_REGISTER:
          if (auto *Regs = parseRegisterDef())
             Spec.getRegisters().insert(Spec.getRegisters().end(),
                                      Regs->begin(), Regs->end());
          break;
      case TK_REGCLASS:
          if (auto *RegClass = parseRegisterClass())
            Spec.getRegClasses().push_back(RegClass);
          break;
      case TK_RESOURCE:
          if (auto *ResDef = parseResourceDef())
            Spec.getResources().insert(Spec.getResources().end(),
                                         ResDef->begin(), ResDef->end());
          break;
      case TK_CPU:
            if (auto *Cpu = parseCpuDef())
              Spec.getCpus().push_back(Cpu);
          break;
      case TK_FUNCUNIT:
          if (auto *Func = parseFuncUnitTemplate())
            Spec.getFuncUnits().push_back(Func);
          break;
      case TK_FUNCGROUP:
          if (auto *Func = parseFuncUnitGroup())
            Spec.getFuncUnitGroups().push_back(Func);
          break;
      case TK_SUBUNIT:
          if (auto *Subunit = parseSubunitTemplate())
            Spec.getSubunits().push_back(Subunit);
          break;
      case TK_LATENCY:
          if (auto *Latency = parseLatencyTemplate())
            Spec.getLatencies().push_back(Latency);
          break;
      case TK_INSTRUCT:
          if (auto *Instruction = parseInstructionDef())
            Spec.getInstructions().push_back(Instruction);
          break;
      case TK_OPERAND:
          if (auto *Operand = parseOperandDef())
            Spec.getOperands().push_back(Operand);
          break;
      case TK_IMPORT:
          if (bool Imported = parseImportFile())
            Success &= Imported;
          break;
      case TK_PREDICATE:
          parsePredicateDef();
          break;
      default:
          ParseError(
              "Invalid MDL statement: family, cpu, func_unit, func_group,\n"
              "  subunit, latency, instruction, operand, register,\n"
              "  register class, resource, pipe, or predicate"
              " definition expected");
          if (ErrorsSeen() > 30) {
            ParseError("Too many syntax errors, aborting");
            return false;
          }
          Token.skipUntil(TK_FAMILY, TK_CPU, TK_FUNCUNIT, TK_FUNCGROUP,
                          TK_SUBUNIT, TK_LATENCY, TK_INSTRUCT, TK_OPERAND,
                          TK_REGISTER, TK_REGCLASS, TK_RESOURCE,
                          TK_PIPE_PHASES, TK_PROTECTED, TK_UNPROTECTED, TK_HARD,
                          TK_IMPORT, TK_PREDICATE, TK_ENDOFFILE);
          break;
    }
  }
  return Success;
}

//-----------------------------------------------------------------------------
// Process a family name specification.
//-----------------------------------------------------------------------------
Identifier *MdlParser::parseFamilySpec() {
  if (!Token.accept(TK_FAMILY)) return nullptr;
  auto *Family = parseIdent("Family name");
  Token.expect(TK_SEMI);
  return Family;
}

//-----------------------------------------------------------------------------
//  Process an import file.
//  Return true if successful, false if any errors are found.
//-----------------------------------------------------------------------------
bool MdlParser::parseImportFile() {
  if (!Token.accept(TK_IMPORT)) return false;

  // Fetch the filename.
  std::filesystem::path ImportName = parseString("Filename");
  if (ImportName.empty()) return false;

  // Get directory names for the current source file and the import file name.
  auto CurrentFileName = Token.getText();
  auto CurrentDir = std::filesystem::path(CurrentFileName).parent_path();
  auto ImportDir = ImportName.parent_path();

  auto AddSlash = [](std::string path_name) {
    if (!path_name.empty() && path_name.back() != '/')
      path_name += "/";
    return path_name;
  };

  // If the import name has directory information, use it.
  if (!ImportDir.empty()) {
    if (!CurrentDir.empty() && !ImportDir.is_absolute())
      return ProcessInputFile(Spec, ImportPath,
                              formatv("{0}{1}{2}", AddSlash(CurrentDir),
                                      AddSlash(ImportDir), ImportName));
    return ProcessInputFile(Spec, ImportPath, ImportName);
  }

  // If the import name doesn't have directory info, see if its in the
  // including file's directory.
  if (!CurrentDir.empty()) {
    auto Name = formatv("{0}{1}", AddSlash(CurrentDir), ImportName);
    if (std::filesystem::exists(Name))
      return ProcessInputFile(Spec, ImportPath, Name);
  }

  // If both the import dir and current directory are empty, check the current
  // directory.
  if (std::filesystem::exists(ImportName))
    return ProcessInputFile(Spec, ImportPath, ImportName);

  // If not found in the current directory, look in the import path.
  if (!ImportPath.empty()) {
    auto Name = formatv("{0}{1}", AddSlash(ImportPath), ImportName);
    if (std::filesystem::exists(Name))
      return ProcessInputFile(Spec, ImportPath, Name);
  }

  // Otherwise, just use the name verbatim.
  return ProcessInputFile(Spec, ImportPath, ImportName);
}

//-----------------------------------------------------------------------------
// Process cpu_def and cpu_stmt rules:
//      cpu_def : CPU|CORE ident cpu_def_names? '{' cpu_stmt+ '}' ';'
//      cpu_stmt: pipe_def | register_def | resource_def | issue_statement |
//                cluster_instantiation | func_unit_instantiation ;
//-----------------------------------------------------------------------------
CpuInstance *MdlParser::parseCpuDef() {
  MdlItem Item(Token());
  if (!Token.accept(TK_CPU)) return nullptr;

  Identifier *Name = parseIdent("Cpu name");
  if (Name == nullptr) return nullptr;

  auto *Pipes = new PipeDefList;
  auto *Issues = new ResourceDefList;
  auto *Resources = new ResourceDefList;
  int ReorderBufferSize = 0;
  auto *FuncUnits = new FuncUnitInstList;
  auto *ForwardStmts = new ForwardStmtList;
  auto *Clusters = new ClusterList;

  // Fetch optional Cpu names. Note that we need at least one name, so
  // use the CPU name by default.
  std::vector<std::string> Aliases = parseCpuDefNames();
  if (Aliases.empty())
    Aliases.push_back(Name->getName());

  if (!Token.expect(TK_LBRACE)) return nullptr;

  // for each non-terminal in cpu_stmt, collect information.
  while (!Token.accept(TK_RBRACE)) {
    switch (Token.getType()) {
      case TK_PIPE_PHASES: case TK_PROTECTED: case TK_UNPROTECTED: case TK_HARD:
          if (auto *Pipe = parsePipeDef())
            Pipes->push_back(Pipe);
          break;
      case TK_RESOURCE:
          if (auto *Res = parseResourceDef())
            Resources->insert(Resources->end(), Res->begin(), Res->end());
          break;
      case TK_FUNCUNIT:
          if (auto *Func = parseFuncUnitInstantiation(Resources))
            FuncUnits->push_back(Func);
          break;
      case TK_FORWARD:
          if (auto *Forward = parseForwardStmt())
            ForwardStmts->push_back(Forward);
          break;
      case TK_ISSUE:
          if (auto *Slots = parseIssueStatement())
            Issues->insert(Issues->end(), Slots->begin(), Slots->end());
          break;
      case TK_CLUSTER:
          if (auto *Cluster = parseClusterInstantiation())
            Clusters->push_back(Cluster);
          break;
      case TK_REORDER_BUFFER:
          if (int Size = parseReorder(); Size != 0)
            ReorderBufferSize = Size;
          break;
      default:
          ParseError("Pipe definition, cluster, resource, func_unit, forward, "
                "reorder, or issue statement expected");
          if (ErrorsSeen() > 30) {
            ParseError("Too many syntax errors, aborting");
            return nullptr;
          }
          Token.skipUntil(TK_RESOURCE, TK_FUNCUNIT, TK_FORWARD, TK_RBRACE,
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
  if (Clusters->empty() || !FuncUnits->empty()) {
    Clusters->push_back(new ClusterInstance(Item, new Identifier(Item, "__"),
                                            Issues, new ResourceDefList,
                                            FuncUnits, new ForwardStmtList));
    Issues = new ResourceDefList; // reset the issues list.
  }

  // Note: forward statements defined at the CPU level stay at the CPU level.
  return new CpuInstance(Item, Name, Pipes, Issues, Resources,
                         ReorderBufferSize, Clusters, ForwardStmts, Aliases);
}

//-----------------------------------------------------------------------------
// Process the optional cpu_def_names part of a CPU definition:
//      cpu_def_names: '(' STRING (',' STRING) ')'
// Return the set of quote-stripped strings.
//-----------------------------------------------------------------------------
std::vector<std::string> MdlParser::parseCpuDefNames() {
  std::vector<std::string> Aliases;
  if (Token.accept(TK_LPAREN)) {
    do {
      auto Name = parseString("Cpu name");
      if (Name.empty()) return Aliases;
      Aliases.push_back(Name);
    } while (Token.accept(TK_COMMA));
    Token.expect(TK_RPAREN);                // Eat the token
  }
  return Aliases;
}

//-----------------------------------------------------------------------------
// Process cluster definition rules:
//      cluster_instantiation: CLUSTER ident '{' cluster_stmt+ '}' ';'
//      cluster_stmt: register_def | resource_def | issue_statement |
//                    func_unit_instantiation ;
// Return a ClusterInstance object that contains all the information.
//-----------------------------------------------------------------------------
ClusterInstance *MdlParser::parseClusterInstantiation() {
  MdlItem Item(Token());
  if (!Token.accept(TK_CLUSTER)) return nullptr;

  Identifier *Name = parseIdent("Cluster name");
  if (Name == nullptr) return nullptr;

  if (!Token.expect(TK_LBRACE)) return nullptr;

  auto *Issues = new ResourceDefList;
  auto *Resources = new ResourceDefList;
  auto *FuncUnits = new FuncUnitInstList;
  auto *ForwardStmts = new ForwardStmtList;

  // For each rule in each cluster_stmt, collect and save information.
  while (!Token.accept(TK_RBRACE)) {
    switch (Token.getType()) {
      case TK_RESOURCE:
          if (ResourceDefList *Res = parseResourceDef())
            Resources->insert(Resources->end(), Res->begin(), Res->end());
          break;
      case TK_FUNCUNIT:
          if (auto *Func = parseFuncUnitInstantiation(Resources))
            FuncUnits->push_back(Func);
          break;
      case TK_FORWARD:
          if (auto *Forward = parseForwardStmt())
            ForwardStmts->push_back(Forward);
          break;
      case TK_ISSUE:
          if (auto *Slots = parseIssueStatement())
            Issues->insert(Issues->end(), Slots->begin(), Slots->end());
          break;
      default:
          ParseError(
              "Resource, func_unit, forward, or issue statement expected");
          if (ErrorsSeen() > 30) {
            ParseError("Too many syntax errors, aborting");
            return nullptr;
          }
          Token.skipUntil(TK_RBRACE, TK_RESOURCE, TK_FUNCUNIT, TK_FORWARD,
                          TK_ISSUE);
          break;
    }
  }
  Token.accept(TK_SEMI);      // Skip optional semicolon

  return new ClusterInstance(Item, Name, Issues, Resources, FuncUnits,
                             ForwardStmts);
}

//-----------------------------------------------------------------------------
// Process functional unit instance name:
//     func_unit_instance: ident ('<>' | '<' number '>')?
//-----------------------------------------------------------------------------
std::tuple<Identifier *, int, bool>
MdlParser::parseFuncUnitInstType(std::string Type) {
  Identifier *Name = parseIdent(Type);
  if (Name == nullptr) return { nullptr, 0, 0 };

  long BufferSize = -1;
  bool Unreserved = false;

  // Parse optional buffer_size/unreserved clause.
  if (Token.accept(TK_LT)) {
    if (Token.check(TK_NUMBER)) {
      parseNumber(BufferSize, "Buffer size");
    }
    if (!Token.expect(TK_GT)) return { nullptr, 0, 0 };
    if (BufferSize == -1)
      Unreserved = true;
  }

  return { Name, BufferSize, Unreserved };
}

//-----------------------------------------------------------------------------
// Parse functional unit pinning clauses:
//     pin_any : '->' ident ('|' ident)+
//     pin_all : '->' ident ('&' ident)+
// Returns "any_list" and "all_list".
//-----------------------------------------------------------------------------
std::pair<IdList *, IdList *> MdlParser::parsePins() {
  if (!Token.accept(TK_ARROW)) return { nullptr, nullptr };

  auto *Issue = parseIdent("Issue slot name");
  if (Issue == nullptr) return { nullptr, nullptr };
  IdList *Units = new IdList;
  Units->push_back(Issue);
  if (!Token.check(TK_AND, TK_OR)) return { nullptr, Units };
  auto Type = Token.getType();

  while (Token.accept(Type)) {
    auto *Issue = parseIdent("Issue slot name");
    if (Issue == nullptr) return { nullptr, nullptr };
    Units->push_back(Issue);
  }

  if (Type == TK_AND) return { nullptr, Units };
  return { Units, nullptr };
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
MdlParser::parseFuncUnitInstantiation(ResourceDefList *Resources) {
  MdlItem Item(Token());
  if (!Token.expect(TK_FUNCUNIT)) return nullptr;

  // Parse the functional unit type.  Handle base types.
  auto [Type, BufferSize, Unreserved] =
                          parseFuncUnitInstType("Functional unit type");
  if (Type == nullptr) return nullptr;

  // Maintain a set of buffer sizes for each functional unit and base.
  std::map<std::string, int> BufferSizes;
  BufferSizes.emplace(Type->getName(), BufferSize);

  // Read any base units.  This will produce an implicitely defined template.
  IdList *Bases = new IdList;
  while (Token.accept(TK_COLON)) {
    auto [Btype, BufferSize, Bunreserved] =
                          parseFuncUnitInstType("Base functional unit type");
    if (Btype == nullptr) return nullptr;
    BufferSizes.emplace(Btype->getName(), BufferSize);
    Bases->push_back(Btype);
  }

  // Parse the functional unit instance name.
  auto *Name = parseIdent("Functional unit instance name");
  if (Name == nullptr) return nullptr;

  // Parse the parameters to the instance.
  // Note: we don't currently allow implicitly defined instances to have
  // arguments passed to them.  If you want that, then define the template!
  if (!Token.expect(TK_LPAREN)) return nullptr;
  ResourceRefList *Refs = nullptr;
  if (Bases->empty())
    Refs = parseResourceRefs(Resources);
  if (!Token.expect(TK_RPAREN)) return nullptr;

  // Parse any functional unit instance pinning.
  auto [PinAny, PinAll] = parsePins();

  if (!Token.expect(TK_SEMI)) return nullptr;

  // If we see a list of bases, create a functional unit template for it.
  if (!Bases->empty()) {
    // Create an aggregate functional unit template, including the top-level
    // type in the aggregate.
    Bases->insert(Bases->begin(), Type);
    auto Derived = basedFuInstance(Bases);
    Type = new Identifier(*Type, Derived);
    if (FindItem(Spec.getFuncUnits(), Derived) == nullptr)
      Spec.getFuncUnits().push_back(new FuncUnitTemplate(Item, Type, Bases));
  }

  if (Refs == nullptr) Refs = new ResourceRefList;
  return new FuncUnitInstance(Item, Type, Name, Unreserved, BufferSizes, Refs,
                              PinAny, PinAll);
}

//-----------------------------------------------------------------------------
// Process a single CPU forward statement:
//     forward_stmt : FORWARD ident '->' forward_to_unit (',' forward_to_unit)?
//     forward_to_unit : ident ('(' snumber ')')?
//-----------------------------------------------------------------------------
ForwardStmt *MdlParser::parseForwardStmt() {
  MdlItem Item(Token());
  if (!Token.expect(TK_FORWARD)) return nullptr;

  ForwardToSet ToUnits;
  Identifier *FromUnit = parseIdent("Functional unit name");
  if (FromUnit == nullptr) return nullptr;
  if (!Token.expect(TK_ARROW)) return nullptr;

  do {
    auto *ToUnit = parseIdent("Functional unit name");
    if (ToUnit == nullptr) return nullptr;
    long Cycles = 0;
    if (Token.accept(TK_LPAREN)) {
      if (!parseSignedNumber(Cycles, "Number of cycles")) return nullptr;
      if (!Token.expect(TK_RPAREN)) return nullptr;
    }
    ToUnits.emplace_back(ToUnit, Cycles);
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);

  return new ForwardStmt(Item, FromUnit, ToUnits);
}

//-----------------------------------------------------------------------------
// Process function_unit_template and func_unit_template_stmt rules:
//  func_unit_template     : FUNCUNIT ident (':' base)* func_unit_params
//                           '{' func_unit_template_stmt* '}' ';'
//  func_unit_template_stmt: resource_def | port_def |
//                              connect_stmt | subunit_instantiation ;
// Return a FuncUnitTemplate object that contains all the information.
//-----------------------------------------------------------------------------
FuncUnitTemplate *MdlParser::parseFuncUnitTemplate() {
  MdlItem Item(Token());
  auto *Ports = new IdList;
  auto *Resources = new ResourceDefList;
  auto *Connects = new ConnectList;
  auto *Subunits = new SubUnitInstList;
  if (!Token.accept(TK_FUNCUNIT)) return nullptr;

  Identifier *Type = parseIdent("Functional unit name");
  IdList *Bases = nullptr;
  if (Type == nullptr) return nullptr;

  if (Token.check(TK_COLON)) {
    Bases = parseBaseList("Functional unit base name");
    if (Bases == nullptr) return nullptr;
  }
  ParamsList *Parameters = parseFuncUnitParams();

  if (!Token.expect(TK_LBRACE)) return nullptr;

  // Process each rule matched in template statements, save off info.
  do {
    switch (Token.getType()) {
      case TK_RESOURCE:
        if (auto *Res = parseResourceDef())
          Resources->insert(Resources->end(), Res->begin(), Res->end());
        break;
      case TK_PORT:
        if (auto *Defs = parsePortDef(Connects))
          Ports->insert(Ports->end(), Defs->begin(), Defs->end());
        break;
      case TK_CONNECT:
        if (auto *Conn = parseConnectStmt())
          Connects->push_back(Conn);
        break;
      case TK_IDENT:       // could be a predicated subunit.
      case TK_SUBUNIT:
        if (auto *Items = parseSubunitInstantiation(Resources))
          Subunits->insert(Subunits->end(), Items->begin(), Items->end());
        break;
      case TK_RBRACE:
        break;
      default:
        ParseError("Resource, port, connect, or subunit statement expected");
        if (ErrorsSeen() > 30) {
          ParseError("Too many syntax errors, aborting");
          return nullptr;
        }
        Token.skipUntil(TK_RBRACE, TK_RESOURCE, TK_PORT, TK_CONNECT,
                        TK_SUBUNIT);
        break;
    }
  } while (!Token.accept(TK_RBRACE));

  Token.accept(TK_SEMI);     // Skip optional semicolon

  return new FuncUnitTemplate(Item, Type, Bases, Parameters, Ports, Resources,
                              Connects, Subunits);
}

//-----------------------------------------------------------------------------
// Process a functional unit group definition:
//     func_unit_group : FUNCGROUP ident ('<' number '>')? ':' name_list ';'
//-----------------------------------------------------------------------------
FuncUnitGroup *MdlParser::parseFuncUnitGroup() {
  MdlItem Item(Token());
  if (!Token.expect(TK_FUNCGROUP)) return nullptr;

  Identifier *Name = parseIdent("Group name");
  if (Name == nullptr) return nullptr;

  long BufferSize = -1;
  if (Token.accept(TK_LT)) {
    if (!parseNumber(BufferSize, "Buffer size")) return nullptr;
    if (!Token.expect(TK_GT)) return nullptr;
  }
  if (!Token.expect(TK_COLON)) return nullptr;

  IdList *Members = parseNameList("Functional unit name", TK_COMMA);
  if (Members == nullptr) return nullptr;
  Token.expect(TK_SEMI);

  return new FuncUnitGroup(Item, Name, BufferSize, Members);
}

//-----------------------------------------------------------------------------
// Process func_unit_params rules: (arguments to a functional unit template).
//      func_unit_params : '(' (fu_decl_item (';' fu_decl_item)*)? ')'
// Return a vector of parameters.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::parseFuncUnitParams() {
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *Parameters = new ParamsList;
  if (Token.accept(TK_RPAREN)) return Parameters;        // Empty params list

  do {
    ParamsList *Param = parseFuDeclItem();
    if (Param == nullptr) return nullptr;
    Parameters->insert(Parameters->end(), Param->begin(), Param->end());
  } while (Token.accept(TK_SEMI));

  if (!Token.expect(TK_RPAREN)) return nullptr;
  return Parameters;  // return the list of parameters.
}

//-----------------------------------------------------------------------------
// Process fu_decl_item rules:
//      func_decl_item : RESOURCE name_list | CLASS name_list
// Each namelist can define a list of parameters. We want to flatten those
// lists to a single list of resources and classes, and return a single list
// of class and resource definitions.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::parseFuDeclItem() {
  ParamType Type;
  if (Token.accept(TK_RESOURCE)) Type = kParamResource;
  else if (Token.accept(TK_CLASS)) Type = kParamClass;
  else return ParseError("Register or Class declaration expected"), nullptr;
  auto Kind = (Type == kParamResource) ? "Resource name" : "Class name";

  IdList *Names = parseNameList(Kind, TK_COMMA);
  if (Names == nullptr) return nullptr;

  auto *Parameters = new ParamsList;
  for (auto *Name : *Names)
    Parameters->push_back(new Params(*Name, Name, Type));
  return Parameters;
}

//-----------------------------------------------------------------------------
// Process the port_def rules: (part of a functional unit template definition).
//      PORT port_decl (',' port_decl )* ';'
// Return a list of port definitions.
//-----------------------------------------------------------------------------
IdList *MdlParser::parsePortDef(ConnectList *Connects) {
  if (!Token.accept(TK_PORT)) return nullptr;
  auto *Names = new IdList;

  do {
    auto *Port = parsePortDecl(Connects);
    if (Port == nullptr) return nullptr;
    Names->push_back(Port);
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);
  return Names;
}

//-----------------------------------------------------------------------------
// Process a single port definition.  The definition may optionally include
// a register class and a list of resource references.
//     ident ('<' reg_class=ident '>')? ('(' ref=resource_ref ')')?
// If a declaration contains connection information, create CONNECT records.
//-----------------------------------------------------------------------------
Identifier *MdlParser::parsePortDecl(ConnectList *Connects) {
  auto *Name = parseIdent("Port name");
  if (Name == nullptr) return nullptr;
  Identifier *RegClass = nullptr;
  ResourceRef *Ref = nullptr;

  if (Token.accept(TK_LT)) {               // Parse a class name
    RegClass = parseIdent("Register class name");
    if (RegClass == nullptr || !Token.expect(TK_GT)) return nullptr;
  }

  if (Token.accept(TK_LPAREN)) {           // parse a resource reference
    Ref = parseResourceRef();
    if (Ref == nullptr || !Token.expect(TK_RPAREN)) return nullptr;
  }

  if (RegClass != nullptr || Ref != nullptr)
    Connects->push_back(new Connect(*Name, Name, RegClass, Ref));
  return Name;
}

//-----------------------------------------------------------------------------
// Process connect_stmt rules: (part of a functional unit template definition).
//      CONNECT ident ('to' ident)? ('via' resource_ref)? ';' ;
// Return a Connect object that contains all the information.
//-----------------------------------------------------------------------------
Connect *MdlParser::parseConnectStmt() {
  MdlItem Item(Token());
  if (!Token.accept(TK_CONNECT)) return nullptr;

  ResourceRef *Ref = nullptr;
  Identifier *RegClass = nullptr;
  Identifier *Port = parseIdent("Port name");
  if (Port == nullptr) return nullptr;

  if (Token.accept(TK_TO)) {
    RegClass = parseIdent("Register class name");
    if (RegClass == nullptr) return nullptr;
  }
  if (Token.accept(TK_VIA)) {
    Ref = parseResourceRef();
    if (Ref == nullptr) return nullptr;
  }
  return new Connect(Item, Port, RegClass, Ref);
}

//-----------------------------------------------------------------------------
// Process subunit_instantiation rules: (also part of func unit definitions).
//   subunit_instantiation:
//                     (predicate=name_list ':')? subunit_statement
//                   | (predicate=name_list ':' '{' subunit_statement* '}' ';'
// Return a SubUnitInstance object that contains the information.
//-----------------------------------------------------------------------------
SubUnitInstList *
MdlParser::parseSubunitInstantiation(ResourceDefList *Resources) {
  // Parse an optional leading predicate.
  IdList *Predicate = nullptr;
  if (Token.check(TK_IDENT))
    if ((Predicate = parsePredicateList()) == nullptr) return nullptr;

  auto *Stmts = new SubUnitInstList;
  // Parse a predicated list of subunit statements.
  if (Predicate != nullptr && Token.accept(TK_LBRACE)) {
    do {
      auto Subunits = parseSubunitStatement(Predicate, Resources);
      if (Subunits != nullptr)
        Stmts->insert(Stmts->end(), Subunits->begin(), Subunits->end());
    } while (!Token.accept(TK_RBRACE));
    Token.accept(TK_SEMI);            // Skip optional semicolon
    return Stmts;
  }

  // Parse a single statement (predicated or not)
  auto *Subunits = parseSubunitStatement(Predicate, Resources);
  if (Subunits != nullptr)
    Stmts->insert(Stmts->end(), Subunits->begin(), Subunits->end());

  return Stmts;
}

//-----------------------------------------------------------------------------
// Process subunit_statement rules:
//     subunit_statement: SUBUNIT subunit_instance (',' subunit_instance)* ';'
//     subunit_instance:  ident '(' resource_refs ')'
//-----------------------------------------------------------------------------
SubUnitInstList *MdlParser::parseSubunitStatement(IdList *Predicate,
                                                  ResourceDefList *Resources) {
  MdlItem Item(Token());
  if (!Token.expect(TK_SUBUNIT)) return nullptr;
  auto *Subunits = new SubUnitInstList;

  do {
    auto *Name = parseIdent("Subunit type");
    if (Name == nullptr) return nullptr;
    if (!Token.expect(TK_LPAREN)) return nullptr;
    auto *Args = parseResourceRefs(Resources);
    if (Args == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;

    Subunits->push_back(new SubUnitInstance(Item, Name, Args, Predicate));
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);
  return Subunits;
}

//-----------------------------------------------------------------------------
// Process subunit_template rules:
//     subunit_template: SUBUNIT ident (':' ident)? '(' su_decl_items ')'
//                 (('{' latency_instance* '}') | ('{{' latency_items? '}}') );
// Return a SubUnitTemplate object that contains all the information.
//-----------------------------------------------------------------------------
SubUnitTemplate *MdlParser::parseSubunitTemplate() {
  MdlItem Item(Token());
  if (!Token.accept(TK_SUBUNIT)) return nullptr;
  Identifier *Name = parseIdent("Subunit template name");
  if (Name == nullptr) return nullptr;

  StringList *Regex = nullptr;
  IdList *Bases = nullptr;
  parseSuBaseList(Bases, Regex);

  ParamsList *Parameters = parseSuDeclItems();
  if (Parameters == nullptr) return nullptr;

  auto *Latencies = new LatencyInstList;

  // If the body of the subunit is a list of subunit statements, return a list
  // of those instantiations.
  if (Token.accept(TK_LBRACE)) {
    while (!Token.accept(TK_RBRACE)) {
      auto *Lats = parseLatencyInstance();
      if (Lats == nullptr) return nullptr;
      Latencies->insert(Latencies->end(), Lats->begin(), Lats->end());
    }
    Token.accept(TK_SEMI);    // Skip optional semicolon
    return new SubUnitTemplate(Item, Name, Bases, Regex, Parameters, Latencies,
                               nullptr);
  }

  // If the body of the subunit is an inlined latency template, create a
  // new latency template (with the same name as the subunit), and create a
  // latency instance which refers to that latency template. The created
  // latency template will be returned as part of the subunit.
  LatencyTemplate *InlineLat = nullptr;
  if (Token.accept(TK_2LBRACE)) {
    auto *Refs = new ReferenceList;
    while (!Token.accept(TK_2RBRACE)) {
      auto *Items = parseLatencyItems(nullptr);
      if (Items == nullptr) return nullptr;
      Refs->insert(Refs->end(), Items->begin(), Items->end());
      delete Items;
    }
    Token.accept(TK_SEMI);    // Skip optional semicolon

    // Create a new latency template, add it to spec.
    Identifier *Tname = new Identifier(Name->getName());
    ParamsList *Tparams = copySuDeclItems(Parameters);
    InlineLat = new LatencyTemplate(Item, Tname, nullptr, Tparams, Refs);
    Spec.getLatencies().push_back(InlineLat);

    // Create an instance for the new latency template for this subunit.
    Identifier *InstName = new Identifier(Name->getName());
    ResourceRefList *Args = new ResourceRefList;
    for (auto *Param : *Parameters)
      Args->push_back(new ResourceRef(*Param->getId(), Param->getId()));
    Latencies->push_back(new LatencyInstance(Item, InstName, Args, nullptr));

    return new SubUnitTemplate(Item, Name, Bases, Regex, Parameters, Latencies,
                               InlineLat);
  }
  return ParseError("Invalid subunit template definition"), nullptr;
}

//-----------------------------------------------------------------------------
// Process su_decl_items rules: (part of a subunit template definition).
//      su_decl_items: '(' su_decl_item (';' su_decl_item)* ')'
// Return a list of subunit parameters.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::parseSuDeclItems() {
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *Parameters = new ParamsList;

  // If there are no parameters, we're done.
  if (Token.accept(TK_RPAREN)) return Parameters;

  // Parse and append the lists of resources or ports together.
  do {
    auto *Param = parseSuDeclItem();
    if (Param == nullptr) return nullptr;
    Parameters->insert(Parameters->end(), Param->begin(), Param->end());
  } while (Token.accept(TK_SEMI));
  if (!Token.expect(TK_RPAREN)) return nullptr;
  return Parameters; // return the list of declared items.
}

//-----------------------------------------------------------------------------
// Process su_decl_item rules: (part of a subunit template definition).
//      su_decl_item: RESOURCE name_list | PORT name_list ;
// Return a list of resource or port parameter definitions.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::parseSuDeclItem() {
  ParamType Type;
  if (Token.accept(TK_RESOURCE)) Type = kParamResource;
  else if (Token.accept(TK_PORT)) Type = kParamPort;
  else return ParseError("Resource or Port declaration expected"), nullptr;
  auto Kind = (Type == kParamResource) ? "Resource name" : "Port name";

  IdList *Names = parseNameList(Kind, TK_COMMA);
  if (Names == nullptr) return nullptr;

  auto *Parameters = new ParamsList;
  for (auto *Name : *Names)
    Parameters->push_back(new Params(*Name, Name, Type));

  delete Names;
  return Parameters; // return the list of resources or ports.
}

//-----------------------------------------------------------------------------
// Make a copy of a Subunit parameter declaration list.
//-----------------------------------------------------------------------------
ParamsList *MdlParser::copySuDeclItems(ParamsList *Parameters) {
  auto *Copy = new ParamsList;

  for (auto *Param : *Parameters)
    Copy->push_back(
       new Params(*Param, new Identifier(Param->getId(), 0), Param->getType()));

  return Copy;
}


//-----------------------------------------------------------------------------
// Process latency_instance rules: (part of a subunit template definition).
//   latency_instance: (predicate=name_list ':')? latency_statement
//                   | (predicate=name_list ':' '{' latency_statement* '}' ';'?
// Return a LatencyInstance object that contains all the information.
//-----------------------------------------------------------------------------
LatencyInstList *MdlParser::parseLatencyInstance() {
  IdList *Predicate = nullptr;
  // Parse an optional leading predicate.
  if (Token.check(TK_IDENT))
    if ((Predicate = parsePredicateList()) == nullptr) return nullptr;

  auto *Stmts = new LatencyInstList;
  if (Predicate != nullptr && Token.accept(TK_LBRACE)) {
    do {
      auto *Stmt = parseLatencyStatement(Predicate);
      if (Stmt == nullptr) return nullptr;
      Stmts->push_back(Stmt);
    } while (!Token.expect(TK_RBRACE));

    Token.accept(TK_SEMI);        // Skip optional semi
    return Stmts;
  }

  // Otherwise parse just a single latency statement.
  auto Stmt = parseLatencyStatement(Predicate);
  if (Stmt == nullptr) return nullptr;
  Stmts->push_back(Stmt);
  return Stmts;
}

//-----------------------------------------------------------------------------
// Process latency_statement rules:
//     latency_statement: LATENCY ident '(' resource_refs? ')' ';'
// Return a LatencyInstance object that contains all the information.
//-----------------------------------------------------------------------------
LatencyInstance *MdlParser::parseLatencyStatement(IdList *Predicates) {
  MdlItem Item(Token());
  if (!Token.expect(TK_LATENCY)) return nullptr;

  auto *Name = parseIdent("Latency template name");
  if (Name == nullptr) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *Args = parseResourceRefs();
  if (!Token.expect(TK_RPAREN)) return nullptr;
  if (!Token.expect(TK_SEMI)) return nullptr;

  return new LatencyInstance(Item, Name, Args, Predicates);
}

//-----------------------------------------------------------------------------
// Process latency_template rules.
//     latency_template: LATENCY ident (':' ident)* su_decl_items
//                                     latency_block
// Return a LatencyTemplate object that contains all the information.
//-----------------------------------------------------------------------------
LatencyTemplate *MdlParser::parseLatencyTemplate() {
  MdlItem Item(Token());
  if (!Token.accept(TK_LATENCY)) return nullptr;

  IdList *Bases = nullptr;
  Identifier *Name = parseIdent("Latency template name");
  if (Name == nullptr) return nullptr;
  if (Token.check(TK_COLON)) {
    Bases = parseBaseList("Latency template base name");
    if (Bases == nullptr) return nullptr;
  }
  ParamsList *Parameters = parseSuDeclItems();
  if (Parameters == nullptr) return nullptr;
  ReferenceList *Refs = parseLatencyBlock(nullptr);
  if (Refs == nullptr) return nullptr;

  return new LatencyTemplate(Item, Name, Bases, Parameters, Refs);
}

//-----------------------------------------------------------------------------
// Process a block of latency items:
//      latency_block: '{' latency_items* '}'
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::parseLatencyBlock(IdList *Predicates) {
  if (!Token.expect(TK_LBRACE)) return nullptr;
  auto *References = new ReferenceList;
  while (!Token.accept(TK_RBRACE)) {
    auto *Refs = parseLatencyItems(Predicates);
    if (Refs == nullptr) return nullptr;
    References->insert(References->end(), Refs->begin(), Refs->end());
    delete Refs;
  };
  Token.accept(TK_SEMI);
  return References;
}

//-----------------------------------------------------------------------------
// Process latency_items rules: (part of a latency template definition).
//      latency_items: (name_list ':')? (latency_item | ('{' latency_item* '}')
// Return a list of Reference objects.
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::parseLatencyItems(IdList *Predicates) {
  if (Predicates && Token.check(TK_IDENT))
    return ParseError("Nested predicates not allowed"), nullptr;

  if (Token.check(TK_IDENT)) {
    Predicates = parsePredicateList();
    if (Predicates == nullptr) return nullptr;
  }
  if (Token.check(TK_LBRACE))
    return parseLatencyBlock(Predicates);

  // Parse a reference. This could return multiple references.
  return parseLatencyItem(Predicates);
}

//-----------------------------------------------------------------------------
// Process latency_item rules: (part of a latency template definition).
//    latency_item : latency_ref
//                 | conditional_ref
//                 | fu_statement ;
// Return a single Reference object that describes a single latency.
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::parseLatencyItem(IdList *Predicates) {
  if (auto *Lat = parseLatencyRef(Predicates)) {
    auto *Refs = new ReferenceList;
    Refs->push_back(Lat);
    return Refs;
  } else if (auto *Cref = parseConditionalRef(Predicates)) {
    auto *Refs = new ReferenceList;
    Refs->push_back(new Reference(*Cref, Predicates, Cref));
    return Refs;
  } else if (auto *Refs = parseFusStatement(Predicates)) {
    return Refs;
  }

  Token.skipUntil(TK_SEMI, TK_USE, TK_DEF, TK_KILL, TK_USEDEF, TK_RES, TK_HOLD,
            TK_FUS, TK_IF, TK_ELSE);
  return ParseError("Invalid latency statement"), nullptr;
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
// actually a feature - the block can have its own predicates.  TODO?
//-----------------------------------------------------------------------------
ConditionalRef *MdlParser::parseConditionalRef(IdList *Predicates) {
  MdlItem Item(Token());
  if (!Token.accept(TK_IF)) return nullptr;
  auto *Pred = parseIdent("Predicate name");
  if (Pred == nullptr) return nullptr;
  auto *Refs = parseLatencyBlock(nullptr);   //  '{' latency_items* '}'
  if (Refs == nullptr) return nullptr;

  ConditionalRef *ElseClause = nullptr;
  if (Token.accept(TK_ELSE)) {
    if (Token.check(TK_IF)) {
      ElseClause = parseConditionalRef(nullptr);  // recur!
      if (ElseClause == nullptr) return nullptr;
    }
    else if (Token.check(TK_LBRACE)) {
      auto *Block = parseLatencyBlock(nullptr);    //  '{' latency_items* '}'
      if (Block == nullptr) return nullptr;
      ElseClause = new ConditionalRef(Item, nullptr, Block, nullptr);
    }
    else return ParseError("Invalid if/then/else construct"), nullptr;
  }

  return new ConditionalRef(Item, Pred, Refs, ElseClause);
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
Reference *MdlParser::parseLatencyRef(IdList *Predicates) {
  MdlItem Item(Token());
  auto Type = parseRefType();
  if (Type == RefTypes::kNull) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;

  // Default values for different components of the latency statement.
  OperandRef *Opnd = nullptr;
  ResourceRefList *Refs = nullptr;
  long Cycles = 1;
  long Repeat = 1;
  long Delay = 1;

  // Parse the phase expression.
  auto *Phase = parseExpr();
  if (Phase == nullptr) return nullptr;

  // Parse a resource refs statement, which may include a cycle count.
  if (Token.accept(TK_COLON)) {
    if (!parseNumber(Cycles, "Cycles")) return nullptr;
    if (!Token.expect(TK_COMMA)) return nullptr;
    if ((Refs = parseLatencyResourceRefs()) == nullptr) return nullptr;
  }

  // Parse an operand ref that may include a repeat/delay clause.
  else if (Token.accept(TK_LBRACKET)) {
    if (!parseNumber(Repeat, "Cycles")) return nullptr;
    if (Token.accept(TK_COMMA) && !parseNumber(Delay, "Delay")) return nullptr;
    if (!Token.expect(TK_RBRACKET)) return nullptr;
    if (!Token.expect(TK_COMMA)) return nullptr;
    if (!isOperand() || (Opnd = parseOperand()) == nullptr) return nullptr;
  }

  // Parse a combined operand/resource reference (no modifiers allowed).
  else {
    if (!Token.expect(TK_COMMA)) return nullptr;
    if (isOperand()) {
      if ((Opnd = parseOperand()) == nullptr) return nullptr;
      if (Token.accept(TK_COMMA))
        if ((Refs = parseLatencyResourceRefs()) == nullptr) return nullptr;
    }
    else
      if ((Refs = parseLatencyResourceRefs()) == nullptr) return nullptr;
  }

  if (!Token.expect(TK_RPAREN)) return nullptr;
  Token.accept(TK_SEMI);
  if (Refs == nullptr) Refs = new ResourceRefList;
  return new Reference(Item, Predicates, Type, Phase, Repeat, Delay, Cycles,
                       Opnd, Refs);
}

//-----------------------------------------------------------------------------
// Parse the type of a latency reference.
//-----------------------------------------------------------------------------
RefType MdlParser::parseRefType() {
  if (!Token.check(TK_USE, TK_DEF, TK_USEDEF, TK_KILL,
                   TK_HOLD, TK_RES, TK_PREDICATE)) return RefTypes::kNull;
  auto Type = convertStringToRefType(Token.getText());
  ++Token;
  return Type;
}

//-----------------------------------------------------------------------------
// Process latency resource_refs rules.
//     latency_resource_refs: latency_resource_ref (',' latency_resource_ref)*
// Return a list of resource references.
//-----------------------------------------------------------------------------
ResourceRefList *MdlParser::parseLatencyResourceRefs() {
  auto *Refs = new ResourceRefList;
  do {
    auto *Ref = parseLatencyResourceRef();
    if (Ref == nullptr) return nullptr;
    Refs->push_back(Ref);
  } while (Token.accept(TK_COMMA));

  return Refs;
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
ResourceRef *MdlParser::parseLatencyResourceRef() {
  ResourceRef *Res = parseResourceRef();
  if (Res == nullptr) return nullptr;
  long Count = -1;

  if (Token.accept(TK_COLON)) {                       // ref : *
    if (Token.accept(TK_MUL)) {
      Res->setUseAllMembers();
      return Res;
    }
    if (Token.accept(TK_COLON)) {                     // ref : : mask
      auto *Mask = parseIdent("Value mask");
      if (Mask == nullptr) return nullptr;
      Res->setValueName(Mask);
      return Res;
    }

    if (Token.check(TK_NUMBER)) {                    // ref : number
      if (!parseNumber(Count, "Resource count")) return nullptr;
      Res->setPoolCount(Count);
    }
    else if (isIdent()) {                             // ref : ident
      auto *CountName = parseIdent("Value mask");
      if (CountName == nullptr) return nullptr;
      Res->setPoolCountName(CountName);
    }

    if (Token.accept(TK_COLON)) {                     // ref : ... : value
      auto *Mask = parseIdent("Value mask");
      if (Mask == nullptr) return nullptr;
      Res->setValueName(Mask);
    }
  }
  return Res;
}

//-----------------------------------------------------------------------------
// Process resource_refs rules.  Return a list of resource references.
//     resource_refs: resource_ref (',' resource_ref)*
// Return a list of resource references.
//-----------------------------------------------------------------------------
ResourceRefList *
MdlParser::parseResourceRefs(ResourceDefList *Resources /* = nullptr */) {
  auto *Refs = new ResourceRefList;
  if (Token.check(TK_RPAREN)) return Refs;
  do {
    auto *Ref = parseResourceRef(Resources);
    if (Ref == nullptr) return nullptr;
    Refs->push_back(Ref);
  } while (Token.accept(TK_COMMA));

  return Refs;
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
MdlParser::parseResourceRef(ResourceDefList *Resources /* = nullptr */) {
  MdlItem Item(Token());
  Identifier *Name = parseIdent("Resource name");
  if (Name == nullptr) return nullptr;

  if (Token.accept(TK_DOT)) {                     // ident '.' member
    Identifier *Id = parseIdent("Member name");
    if (Id == nullptr) return nullptr;
    return new ResourceRef(*Id, Name, Id);
  }
  if (Token.accept(TK_LBRACKET)) {                // ident '[' range|number ']'
    long First = -1, Last = -1;
    std::string Type = Token.peek(TK_DOTDOT) ? "Range start" : "Index";
    if (!parseNumber(First, Type)) return nullptr;
    if (Token.accept(TK_DOTDOT)) {
      if (!parseNumber(Last, "Range end")) return nullptr;
    }
    if (!Token.expect(TK_RBRACKET)) return nullptr;
    if (Last != -1)
      return new ResourceRef(Item, Name, First, Last);
    return new ResourceRef(Item, Name, First, First);
  }

  if (!Token.check(TK_OR, TK_AND))            // just an ident.
    return new ResourceRef(Item, Name);

  // Handle implicitly defined resource groups.
  int Index = 0;
  auto *Members = new IdList;
  Members->push_back(Name);
  Name->setIndex(Index++);
  auto GroupType = Token.getType();             // save off first separator

  while (Token.accept(GroupType)) {             // ident | ident | ident ...
    auto *Member = parseIdent("Member name");
    if (Member == nullptr) return nullptr;
    Member->setIndex(Index++);
    Members->push_back(Member);
  }

  if (Resources == nullptr) {
    ParseError(&Item, "Implicit group definition not allowed");
    return new ResourceRef(Item, Name);
  }

  // Create a resource group in the scope surrounding the reference.
  // Register it in the parent's resource list.
  static int ImplicitId = 0;
  Name = new Identifier(Item, formatv("<group_{0}>", ImplicitId++));
  auto *Def = new ResourceDef(Item, Name, -1, Members, nullptr, nullptr);
  Def->setImplicitGroup();
  Def->setGroupType(GroupType == TK_AND ? GroupType::kUseAll
                                         : GroupType::kUseSingle);
  Resources->push_back(Def);
  return new ResourceRef(Item, Name);
}

//-----------------------------------------------------------------------------
// Process a functional unit usage statement.
//    fus_statement: FUS '(' (fus_item ('&' fus_item)* ',') snumber
//                                  (',' fus_attribute)* ')' ';'
//    fus_attribute : BEGINGROUP | ENDGROUP | SINGLEISSUE  | RETIREOOO
//-----------------------------------------------------------------------------
ReferenceList *MdlParser::parseFusStatement(IdList *Predicates) {
  MdlItem Item(Token());
  if (!Token.accept(TK_FUS)) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;

  auto *Refs = new ReferenceList;

  // Parse the list of optional fus_items.
  if (isIdent()) {
    do {
      auto *Ref = parseFusItem(Predicates, Item);
      if (Ref == nullptr) return nullptr;
      Refs->push_back(Ref);
    } while (Token.accept(TK_AND));
    if (!Token.expect(TK_COMMA)) return nullptr;
  }

  // Parse the micro_ops field.  Record it in the first reference (only)
  long MicroOps;
  if (!parseSignedNumber(MicroOps, "Micro-ops")) return nullptr;
  if (!Refs->empty())
    Refs->front()->setMicroOps(MicroOps);

  // If we see functional unit attributes, collect them.
  RefFlags::Item Flags = RefFlags::kNone;
  while (Token.accept(TK_COMMA)) {
    if (Token.accept(TK_BEGINGROUP))
      Flags |= RefFlags::kBeginGroup;
    else if (Token.accept(TK_ENDGROUP))
      Flags |= RefFlags::kEndGroup;
    else if (Token.accept(TK_SINGLEISSUE))
      Flags |= RefFlags::kSingleIssue;
    else if (Token.accept(TK_RETIREOOO))
      Flags |= RefFlags::kRetireOOO;
    else return ParseError("Unrecognized functional unit attribute"), nullptr;
  }
  if (!Token.expect(TK_RPAREN)) return nullptr;
  Token.accept(TK_SEMI);

  // If there weren't any functional units specified, generate a single
  // micro_ops reference.
  if (Refs->empty()) {
    Refs->push_back(new Reference(Item, Predicates, MicroOps, Flags));
    return Refs;
  }

  // If we found flags, annotate all the fu references.
  if (Flags != RefFlags::kNone)
    for (auto *Ref : *Refs)
      Ref->setFuFlags(Flags);

  // Note that we've seen an Fus reference.  If none seen, we can avoid looking
  // for them later.
  Spec.setExplicitFuRefs();
  return Refs;
}

//-----------------------------------------------------------------------------
// Process an fus_item:
//    fus_item: ident ('<' (expr ':')? number '>')?
//-----------------------------------------------------------------------------
Reference *MdlParser::parseFusItem(IdList *Predicates, MdlItem &Item) {
  auto *Fu = parseIdent("Functional unit name");
  if (Fu == nullptr) return nullptr;

  PhaseExpr *Expr = nullptr;
  long Cycles = 1;

  // parse the optional Phase/Cycles expressions. Note that the expression
  // is optional as well, so we check for the number first.
  if (Token.accept(TK_LT)) {
    if (Token.check(TK_NUMBER) && Token.peek(TK_GT)) {
      parseNumber(Cycles, "Cycle count");
    } else {
      if ((Expr = parseExpr()) == nullptr) return nullptr;
      if (!Token.expect(TK_COLON)) return nullptr;
      if (!parseNumber(Cycles, "Cycle count")) return nullptr;
    }
    if (!Token.expect(TK_GT)) return nullptr;
  }

  return new Reference(Item, Predicates, RefTypes::kFus, Expr,
                    Cycles, 0, RefFlags::kNone, new ResourceRef(*Fu, Fu));
}

//-----------------------------------------------------------------------------
// Process expr rules.  These are part of latency specs, and are a limited
// set of operations for calculating pipeline latencies:
//     expr : (term '+' term) | (term  '-' term)
// Return a single expression tree root.
//-----------------------------------------------------------------------------
PhaseExpr *MdlParser::parseExpr() {
  MdlItem Item(Token());
  PhaseExpr *Lhs = nullptr, *Rhs = nullptr;
  if ((Lhs = parseTerm()) == nullptr) return nullptr;
  while (auto Op = Token.accept(TK_PLUS, TK_MINUS)) {
    if ((Rhs = parseTerm()) == nullptr) return nullptr;
    Lhs = new PhaseExpr(Item, (Op == TK_PLUS) ? kPlus : kMinus, Lhs, Rhs);
  }
  return Lhs;
}

//-----------------------------------------------------------------------------
//  Parse a "term" expression component
//     term : (factor '*' factor) | (factor '/' factor)
//-----------------------------------------------------------------------------
PhaseExpr *MdlParser::parseTerm() {
  MdlItem Item(Token());
  PhaseExpr *Lhs = nullptr, *Rhs = nullptr;
  if ((Lhs = parseFactor()) == nullptr) return nullptr;
  while (auto Op = Token.accept(TK_MUL, TK_DIV)) {
    if ((Rhs = parseFactor()) == nullptr) return nullptr;
    Lhs = new PhaseExpr(Item, (Op == TK_MUL) ? kMult : kDiv , Lhs, Rhs);
  }
  return Lhs;
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
PhaseExpr *MdlParser::parseFactor() {
  MdlItem Item(Token());
  PhaseExpr *Expr;
  if (Token.accept(TK_LPAREN)) {
    if ((Expr = parseExpr()) == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;
    return Expr;
  }
  if (Token.accept(TK_LBRACE)) {
    if ((Expr = parseExpr()) == nullptr) return nullptr;
    if (!Token.expect(TK_RBRACE)) return nullptr;
    return new PhaseExpr(Item, kPositive, Expr, nullptr);
  }
  if (Token.accept(TK_MINUS)) {
    if ((Expr = parseExpr()) == nullptr) return nullptr;
    return new PhaseExpr(Item, kNeg, Expr, nullptr);
  }

  // Parse an operand reference ( (ident ':')? '$' ident, etc)
  if (isOperand()) {
    auto *Opnd = parseOperand();
    if (Opnd) return new PhaseExpr(Item, kOpnd, Opnd);
  }
  if (isIdent()) {
    return new PhaseExpr(Item, kPhase, parseIdent("Phase name"));
  }
  if (Token.check(TK_NUMBER)) {
    long Number;
    parseNumber(Number, "Number");
    return new PhaseExpr(Item, kInt, Number);
  }

  // If we get here, we have a bogus expression.
  return ParseError("Error in expression"), nullptr;
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
OperandRef *MdlParser::parseOperand() {
  MdlItem Item(Token());
  Identifier *Type = nullptr;
  Identifier *Opnd = nullptr;
  long Number = -1;

  // We've already determined (via isOperand()) that the next tokens form the
  // prefix of an operand, so we don't need to check again.
  // If there's an operand type (ident ':'), parse it.
  if (isIdent()) {
    Type = parseIdent("Operand type");
    Token.expect(TK_COLON);
  }

  // Parse the operand, starting with "$" or "$$".
  bool Variadic = Token.check(TK_2DOLLAR);
  ++Token;    // Consume the "$" or "$$"

  MdlItem NameItem(Token());
  if (!Variadic && isIdent()) {
    if ((Opnd = parseIdent("Operand name")) == nullptr)
      return nullptr;
  } else {
    std::string Type = Variadic ? "Variadic operand index" : "Operand index";
    if (!parseNumber(Number, Type)) return nullptr;
  }

  // Handle the normal case of an operand name. We allow suboperands to be
  // specified either by name or by index.
  auto *Operand = new IdList;
  if (Opnd) {
    Operand->push_back(Opnd);

    // If there are operand qualifiers (suboperands), add them to the list.
    while (Token.accept(TK_DOT)) {
      MdlItem SubItem(Token());
      if (Token.check(TK_NUMBER)) {
        parseNumber(Number, "Operand number");
        Operand->push_back(new Identifier(SubItem, std::to_string(Number)));
      }
      else if (isIdent())
        Operand->push_back(parseIdent("Sub-operand name"));
      else ParseError("Invalid operand specification");
    }
    return new OperandRef(Item, Type, Operand);
  }

  // Handle an operand index reference ($<number>, or $$<number>).
  if (Number != -1) {
    auto Name = formatv("{0}{1}", Variadic ? "$" : "", Number);
    Operand->push_back(new Identifier(NameItem, Name));
    return new OperandRef(Item, Type, Operand);
  }
  return ParseError("Panic, shouldn't get here..."), nullptr;
}

//-----------------------------------------------------------------------------
// Process pipe_def rules.  This is a top-level description of the
// processor's pipeline phases:
//    pipe_def: protection? PIPE_PHASE ident '{' pipe_phases '}' ';' ;
// Return a PipePhases object which contains the names of pipeline
// phases of a CPU, and the protected attribute of the pipeline.
//-----------------------------------------------------------------------------
PipePhases *MdlParser::parsePipeDef() {
  MdlItem Item(Token());
  // Determine if this is a protected or unprotected pipeine. If you don't
  // specify it, we assume its protected.  Note if we didn't see any.
  bool IsProtected = true;
  bool IsHard = false;
  bool Qualifiers = true;
  if (Token.accept(TK_PROTECTED)) IsProtected = true;
  else if (Token.accept(TK_UNPROTECTED)) IsProtected = false;
  else if (Token.accept(TK_HARD)) IsHard = true;
  else Qualifiers = false;

  // If we've seen qualifiers, we must see a PIPE_PHASE keyword.
  if (Qualifiers && !Token.expect(TK_PIPE_PHASES)) return nullptr;
  if (!Qualifiers && !Token.accept(TK_PIPE_PHASES)) return nullptr;

  Identifier *Name = parseIdent("Pipeline name");
  if (Name == nullptr) return nullptr;

  if (!Token.expect(TK_LBRACE)) return nullptr;
  PhaseName *ExePhase = nullptr;
  auto *Phases = parsePipePhases(IsProtected, IsHard, ExePhase);
  if (Phases == nullptr) return nullptr;
  if (!Token.expect(TK_RBRACE)) return nullptr;
  Token.accept(TK_SEMI);         // Skip optional semicolon

  return new PipePhases(Item, Name, Phases, ExePhase, IsProtected, IsHard);
}

//-----------------------------------------------------------------------------
// Process pipe_phases rules.  These correspond to a set of pipeline phases.
//     pipe_phases: phase_id (',' phase_id)* ;
// Return a list of pipeline phase names.
//-----------------------------------------------------------------------------
PhaseNameList *MdlParser::parsePipePhases(bool IsProtected, bool IsHard,
                                          PhaseName *&ExePhase) {
  // Each item in the list can return a list of phases.
  auto *Phases = new PhaseNameList;
  bool IsFirst;
  do {
    auto *PhaseSet = parsePhaseId(IsProtected, IsHard, IsFirst);
    if (PhaseSet == nullptr) return nullptr;
    if (IsFirst && ExePhase == nullptr)
      ExePhase = PhaseSet->front();
    Phases->insert(Phases->end(), PhaseSet->begin(), PhaseSet->end());
  } while (Token.accept(TK_COMMA));

  // After we've seen all the phases, assign phase indexes to each:
  // If a value was provided (e.g. name=23), use that value as the next id
  // to use. If not, just assign the next sequential id.  NOTE that this
  // explicitly allows duplicates!!
  int PhaseIndex = 0;
  for (auto *Phase : *Phases) {
    if (Phase->getIndex() == -1)
      Phase->setIndex(PhaseIndex++);
    else
      PhaseIndex = Phase->getIndex() + 1;
  }

  return Phases;
}

//-----------------------------------------------------------------------------
// Process phase_id rules. These return a single name or a set of names.
//     phase_id: '#'? ident ('[' range ']')? ('=' number)? ;
// Return a list of (possibly one) pineline phase names.
//-----------------------------------------------------------------------------
PhaseNameList *MdlParser::parsePhaseId(
                      bool IsProtected, bool IsHard, bool &IsFirst) {
  MdlItem Item(Token());
  long First = -1, last = -1;
  long Cycle = -1;

  // Check for the "First execute stage" indicator.
  IsFirst = Token.accept(TK_SHARP);
  Identifier *Name = parseIdent("Pipe phase name");
  if (Name == nullptr) return nullptr;

  auto *Phases = new PhaseNameList;

  // Parse the optional range
  if (Token.accept(TK_LBRACKET)) {
    if (!parseRange(First, last)) return nullptr;
    if (!Token.expect(TK_RBRACKET)) return nullptr;
  }
  if (Token.accept(TK_EQUAL) && !parseNumber(Cycle, "Cycle")) return nullptr;

  // If a range was not specified, just return the name we found.
  // Set its pipe phase (its index) if the cycle is specified.
  if (First == -1) {
    auto *Phase = new PhaseName(Item, Name->getName(), IsProtected, IsHard);
    if (Cycle != -1) Phase->setIndex(Cycle);
    Phases->push_back(Phase);
  } else {
    // If a range was specified, create a vector of names (range inclusive).
    for (int Id = First; Id <= last; Id++) {
      auto *Phase = new PhaseName(
          Item, formatv("{0}{1}", Name->getName(), Id), IsProtected, IsHard);
      if (Cycle != -1) Phase->setIndex(Cycle++);
      Phases->push_back(Phase);
    }
  }
  return Phases;
}


//-----------------------------------------------------------------------------
// Process resource_def rules:
//     resource_def : RESOURCE ('(' phase ('..' phase)? ')')
//                             resource_decl (',' resource_decl)* ';'
// Return a list of resource definitions.
//-----------------------------------------------------------------------------
ResourceDefList *MdlParser::parseResourceDef() {
  if (!Token.accept(TK_RESOURCE)) return nullptr;
  Identifier *Start = nullptr;
  Identifier *End = nullptr;

  if (Token.check(TK_LPAREN) && !parsePhaseRange(Start, End)) return nullptr;

  auto *Defs = new ResourceDefList;
  do {
    auto *Def = parseResourceDecl(Start, End);
    if (Def == nullptr) return nullptr;
    Defs->push_back(Def);
  } while (Token.accept(TK_COMMA));

  Token.expect(TK_SEMI);
  return Defs;
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
ResourceDef *MdlParser::parseResourceDecl(Identifier *Start, Identifier *End) {
  MdlItem Item(Token());
  Identifier *Name = parseIdent("Resource name");
  if (Name == nullptr) return nullptr;

  long Bits = -1;
  long Count = -1;;
  if (Token.accept(TK_COLON)) {                  // ':' number
    if (!parseNumber(Bits, "Bit width")) return nullptr;
  }

  if (Token.accept(TK_LBRACE)) {            // '{' group_list '}'
    GroupType Type;
    IdList *Members = parseGroupList(Type);
    if (!Token.expect(TK_RBRACE)) return nullptr;
    auto *Group = new ResourceDef(Item, Name, Bits, Members, Start, End);
    Group->setGroupType(Type);
    return Group;
  }

  if (Token.accept(TK_LBRACKET)) {               // '[' number ']'
    if (!parseNumber(Count, "Resource count")) return nullptr;
    if (!Token.expect(TK_RBRACKET)) return nullptr;
  }

  return new ResourceDef(Item, Name, Bits, Count, Start, End);
}

//-----------------------------------------------------------------------------
// Process an issue_statement rule:
//     issue_statement : ISSUE ('(' phase ('..' phase)? ')'? name_list ';'
// Return a list of resource definitions.
//-----------------------------------------------------------------------------
ResourceDefList *MdlParser::parseIssueStatement() {
  Identifier *Start = nullptr;
  Identifier *End = nullptr;

  if (!Token.accept(TK_ISSUE)) return nullptr;
  if (Token.check(TK_LPAREN) && !parsePhaseRange(Start, End)) return nullptr;

  auto *Slots = parseNameList("Issue slot name", TK_COMMA);
  if (Slots == nullptr) return nullptr;
  Token.expect(TK_SEMI);

  auto *Defs = new ResourceDefList;
  for (auto *Id : *Slots)
    Defs->push_back(new ResourceDef(*Id, Id, -1, -1, Start, End));
  return Defs;
}

//-----------------------------------------------------------------------------
// Process a reorder_buffer_def rule:
//     reorder_buffer_def: REORDER_BUFFER '<' number '>' ';'
//-----------------------------------------------------------------------------
int MdlParser::parseReorder() {
  if (!Token.accept(TK_REORDER_BUFFER)) return 0;
  if (!Token.expect(TK_LT)) return 0;
  if (!Token.check(TK_NUMBER)) return 0;
  long Number;
  parseNumber(Number, "Reorder buffer size");
  if (!Token.expect(TK_GT)) return 0;
  Token.expect(TK_SEMI);
  return Number;
}

//-----------------------------------------------------------------------------
// Process a range rule.
//     range: number ".." number
//-----------------------------------------------------------------------------
bool MdlParser::parseRange(long &First, long &Last) {
  First = Last = 0;
  if (!parseNumber(First, "Range start")) return false;
  if (!Token.expect(TK_DOTDOT)) return false;
  if (!parseNumber(Last, "Range end")) return false;
  if (First > Last) { ParseError("Invalid range"); return false; }
  return true;
}

//-----------------------------------------------------------------------------
// Parse a phase range:
//    '(' phase ('..' phase)? ')'
//-----------------------------------------------------------------------------
bool MdlParser::parsePhaseRange(Identifier *&Start, Identifier *&End) {
  Start = End = nullptr;
  if (Token.accept(TK_LPAREN)) {
    Start = parseIdent("Phase name");
    if (Start == nullptr) return false;
    if (Token.accept(TK_DOTDOT) && (End = parseIdent("Phase name")) == nullptr)
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
IdList *MdlParser::parseNameList(std::string Type, TokenKind Separator) {
  auto *Names = new IdList;
  int Index = 0;
  do {
    auto *Name = parseIdent(Type);
    if (Name == nullptr) return nullptr;
    Name->setIndex(Index++);
    Names->push_back(Name);
  } while (Token.accept(Separator));
  return Names;
}

//-----------------------------------------------------------------------------
// Parse a predicate list for subunits and latency statements.
//      predicate_list:  name_list ':'
// Note: predicates cannot be reserved keywords.
//-----------------------------------------------------------------------------
IdList *MdlParser::parsePredicateList() {
  IdList *Predicates = nullptr;
  if (Token.check(TK_IDENT)) {
    Predicates = parseNameList("Predicate name", TK_COMMA);
    if (!Token.expect(TK_COLON)) return nullptr;
  }

  return Predicates;
}

//-----------------------------------------------------------------------------
// Process a group_list rule.
//       group_list: ident ('|' ident)*
//                 | ident ('&' ident)*
// Return a list of names.
//-----------------------------------------------------------------------------
IdList *MdlParser::parseGroupList(GroupType &Type) {
  auto *Names = new IdList;
  Identifier *Member = nullptr;
  Type = GroupType::kUseAll;

  // Get the first member.  Note the type of separator.
  if ((Member = parseIdent("Member name")) == nullptr) return nullptr;
  Names->push_back(Member);
  if (!Token.check(TK_AND, TK_OR, TK_COMMA)) return Names;
  auto Separator = Token.getType();
  if (Separator != TK_AND) Type = GroupType::kUseSingle;

  while (Token.accept(Separator)) {
    if ((Member = parseIdent("Member name")) == nullptr) return nullptr;
    Names->push_back(Member);
  } while (Token.accept(Separator));

  // Set indexes for each member.
  int Index = 0;
  for (auto *Name : *Names)
    Name->setIndex(Index++);
  return Names;
}

//-----------------------------------------------------------------------------
// Process a list of base templates.
//       base_list: (':' ident)* ;
//-----------------------------------------------------------------------------
IdList *MdlParser::parseBaseList(std::string Type) {
  if (!Token.check(TK_COLON)) return nullptr;
  auto *Names = new IdList;
  while (Token.accept(TK_COLON)) {
    auto *Base = parseIdent(Type);
    if (Base == nullptr) return nullptr;
    Names->push_back(Base);
  }
  return Names;
}

//-----------------------------------------------------------------------------
// Process a list of subunit bases, which can be subunit names or strings
// representing regular expressions of instruction names.
//       su_base_list: (':' (ident|STRING_LITERAL))* ;
//-----------------------------------------------------------------------------
void MdlParser::parseSuBaseList(IdList *&Bases, StringList *&Regex) {
  if (!Token.check(TK_COLON)) return;

  while (Token.accept(TK_COLON)) {
    if (isIdent()) {
      if (Bases == nullptr) Bases = new IdList;
      Bases->push_back(parseIdent("Subunit base name"));
    }
    if (Token.check(TK_STRING)) {
      auto Pattern = parseString("RegEx pattern");
      if (Pattern.empty()) continue;
      if (Regex == nullptr) Regex = new StringList;
      Regex->push_back(Pattern);
    }
  }
}

//-----------------------------------------------------------------------------
// Process a register_def rule:
//      register_def : REGISTER register_decl (',' register_decl)* ';'
// A register declaration can declare a set of registers, each of which
// can be a set of registers. Expand them all to a single vector, and return it.
//-----------------------------------------------------------------------------
RegisterDefList *MdlParser::parseRegisterDef() {
  if (!Token.accept(TK_REGISTER)) return nullptr;

  auto *Regs = parseRegisterList();
  if (Regs == nullptr) return nullptr;
  Token.expect(TK_SEMI);
  return Regs;
}

//-----------------------------------------------------------------------------
// Process a register_class rule:
//    register_class : REGCLASS ident '{'
//                             register_decl (',' register_decl)* '}' ';' ;
// A register class declaration creates a collection of register definitions.
//-----------------------------------------------------------------------------
RegisterClass *MdlParser::parseRegisterClass() {
  MdlItem Item(Token());
  if (!Token.accept(TK_REGCLASS)) return nullptr;
  Identifier *Name = parseIdent("Register class name");
  if (Name == nullptr) return nullptr;
  if (!Token.expect(TK_LBRACE)) return nullptr;

  RegisterDefList *Regs = nullptr;
  // Parse the registers in this class.
  if (!Token.check(TK_RBRACE))
    Regs = parseRegisterList();
  if (!Token.expect(TK_RBRACE)) return nullptr;

  if (Regs == nullptr) Regs = new RegisterDefList;
  Token.accept(TK_SEMI);     // Skip optional semicolon
  return new RegisterClass(Item, Name, Regs);
}

//-----------------------------------------------------------------------------
// Parse a register definition list:
//-----------------------------------------------------------------------------
RegisterDefList *MdlParser::parseRegisterList() {
  // Each register declaration can return a list, so append them together.
  auto *Regs = new RegisterDefList;
  do {
    RegisterDefList *Regset;
    if ((Regset = parseRegisterDecl()) == nullptr) return nullptr;
    Regs->insert(Regs->end(), Regset->begin(), Regset->end());
    delete Regset;
  } while (Token.accept(TK_COMMA));
  return Regs;
}

//-----------------------------------------------------------------------------
// Process a register_decl rule:
//     register_decl: ident | ident '[' range ']' ;
// If a range of registers is specified, expand to a list.
//-----------------------------------------------------------------------------
RegisterDefList *MdlParser::parseRegisterDecl() {
  MdlItem Item(Token());
  auto *Reg = parseIdent("Register name");
  if (Reg == nullptr) return nullptr;

  auto *Regs = new RegisterDefList;
  // If no range was specified, just return a single register.
  if (!Token.accept(TK_LBRACKET)) {
    Regs->push_back(new RegisterDef(Item, Reg));
    return Regs; // return the single definition in a vector.
  }

  // If a range was specified, create a vector of register names.
  long First, Last;
  parseRange(First, Last);
  for (int Id = First; Id <= Last; Id++) {
    auto *Def = new Identifier(Item, Reg->getName() + std::to_string(Id));
    Regs->push_back(new RegisterDef(Item, Def));
  }
  Token.expect(TK_RBRACKET);
  return Regs;
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
Identifier *MdlParser::parseIdent(std::string Type) {
  // First check for keywords that we allow as identifiers.
  if (!isIdent())
    return ParseError("{0} expected", Type), nullptr;

  auto *Ident = new Identifier(Token(), Token.getText());
  ++Token;
  return Ident;
}

//-----------------------------------------------------------------------------
// Process a required string token.
// Return an empty string if non-string encountered.
//-----------------------------------------------------------------------------
std::string MdlParser::parseString(std::string Type) {
  if (!Token.check(TK_STRING)) return ParseError("{0} expected", Type), "";
  auto Text = Token.getText();
  ++Token;
  return Text;
}

//-----------------------------------------------------------------------------
// Process a required positive number token.
// Return true if number not found.
//-----------------------------------------------------------------------------
bool MdlParser::parseNumber(long &Value, std::string Type) {
  if (!Token.check(TK_NUMBER))
    return ParseError("{0} expected", Type), false;
  Value = Token.getValue();
  ++Token;
  return true;
}

//-----------------------------------------------------------------------------
// Process a required possibly signed number token.
//-----------------------------------------------------------------------------
bool MdlParser::parseSignedNumber(long &Value, std::string Type) {
  bool Neg = Token.accept(TK_MINUS);
  if (!Token.check(TK_NUMBER))
     return ParseError("{0} expected", Type), false;
  Value = Token.getValue();
  if (Neg) Value = -Value;
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

InstructionDef *MdlParser::parseInstructionDef() {
  MdlItem Item(Token());
  if (!Token.accept(TK_INSTRUCT)) return nullptr;
  Identifier *Name = parseIdent("Instruction name");
  if (Name == nullptr) return nullptr;

  // Parse the instructions operands.
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *Operands = new OperandDeclList;
  bool Ellipsis = false;

  if (!Token.check(TK_RPAREN))
  do {
    auto *Operand = parseOperandDecl(kOpndNameRequired);
    if (Operand == nullptr) return nullptr;
    if (Operand->isEllipsis()) {
      if (Token.check(TK_COMMA))
        ParseError(Operand, "Ellipsis must be last declared operand");
      else Ellipsis = true;
    } else Operands->push_back(Operand);
  } while (Token.accept(TK_COMMA));

  if (!Token.expect(TK_RPAREN)) return nullptr;
  if (!Token.expect(TK_LBRACE)) return nullptr;

  IdList *Subunits = nullptr;
  if (Token.accept(TK_SUBUNIT)) {
    if (!Token.expect(TK_LPAREN)) return nullptr;
    Subunits = parseNameList("Subunit name", TK_COMMA);
    if (Subunits == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;
    Token.expect(TK_SEMI);
  }
  IdList *Derived = nullptr;
  if (Token.accept(TK_DERIVED)) {
    if (!Token.expect(TK_LPAREN)) return nullptr;
    Derived = parseNameList("Base instruction name", TK_COMMA);
    if (Derived == nullptr) return nullptr;
    if (!Token.expect(TK_RPAREN)) return nullptr;
    Token.expect(TK_SEMI);
  }
  if (!Token.expect(TK_RBRACE)) return nullptr;
  Token.accept(TK_SEMI);      // Skip optional semicolon

  return new InstructionDef(Item, Name, Operands, Subunits, Derived, Ellipsis);
}

//-----------------------------------------------------------------------------
// Process an operand declaration rule (used in instructions and operands) :
//     operand_decl : (type=ident (name=ident)? ('(I)' | '(O)')?) | '...'
// Operand definitions can have unnamed operands.  We create a name based on
// the operand index (like "$2").
// Instruction definitions may have implied operands that only have a name, but
// these names MUST be a defined register.  Since we can't check that yet, we
// set the name to the type if a name isn't provided.  We check this later.
//-----------------------------------------------------------------------------
OperandDecl *MdlParser::parseOperandDecl(int OpndId) {
  MdlItem Item(Token());
  Identifier *Type = nullptr;
  Identifier *Name = nullptr;

  // We only allow ellipses operands for instructions.
  if (Token.accept(TK_ELLIPSIS)) {
    if (OpndId != kOpndNameRequired) {
      ParseError(&Item, "Ellipsis not allowed in operand definitions");
      Name = Type = new Identifier(Item, "...");
    } else Name = new Identifier(Item, "");
    return new OperandDecl(Item, Type, Name, true, false, false, false);
  }

  // Parse the operand type, followed by the sometimes optional operand name.
  if ((Type = parseIdent("Operand type")) == nullptr) return nullptr;
  if (isIdent())
    if ((Name = parseIdent("Operand name")) == nullptr) return nullptr;

  bool IsInput = false;
  bool IsOutput = false;
  if (!parseInputOutput(IsInput, IsOutput)) return nullptr;

  // Handle case where we didn't see an operand name.
  // For operand operands, synthesize a name based on the operand id.
  if (Name == nullptr && OpndId != kOpndNameRequired)
    Name = new Identifier(Item, std::to_string(OpndId));
  // For instruction operands, use the type as the name, if not provided.
  bool HasName = Name != nullptr;
  if (Name == nullptr)
    Name = Type;

  return new OperandDecl(Item, Type, Name, false, IsInput, IsOutput, HasName);
}

//-----------------------------------------------------------------------------
// Process the operand input output qualifiers:
//     '(I)' | '(O)'
// Return true if no errors found.
//-----------------------------------------------------------------------------
bool MdlParser::parseInputOutput(bool &IsInput, bool &IsOutput) {
  if (!Token.accept(TK_LPAREN)) return true;
  auto *Type = parseIdent("Input/Output attribute");
  if (Type == nullptr) return false;
  if (!Token.expect(TK_RPAREN)) return false;

  if (Type->getName() == "I") IsInput = true;
  else if (Type->getName() == "O") IsOutput = true;
  else return ParseError("Invalid I/O attribute"), false;
  return true;
}

//-----------------------------------------------------------------------------
// Process an operand or derived operand definition rule:
// operand_def : OPERAND name=ident '(' (operand_decl (',' operand_decl)*)? ')'
//                          '{' (operand_type | operand_attribute)* '}' ';'?
// derived_operand_def : OPERAND name=ident ':' base=ident ('(' ')')?
//                          '{' (operand_type | operand_attribute)* '}' ';'?
//-----------------------------------------------------------------------------
OperandDef *MdlParser::parseOperandDef() {
  MdlItem Item(Token());
  if (!Token.accept(TK_OPERAND)) return nullptr;
  auto *Name = parseIdent("Operand name");
  if (Name == nullptr) return nullptr;
  Identifier *Type = nullptr;
  auto *Operands = new OperandDeclList;
  auto *Attributes = new OperandAttributeList;

  // Parse the base operands of a derived operand definition.
  IdList *Bases = nullptr;
  if (Token.check(TK_COLON))  {
    Bases = parseBaseList("Operand base name");
    if (Bases == nullptr) return nullptr;
    if (Token.accept(TK_LPAREN)) Token.expect(TK_RPAREN);
  }
  // Parse the suboperands of a regular operand definition.
  else if (Token.expect(TK_LPAREN)) {
    int OpndId = 0;
    if (!Token.accept(TK_RPAREN)) {
      do {
        auto *Operand = parseOperandDecl(OpndId++);
        if (Operand == nullptr) return nullptr;
        Operands->push_back(Operand);
      } while (Token.accept(TK_COMMA));
      if (!Token.expect(TK_RPAREN)) return nullptr;
    }
  }

  // Parse the operand body (for both operands and derived operands).
  if (!Token.expect(TK_LBRACE)) return nullptr;
  do {
    Type = parseOperandType(Type);
    if (auto *Attr = parseOperandAttribute())
      Attributes->insert(Attributes->end(), Attr->begin(), Attr->end());
  } while (!Token.accept(TK_RBRACE));
  Token.accept(TK_SEMI);             // Skip optional semicolon

  return new OperandDef(Item, Name, Operands, Type, Attributes, Bases);
}

//-----------------------------------------------------------------------------
// Process an operand_type rule:
//    operand_type: TYPE '(' ident ')' ';'
//-----------------------------------------------------------------------------
Identifier *MdlParser::parseOperandType(Identifier *LastType) {
  MdlItem Item(Token());
  if (!Token.accept(TK_TYPE)) return nullptr;
  if (!Token.expect(TK_LPAREN)) return nullptr;
  auto *Type = parseIdent("Operand type");
  if (Type == nullptr) return nullptr;
  if (!Token.expect(TK_RPAREN)) return nullptr;
  if (LastType)
    ParseError(&Item, "Only one type specification allowed");
  Token.expect(TK_SEMI);
  return Type;
}

//-----------------------------------------------------------------------------
// Process an operand attribute definition rule:
// operand_attribute :
//        (predicate=name_list ':')? operand_attribute_stmt
//      | predicate=name_list ':' '{' operand_attribute_stmt* '}' ';'?
//-----------------------------------------------------------------------------
OperandAttributeList *
MdlParser::parseOperandAttribute() {
  // Parse an optional predicate set.
  IdList *Predicates = nullptr;
  if (Token.check(TK_IDENT)) {
    Predicates = parsePredicateList();
    if (Predicates == nullptr) return nullptr;
  }

  // Parse a block of attributes, or just a single attribute.
  auto *Attributes = new OperandAttributeList;
  bool IsBlock = Token.accept(TK_LBRACE);
  do {
    auto *Attr = parseOperandAttributeStmt(Predicates);
    if (Attr == nullptr) return nullptr;
    Attributes->push_back(Attr);
  } while(IsBlock && !Token.accept(TK_RBRACE));
  if (IsBlock) Token.accept(TK_SEMI);

  return Attributes;
}

//-----------------------------------------------------------------------------
// Process an operand attribute definition rule:
//  operand_attribute_stmt : ATTRIBUTE ident '=' (snumber | tuple)
//                           (IF type '[' pred_value (',' pred_value)* ']')? ';'
//  tuple : '[' snumber (',' snumber)* ']'
//-----------------------------------------------------------------------------
OperandAttribute *
MdlParser::parseOperandAttributeStmt(IdList *Predicate) {
  MdlItem Item(Token());
  if (!Token.accept(TK_ATTRIBUTE)) return nullptr;
  auto *Name = parseIdent("Operand attribute name");
  if (Name == nullptr) return nullptr;
  if (!Token.expect(TK_EQUAL)) return nullptr;
  long Number = 0;

  // Parse either a single value, or a tuple of values.
  auto *Values = new std::vector<int>;
  auto *PredValues = new PredValueList;
  bool IsTuple = Token.accept(TK_LBRACKET);
  do {
    if (!parseSignedNumber(Number, "Number")) return nullptr;
    Values->push_back(Number);
  } while (IsTuple && Token.accept(TK_COMMA));

  if (IsTuple && !Token.expect(TK_RBRACKET)) return nullptr;
  std::string Type;

  // Parse optional IF clause.
  if (Token.accept(TK_IF)) {
    auto *Ident = parseIdent("Attribute value type");
    if (Ident == nullptr) return nullptr;
    if (Ident->getName() != "label" && Ident->getName() != "address" &&
        Ident->getName() != "lit") {
      ParseError(&Item, "Invalid predicate type: {0}", Ident->getName());
      Token.accept(TK_SEMI);
      return nullptr;
    }
    Type = Ident->getName();

    // Parse the optional predicate value list.
    if (Token.accept(TK_LBRACKET)) {
      do {
        auto *PredValue = parsePredValue();
        if (PredValue == nullptr) return nullptr;
        PredValues->push_back(PredValue);
      } while (Token.accept(TK_COMMA));
      Token.expect(TK_RBRACKET);
    }
  }
  Token.expect(TK_SEMI);

  return new OperandAttribute(Item, Name, Values, Type, PredValues, Predicate);
}

//-----------------------------------------------------------------------------
// Process a predicate value:
//     value=snumber | low=snumber '..' high=snumber | '{' mask=number '}'
//-----------------------------------------------------------------------------
PredValue *MdlParser::parsePredValue() {
  MdlItem Item(Token());

  // Parse and return a "mask" value.
  if (Token.accept(TK_LBRACE)) {
    long Number;
    if (!parseSignedNumber(Number, "Mask value")) return nullptr;
    if (!Token.expect(TK_RBRACE)) return nullptr;
    return new PredValue(Item, Number);
  }

  // Parse either a single number, or a range.
  long Low, High;
  if (!parseSignedNumber(Low, "Low range number")) return nullptr;
  if (Token.accept(TK_DOTDOT)) {                      // snumber .. snumber
    if (!parseSignedNumber(High, "High range number")) return nullptr;
    if (Low <= High) return new PredValue(Item, Low, High);
    ParseError(&Item, "Invalid value range: {0}..{1}", Low, High);
    return new PredValue(Item, 0, 0); // dummy value
  }
  // Return a single signed number;
  return new PredValue(Item, Low, Low);
}

//-----------------------------------------------------------------------------
// Convert a predicate expression string to an internal expression type.
//-----------------------------------------------------------------------------
static PredOp NameToOp(std::string Name) {
  static auto *PredicateOps = new std::unordered_map<std::string, PredOp>(
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

  if (PredicateOps->count(Name))
    return (*PredicateOps)[Name];
  return PredOp::kEmpty;
}

//-----------------------------------------------------------------------------
// Process a predicate definition:
//    predicate_def : PREDICATE ident ':' predicate_op? ';'
// Return true if it appeared to be a predicate statement (even if errors).
//-----------------------------------------------------------------------------
bool MdlParser::parsePredicateDef() {
  if (!Token.accept(TK_PREDICATE)) return false;

  auto *Name = parseIdent("Predicate name");
  if (Name == nullptr) return true;
  if (!Token.expect(TK_COLON)) return true;

  auto *Expr = parsePredicateOp();
  if (Expr == nullptr) return true;
  Token.expect(TK_SEMI);

  // If we see a predicate expression, add it to the table.
  Spec.enterPredicate(Name->getName(), Expr);
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
PredExpr *MdlParser::parsePredicateOp() {
  MdlItem Item(Token());

  auto code_escape = parseCodeEscape();
  if (!code_escape.empty())
    return new PredExpr(Item, PredOp::kCode, code_escape);

  // Parse the predicate opcode name, and look it up.
  auto *Name = parseIdent("Predicate name");
  if (Name == nullptr) return nullptr;
  auto Opcode = NameToOp(Name->getName());
  if (Opcode == PredOp::kEmpty)
    return new PredExpr(Item, PredOp::kName, Name->getName());

  // Handle parameterless definitions.
  if (!Token.accept(TK_LT)) return new PredExpr(Item, Opcode);

  // Read in parameters and create the overall expression.
  std::vector<PredExpr *> Opnds;
  do {
    if (Token.check(TK_GT)) break;           // Skip if '<' '>'
    auto *Opnd = parsePredicateOpnd();
    if (Opnd == nullptr) return nullptr;
    Opnds.push_back(Opnd);
  } while (Token.accept(TK_COMMA));
  if (!Token.expect(TK_GT)) return nullptr;

  // Error check the number and general type of operands. Each predicate type
  // has its own expected operands.  We check the form of these operands now,
  // and we check the semantics after the entire spec has been visited.
  unsigned MinOpnds, MaxOpnds;
  switch (Opcode) {
  default:
    return new PredExpr(Item, Opcode, Opnds);

  // All operands are names (we'll check that they're instructions later).
  case PredOp::kCheckOpcode:
    for (auto *Opnd : Opnds)
      if (!Opnd->isName())
        ParseError(Opnd, "Instruction name expected");
    return new PredExpr(Item, Opcode, Opnds);

  // All operands should be predicate expressions.
  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kCheckAll:
  case PredOp::kCheckAny:
    for (auto *Opnd : Opnds)
      if (!Opnds.empty() && !Opnd->isPred())
        ParseError(Opnd, "Predicate expression expected");
    return new PredExpr(Item, Opcode, Opnds);

  // The second operand should be a predicate expression.
  case PredOp::kOpcodeSwitchCase:
    if (Opnds.size() > 1 && !Opnds[1]->isPred())
      ParseError(Opnds[1], "Predicate expression expected");
    MinOpnds = MaxOpnds = 2;
    break;

  // The single operand should be a predicate expression.
  case PredOp::kReturnStatement:
  case PredOp::kCheckNot:
    if (!Opnds.empty() && !Opnds[0]->isPred())
      ParseError(Opnds[0], "Predicate expression expected");
    MinOpnds = MaxOpnds = 1;
    break;

  // The single operand should be an operand index.
  case PredOp::kCheckIsRegOperand:
  case PredOp::kCheckIsImmOperand:
  case PredOp::kCheckInvalidRegOperand:
  case PredOp::kCheckZeroOperand:
    if (!Opnds.empty() && !Opnds[0]->isIntegerOrOperand())
      ParseError(Opnds[0], "Operand index expected");
    MinOpnds = MaxOpnds = 1;
    break;

  case PredOp::kCheckSameRegOperand:
    if (!Opnds.empty() && !Opnds[0]->isIntegerOrOperand())
      ParseError(Opnds[0], "Operand index expected");
    if (Opnds.size() > 1 && !Opnds[1]->isIntegerOrOperand())
      ParseError(Opnds[1], "Operand index expected");
    MinOpnds = MaxOpnds = 2;
    break;

  // The single operand should be an operand count.
  case PredOp::kCheckNumOperands:
    if (!Opnds.empty() && !Opnds[0]->isInteger())
      ParseError(Opnds[0], "Operand count expected");
    MinOpnds = MaxOpnds = 1;
    break;

  // Function predicates have strings representing MI and MC functions.
  case PredOp::kCheckFunctionPredicateWithTII:
  case PredOp::kCheckFunctionPredicate:
    if (!Opnds.empty() && !Opnds[0]->isString())
      ParseError(Opnds[0], "MC function expected");
    if (Opnds.size() > 1 && !Opnds[1]->isString())
      ParseError(Opnds[1], "MI function expected");
    if (Opcode == PredOp::kCheckFunctionPredicate) {
      MinOpnds = MaxOpnds = 2;
      break;
    }
    if (Opnds.size() > 2 && !Opnds[1]->isString())
      ParseError(Opnds[1], "TII prefix expected");
    MinOpnds = 2;
    MaxOpnds = 3; // TII operand is optional
    break;

  // Register operand predicates have an index, a register name, and an
  // optional function.
  case PredOp::kCheckRegOperand:
    if (!Opnds.empty() && !Opnds[0]->isIntegerOrOperand())
      ParseError(Opnds[0], "Operand index expected");
    if (Opnds.size() > 1 && !Opnds[1]->isName())
      ParseError(Opnds[1], "Register name expected");
    if (Opnds.size() > 2 && !Opnds[2]->isString())
      ParseError(Opnds[2], "Predicate function name expected");
    MinOpnds = 2;
    MaxOpnds = 3;
    break;

  // Immediate operand predicates have an index, an integer or symbolic name,
  // and an optional function.
  case PredOp::kCheckImmOperand:
    if (!Opnds.empty() && !Opnds[0]->isIntegerOrOperand())
      ParseError(Opnds[0], "Operand index expected");
    if (Opnds.size() > 1 && !Opnds[1]->isInteger() && !Opnds[1]->isString())
      ParseError(Opnds[1], "Immediate value expected");
    if (Opnds.size() > 2 && !Opnds[2]->isString())
      ParseError(Opnds[2], "Predicate function name expected");
    MinOpnds = 1;
    MaxOpnds = 3;
    break;
  }

  if (Opnds.size() < MinOpnds)
    ParseError(&Item, "Missing operands ({0} expected)", MinOpnds);
  if (Opnds.size() > MaxOpnds)
    ParseError(&Item, "Extra operands ({0} expected)", MaxOpnds);

  return new PredExpr(Item, Opcode, Opnds);
}

//-----------------------------------------------------------------------------
// Parse a predicate code escape:
//      '[' '{' <c code> '}' ']'
//-----------------------------------------------------------------------------
std::string MdlParser::parseCodeEscape() {
  if (!Token.check(TK_LBRACKET) || !Token.peek(TK_LBRACE)) return "";

  // parse and concatenate tokens until a '}' and ']' are found.
  if (!Token.getCodeEscape()) return "";
  auto Code = "[{ " + Token.getText() + " }]";
  ++Token;
  return Code;
}

//-----------------------------------------------------------------------------
// Process a predicate operand:
//    pred_opnd : name=ident | snumber | STRING_LITERAL | predicate_op |
//                '[' opcode=ident (',' ident)* ']' | operand
//-----------------------------------------------------------------------------
PredExpr *MdlParser::parsePredicateOpnd() {
  MdlItem Item(Token());
  // Parse string parameters.
  if (Token.check(TK_STRING))
    return new PredExpr(Item, PredOp::kString, parseString("String"));

  // parse signed and unsigned numbers
  if (Token.check(TK_NUMBER)) {
    long Number;
    parseSignedNumber(Number, "Number");
    return new PredExpr(Item, PredOp::kNumber, std::to_string(Number));
  }

  // Parse an opcode list.
  if (Token.accept(TK_LBRACKET)) {
    auto *Names = parseNameList("Instruction name", TK_COMMA);
    Token.expect(TK_RBRACKET);
    if (Names == nullptr) return nullptr;
    std::vector<PredExpr *> Opcodes;
    for (auto *Name : *Names)
      Opcodes.push_back(new PredExpr(*Name, PredOp::kName, Name->getName()));
    delete Names;
    return new PredExpr(Item, PredOp::kCheckOpcode, Opcodes);
  }

  // Parse an operand reference ( (ident ':')? '$' ident, etc)
  if (isOperand()) {
    auto *Opnd = parseOperand();
    if (Opnd) return new PredExpr(Item, PredOp::kOperandRef, Opnd);
  }

  // If we get here, it must be a predicate_op.
  return parsePredicateOp();
}

} // namespace mdl
} // namespace mpact

//===- mdl_visitor.cpp - Parse the file and process the parse tree --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Parser driver and visitors for the machine description language.
//
// This file contains the top level parser driver and visitor functions used
// to extract semantic information from the generated parse tree.
//
// IF YOU CHANGE MDL.G4, YOU SHOULD ASSUME YOU HAVE TO CHANGE THIS FILE.
//  - some mdl changes will result in compilation failures.
//  - some changes could result in runtime crashes.
//
//===----------------------------------------------------------------------===//

#include "mdl_visitor.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "llvm/Support/Error.h"

namespace mpact {
namespace mdl {

//-----------------------------------------------------------------------------
// Create Antlr lexer and parser, then parse the input stream.
// After parsing, visit all the notes in the tree, collect all the
// CPU definition information and add it to the MdlSpec object.
// Return true if successful, false if errors found.
//-----------------------------------------------------------------------------
bool MdlVisitor::ProcessInputFile(std::string file_name) {
  // If we've already seen this file name, not an error, but there's no point
  // in parsing it again.
  if (!imported_files_.emplace(file_name).second)
    return true;

  // Attempt to open the file.
  std::fstream *mdl_source = new std::fstream(file_name, std::fstream::in);
  if (!mdl_source->is_open()) {
    llvm::errs() << formatv("File not found: \"{0}\"\n", file_name);
    return false;
  }

  // Create an input stream for Antlr.
  antlr4::ANTLRInputStream mdl_input;
  mdl_input.load(*mdl_source);

  // Create an Antlr lexer.
  mdlLexer lexer(&mdl_input);

  // Create a token stream for the parser.
  antlr4::CommonTokenStream tokens(&lexer);

  // Create an Antlr parser.
  mdlParser parser(&tokens);

  // Create an error listener and attach to parser (to look for syntax errors).
  MdlErrorListener error_listener(file_name);
  parser.removeErrorListeners();
  parser.addErrorListener(&error_listener);

  // Run the parser over the token stream.
  ArchitectureSpecCtx *arch = parser.architecture_spec();

  if (error_listener.syntax_error_count())
    return false;

  // Visit all the nodes in the tree.
  std::string old_file_name = current_file_name();
  set_current_file_name(file_name);

  bool success = VisitArchitectureSpec(arch);
  set_current_file_name(old_file_name);
  return success;
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
bool MdlVisitor::VisitArchitectureSpec(ArchitectureSpecCtx *ctx) {
  bool success = true;
  for (auto *item : ctx->architecture_item()) {
    if (auto *pipe = item->pipe_def()) {
      spec().pipe_phases().push_back(VisitPipeDef(pipe));
    } else if (auto *reg = item->register_def()) {
      RegisterDefList *regs = VisitRegisterDef(reg);
      spec().registers().insert(spec().registers().end(), regs->begin(),
                                regs->end());
    } else if (auto *regclass = item->register_class()) {
      spec().reg_classes().push_back(VisitRegisterClass(regclass));
    } else if (auto *resdef = item->resource_def()) {
      ResourceDefList *res = VisitResourceDef(resdef);
      spec().resources().insert(spec().resources().end(), res->begin(),
                                res->end());
    } else if (auto *cpu = item->cpu_def()) {
      spec().cpus().push_back(VisitCpuDef(cpu));
    } else if (auto *func = item->func_unit_template()) {
      spec().func_units().push_back(VisitFuncUnitTemplate(func));
    } else if (auto *func = item->func_unit_group()) {
      spec().func_unit_groups().push_back(VisitFuncUnitGroup(func));
    } else if (auto *subunit = item->subunit_template()) {
      spec().subunits().push_back(VisitSubunitTemplate(subunit));
    } else if (auto *latency = item->latency_template()) {
      spec().latencies().push_back(VisitLatencyTemplate(latency));
    } else if (auto *instruction = item->instruction_def()) {
      spec().instructions().push_back(VisitInstructionDef(instruction));
    } else if (auto *operand = item->operand_def()) {
      spec().operands().push_back(VisitOperandDef(operand));
    } else if (auto *operand = item->derived_operand_def()) {
      spec().operands().push_back(VisitDerivedOperandDef(operand));
    } else if (auto *import_file = item->import_file()) {
      success &= VisitImportFile(import_file);
    } else if (auto *family = item->family_name()) {
      spec().set_family_name(VisitFamilySpec(family));
    } else if (auto *pred_def = item->predicate_def()) {
      VisitPredicateDef(pred_def, spec());
    }
  }

  // Find all the implicit latency templates that were created for subunits,
  // and add them to the global set of latency templates.
  for (auto *subunit : spec().subunits())
    if (subunit->inline_latency())
      spec().latencies().push_back(subunit->inline_latency());

  return success;
}

//-----------------------------------------------------------------------------
// Process a family name specification.
//-----------------------------------------------------------------------------
Identifier *MdlVisitor::VisitFamilySpec(FamilyCtx *ctx) {
  return VisitIdent(ctx->ident());
}

//-----------------------------------------------------------------------------
//  Process an import file.
//  Return true if successful, false if any errors are found.
//-----------------------------------------------------------------------------
bool MdlVisitor::VisitImportFile(ImportFileCtx *ctx) {
  // The literal includes the double quotes.  Remove them.
  std::string literal = ctx->STRING_LITERAL()->getText();
  std::filesystem::path import_name = literal.substr(1, literal.length() - 2);

  // Get directory names for the current source file and the import file name.
  auto current_dir = std::filesystem::path(current_file_name()).parent_path();
  auto import_dir = import_name.parent_path();

  auto AddSlash = [](std::string path_name) {
    if (!path_name.empty() && path_name.back() != '/')
      path_name += "/";
    return path_name;
  };

  // If the import name has directory information, use it.
  if (!import_dir.empty()) {
    if (!current_dir.empty() && !import_dir.is_absolute())
      return ProcessInputFile(formatv("{0}{1}{2}", AddSlash(current_dir),
                                      AddSlash(import_dir), import_name));
    return ProcessInputFile(import_name);
  }

  // If the import name doesn't have directory info, see if its in the
  // including file's directory.
  if (!current_dir.empty()) {
    auto name = formatv("{0}{1}", AddSlash(current_dir), import_name);
    if (std::filesystem::exists(name))
      return ProcessInputFile(name);
  }

  // If both the import dir and current directory are empty, check the current
  // directory.
  if (std::filesystem::exists(import_name))
    return ProcessInputFile(import_name);

  // If not found in the current directory, look in the import path.
  if (!import_path_.empty()) {
    auto name = formatv("{0}{1}", AddSlash(import_path_), import_name);
    if (std::filesystem::exists(name))
      return ProcessInputFile(name);
  }

  // Otherwise, just use the name verbatim.
  return ProcessInputFile(import_name);
}

//-----------------------------------------------------------------------------
// Process cpu_def and cpu_stmt rules:
//      cpu_def : CPU|CORE ident '{' cpu_stmt+ '}'
//      cpu_stmt: pipe_def | register_def | resource_def | issue_statement |
//                cluster_instantiation | func_unit_instantiation ;
//-----------------------------------------------------------------------------
CpuInstance *MdlVisitor::VisitCpuDef(CpuDefCtx *ctx) {
  Identifier *name = VisitIdent(ctx->ident());
  auto *pipes = new PipeDefList;
  auto *issues = new ResourceDefList;
  auto *resources = new ResourceDefList;
  int reorder_buffer_size = 0;
  auto *func_units = new FuncUnitInstList;
  auto *forward_stmts = new ForwardStmtList;
  auto *clusters = new ClusterList;

  // Fetch optional names and strip off quotes. Note that we need at least
  // one name, so use the CPU name by default.
  std::vector<std::string> aliases;
  for (auto *item : ctx->STRING_LITERAL()) {
    const auto &alias = item->getText();
    aliases.push_back(alias.substr(1, alias.length() - 2));
  }
  if (aliases.empty())
    aliases.push_back(name->name());

  // for each non-terminal in cpu_stmt, collect information.
  for (auto *stmt : ctx->cpu_stmt()) {
    if (auto *pipe = stmt->pipe_def()) {
      pipes->push_back(VisitPipeDef(pipe));
    } else if (auto *res_def = stmt->resource_def()) {
      ResourceDefList *res = VisitResourceDef(res_def);
      resources->insert(resources->end(), res->begin(), res->end());
    } else if (auto *func = stmt->func_unit_instantiation()) {
      func_units->push_back(VisitFuncUnitInstantiation(func, resources));
    } else if (auto *forward = stmt->forward_stmt()) {
      forward_stmts->push_back(VisitForwardStmt(forward));
    } else if (auto *issue = stmt->issue_statement()) {
      ResourceDefList *slots = VisitIssueStatement(issue);
      issues->insert(issues->end(), slots->begin(), slots->end());
    } else if (auto *cluster = stmt->cluster_instantiation()) {
      clusters->push_back(VisitClusterInstantiation(cluster));
    } else if (auto *reorder = stmt->reorder_buffer_def())
      reorder_buffer_size = reorder->size->value;
  }

  // If we found functional unit instantiations at the cpu level, create a
  // cluster which contains those functional units. (This simplifies things
  // downstream.) Resources can remain global, but any issue resources must
  // also be associated with the generated cluster.
  // If we (still) didn't see any clusters, add an empty one.
  // NOTE: Currently, by design clusters are defined as having their own
  // issue slots. This enforces that rule. If we want clusters to access
  // CPU-level issue slots we'll have to redesign how this works.
  MdlItem item(ctx, current_file_name());
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
// Process cluster definition rules:
//      cluster_instantiation: CLUSTER ident '{' cluster_stmt+ '}'
//      cluster_stmt: register_def | resource_def | issue_statement |
//                    func_unit_instantiation ;
// Return a ClusterInstance object that contains all the information.
//-----------------------------------------------------------------------------
ClusterInstance *
MdlVisitor::VisitClusterInstantiation(ClusterInstantiationCtx *ctx) {
  Identifier *name = VisitIdent(ctx->ident());
  auto *issues = new ResourceDefList;
  auto *resources = new ResourceDefList;
  auto *func_units = new FuncUnitInstList;
  auto *forward_stmts = new ForwardStmtList;

  // For each rule in each cluster_stmt, collect and save information.
  for (auto *stmt : ctx->cluster_stmt()) {
    if (auto *res_def = stmt->resource_def()) {
      ResourceDefList *res = VisitResourceDef(res_def);
      resources->insert(resources->end(), res->begin(), res->end());
    } else if (auto *func = stmt->func_unit_instantiation()) {
      func_units->push_back(VisitFuncUnitInstantiation(func, resources));
    } else if (auto *forward = stmt->forward_stmt()) {
      forward_stmts->push_back(VisitForwardStmt(forward));
    } else if (auto *issue = stmt->issue_statement()) {
      ResourceDefList *slots = VisitIssueStatement(issue);
      issues->insert(issues->end(), slots->begin(), slots->end());
    }
  }

  MdlItem item(ctx, current_file_name());
  return new ClusterInstance(item, name, issues, resources, func_units,
                             forward_stmts);
}

//-----------------------------------------------------------------------------
// Process func_unit_instantiation rules:
//     func_unit_instantiation:
//                       FUNCUNIT func_unit_instance (':' func_unit_instance)*
//                       ident '(' resource refs ')'
//                       ('-> (pin_one | pin_any | pin_all))? ';' ;
//     func_unit_instance: ident ('<>' | '<' number '>')  ;
// Return a FuncUnitInstance object that contains all the information.
//-----------------------------------------------------------------------------
FuncUnitInstance *
MdlVisitor::VisitFuncUnitInstantiation(FuncUnitInstantiationCtx *ctx,
                                       ResourceDefList *resources) {
  auto *type = VisitIdent(ctx->type->ident());
  bool unreserved = ctx->type->unreserved;           // Looking for <> syntax
  int buffer_size = ctx->type->buffered ? ctx->type->buffered->value : -1;
  auto *name = VisitIdent(ctx->ident());
  auto *refs = VisitResourceRefs(ctx->resource_refs(), resources);
  IdList *pin_any = nullptr;
  IdList *pin_all = nullptr;
  MdlItem item(ctx, current_file_name());

  // Maintain a set of buffer sizes for each functional unit and base.
  std::map<std::string, int> buffer_sizes;
  buffer_sizes.emplace(type->name(), buffer_size);

  // If we see a list of bases, create a functional unit template for it.
  // Note: we don't currently allow implicitly defined instances to have
  // arguments passed to them.  If you want that, then define the template!
  if (ctx->bases) {
    IdList *bases = new IdList;
    // Collect some information about base classes.
    for (auto *base : ctx->func_unit_bases()) {
      auto *instance = base->func_unit_instance();
      auto *base_name = VisitIdent(instance->ident());
      bases->push_back(base_name);
      int buffer_size = instance->buffered ? instance->buffered->value : -1;
      buffer_sizes.emplace(base_name->name(), buffer_size);
    }

    // Create an aggregate functional unit template, including the top-level
    // type in the aggregate.
    bases->insert(bases->begin(), type);
    auto derived = based_fu_instance(bases);
    type = new Identifier(*type, derived);
    if (FindItem(spec().func_units(), derived) == nullptr)
      spec().func_units().push_back(new FuncUnitTemplate(item, type, bases));
  }

  if (ctx->one) {
    pin_any = new IdList;
    pin_any->push_back(VisitIdent(ctx->one->ident()));
  }

  if (ctx->all)
    pin_all = VisitPinAll(ctx->all);
  if (ctx->any)
    pin_any = VisitPinAny(ctx->any);

  return new FuncUnitInstance(item, type, name, unreserved, buffer_sizes, refs,
                              pin_any, pin_all);
}

//-----------------------------------------------------------------------------
// Process a single CPU forward statement:
//     forward_stmt : FORWARD ident '->' forward_to_unit (',' forward_to_unit)?
//     forward_to_unit : ident ('(' snumber ')')?
//-----------------------------------------------------------------------------
ForwardStmt *MdlVisitor::VisitForwardStmt(ForwardStmtCtx *ctx) {
  ForwardToSet to_units;
  Identifier *from_unit = VisitIdent(ctx->ident());

  for (auto *forward : ctx->forward_to_unit()) {
    Identifier *to_unit = VisitIdent(forward->ident());
    int cycles = forward->cycles ? forward->cycles->value : 1;
    to_units.emplace_back(to_unit, cycles);
  }

  MdlItem item(ctx, current_file_name());
  return new ForwardStmt(item, from_unit, to_units);
}

//-----------------------------------------------------------------------------
// Process a pin_any rule.
//       pin_any: ident ('|' ident)+ ;
// Return a list of names.
//-----------------------------------------------------------------------------
IdList *MdlVisitor::VisitPinAny(PinAnyCtx *ctx) {
  auto *names = new IdList;
  for (auto *name_ctx : ctx->ident())
    names->push_back(VisitIdent(name_ctx));
  return names;
}

//-----------------------------------------------------------------------------
// Process a pin_all rule.
//       pin_all: ident ('&' ident)+ ;
// Return a list of names.
//-----------------------------------------------------------------------------
IdList *MdlVisitor::VisitPinAll(PinAllCtx *ctx) {
  auto *names = new IdList;
  for (auto *name_ctx : ctx->ident())
    names->push_back(VisitIdent(name_ctx));
  return names;
}

//-----------------------------------------------------------------------------
// Process function_unit_template and func_unit_template_stmt rules:
//  func_unit_template     : FUNCUNIT ident (':' base) '(' func_unit_params? ')'
//                           '{' func_unit_template_stmt* '}'
//  func_unit_template_stmt: resource_def | port_def |
//                              connect_stmt | subunit_instantiation ;
// Return a FuncUnitTemplate object that contains all the information.
//-----------------------------------------------------------------------------
FuncUnitTemplate *MdlVisitor::VisitFuncUnitTemplate(FuncUnitTemplateCtx *ctx) {
  auto *ports = new IdList;
  auto *resources = new ResourceDefList;
  auto *connects = new ConnectList;
  auto *subunits = new SubUnitInstList;

  Identifier *type = VisitIdent(ctx->type);
  IdList *bases = VisitBaseList(ctx->base);
  ParamsList *params = VisitFuncUnitParams(ctx->func_unit_params());

  // Process each rule matched in template statements, save off info.
  for (auto *stmt : ctx->func_unit_template_stmt()) {
    if (auto *res_def = stmt->resource_def()) {
      ResourceDefList *res = VisitResourceDef(res_def);
      resources->insert(resources->end(), res->begin(), res->end());
    } else if (auto *port_def = stmt->port_def()) {
      IdList *defs = VisitPortDef(port_def, connects);
      ports->insert(ports->end(), defs->begin(), defs->end());
    } else if (auto *conn = stmt->connect_stmt()) {
      connects->push_back(VisitConnectStmt(conn));
    } else if (auto *subunit = stmt->subunit_instantiation()) {
      SubUnitInstList *items = VisitSubunitInstantiation(subunit, resources);
      subunits->insert(subunits->end(), items->begin(), items->end());
    }
  }

  MdlItem item(ctx, current_file_name());
  return new FuncUnitTemplate(item, type, bases, params, ports, resources,
                              connects, subunits);
}

//-----------------------------------------------------------------------------
// Process a functional unit group definition:
//     func_unit_group : FUNCGROUP ident ('<' number '>')? ':' name_list ';'
//-----------------------------------------------------------------------------
FuncUnitGroup *MdlVisitor::VisitFuncUnitGroup(FuncUnitGroupCtx *ctx) {
  Identifier *name = VisitIdent(ctx->name);
  IdList *members = VisitNameList(ctx->members);
  int buffer_size = ctx->buffered ? ctx->buffered->value : -1;

  MdlItem item(ctx, current_file_name());
  return new FuncUnitGroup(item, name, buffer_size, members);
}

//-----------------------------------------------------------------------------
// Process func_unit_params rules: (arguments to a functional unit template).
//      func_unit_params : fu_decl_item (';' fu_decl_item)*
// Return a vector of parameters.
//-----------------------------------------------------------------------------
ParamsList *MdlVisitor::VisitFuncUnitParams(FuncUnitParamsCtx *ctx) {
  auto *params = new ParamsList;
  if (ctx == nullptr)
    return params; // Return if zero parameters.

  for (auto *decl : ctx->fu_decl_item()) {
    ParamsList *param = VisitFuDeclItem(decl);
    params->insert(params->end(), param->begin(), param->end());
  }
  return params; // return the list of parameters.
}

//-----------------------------------------------------------------------------
// Process fu_decl_item rules:
//      func_decl_item : RESOURCE name_list | CLASS name_list
// Each namelist can define a list of parameters. We want to flatten those
// lists to a single list of resources and classes, and return a single list
// of class and resource definitions.
//-----------------------------------------------------------------------------
ParamsList *MdlVisitor::VisitFuDeclItem(FuDeclItemCtx *ctx) {
  auto *params = new ParamsList;
  ParamType type = ctx->RESOURCE() ? kParamResource : kParamClass;
  IdList *names = VisitNameList(ctx->name_list());

  for (auto *name : *names) {
    MdlItem item(*name);
    params->push_back(new Params(item, name, type));
  }

  return params;
}

//-----------------------------------------------------------------------------
// Process the port_def rules: (part of a functional unit template definition).
//      PORT port_decl (',' port_decl )*
// Return a list of port definitions.
//-----------------------------------------------------------------------------
IdList *MdlVisitor::VisitPortDef(PortDefCtx *ctx, ConnectList *connects) {
  auto *names = new IdList;

  // Append the port definition names
  for (auto *item : ctx->port_decl())
    names->push_back(VisitPortDecl(item, connects));

  return names;
}

//-----------------------------------------------------------------------------
// Process a single port definition.  The definition may optionally include
// a register class and a list of resource references.
//     ident ('<' reg_class=ident '>')? ('(' ref=resource_ref ')')?
// If a declaration contains connection information, create CONNECT records.
//-----------------------------------------------------------------------------
Identifier *MdlVisitor::VisitPortDecl(PortDeclCtx *ctx, ConnectList *connects) {
  auto *name = VisitIdent(ctx->name);

  if (ctx->reg_class != nullptr || ctx->ref != nullptr) {
    auto *reg_class = VisitIdent(ctx->reg_class);
    auto *ref = VisitResourceRef(ctx->ref);
    MdlItem item(ctx, current_file_name());
    connects->push_back(new Connect(item, name, reg_class, ref));
  }
  return name;
}

//-----------------------------------------------------------------------------
// Process connect_stmt rules: (part of a functional unit template definition).
//      CONNECT ident ('to' ident)? ('via' resource_ref)? ';' ;
// Return a Connect object that contains all the information.
//-----------------------------------------------------------------------------
Connect *MdlVisitor::VisitConnectStmt(ConnectStmtCtx *ctx) {
  auto *port = VisitIdent(ctx->port);
  auto *reg_class = VisitIdent(ctx->reg_class);
  auto *ref = VisitResourceRef(ctx->resource_ref());

  MdlItem item(ctx, current_file_name());
  return new Connect(item, port, reg_class, ref);
}

//-----------------------------------------------------------------------------
// Process subunit_instantiation rules: (also part of func unit definitions).
//     subunit_instantiation:
//                       (predicate=name_list ':')? subunit_statement
//                     | (predicate=name_list ':' '{' subunit_statement* '}' ';'
// Return a SubUnitInstance object that contains the information.
//-----------------------------------------------------------------------------
SubUnitInstList *
MdlVisitor::VisitSubunitInstantiation(SubunitInstantiationCtx *ctx,
                                      ResourceDefList *resources) {
  IdList *predicate = VisitNameList(ctx->predicate);
  auto *statements = new SubUnitInstList;

  for (auto *stmt : ctx->subunit_statement()) {
    auto subunits = VisitSubunitStatement(stmt, predicate, resources);
    statements->insert(statements->end(), subunits->begin(), subunits->end());
  }

  return statements;
}

//-----------------------------------------------------------------------------
// Process subunit_statement rules:
//     subunit_statement: SUBUNIT subunit_instance (',' subunit_instance)* ';'
//     subunit_instance:  ident '(' resource_refs ')'
//-----------------------------------------------------------------------------
SubUnitInstList *MdlVisitor::VisitSubunitStatement(SubunitStatementCtx *ctx,
                                                   IdList *predicate,
                                                   ResourceDefList *resources) {
  auto *subunits = new SubUnitInstList;

  for (auto *instance : ctx->subunit_instance()) {
    auto *name = VisitIdent(instance->ident());
    auto *args = VisitResourceRefs(instance->resource_refs(), resources);

    MdlItem item(ctx, current_file_name());
    subunits->push_back(new SubUnitInstance(item, name, args, predicate));
  }
  return subunits;
}

//-----------------------------------------------------------------------------
// Process subunit_template rules:
//     subunit_template: SUBUNIT ident (':' ident)? '(' su_decl_items ')'
//                 (('{' subunit_body* '}') | ('{{' latency_items? '}}') );
// Return a SubUnitTemplate object that contains all the information.
//-----------------------------------------------------------------------------
SubUnitTemplate *MdlVisitor::VisitSubunitTemplate(SubunitTemplateCtx *ctx) {
  Identifier *name = VisitIdent(ctx->name);
  ParamsList *params = VisitSuDeclItems(ctx->su_decl_items());

  IdList *bases = nullptr;
  StringList *regex = nullptr;
  VisitSuBaseList(ctx->base, bases, regex);

  auto *latencies = new LatencyInstList;

  LatencyTemplate *inline_lat = nullptr;
  MdlItem item(ctx, current_file_name());

  // If the body of the subunit is a list of subunit statements, return a list
  // of those instantiations.
  if (ctx->body) {
    for (auto *stmt : ctx->subunit_body()) {
      if (auto *latency = stmt->latency_instance()) {
        auto *lats = (VisitLatencyInstance(latency));
        latencies->insert(latencies->end(), lats->begin(), lats->end());
        continue;
      }
    }
  }

  // If the body of the subunit is an inlined latency template, create a
  // new latency template (with the same name as the subunit), and create a
  // latency instance which refers to that latency template. The created
  // latency template will be returned as part of the subunit.
  if (!ctx->latency_items().empty()) {
    // Create a new latency template.
    Identifier *tname = VisitIdent(ctx->name);
    ParamsList *tparams = VisitSuDeclItems(ctx->su_decl_items());
    auto items = ctx->latency_items();
    ReferenceList *refs = VisitLatencyItems(items);
    inline_lat = new LatencyTemplate(item, tname, nullptr, tparams, refs);

    // Create an instance for the new latency template for this subunit.
    Identifier *name = VisitIdent(ctx->name);
    ResourceRefList *args = new ResourceRefList;
    for (auto *param : *params)
      args->push_back(new ResourceRef(item, param->id()));
    latencies->push_back(new LatencyInstance(item, name, args, nullptr));
  }

  return new SubUnitTemplate(item, name, bases, regex, params, latencies,
                             inline_lat);
}

//-----------------------------------------------------------------------------
// Process su_decl_items rules: (part of a subunit template definition).
//      su_decl_items: su_decl_item (';' subunit_decl_item)* ;
// Return a list of subunit parameters.
//-----------------------------------------------------------------------------
ParamsList *MdlVisitor::VisitSuDeclItems(SuDeclItemsCtx *ctx) {
  auto *params = new ParamsList;

  if (ctx == nullptr)
    return params; // Return if no parameters.

  // Append the lists of resources or ports together.
  for (auto *item : ctx->su_decl_item()) {
    ParamsList *param = VisitSuDeclItem(item);
    params->insert(params->end(), param->begin(), param->end());
  }
  return params; // return the list of declared items.
}

//-----------------------------------------------------------------------------
// Process su_decl_item rules: (part of a subunit template definition).
//      su_decl_item: RESOURCE name_list | PORT name_list ;
// Return a list of resource or port parameter definitions.
//-----------------------------------------------------------------------------
ParamsList *MdlVisitor::VisitSuDeclItem(SuDeclItemCtx *ctx) {
  auto *params = new ParamsList;
  ParamType type = ctx->RESOURCE() ? kParamResource : kParamPort;
  IdList *names = VisitNameList(ctx->name_list());

  for (auto *name : *names) {
    MdlItem item(*name);
    params->push_back(new Params(item, name, type));
  }

  return params; // return the list of resources or ports.
}

//-----------------------------------------------------------------------------
// Process latency_instance rules: (part of a subunit template definition).
//     latency_instance: (predicate=name_list ':')? latency_statement
//                     | (predicate=name_list ':' '{' latency_statement* '}' ';'
// Return a LatencyInstance object that contains all the information.
//-----------------------------------------------------------------------------
LatencyInstList *MdlVisitor::VisitLatencyInstance(LatencyInstanceCtx *ctx) {
  if (ctx == nullptr)
    return nullptr;

  IdList *predicates = VisitNameList(ctx->predicate);
  auto *statements = new LatencyInstList;

  for (auto *stmt : ctx->latency_statement())
    statements->push_back(VisitLatencyStatement(stmt, predicates));

  return statements;
}

//-----------------------------------------------------------------------------
// Process latency_statement rules:
//     latency_instance: LATENCY ident '(' name_list ')' ';' ;
// Return a LatencyInstance object that contains all the information.
//-----------------------------------------------------------------------------
LatencyInstance *MdlVisitor::VisitLatencyStatement(LatencyStatementCtx *ctx,
                                                   IdList *predicates) {
  auto *name = VisitIdent(ctx->ident());
  auto *args = VisitResourceRefs(ctx->resource_refs());

  MdlItem item(ctx, current_file_name());
  return new LatencyInstance(item, name, args, predicates);
}

//-----------------------------------------------------------------------------
// Process latency_template rules.
//     latency_template: LATENCY ident (':' ident)* '(' su_decl_items ')'
//                             '{' latency_items? '}' ;
// Return a LatencyTemplate object that contains all the information.
//-----------------------------------------------------------------------------
LatencyTemplate *MdlVisitor::VisitLatencyTemplate(LatencyTemplateCtx *ctx) {
  Identifier *name = VisitIdent(ctx->name);
  IdList *base = VisitBaseList(ctx->base);

  ParamsList *params = VisitSuDeclItems(ctx->su_decl_items());
  auto items = ctx->latency_items();
  ReferenceList *refs = VisitLatencyItems(items);

  MdlItem item(ctx, current_file_name());
  return new LatencyTemplate(item, name, base, params, refs);
}

//-----------------------------------------------------------------------------
// Process latency_items rules: (part of a latency template definition).
//      latency_items: (name_list ':')? (latency_item | ('{' latencyitem* '}')
// Return a list of Reference objects.
//-----------------------------------------------------------------------------
ReferenceList *
MdlVisitor::VisitLatencyItems(std::vector<LatencyItemsCtx *> &ctx) {
  auto *references = new ReferenceList;
  for (auto *lat_item : ctx) {
    IdList *predicates = VisitNameList(lat_item->predicate);
    auto item = lat_item->latency_item();
    auto *refs = VisitLatencyItem(item, predicates);
    references->insert(references->end(), refs->begin(), refs->end());
  }
  return references;
}

//-----------------------------------------------------------------------------
// Process latency_item rules: (part of a latency template definition).
//    latency_item : latency_ref
//                 | conditional_ref
//                 | fu_statement ;
// Return a single Reference object that describes a single latency.
//-----------------------------------------------------------------------------
ReferenceList *MdlVisitor::VisitLatencyItem(std::vector<LatencyItemCtx *> &ctx,
                                            IdList *predicates) {
  ReferenceList *refs = new ReferenceList;
  for (auto *lat : ctx) {
    if (lat->latency_ref())
      refs->push_back(VisitLatencyRef(lat->latency_ref(), predicates));
    if (lat->conditional_ref())
      refs->push_back(VisitConditionalRef(lat->conditional_ref(), predicates));
    if (lat->fus_statement())
      VisitFusStatement(refs, predicates, lat->fus_statement());
  }
  return refs;
}

//-----------------------------------------------------------------------------
// Process a conditional reference statement.
//   conditional_ref : 'if' ident '{' latency_item* '}'
//                                   (conditional_elseif | conditional_else)?
//   conditional_elseif : 'else' 'if' ident '{' latency_item* '}'
//                                   (conditional_elseif | conditional_else)?
//   conditional_else : 'else' '{' latency_item* '}'
//-----------------------------------------------------------------------------
Reference *MdlVisitor::VisitConditionalRef(ConditionalRefCtx *ctx,
                                           IdList *predicates) {
  auto *pred = VisitIdent(ctx->ident());
  auto lat = ctx->latency_item();
  auto *refs = VisitLatencyItem(lat, nullptr);

  auto *elseif = VisitConditionalElseIf(ctx->conditional_elseif());
  auto *ref_else = VisitConditionalElse(ctx->conditional_else());
  auto else_clause = elseif ? elseif : ref_else;

  MdlItem item(ctx, current_file_name());
  auto *ref_if = new ConditionalRef(item, pred, refs, else_clause);
  return new Reference(item, predicates, ref_if);
}

ConditionalRef *MdlVisitor::VisitConditionalElseIf(ConditionalElseIfCtx *ctx) {
  if (ctx == nullptr)
    return nullptr;
  auto *pred = VisitIdent(ctx->ident());
  auto lat = ctx->latency_item();
  auto *refs = VisitLatencyItem(lat, nullptr);
  ConditionalRef *elseif = nullptr;
  ConditionalRef *ref_else = nullptr;

  if (ctx->conditional_elseif())
    elseif = VisitConditionalElseIf(ctx->conditional_elseif());
  if (ctx->conditional_else())
    ref_else = VisitConditionalElse(ctx->conditional_else());
  auto else_clause = elseif ? elseif : ref_else;

  MdlItem item(ctx, current_file_name());
  return new ConditionalRef(item, pred, refs, else_clause);
}

ConditionalRef *MdlVisitor::VisitConditionalElse(ConditionalElseCtx *ctx) {
  if (ctx == nullptr)
    return nullptr;
  auto lat = ctx->latency_item();
  auto *refs = VisitLatencyItem(lat, nullptr);

  MdlItem item(ctx, current_file_name());
  return new ConditionalRef(item, nullptr, refs, nullptr);
}

//-----------------------------------------------------------------------------
// Process latency_ref rules: (part of a latency template definition).
//    latency_ref: ref_type '(' latency_spec ')' ';'
//     latency_spec: phase_expr (':' cycles=number)? ',' resource_refs
//                 | phase_expr ('[' repeat=number (',' delay=number)? ']')?
//                              ',' operand
//                 | phase_expr ',' operand ',' resource_refs ;
// Return a single Reference object that describes a single latency.
//-----------------------------------------------------------------------------
Reference *MdlVisitor::VisitLatencyRef(LatencyRefCtx *ctx, IdList *predicates) {
  auto ref_type = StringToRefType(ctx->ref_type()->getText());
  auto *spec = ctx->latency_spec();

  auto *phase = VisitExpr(spec->expr());
  auto *opnd = VisitOperand(spec->operand());
  auto *refs = VisitLatencyResourceRefs(spec->latency_resource_refs());
  int cycles = 1;
  if (spec->cycles)
    cycles = spec->cycles->value;
  int repeat = 1;
  if (spec->repeat)
    repeat = spec->repeat->value;
  int delay = 1;
  if (spec->delay)
    delay = spec->delay->value;

  MdlItem item(ctx, current_file_name());
  return new Reference(item, predicates, ref_type, phase, repeat, delay, cycles,
                       opnd, refs);
}

//-----------------------------------------------------------------------------
// Process expr rules.  These are part of latency specs, and are a limited
// set of operations for calculating pipeline latencies:
//     expr: '-' expr  | expr ('*'|'/') expr | expr ('+'|'-') expr |
//           '(' expr ')' | number | operand ;
// Return a single expression tree root.
//-----------------------------------------------------------------------------
PhaseExpr *MdlVisitor::VisitExpr(ExprCtx *ctx) {
  // If expression is missing (for fus entries) just return nullptr;
  if (ctx == nullptr)
    return nullptr;

  MdlItem item(ctx, current_file_name());

  // Handle unary negation.
  if (ctx->negate)
    return new PhaseExpr(item, kNeg, VisitExpr(ctx->negate), nullptr);

  // Handle multiply and divide.
  if (ctx->mop) {
    PhaseOp op = ctx->mop->getText()[0] == '*' ? kMult : kDiv;
    return new PhaseExpr(item, op, VisitExpr(ctx->left), VisitExpr(ctx->right));
  }

  // Handle addition and subtraction.
  if (ctx->aop) {
    PhaseOp op = ctx->aop->getText()[0] == '+' ? kPlus : kMinus;
    return new PhaseExpr(item, op, VisitExpr(ctx->left), VisitExpr(ctx->right));
  }

  // Handle a phase name.
  if (ctx->phase_name)
    return new PhaseExpr(item, kPhase, VisitIdent(ctx->phase_name));

  // Handle a "truncate to positive" operator {...}.
  if (ctx->posexpr) {
    return new PhaseExpr(item, kPositive, VisitExpr(ctx->posexpr), nullptr);
  }

  // Handle a parenthesised subexpression.
  if (ctx->subexpr)
    return VisitExpr(ctx->subexpr);

  // Handle a literal constant.
  if (ctx->num)
    return new PhaseExpr(item, kInt, ctx->num->value);

  // Handle an instruction operand reference.
  OperandRef *opnd = VisitOperand(ctx->opnd);
  return new PhaseExpr(item, kOpnd, opnd);
}

//-----------------------------------------------------------------------------
// Process operand rules.  Operands are part of latency expressions, and
// refer to operands in the target instructions.
//      operand: (ident ':')? '$' ident ('.' operand_ref)*
//             | (ident ':')? '$' number
//             | (ident ':')? '$$' number
// This syntax corresponds closely to operands in llvm td files.
// A "$number" operand refers directly to an operand by index.
// A "$$number" operand refers to a variadic operand, by index (1,2,3...).
// Return an OperandRef, which references an operand of an instruction.
//-----------------------------------------------------------------------------
OperandRef *MdlVisitor::VisitOperand(OperandCtx *ctx) {
  if (ctx == nullptr)
    return nullptr;
  auto *operand = new IdList;
  Identifier *type = VisitIdent(ctx->type);
  Identifier *opnd = ctx->opnd ? VisitIdent(ctx->opnd) : nullptr;

  // Handle the normal case of an operand name.
  if (opnd)
    operand->push_back(opnd);
  // Handle an operand index reference ($<number>).
  if (ctx->opnd_id) {
    MdlItem item(ctx->opnd_id, current_file_name());
    operand->push_back(new Identifier(item, ctx->opnd_id->getText()));
  }
  // Handle a variadic operand index reference ($$<number>).
  if (ctx->var_opnd_id) {
    MdlItem item(ctx->var_opnd_id, current_file_name());
    operand->push_back(
        new Identifier(item, formatv("${0}", ctx->var_opnd_id->value)));
  }

  // If there are operand qualifiers (suboperands), add them to the list.
  for (auto *ref_ctx : ctx->operand_ref()) {
    MdlItem item(ref_ctx, current_file_name());
    operand->push_back(new Identifier(item, ref_ctx->getText()));
  }

  MdlItem item(ctx, current_file_name());
  return new OperandRef(item, type, operand);
}

//-----------------------------------------------------------------------------
// Process a functional unit usage statement.
//    fu_statement: FUS '(' fus_item ('&' fus_item)* ')' ',' snumber
//                                  (',' fus_attribute)* ')' ';'
//    fus_item: name=ident ('<' (expr ':')? number '>')?
//-----------------------------------------------------------------------------
void MdlVisitor::VisitFusStatement(
         ReferenceList *refs, IdList *predicates, FusStatementCtx *ctx) {
  MdlItem item(ctx, current_file_name());
  int micro_ops = ctx->micro_ops->value;

  // If we've seen functional unit attributes, collect them.
  RefFlags::Item flags = RefFlags::kNone;
  for (auto flag : ctx->fus_attribute()) {
    if (flag->getText() == "BeginGroup")
      flags |= RefFlags::kBeginGroup;
    else if (flag->getText() == "EndGroup")
      flags |= RefFlags::kEndGroup;
    else if (flag->getText() == "SingleIssue")
      flags |= RefFlags::kSingleIssue;
    else if (flag->getText() == "RetireOOO")
      flags |= RefFlags::kRetireOOO;
  }

  // If there weren't any functional units specified, generate a single
  // reference.
  if (ctx->fus_item().empty()) {
    MdlItem ref(ctx, current_file_name());
    refs->push_back(new Reference(item, predicates, micro_ops, flags));
    return;
  }

  for (auto *funit : ctx->fus_item()) {
    MdlItem ref(funit, current_file_name());
    auto *fu = VisitIdent(funit->ident());
    auto *expr = VisitExpr(funit->expr());
    int cycles = funit->number() ? funit->number()->value : 1;
    refs->push_back(new Reference(item, predicates, RefTypes::kFus, expr,
                                  cycles, micro_ops, flags,
                                  new ResourceRef(ref, fu)));
    micro_ops = 0; // Only include micro-ops on the first unit
  }

  // Note that we've seen an Fus reference.  If none seen, we can avoid looking
  // for them later.
  spec().set_explicit_fu_refs();
}

//-----------------------------------------------------------------------------
// Process pipe_def rules.  This is a top-level description of the
// processor's pipeline phases:
//    pipe_def: protection? PIPE_PHASE ident '{' pipe_phases '}' ';' ;
// Return a PipePhases object which contains the names of pipeline
// phases of a CPU, and the protected attribute of the pipeline.
//-----------------------------------------------------------------------------
PipePhases *MdlVisitor::VisitPipeDef(PipeDefCtx *ctx) {
  // Determine if this is a protected or unprotected pipeine. If you don't
  // specify it, we assume its protected.
  bool is_protected = true;
  bool is_hard = false;
  if (auto *protect = ctx->protection()) {
    if (protect->UNPROTECTED())
      is_protected = false;
    if (protect->HARD())
      is_hard = true;
  }
  Identifier *name = VisitIdent(ctx->ident());
  PhaseName *exe_phase = nullptr;
  auto *phases =
      VisitPipePhases(ctx->pipe_phases(), is_protected, is_hard, exe_phase);

  MdlItem item(ctx, current_file_name());
  return new PipePhases(item, name, phases, exe_phase, is_protected, is_hard);
}

//-----------------------------------------------------------------------------
// Process pipe_phases rules.  These correspond to a set of pipeline phases.
//     pipe_phases: phase_id (',' phase_id)* ;
// Return a list of pipeline phase names.
//-----------------------------------------------------------------------------
PhaseNameList *MdlVisitor::VisitPipePhases(PipePhasesCtx *ctx,
                                           bool is_protected, bool is_hard,
                                           PhaseName *&exe_phase) {
  auto *phases = new PhaseNameList;
  bool is_first;

  // Since each phase_id can return a list, append the lists together.
  for (auto *phase_ctx : ctx->phase_id()) {
    auto *phaseset = VisitPhaseId(phase_ctx, is_protected, is_hard, is_first);
    if (is_first && exe_phase == nullptr)
      exe_phase = phaseset->front();
    phases->insert(phases->end(), phaseset->begin(), phaseset->end());
  }

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
//     phase_id: ident ('[' range ']') ('=' number)? ;
// Return a list of (possibly one) pineline phase names.
//-----------------------------------------------------------------------------
PhaseNameList *MdlVisitor::VisitPhaseId(PhaseIdCtx *ctx, bool is_protected,
                                        bool is_hard, bool &is_first) {
  auto *phases = new PhaseNameList;
  Identifier *phase = VisitIdent(ctx->ident());
  MdlItem item(ctx, current_file_name());

  // If a range was not specified, just return the name we found.
  if (!ctx->range()) {
    phases->push_back(
        new PhaseName(item, phase->name(), is_protected, is_hard));
  } else {
    // If a range was specified, create a vector of names (range inclusive).
    int first = ctx->range()->first->value;
    int last = ctx->range()->last->value;
    if (first > last)
      first = last; // silly, but sanity check it.

    for (int id = first; id <= last; id++)
      phases->push_back(new PhaseName(
          item, formatv("{0}{1}", phase->name(), id), is_protected, is_hard));
  }

  // If a number was specified, set the first index in the list.
  if (ctx->number())
    phases->front()->set_index(ctx->number()->value);

  is_first = (ctx->first_exe != nullptr);
  return phases;
}

//-----------------------------------------------------------------------------
// Process resource_def rules, return a list of defs.
//     resource_def : RESOURCE resource_decl (',' resource_decl)* ';'
// An alternate form allows you to specify a range of phase ids associated
// with a resource:
//     resource_def : RESOURCE '(' phase ('..' phase)? ')' resource_decl...
// Return a list of resource definitions.
//-----------------------------------------------------------------------------
ResourceDefList *MdlVisitor::VisitResourceDef(ResourceDefCtx *ctx) {
  auto *defs = new ResourceDefList;
  Identifier *start = VisitIdent(ctx->start);
  Identifier *end = VisitIdent(ctx->end);

  for (auto *def_ctx : ctx->resource_decl())
    defs->push_back(VisitResourceDecl(def_ctx, start, end));

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
ResourceDef *MdlVisitor::VisitResourceDecl(ResourceDeclCtx *ctx,
                                           Identifier *start, Identifier *end) {
  Identifier *name = VisitIdent(ctx->ident());
  MdlItem item(ctx, current_file_name());

  int bits = ctx->bits ? ctx->bits->value : -1;
  int count = ctx->count ? ctx->count->value : -1;

  if (ctx->name_list()) {
    IdList *members = VisitNameList(ctx->name_list());
    return new ResourceDef(item, name, bits, members, start, end);
  }

  if (ctx->group_list()) {
    IdList *members = VisitGroupList(ctx->group_list());
    auto *group = new ResourceDef(item, name, bits, members, start, end);
    group->set_group_type(ctx->group_list()->group_and != nullptr
                              ? GroupType::kUseAll
                              : GroupType::kUseSingle);
    return group;
  }

  return new ResourceDef(item, name, bits, count, start, end);
}

//-----------------------------------------------------------------------------
// Process latency resource_refs rules.
//     latency_resource_refs: latency_resource_ref (',' latency_resource_ref)*
// Return a list of resource references.
//-----------------------------------------------------------------------------
ResourceRefList *
MdlVisitor::VisitLatencyResourceRefs(LatencyResourceRefsCtx *ctx) {
  auto *refs = new ResourceRefList;
  if (ctx == nullptr)
    return refs;

  for (auto *ref_ctx : ctx->latency_resource_ref())
    refs->push_back(VisitLatencyResourceRef(ref_ctx));

  return refs;
}

//-----------------------------------------------------------------------------
// Process resource_ref rules.  Handle a single resource reference.
//   resource_ref: resource_ref ':' count=number (':' value=ident)?
//               | resource_ref ':' countname=ident (':' value=ident)?
//               | resource_ref (':' ':' value=ident)?
//               | resource_ref ':' all='*'
//               | resource_ref
// Return a single resource reference object.
//-----------------------------------------------------------------------------
ResourceRef *MdlVisitor::VisitLatencyResourceRef(LatencyResourceRefCtx *ctx) {
  ResourceRef *res = VisitResourceRef(ctx->resource_ref());
  Identifier *countname = VisitIdent(ctx->countname);
  Identifier *value_mask = VisitIdent(ctx->value);
  int count = ctx->count ? ctx->count->value : -1;
  MdlItem item(ctx, current_file_name());

  if (value_mask)
    res->set_value_name(value_mask);
  if (ctx->countname)
    res->set_pool_count_name(countname);
  if (ctx->count)
    res->set_pool_count(count);
  if (ctx->all)
    res->set_use_all_members();
  return res;
}

//-----------------------------------------------------------------------------
// Process resource_refs rules.  Return a list of resource references.
//     resource_refs: resource_ref (',' resource_ref)*
// Return a list of resource references.
//-----------------------------------------------------------------------------
ResourceRefList *
MdlVisitor::VisitResourceRefs(ResourceRefsCtx *ctx,
                              ResourceDefList *resources /* = nullptr */) {
  auto *refs = new ResourceRefList;

  if (ctx == nullptr)
    return refs;

  for (auto *ref_ctx : ctx->resource_ref())
    refs->push_back(VisitResourceRef(ref_ctx, resources));

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
MdlVisitor::VisitResourceRef(ResourceRefCtx *ctx,
                             ResourceDefList *resources /* = nullptr */) {
  if (ctx == nullptr)
    return nullptr;
  Identifier *name = VisitIdent(ctx->name);
  MdlItem item(ctx, current_file_name());

  if (ctx->member) {
    Identifier *id = VisitIdent(ctx->member);
    return new ResourceRef(item, name, id);
  }
  if (ctx->range())
    return new ResourceRef(item, name, ctx->range()->first->value,
                           ctx->range()->last->value);
  if (ctx->index)
    return new ResourceRef(item, name, ctx->index->value, ctx->index->value);

  // Handle implicitly defined resource groups.
  if (ctx->group_and || ctx->group_or) {
    // We only allow implicitly defined groups in some circumstances.
    if (resources == nullptr) {
      spec().ErrorLog(&item, "Implicit group definition not allowed");
      return new ResourceRef(item, name);
    }

    IdList *members = new IdList;
    int index = 0;
    for (auto *member : ctx->ident()) {
      auto *newmem = VisitIdent(member);
      newmem->set_index(index++);
      members->push_back(newmem);
    }

    // Create a resource group in the scope surrounding the reference.
    static int implicit_id = 0;
    name = new Identifier(item, formatv("<group_{0}>", implicit_id++));
    auto *def = new ResourceDef(item, name, -1, members, nullptr, nullptr);
    def->set_implicit_group();
    def->set_group_type(ctx->group_and != nullptr ? GroupType::kUseAll
                                                  : GroupType::kUseSingle);
    resources->push_back(def);
    return new ResourceRef(item, name);
  }

  return new ResourceRef(item, name);
}

//-----------------------------------------------------------------------------
// Process an issue_statement rule:
//     issue_statement : ISSUE ('(' phase ('..' phase)? ')'? name_list ';'
// Return a list of resource definitions.
//-----------------------------------------------------------------------------
ResourceDefList *MdlVisitor::VisitIssueStatement(IssueStatementCtx *ctx) {
  auto *defs = new ResourceDefList;
  Identifier *start = VisitIdent(ctx->start);
  Identifier *end = VisitIdent(ctx->end);
  IdList *slots = VisitNameList(ctx->name_list());

  MdlItem item(ctx, current_file_name());
  for (auto *id : *slots)
    defs->push_back(new ResourceDef(item, id, -1, -1, start, end));

  return defs;
}

//-----------------------------------------------------------------------------
// Process a name_list rule.
//       name_list: ident (',' ident)* ;
// Return a list of names.
//-----------------------------------------------------------------------------
IdList *MdlVisitor::VisitNameList(NameListCtx *ctx) {
  if (ctx == nullptr)
    return nullptr;

  auto *names = new IdList;
  for (auto *name_ctx : ctx->ident())
    names->push_back(VisitIdent(name_ctx));
  int index = 0;
  for (auto *name : *names)
    name->set_index(index++);
  return names;
}

//-----------------------------------------------------------------------------
// Process a name_list rule.
//       name_list: ident (',' ident)* ;
// Return a list of names.
//-----------------------------------------------------------------------------
IdList *MdlVisitor::VisitGroupList(GroupListCtx *ctx) {
  auto *names = new IdList;
  for (auto *name_ctx : ctx->ident())
    names->push_back(VisitIdent(name_ctx));
  int index = 0;
  for (auto *name : *names)
    name->set_index(index++);
  return names;
}

//-----------------------------------------------------------------------------
// Process a list of base templates.
//       base_list: (':' ident)* ;
//-----------------------------------------------------------------------------
IdList *MdlVisitor::VisitBaseList(BaseListCtx *ctx) {
  if (ctx == nullptr)
    return nullptr;

  auto *names = new IdList;
  for (auto *name_ctx : ctx->ident())
    names->push_back(VisitIdent(name_ctx));
  return names;
}

//-----------------------------------------------------------------------------
// Process a list of subunit bases, which can be subunit names or strings
// representing regular expressions of instruction names.
//       su_base_list: (':' (ident|STRING_LITERAL))* ;
//-----------------------------------------------------------------------------
void MdlVisitor::VisitSuBaseList(SuBaseListCtx *ctx, IdList *&bases,
                                 StringList *&regex) {
  if (ctx == nullptr)
    return;

  if (ctx->unit != nullptr) {
    bases = new IdList;
    for (auto *base_ctx : ctx->ident())
      bases->push_back(VisitIdent(base_ctx));
  }

  if (ctx->regex != nullptr) {
    regex = new StringList;
    for (auto *item : ctx->STRING_LITERAL()) {
      auto expr = item->getText();
      regex->push_back(expr.substr(1, expr.length() - 2));
    }
  }
}

//-----------------------------------------------------------------------------
// Process a register_def rule:
//      register_def : REGISTER register_decl (',' register_decl)* ';' ;
// A register declaration can declare a set of registers, each of which
// can be a set of registers. Expand them all to a single vector, and return it.
//-----------------------------------------------------------------------------
RegisterDefList *MdlVisitor::VisitRegisterDef(RegisterDefCtx *ctx) {
  auto *regs = new RegisterDefList;

  // Each register declaration can return a list, so append them together.
  for (auto *reg_ctx : ctx->register_decl()) {
    RegisterDefList *regset = VisitRegisterDecl(reg_ctx);
    regs->insert(regs->end(), regset->begin(), regset->end());
  }
  return regs;
}

//-----------------------------------------------------------------------------
// Process a register_decl rule:
//     register_decl: ident | ident '[' range ']' ;
// If a range of registers is specified, expand to a list.
//-----------------------------------------------------------------------------
RegisterDefList *MdlVisitor::VisitRegisterDecl(RegisterDeclCtx *ctx) {
  auto *regs = new RegisterDefList;
  Identifier *reg = VisitIdent(ctx->ident());
  MdlItem item(ctx, current_file_name());

  // If no range was specified, just return a single register.
  if (ctx->range() == nullptr) {
    regs->push_back(new RegisterDef(item, reg));
    return regs; // return the single definition in a vector.
  }

  // If a range was specified, create a vector of register names.
  for (int id = ctx->range()->first->value; id <= ctx->range()->last->value;
       id++) {
    auto *def = new Identifier(item, reg->name() + std::to_string(id));
    regs->push_back(new RegisterDef(item, def));
  }

  return regs; // return the list of names.
}

//-----------------------------------------------------------------------------
// Process a register_class rule:
//      register_class : REGCLASS { register_decl (',' register_decl)* '}' ';' ;
// A register class declaration creates a collection of register definitions.
//-----------------------------------------------------------------------------
RegisterClass *MdlVisitor::VisitRegisterClass(RegisterClassCtx *ctx) {
  auto *regs = new RegisterDefList;
  Identifier *name = VisitIdent(ctx->ident());

  // Each register declaration can return a list, so append them together.
  for (auto *reg_ctx : ctx->register_decl()) {
    RegisterDefList *regset = VisitRegisterDecl(reg_ctx);
    regs->insert(regs->end(), regset->begin(), regset->end());
  }

  MdlItem item(ctx, current_file_name());
  return new RegisterClass(item, name, regs);
}

//-----------------------------------------------------------------------------
// Process an ident rule:
//     ident : IDENT ;
// Every identifier creates an Identifier object, with context.
//-----------------------------------------------------------------------------
Identifier *MdlVisitor::VisitIdent(IdentCtx *ctx) {
  if (ctx == nullptr)
    return nullptr;

  MdlItem item(ctx, current_file_name());
  return new Identifier(item, ctx->getText());
}

//-----------------------------------------------------------------------------
// Process an instruction definition rule:
// instruction_def : INSTRUCT name=ident
//                       '(' operand_decl (',' operand_decl)* ');
//                       '{' (SUBUNIT '(' subunit=ident ')' ';')?
//                           (DERIVED '(' derived=ident ')' ';')? '}'
//-----------------------------------------------------------------------------
const int kOpndNameRequired = -1;

InstructionDef *MdlVisitor::VisitInstructionDef(InstructionDefCtx *ctx) {
  auto *operands = new OperandDeclList;
  bool ellipsis = false;

  for (auto *opnd_ctx : ctx->operand_decl()) {
    auto *operand = VisitOperandDecl(opnd_ctx, kOpndNameRequired);
    if (operand->is_ellipsis()) {
      if (opnd_ctx != ctx->operand_decl().back())
        spec().ErrorLog(operand, "Ellipsis must be last declared operand");
      else ellipsis = true;
    }
    else
      operands->push_back(operand);
  }

  auto *name = VisitIdent(ctx->name);
  auto *subunit = VisitNameList(ctx->subunit);
  auto *derived = VisitNameList(ctx->derived);

  MdlItem item(ctx, current_file_name());
  return new InstructionDef(item, name, operands, subunit, derived, ellipsis);
}

//-----------------------------------------------------------------------------
// Process an operand declaration rule (used in instructions and operands) :
//     operand_decl : type=ident (name=ident)?
//-----------------------------------------------------------------------------
OperandDecl *MdlVisitor::VisitOperandDecl(OperandDeclCtx *ctx, int opnd_id) {
  auto *type = VisitIdent(ctx->type);
  auto *name = VisitIdent(ctx->name);
  bool is_ellipsis = ctx->ellipsis != nullptr;
  bool is_input = ctx->input != nullptr;
  bool is_output = ctx->output != nullptr;
  MdlItem item(ctx, current_file_name());

  // If an operand name is not provided, we sythesize a name based
  // on the component index.
  if (name == nullptr && !is_ellipsis && opnd_id != kOpndNameRequired)
    name = new Identifier(item, std::to_string(opnd_id));
  if (name == nullptr)
    name = new Identifier(item, "");

  // We only allow ellipses operands for instructions.
  if (is_ellipsis && opnd_id != kOpndNameRequired) {
    spec().ErrorLog(&item, "Ellipsis not allowed in operand definitions");
    name = type = new Identifier(item, "...");
  }
  return new OperandDecl(item, type, name, is_ellipsis, is_input, is_output);
}

//-----------------------------------------------------------------------------
// Process an operand definition rule:
// operand_def : OPERAND name=ident '(' (operand_decl (',' operand_decl)*)? ');
//                          '{' (operand_type | operand_attribute)* '}' ';'?
//-----------------------------------------------------------------------------
OperandDef *MdlVisitor::VisitOperandDef(OperandDefCtx *ctx) {
  auto *operands = new OperandDeclList;
  Identifier *type = nullptr;
  auto *attributes = new OperandAttributeList;
  auto *name = VisitIdent(ctx->name);
  MdlItem item(ctx, current_file_name());

  int opnd_id = 0;
  for (auto *opnd_ctx : ctx->operand_decl())
    operands->push_back(VisitOperandDecl(opnd_ctx, opnd_id++));

  if (!ctx->operand_type().empty()) {
    if (ctx->operand_type().size() != 1)
      spec().ErrorLog(&item, "Only one type specification allowed");
    else
      type = VisitIdent(ctx->operand_type()[0]->type);
  }

  for (auto *attribute_ctx : ctx->operand_attribute()) {
    auto *attr = VisitOperandAttribute(attribute_ctx);
    attributes->insert(attributes->end(), attr->begin(), attr->end());
  }

  return new OperandDef(item, name, operands, type, attributes, nullptr);
}

//-----------------------------------------------------------------------------
// Process a derived operand definition rule:
// derived_operand_def : OPERAND name=ident ':' base=ident
//                          '{' (operand_type | operand_attribute)* '}' ';'?
//-----------------------------------------------------------------------------
OperandDef *MdlVisitor::VisitDerivedOperandDef(DerivedOperandDefCtx *ctx) {
  auto *operands = new OperandDeclList; // always empty, by definition
  Identifier *type = nullptr;
  auto *attributes = new OperandAttributeList;
  auto *name = VisitIdent(ctx->name);
  auto *bases = VisitBaseList(ctx->base_list());
  MdlItem item(ctx, current_file_name());

  if (!ctx->operand_type().empty()) {
    if (ctx->operand_type().size() != 1)
      spec().ErrorLog(&item, "Only one type specification allowed");
    else
      type = VisitIdent(ctx->operand_type()[0]->type);
  }

  for (auto *attribute_ctx : ctx->operand_attribute()) {
    auto *attr = VisitOperandAttribute(attribute_ctx);
    attributes->insert(attributes->end(), attr->begin(), attr->end());
  }

  return new OperandDef(item, name, operands, type, attributes, bases);
}

//-----------------------------------------------------------------------------
// Process an operand attribute definition rule:
// operand_attribute :
//        (predicate=name_list ':')? operand_attribute_stmt
//      | predicate=name_list ':' '{' operand_attribute_stmt* '}' ';'?
//-----------------------------------------------------------------------------
OperandAttributeList *
MdlVisitor::VisitOperandAttribute(OperandAttributeCtx *ctx) {
  auto *predicate = VisitNameList(ctx->predicate);
  auto *attributes = new OperandAttributeList;

  for (auto *stmt : ctx->operand_attribute_stmt())
    attributes->push_back(VisitOperandAttributeStmt(stmt, predicate));

  return attributes;
}

//-----------------------------------------------------------------------------
// Process an operand attribute definition rule:
// operand_attribute_stmt :
//     ATTRIBUTE ident '=' snumber
//                            (IF type '[' pred_value (',' pred_value)* ']' ';'
//-----------------------------------------------------------------------------
OperandAttribute *
MdlVisitor::VisitOperandAttributeStmt(OperandAttributeStmtCtx *ctx,
                                      IdList *predicate) {
  auto *name = VisitIdent(ctx->name);
  std::vector<int> *values;
  MdlItem item(ctx, current_file_name());

  // Process either a single value, or a tuple of values.
  if (ctx->value) {
    values = new std::vector<int>;
    values->push_back(ctx->snumber()->value);
  } else {
    values = VisitTuple(ctx->values);
  }

  std::string type = ctx->type ? ctx->type->IDENT()->getText() : "";
  if (!type.empty() && type != "label" && type != "address" && type != "lit")
    spec().ErrorLog(&item, "Invalid predicate type: {0}", type);

  auto *pred_values = new PredValueList;

  for (auto *pred_value_ctx : ctx->pred_value())
    pred_values->push_back(VisitPredValue(pred_value_ctx));

  return new OperandAttribute(item, name, values, type, pred_values, predicate);
}

//-----------------------------------------------------------------------------
// Process a tuple:  tuple: '[' snumber (',' snumber)* ']'
//-----------------------------------------------------------------------------
std::vector<int> *MdlVisitor::VisitTuple(TupleCtx *ctx) {
  auto values = new std::vector<int>;
  for (auto *snumber : ctx->snumber())
    values->push_back(snumber->value);
  return values;
}

//-----------------------------------------------------------------------------
// Process a predicate value:
//     value=snumber | low=snumber '..' high=snumber | '{' mask=number '}'
//-----------------------------------------------------------------------------
PredValue *MdlVisitor::VisitPredValue(PredValueCtx *ctx) {
  MdlItem item(ctx, current_file_name());
  if (ctx->value)
    return new PredValue(item, ctx->value->value, ctx->value->value);

  if (ctx->mask)
    return new PredValue(item, ctx->mask->value);

  if (ctx->low->value <= ctx->high->value)
    return new PredValue(item, ctx->low->value, ctx->high->value);

  spec().ErrorLog(&item, "Invalid value range: {0}..{1}", ctx->low->value,
                  ctx->high->value);
  return new PredValue(item, 0, 0); // dummy value
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
//-----------------------------------------------------------------------------
void MdlVisitor::VisitPredicateDef(PredicateDefCtx *ctx, MdlSpec &spec) {
  auto name = ctx->ident()->getText();
  PredicateOpCtx *def = ctx->predicate_op();

  // Handle predicate definitions that don't have a predicate expression.
  // (Mostly this is handling "TruePred" and "FalsePred").
  if (def == nullptr) {
    MdlItem item(ctx, current_file_name());
    spec.EnterPredicate(name, new PredExpr(item, NameToOp(name)));
    return;
  }

  // If we see a predicate expression, add it to the table.
  spec.EnterPredicate(name, VisitPredicateOp(def));
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
PredExpr *MdlVisitor::VisitPredicateOp(PredicateOpCtx *ctx) {
  MdlItem item(ctx, current_file_name());

  // Handle the easy ones first: names and code escapes.
  if (ctx->ident()) {
    auto expr = NameToOp(ctx->ident()->getText());
    if (expr != PredOp::kEmpty)
      return new PredExpr(item, expr);
    return new PredExpr(item, PredOp::kName, ctx->ident()->getText());
  }

  if (ctx->code_escape())
    return new PredExpr(item, PredOp::kCode, ctx->code_escape()->getText());

  // If definition was empty, just return.
  if (ctx->pred_opcode() == nullptr)
    return nullptr;

  // Create an internal representation of a predicate expression.
  auto opcode = NameToOp(ctx->pred_opcode()->getText());
  std::vector<PredExpr *> opnds;
  for (auto *opnd : ctx->pred_opnd())
    opnds.push_back(VisitPredicateOpnd(opnd));

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
    if (!opnds.empty() && !opnds[0]->IsIntegerOrOperand()) {
      spec().ErrorLog(opnds[0], "Operand index expected");
      std::cout << formatv("Error: {0}\n", opnds[0]->ToString(0));
    }
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
    if (!opnds.empty() && !opnds[0]->IsIntegerOrOperand()) {
      spec().ErrorLog(opnds[0], "Operand index expected");
      std::cout << formatv("Error: {0}\n", opnds[0]->ToString(0));
    }
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
// Process a predicate operand:
//    pred_opnd : name=ident | snumber | STRING_LITERAL | predicate_op |
//                '[' opcode=ident (',' ident)* ']' | operand
//-----------------------------------------------------------------------------
PredExpr *MdlVisitor::VisitPredicateOpnd(PredicateOpndCtx *ctx) {
  MdlItem item(ctx, current_file_name());
  if (ctx->name)
    return new PredExpr(item, PredOp::kName, ctx->name->getText());
  if (ctx->operand())
    return new PredExpr(item, PredOp::kOperandRef,
                        VisitOperand(ctx->operand()));
  if (ctx->STRING_LITERAL())
    return new PredExpr(item, PredOp::kString,
                        ctx->STRING_LITERAL()->getText());

  if (ctx->snumber())
    return new PredExpr(item, PredOp::kNumber, ctx->snumber()->getText());

  if (ctx->predicate_op())
    return VisitPredicateOp(ctx->predicate_op());

  if (ctx->opcode_list) {
    std::vector<PredExpr *> opcodes;
    for (auto opcode : ctx->ident())
      opcodes.push_back(new PredExpr(item, PredOp::kName, opcode->getText()));
    return new PredExpr(item, PredOp::kCheckOpcode, opcodes);
  }

  return nullptr;
}

} // namespace mdl
} // namespace mpact

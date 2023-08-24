//===- mdl_visitor.h - Definitions for the ANTLR parse tree ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definitions for processing the MDL language Antlr parse tree.
//
//===----------------------------------------------------------------------===//
//
#ifndef MDL_COMPILER_MDL_VISITOR_H_
#define MDL_COMPILER_MDL_VISITOR_H_

#include <exception>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "antlr4-runtime.h"
#include "mdl.h"
#include "mdlLexer.h"
#include "mdlParser.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"

namespace mpact {
namespace mdl {

using mpact::mdl::generated::mdlLexer;
using mpact::mdl::generated::mdlParser;

//----------------------------------------------------------------------------
// Define a error listener class to capture syntax errors.
//----------------------------------------------------------------------------
class MdlErrorListener : public antlr4::BaseErrorListener {
public:
  explicit MdlErrorListener(const std::string &file_name)
      : syntax_error_count_(0), file_name_(file_name) {}

  void syntaxError(antlr4::Recognizer *recognizer,
                   antlr4::Token *offendingSymbol, size_t line,
                   size_t charPositionInLine, const std::string &msg,
                   std::exception_ptr e) override {
    llvm::errs() << std::string(llvm::formatv("{0}:{1}:{2}:{3}\n", file_name_,
                                              line, charPositionInLine, msg));
    syntax_error_count_++;
  }

  int syntax_error_count() const { return syntax_error_count_; }

  const std::string &file_name() const { return file_name_; }
  void set_file_name(std::string file_name) {
    file_name_ = std::move(file_name);
  }

private:
  int syntax_error_count_ = 0;
  std::string file_name_; // name of current input file
};

//----------------------------------------------------------------------------
// Define a visitor class using the antlr base visitor class.
// This is the top-level interface to the Antlr lexer/parser.
//----------------------------------------------------------------------------
class MdlVisitor {
public:
  explicit MdlVisitor(MdlSpec &spec, const std::string &import_path)
      : spec_(spec), import_path_(import_path) {}

  // Perform the lexing and parsing of the input stream. A representation of
  // the input is returned in spec.  Return false if syntax errors found.
  bool ProcessInputFile(std::string file_name);

  // Type aliases for antlr4 context types.
  using ArchitectureSpecCtx = mdlParser::Architecture_specContext;
  using FamilyCtx = mdlParser::Family_nameContext;
  using CpuDefCtx = mdlParser::Cpu_defContext;
  using ClusterInstantiationCtx = mdlParser::Cluster_instantiationContext;
  using FuncUnitInstantiationCtx = mdlParser::Func_unit_instantiationContext;
  using FuncUnitInstanceCtx = mdlParser::Func_unit_instanceContext;
  using FuncUnitBasesCtx = mdlParser::Func_unit_basesContext;
  using PinAllCtx = mdlParser::Pin_allContext;
  using PinAnyCtx = mdlParser::Pin_anyContext;
  using ForwardStmtCtx = mdlParser::Forward_stmtContext;
  using ForwardToUnitCtx = mdlParser::Forward_to_unitContext;
  using FuncUnitTemplateCtx = mdlParser::Func_unit_templateContext;
  using FuncUnitParamsCtx = mdlParser::Func_unit_paramsContext;
  using FuncUnitGroupCtx = mdlParser::Func_unit_groupContext;
  using FuDeclItemCtx = mdlParser::Fu_decl_itemContext;
  using PortDefCtx = mdlParser::Port_defContext;
  using PortDeclCtx = mdlParser::Port_declContext;
  using ConnectStmtCtx = mdlParser::Connect_stmtContext;
  using SubunitInstantiationCtx = mdlParser::Subunit_instantiationContext;
  using SubunitStatementCtx = mdlParser::Subunit_statementContext;
  using SubunitInstanceCtx = mdlParser::Subunit_instanceContext;

  using SubunitTemplateCtx = mdlParser::Subunit_templateContext;
  using SuDeclItemsCtx = mdlParser::Su_decl_itemsContext;
  using SuDeclItemCtx = mdlParser::Su_decl_itemContext;
  using SubunitBodyCtx = mdlParser::Subunit_bodyContext;
  using LatencyInstanceCtx = mdlParser::Latency_instanceContext;
  using LatencyStatementCtx = mdlParser::Latency_statementContext;

  using LatencyTemplateCtx = mdlParser::Latency_templateContext;
  using LatencyItemsCtx = mdlParser::Latency_itemsContext;
  using LatencyItemCtx = mdlParser::Latency_itemContext;
  using ConditionalRefCtx = mdlParser::Conditional_refContext;
  using ConditionalElseIfCtx = mdlParser::Conditional_elseifContext;
  using ConditionalElseCtx = mdlParser::Conditional_elseContext;
  using LatencyRefCtx = mdlParser::Latency_refContext;
  using LatencySpecCtx = mdlParser::Latency_specContext;
  using LatencyResourceRefsCtx = mdlParser::Latency_resource_refsContext;
  using LatencyResourceRefCtx = mdlParser::Latency_resource_refContext;
  using FusStatementCtx = mdlParser::Fus_statementContext;
  using FusItemCtx = mdlParser::Fus_itemContext;
  using FusAttributeCtx = mdlParser::Fus_attributeContext;
  using ExprCtx = mdlParser::ExprContext;
  using OperandCtx = mdlParser::OperandContext;
  using OperandRefCtx = mdlParser::Operand_refContext;
  using PipeDefCtx = mdlParser::Pipe_defContext;
  using PipePhasesCtx = mdlParser::Pipe_phasesContext;
  using PhaseIdCtx = mdlParser::Phase_idContext;

  using ResourceDefCtx = mdlParser::Resource_defContext;
  using ResourceDeclCtx = mdlParser::Resource_declContext;
  using ResourceRefsCtx = mdlParser::Resource_refsContext;
  using ResourceRefCtx = mdlParser::Resource_refContext;
  using IssueStatementCtx = mdlParser::Issue_statementContext;
  using NameListCtx = mdlParser::Name_listContext;
  using GroupListCtx = mdlParser::Group_listContext;
  using SuBaseListCtx = mdlParser::Su_base_listContext;
  using BaseListCtx = mdlParser::Base_listContext;
  using RegisterDefCtx = mdlParser::Register_defContext;
  using RegisterDeclCtx = mdlParser::Register_declContext;
  using RegisterClassCtx = mdlParser::Register_classContext;
  using IdentCtx = mdlParser::IdentContext;
  using InstructionDefCtx = mdlParser::Instruction_defContext;
  using OperandDeclCtx = mdlParser::Operand_declContext;
  using OperandDefCtx = mdlParser::Operand_defContext;
  using OperandTypeCtx = mdlParser::Operand_typeContext;
  using DerivedOperandDefCtx = mdlParser::Derived_operand_defContext;
  using PredicateDefCtx = mdlParser::Predicate_defContext;
  using PredicateOpCtx = mdlParser::Predicate_opContext;
  using PredicateOpndCtx = mdlParser::Pred_opndContext;
  using PredicateOpcodeCtx = mdlParser::Pred_opcodeContext;

  using OperandAttributeCtx = mdlParser::Operand_attributeContext;
  using OperandAttributeStmtCtx = mdlParser::Operand_attribute_stmtContext;
  using TupleCtx = mdlParser::TupleContext;
  using PredValueCtx = mdlParser::Pred_valueContext;
  using ImportFileCtx = mdlParser::Import_fileContext;

  // Visitor methods for all rules in the grammar.
  bool VisitArchitectureSpec(ArchitectureSpecCtx *ctx);
  Identifier *VisitFamilySpec(FamilyCtx *ctx);
  bool VisitImportFile(ImportFileCtx *ctx);
  CpuInstance *VisitCpuDef(CpuDefCtx *ctx);
  ClusterInstance *VisitClusterInstantiation(ClusterInstantiationCtx *ctx);
  FuncUnitInstance *VisitFuncUnitInstantiation(FuncUnitInstantiationCtx *ctx,
                                               ResourceDefList *resources);
  ForwardStmt *VisitForwardStmt(ForwardStmtCtx *ctx);
  IdList *VisitPinAll(PinAllCtx *ctx);
  IdList *VisitPinAny(PinAnyCtx *ctx);
  FuncUnitTemplate *VisitFuncUnitTemplate(FuncUnitTemplateCtx *ctx);
  FuncUnitGroup *VisitFuncUnitGroup(FuncUnitGroupCtx *ctx);
  ParamsList *VisitFuncUnitParams(FuncUnitParamsCtx *ctx);
  ParamsList *VisitFuDeclItem(FuDeclItemCtx *ctx);
  IdList *VisitPortDef(PortDefCtx *ctx, ConnectList *connects);
  Identifier *VisitPortDecl(PortDeclCtx *ctx, ConnectList *connects);
  Connect *VisitConnectStmt(ConnectStmtCtx *ctx);
  SubUnitInstList *VisitSubunitInstantiation(SubunitInstantiationCtx *ctx,
                                             ResourceDefList *resources);
  SubUnitInstList *VisitSubunitStatement(SubunitStatementCtx *ctx,
                                         IdList *predicate,
                                         ResourceDefList *resources);
  SubUnitTemplate *VisitSubunitTemplate(SubunitTemplateCtx *ctx);
  ParamsList *VisitSuDeclItems(SuDeclItemsCtx *ctx);
  ParamsList *VisitSuDeclItem(SuDeclItemCtx *ctx);
  LatencyInstList *VisitLatencyInstance(LatencyInstanceCtx *ctx);
  LatencyInstance *VisitLatencyStatement(LatencyStatementCtx *ctx,
                                         IdList *predicate);
  LatencyTemplate *VisitLatencyTemplate(LatencyTemplateCtx *ctx);
  ReferenceList *VisitLatencyItems(std::vector<LatencyItemsCtx *> &ctx);
  ReferenceList *VisitLatencyItem(std::vector<LatencyItemCtx *> &ctx,
                                  IdList *predicates);
  Reference *VisitConditionalRef(ConditionalRefCtx *ctx, IdList *predicates);
  ConditionalRef *VisitConditionalElseIf(ConditionalElseIfCtx *ctx);
  ConditionalRef *VisitConditionalElse(ConditionalElseCtx *ctx);

  Reference *VisitLatencyRef(LatencyRefCtx *ctx, IdList *predicates);
  ResourceRefList *VisitLatencyResourceRefs(LatencyResourceRefsCtx *ctx);
  ResourceRef *VisitLatencyResourceRef(LatencyResourceRefCtx *ctx);
  PhaseExpr *VisitExpr(ExprCtx *ctx);
  void VisitFusStatement(ReferenceList *refs, IdList *predicates,
                         FusStatementCtx *ctx);
  OperandRef *VisitOperand(OperandCtx *ctx);
  PipePhases *VisitPipeDef(PipeDefCtx *ctx);
  PhaseNameList *VisitPipePhases(PipePhasesCtx *ctx, bool is_protected,
                                 bool is_hard, PhaseName *&exe_phase);
  PhaseNameList *VisitPhaseId(PhaseIdCtx *ctx, bool is_protected, bool is_hard,
                              bool &is_first);
  ResourceDefList *VisitResourceDef(ResourceDefCtx *ctx);
  ResourceDef *VisitResourceDecl(ResourceDeclCtx *ctx, Identifier *start,
                                 Identifier *end);
  ResourceRefList *VisitResourceRefs(ResourceRefsCtx *ctx,
                                     ResourceDefList *resources = nullptr);
  ResourceRef *VisitResourceRef(ResourceRefCtx *ctx,
                                ResourceDefList *resources = nullptr);
  ResourceDefList *VisitIssueStatement(IssueStatementCtx *ctx);
  IdList *VisitNameList(NameListCtx *ctx);
  IdList *VisitGroupList(GroupListCtx *ctx);
  void VisitSuBaseList(SuBaseListCtx *ctx, IdList *&bases, StringList *&regex);
  IdList *VisitBaseList(BaseListCtx *ctx);
  RegisterDefList *VisitRegisterDef(RegisterDefCtx *ctx);
  RegisterDefList *VisitRegisterDecl(RegisterDeclCtx *ctx);
  RegisterClass *VisitRegisterClass(RegisterClassCtx *ctx);

  InstructionDef *VisitInstructionDef(InstructionDefCtx *ctx);

  OperandDecl *VisitOperandDecl(OperandDeclCtx *ctx, int opnd_id);
  OperandDef *VisitOperandDef(OperandDefCtx *ctx);
  OperandDef *VisitDerivedOperandDef(DerivedOperandDefCtx *ctx);
  OperandAttributeList *VisitOperandAttribute(OperandAttributeCtx *ctx);
  OperandAttribute *VisitOperandAttributeStmt(OperandAttributeStmtCtx *ctx,
                                              IdList *predicate);
  PredValue *VisitPredValue(PredValueCtx *ctx);
  void VisitPredicateDef(PredicateDefCtx *ctx, MdlSpec &spec);
  PredExpr *VisitPredicateOp(PredicateOpCtx *ctx);
  PredExpr *VisitPredicateOpnd(PredicateOpndCtx *ctx);

  std::vector<int> *VisitTuple(TupleCtx *ctx);
  Identifier *VisitIdent(IdentCtx *ctx);

  std::string &current_file_name() { return current_file_name_; }
  void set_current_file_name(const std::string &file_name) {
    current_file_name_ = file_name;
  }

  MdlSpec &spec() { return spec_; }

private:
  MdlSpec &spec_;
  std::unordered_set<std::string> imported_files_;
  std::string current_file_name_;
  std::string import_path_;
};

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_VISITOR_H_

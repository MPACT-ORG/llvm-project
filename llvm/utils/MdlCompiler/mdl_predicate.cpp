//===- mdl_predicate.cpp - Process Tablegen predicates --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that process reference predicates.
//
//===----------------------------------------------------------------------===//

#include <string>
#include <unordered_map>
#include <vector>

#include "mdl.h"
#include "mdl_output.h"

namespace mpact {
namespace mdl {

//---------------------------------------------------------------------------
// Attempt to evaluate a user-defined predicate over a specific instruction.
// We try to evaluate all of the predicate at MDL build time.  Anything that
// cannot be evaluated will be evaluated at compile time.  So the goal is to
// prune as much of the expression as possible, leaving things that we will
// need to generate compile-time predicate code for.
//---------------------------------------------------------------------------
using PredFunc = PredExpr *(MdlSpec::*)(PredExpr *pred,
                                        const InstructionDef *def);

// Table mapping predicate expression types to string names.
std::string PredExpr::PredName() {
  static auto *pred_name = new std::unordered_map<PredOp, std::string>(
      {{PredOp::kTrue, kTrue},
       {PredOp::kFalse, kFalse},
       {PredOp::kEmpty, kEmpty},
       {PredOp::kCheckAny, kCheckAny},
       {PredOp::kCheckAll, kCheckAll},
       {PredOp::kCheckNot, kCheckNot},
       {PredOp::kCheckOpcode, kCheckOpcode},
       {PredOp::kCheckIsRegOperand, kCheckIsRegOperand},
       {PredOp::kCheckIsImmOperand, kCheckIsImmOperand},
       {PredOp::kCheckZeroOperand, kCheckZeroOperand},
       {PredOp::kCheckFunctionPredicate, kCheckFunctionPredicate},
       {PredOp::kCheckFunctionPredicateWithTII, kCheckFunctionPredicateWithTII},
       {PredOp::kCheckNumOperands, kCheckNumOperands},
       {PredOp::kCheckRegOperand, kCheckRegOperand},
       {PredOp::kCheckInvalidRegOperand, kCheckInvalidRegOperand},
       {PredOp::kCheckImmOperand, kCheckImmOperand},
       {PredOp::kCheckSameRegOperand, kCheckSameRegOperand},
       {PredOp::kOpcodeSwitchStmt, kOpcodeSwitchStmt},
       {PredOp::kOpcodeSwitchCase, kOpcodeSwitchCase},
       {PredOp::kReturnStatement, kReturnStatement}});
  return (*pred_name)[opcode_];
}

PredExpr *MdlSpec::EvaluatePredicate(std::string name,
                                     const InstructionDef *instr) {
  if (predicate_table_.count(name) == 0)
    return nullptr;
  return EvaluatePredicate(predicate_table_[name], instr);
}

PredExpr *MdlSpec::EvaluatePredicate(PredExpr *pred,
                                     const InstructionDef *instr) {
  // Table of operation-to-evaluation functions.
  static auto *pred_ops = new std::unordered_map<PredOp, PredFunc>({
      {PredOp::kTrue, &MdlSpec::PredSimple},
      {PredOp::kFalse, &MdlSpec::PredSimple},
      {PredOp::kEmpty, &MdlSpec::PredSimple},
      {PredOp::kName, &MdlSpec::PredEvalName},
      {PredOp::kCheckAny, &MdlSpec::PredCheckAny},
      {PredOp::kCheckAll, &MdlSpec::PredCheckAll},
      {PredOp::kCheckNot, &MdlSpec::PredCheckNot},
      {PredOp::kCheckOpcode, &MdlSpec::PredCheckOpcode},
      {PredOp::kCheckIsRegOperand, &MdlSpec::PredCheckIsReg},
      {PredOp::kCheckRegOperand, &MdlSpec::PredCheckReg},
      {PredOp::kCheckInvalidRegOperand, &MdlSpec::PredCheckInvalidReg},
      {PredOp::kCheckSameRegOperand, &MdlSpec::PredCheckSameReg},
      {PredOp::kCheckNumOperands, &MdlSpec::PredCheckNumOperand},
      {PredOp::kCheckIsImmOperand, &MdlSpec::PredCheckIsImm},
      {PredOp::kCheckImmOperand, &MdlSpec::PredCheckImm},
      {PredOp::kCheckZeroOperand, &MdlSpec::PredCheckZero},
      {PredOp::kCheckFunctionPredicate, &MdlSpec::PredSimple},
      {PredOp::kCheckFunctionPredicateWithTII, &MdlSpec::PredSimple},
      {PredOp::kOpcodeSwitchStmt, &MdlSpec::PredOpcodeSwitchStmt},
      {PredOp::kReturnStatement, &MdlSpec::PredReturnStatement},
  });

  PredOp opcode = pred->opcode();
  if (pred_ops->count(opcode))
    return (this->*(*pred_ops)[opcode])(pred, instr);

  return pred;
}

// Look up a predicate by name, and return the associated predicate.
// If the predicate maps to a name, recur on that name.
PredExpr *MdlSpec::LookupPredicate(PredExpr *pred) {
  if (!IsValidInstructionPredicate(pred->value())) {
    ErrorLog(pred, "Undefined predicate: {0}", pred->value());
    return new PredExpr(PredOp::kFalse);
  }

  auto *item = predicate_table_[pred->value()];
  if (item->opcode() == PredOp::kName)
    return LookupPredicate(item);
  return item;
}

// Evaluate a named predicate.  Since CheckNots aren't propagated through
// named predicates, we need to handle negates explicitly.
PredExpr *MdlSpec::PredEvalName(PredExpr *pred, const InstructionDef *instr) {
  auto *item = LookupPredicate(pred);
  auto *result = EvaluatePredicate(item, instr);
  if (pred->negate())
    return PredSimplify(new PredExpr(*pred, PredOp::kCheckNot, result));

  return result;
}

// Logical OR operator on child predicates:
//   - immediately return True on a predicate that evaluates to True.
//   - discard any predicates that evaluate to False.
//   - if the predicate can't be completely evaluated, add to result set.
PredExpr *MdlSpec::PredCheckAny(PredExpr *pred, const InstructionDef *instr) {
  std::vector<PredExpr *> result;
  for (auto *or_op : pred->operands()) {
    auto *item = EvaluatePredicate(or_op, instr);
    if (item->IsTrue())
      return item;
    if (!item->IsFalse())
      result.push_back(item);
  }
  // If we didn't find True or partially evaluated predicates, return False.
  if (result.empty())
    return new PredExpr(PredOp::kFalse);
  // If we only found one partially evaluated predicate, just return it.
  if (result.size() == 1)
    return result[0];
  // If there is more than one predicate, return an OR of them.
  return new PredExpr(*pred, PredOp::kCheckAny, result);
}

// Logical AND operator on child predicates:
//   - immediately return False on a predicate that evaluates to False.
//   - discard any predicates that evaluate to True.
//   - if the predicate can't be completely evaluated, add to result set.
PredExpr *MdlSpec::PredCheckAll(PredExpr *pred, const InstructionDef *instr) {
  std::vector<PredExpr *> result;
  for (auto *and_op : pred->operands()) {
    auto *item = EvaluatePredicate(and_op, instr);
    if (item->IsFalse())
      return item;
    if (!item->IsTrue())
      result.push_back(item);
  }
  // If we didn't find True or partially evaluated predicates, return True.
  if (result.empty())
    return new PredExpr(PredOp::kTrue);
  // If we only found one partially evaluated predicate, just return it.
  if (result.size() == 1)
    return result[0];
  // If there is more than one predicate, return an AND of them.
  return new PredExpr(*pred, PredOp::kCheckAll, result);
}

// Logical NOT operator on the child predicate.
// kCheckNot operators are almost always simplified away, so when evaluating one
// we need to preserve it in the expression unless the child is simplified to
// true or false.
PredExpr *MdlSpec::PredCheckNot(PredExpr *pred, const InstructionDef *instr) {
  auto *item = EvaluatePredicate(pred->operands()[0], instr);
  if (item->IsFalse())
    return new PredExpr(PredOp::kTrue);
  if (item->IsTrue())
    return new PredExpr(PredOp::kFalse);
  return new PredExpr(*pred, PredOp::kCheckNot, item);
}

// Check for a particular opcode.  This always return either true or false.
PredExpr *MdlSpec::PredCheckOpcode(PredExpr *pred,
                                   const InstructionDef *instr) {
  std::vector<PredExpr *> result;
  for (auto *opcode : pred->operands()) {
    if (!opcode->IsName())
      ErrorLog(opcode, "Instruction name expected");
    else if (instruction_map_.count(opcode->value()) == 0)
      ErrorLog(opcode, "Invalid instruction name: {0}", opcode->value());
    if (opcode->value() == instr->name())
      return new PredExpr(PredOp::kTrue, pred->negate());
  }
  return new PredExpr(PredOp::kFalse, pred->negate());
}

// If a predicate operand is a predicate index, look up the operand and
// check it for validity.  Return -1 if its invalid.
// If the operand is an operand reference, look it up and return its index.
int MdlSpec::PredOperandIndex(const PredExpr *pred,
                              const InstructionDef *instr) {
  // Predicate operand indexes are flattened operand indexes.
  if (pred->opcode() == PredOp::kNumber) {
    int index = pred->int_value();
    int num_operands = instr->num_flat_operands();
    if (index < 0) {
      ErrorLog(pred, "Invalid operand index: {0}", index);
      return -1;
    }
    return (index < num_operands || instr->has_ellipsis()) ? index : -1;
  }

  if (pred->opcode() == PredOp::kOperandRef)
    return FindOperandName(instr, *pred->opnd()->op_names(), RefTypes::kNull);

  ErrorLog(pred, "Operand index expected");
  return -1;
}

// Check if a specified operand is a register operand.  We look for register
// class operands or a register name. If we reference a defined operand, we
// can always determine if it's a register or not. If it refers to a variadic
// operand, we have to generate a compile-time test.
PredExpr *MdlSpec::PredCheckIsReg(PredExpr *pred, const InstructionDef *instr) {
  int index = PredOperandIndex(pred->operands()[0], instr);
  if (index == -1)
    return new PredExpr(PredOp::kFalse, pred->negate());
  if (index >= instr->num_flat_operands()) {
    if (instr->has_ellipsis())
      return pred;
    return new PredExpr(PredOp::kFalse, pred->negate());
  }

  // If it's a valid operand, we can always determine whether or not it is a
  // register operand.
  auto *opnd = (*instr->flat_operands())[index];
  auto type = opnd->base_type()->name();
  if (!reg_class_map().count(type) && FindItem(registers(), type) == nullptr)
    return new PredExpr(PredOp::kFalse, pred->negate());
  return new PredExpr(PredOp::kTrue, pred->negate());
}

// Check if a specific register operand is an invalid register.  We usually
// need to generate a compile-time check for this, but can do some sanity
// checking at compiler build time.
PredExpr *MdlSpec::PredCheckInvalidReg(PredExpr *pred,
                                       const InstructionDef *instr) {
  int index = PredOperandIndex(pred->operands()[0], instr);
  if (index == -1)
    return new PredExpr(PredOp::kFalse, pred->negate());
  if (index >= instr->num_flat_operands()) {
    if (instr->has_ellipsis())
      return pred;
    return new PredExpr(PredOp::kFalse, pred->negate());
  }

  // If the operand type is a named register, then it can't be invalid.
  auto *opnd = (*instr->flat_operands())[index];
  auto type = opnd->base_type()->name();
  if (FindItem(registers(), type))
    return new PredExpr(PredOp::kFalse, pred->negate());

  return pred;
}

// Check if an operand is a specific register. There are several cases we
// can handle at compiler build time:
// - If the declared operand has a register name, we can match it against the
//   specified register name.
// - If the declared operand is a register class, we can check whether the
//   specified register name is NOT in that class.
// - If it's an invalid operand index, we can return kFalse.
// In all other cases, we need to generate a compile-time test.
PredExpr *MdlSpec::PredCheckReg(PredExpr *pred, const InstructionDef *instr) {
  int index = PredOperandIndex(pred->operands()[0], instr);
  if (index == -1)
    return new PredExpr(PredOp::kFalse, pred->negate());
  if (index >= instr->num_flat_operands()) {
    if (instr->has_ellipsis())
      return pred;
    return new PredExpr(PredOp::kFalse, pred->negate());
  }

  // If we have a custom function to call, we can't evaluate it.
  if (pred->operands().size() == 3)
    return pred;

  // Check that its actually a register name, treat invalid names as an error.
  auto reg_name = pred->operands()[1]->value();
  if (!FindItem(registers(), reg_name)) {
    ErrorLog(pred->operands()[1], "Invalid register name: {0}", reg_name);
    return new PredExpr(PredOp::kFalse, pred->negate());
  }

  auto *opnd = (*instr->flat_operands())[index];
  auto type = opnd->base_type()->name();

  // If the operand type is a register, see if it matches the specified name.
  if (FindItem(registers(), type)) {
    auto opcode = (type == reg_name) ? PredOp::kTrue : PredOp::kFalse;
    return new PredExpr(opcode, pred->negate());
  }

  // If the operand type is a register class, see if the specified name is NOT
  // in the class.
#ifdef HAVE_REGISTER_OVERLAP_INFORMATION
  // We currently can't do this if the target has overlapping classes.
  // This is specified in the td files, but we don't currently reflect this
  // information in the machine description.
  // TODO(tdb): Scrape overlapping register information from the td files,
  // and use that information here.
  if (reg_class_map().count(type) &&
      !FindItem(*reg_class_map()[type]->members(), reg_name))
    return new PredExpr(PredOp::kFalse, pred->negate());
#endif

  return pred;
}

// In general, we need to do a runtime test unless the indexes are invalid.
// We -could- check for cases involving literal register operands and/or
// non-intersecting register classes.
PredExpr *MdlSpec::PredCheckSameReg(PredExpr *pred,
                                    const InstructionDef *instr) {
  int index0 = PredOperandIndex(pred->operands()[0], instr);
  if (index0 == -1)
    return new PredExpr(PredOp::kFalse, pred->negate());
  if (index0 >= instr->num_flat_operands())
    return instr->has_ellipsis() ? pred
                                 : new PredExpr(PredOp::kFalse, pred->negate());
  int index1 = PredOperandIndex(pred->operands()[1], instr);
  if (index1 == -1)
    return new PredExpr(PredOp::kFalse, pred->negate());
  if (index1 >= instr->num_flat_operands())
    return instr->has_ellipsis() ? pred
                                 : new PredExpr(PredOp::kFalse, pred->negate());

  // Make sure they're both register operands.
  auto *opnd0 = (*instr->flat_operands())[index0];
  auto type0 = opnd0->base_type()->name();
  auto *opnd1 = (*instr->flat_operands())[index1];
  auto type1 = opnd1->base_type()->name();

  bool reg0 = FindItem(registers(), type0) || reg_class_map().count(type0);
  bool reg1 = FindItem(registers(), type1) || reg_class_map().count(type1);
  if (!reg0 || !reg1)
    return new PredExpr(PredOp::kFalse, pred->negate());

  return pred;
}

// Check that an instruction has a specified number of operands.
// If the instruction has variadic operands, we generally need to generate a
// compile time test.
PredExpr *MdlSpec::PredCheckNumOperand(PredExpr *pred,
                                       const InstructionDef *instr) {
  if (!pred->operands()[0]->IsInteger() ||
       pred->operands()[0]->int_value() < 0) {
    ErrorLog(pred, "Operand count expected");
    return pred;
  }
  int index = pred->operands()[0]->int_value();
  int num_operands = instr->num_flat_operands();
  bool has_ellipsis = instr->has_ellipsis();

  if (index < num_operands)
    return new PredExpr(PredOp::kFalse, pred->negate());
  if (has_ellipsis)
    return pred;
  auto opcode = (index != num_operands) ? PredOp::kFalse : PredOp::kTrue;
  return new PredExpr(opcode, pred->negate());
}

// Check that an operand has a specific immediate value. There are several
// things we can check:
// - If the operand is a register operand, we can return kFalse.
// - If it's an invalid operand index, we can return kFalse.
// Otherwise we generate a compile-time check.
PredExpr *MdlSpec::PredCheckIsImm(PredExpr *pred, const InstructionDef *instr) {
  int index = PredOperandIndex(pred->operands()[0], instr);
  if (index == -1)
    return new PredExpr(PredOp::kFalse, pred->negate());
  if (index >= instr->num_flat_operands())
    return instr->has_ellipsis() ? pred
                                 : new PredExpr(PredOp::kFalse, pred->negate());

  // Check for register operands?
  return pred;
}

// We generally need a compile-time check to look for specific immediate values,
// so for now we just check that it's a valid immedidate operand.
PredExpr *MdlSpec::PredCheckImm(PredExpr *pred, const InstructionDef *instr) {
  return PredCheckIsImm(pred, instr);
}

// Ditto for PredCheckIsImm.
PredExpr *MdlSpec::PredCheckZero(PredExpr *pred, const InstructionDef *instr) {
  return PredCheckIsImm(pred, instr);
}

// When we evaluate an OpcodeSwitchStmt against a single instruction we can
// trivally simplify the opcode-based switch statement to a single case and
// return statement.
PredExpr *MdlSpec::PredOpcodeSwitchStmt(PredExpr *pred,
                                        const InstructionDef *instr) {
  for (auto *opnd : pred->operands()) {
    // If we encounter a named predicate, find its associated predicate, which
    // needs to be either a switch case or a return statement.
    auto *cases = opnd;       // Don't overwrite the iterator.
    if (cases->opcode() == PredOp::kName)
      cases = LookupPredicate(cases);

    // We expect just SwitchCases and ReturnStatements. We handle these two
    // cases inline, since they have a particular semantic we need
    // to implement.
    if (cases->opcode() == PredOp::kOpcodeSwitchCase) {
      if (EvaluatePredicate(cases->operands()[0], instr)->IsTrue())
        return EvaluatePredicate(cases->operands()[1], instr);
      continue;
    }
    // A ReturnStatement is the switch Default.  Just evaluate and return its
    // underlying predicate.
    if (cases->opcode() == PredOp::kReturnStatement)
      return EvaluatePredicate(cases->operands()[0], instr);

    // If the predicate isn't a SwitchCase or return statement, we have a
    // poorly defined switch statement, so complain.
    ErrorLog(pred, "Malformed switch predicate");
  }
  return new PredExpr(PredOp::kFalse, pred->negate());
}

// Trivially return the first operand of a Return statement.
PredExpr *MdlSpec::PredReturnStatement(PredExpr *pred,
                                        const InstructionDef *instr) {
  return EvaluatePredicate(pred->operands()[0], instr);
}

// Write out a predicate expression for debug.
std::string PredExpr::ToString(int indent) {
  auto sep = "";
  std::string out = formatv("{0}{1}{2}", std::string(indent * 2 + 2, ' '),
                            negate() ? "!" : "", PredName());

  switch (opcode()) {
  case PredOp::kTrue:
  case PredOp::kFalse:
  case PredOp::kEmpty:
    return out;

  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kOpcodeSwitchCase:
  case PredOp::kReturnStatement:
  case PredOp::kCheckAny:
  case PredOp::kCheckAll:
  case PredOp::kCheckNot:
    out += "<";
    for (auto *opnd : operands())
      out += formatv("\n{0}", opnd->ToString(indent + 1));
    return out + ">";

  case PredOp::kCheckOpcode:
    out += " [";
    for (auto *opnd : operands()) {
      out += formatv("{0}{1}", sep, opnd->ToString(-1));
      sep = ", ";
    }
    out += "]";
    return out;

  case PredOp::kCheckIsRegOperand:
  case PredOp::kCheckIsImmOperand:
  case PredOp::kCheckZeroOperand:
  case PredOp::kCheckNumOperands:
  case PredOp::kCheckRegOperand:
  case PredOp::kCheckInvalidRegOperand:
  case PredOp::kCheckImmOperand:
  case PredOp::kCheckSameRegOperand:
  case PredOp::kCheckFunctionPredicate:
  case PredOp::kCheckFunctionPredicateWithTII:
    out += "<";
    for (auto *opnd : operands()) {
      out += formatv("{0}{1}", sep, opnd->ToString(-1));
      sep = ", ";
    }
    return out + ">";

  case PredOp::kOperandRef:
    return opnd()->ToString();
  case PredOp::kString:
  case PredOp::kNumber:
  case PredOp::kName:
  case PredOp::kCode:
    return out + value();
  }
  return "Error";
}

// Helper functions to find recusively defined predicates.
bool FindPredicateCycle(PredExpr *root, PredExpr *pred,
                        std::set<PredExpr *> &visited, MdlSpec *md) {
  visited.insert(pred);

  // For predicate types that have predicate operands, recur over all
  // predicate operands. For kName operands, explicitly check for recursion.
  switch (pred->opcode()) {
    case PredOp::kCheckAll:
    case PredOp::kCheckAny:
    case PredOp::kCheckNot:
    case PredOp::kReturnStatement:
    case PredOp::kOpcodeSwitchStmt:
    case PredOp::kOpcodeSwitchCase:
      for (auto *opnd : pred->operands())
        if (opnd->IsPred() &&
            FindPredicateCycle(root, opnd, visited, md))
          return true;
      return false;

    case PredOp::kName: {
        if (md->predicate_table().count(pred->value()) == 0)
          return false;
        auto *child = md->predicate_table()[pred->value()];
        if (child == root)
          return true;
        if (visited.count(child) == 0)
           return FindPredicateCycle(root, child, visited, md);
        return false;
      }
    default: return false;
  }
  return false;
}

bool FindPredicateCycles(std::string name, PredExpr *pred, MdlSpec *md) {
  std::set<PredExpr *> visited;
  if (FindPredicateCycle(pred, pred, visited, md)) {
    md->ErrorLog(pred, "Recursively defined predicate: {0}", name);
    return true;
  }
  return false;
}

// Simplify all predicates registered in the predicate table.
void MdlSpec::SimplifyPredicates() {
  for (auto [name, pred] : predicate_table_)
    if (!FindPredicateCycles(name, pred, this))
      predicate_table_[name] = PredSimplify(pred);
}

// Simplify predicates if possible. In particular we want to propagate
// negate operators (kCheckNot) down the expression.
PredExpr *MdlSpec::PredSimplify(PredExpr *expr) {
  auto &operands = expr->operands();

  switch (expr->opcode()) {
  // For Any/All case, if negated, reverse opcode and negate all operands.
  case PredOp::kCheckAny:
  case PredOp::kCheckAll:
    if (expr->negate()) {
      PredOp op = (expr->opcode() == PredOp::kCheckAll) ? PredOp::kCheckAny
                                                        : PredOp::kCheckAll;
      expr->set_opcode(op);
      for (auto *operand : expr->operands())
        operand->set_negate();
    }
    for (unsigned i = 0; i < operands.size(); i++)
      operands[i] = PredSimplify(operands[i]);
    expr->reset_negate();

    // If they only have one operand, just return the single operand.
    if (expr->operands().size() == 1)
      return expr->operands()[0];
    return expr;

  // Note that we don't simplify across kNames, since the expressions
  // associated with names are shared across all uses of the kName.  We could,
  // but we'd have to recursively clone the predicate expression for each
  // use, and its just not particuarly compelling.
  case PredOp::kName:
    return expr;

  // For NOT case, negate operand, and simplify it.
  case PredOp::kCheckNot:
    if (!expr->negate())
      operands[0]->set_negate();
    expr->reset_negate();
    return PredSimplify(operands[0]);

  case PredOp::kTrue:
    if (expr->negate())
      expr->set_opcode(PredOp::kFalse);
    expr->reset_negate();
    return expr;
  case PredOp::kFalse:
    if (expr->negate())
      expr->set_opcode(PredOp::kTrue);
    expr->reset_negate();
    return expr;

  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kOpcodeSwitchCase:
  case PredOp::kReturnStatement:
    for (unsigned i = 0; i < operands.size(); i++)
      operands[i] = PredSimplify(operands[i]);
    return expr;

  default:
    return expr;
  }
  return expr;
}

//-----------------------------------------------------------------------------
// Predicate function generation code.
//-----------------------------------------------------------------------------

// Top level interface for generating a function to evaluate a predicate.
std::string OutputState::FormatPredicateFunc(PredExpr *expr) {
  auto &operands = expr->operands();

  switch (expr->opcode()) {
  case PredOp::kCheckAny:
    return expr->CheckCompound(this);
  case PredOp::kCheckAll:
    return expr->CheckCompound(this);
  case PredOp::kCheckNot:
    return formatv("!({0})", FormatPredicateFunc(operands[0]));

  case PredOp::kCheckIsRegOperand:
  case PredOp::kCheckIsImmOperand:
    return expr->OperandType();

  case PredOp::kCheckInvalidRegOperand:
    return expr->InvalidRegOperand();
  case PredOp::kCheckRegOperand:
    return expr->RegOperand(spec().family_name());
  case PredOp::kCheckSameRegOperand:
    return expr->SameRegOperand();
  case PredOp::kCheckImmOperand:
    return expr->ImmOperand();
  case PredOp::kCheckZeroOperand:
    return expr->ImmZeroOperand();
  case PredOp::kCheckFunctionPredicate:
    return expr->FunctionPredicate(false, this);
  case PredOp::kCheckFunctionPredicateWithTII:
    return expr->FunctionPredicate(true, this);
  case PredOp::kCheckNumOperands:
    return expr->NumOperands();

  case PredOp::kCode:
    return expr->CheckCode(this);
  case PredOp::kName: {
    std::string out =
        FormatPredicateFunc(spec().predicate_table()[expr->value()]);
    if (expr->negate())
      return formatv("!({0})", out);
    return out;
  }

  // These should be all resolved, and don't need to be formatted.
  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kOpcodeSwitchCase:
  case PredOp::kCheckOpcode:
  case PredOp::kReturnStatement:
  case PredOp::kNumber:
  case PredOp::kString:
  default:
    return formatv("ERROR {0}: {1}", expr->Location(), expr->ToString(0));

  case PredOp::kTrue:
    return "true";
  case PredOp::kFalse:
    return "false";
  case PredOp::kEmpty:
    return "empty";
  }

  return "";
}

std::string PredExpr::GetOperand(PredExpr *index) const {
  return formatv("MI->getOperand({0})", index->value());
}

std::string PredExpr::OperandType() const {
  PredExpr *index = operands_[0];
  auto type = (opcode_ == PredOp::kCheckIsRegOperand) ? "isOpndRegister"
                                                      : "isOpndLiteral";
  auto op = negate() ? "!" : "";
  return formatv("{0}MI->{1}({2})", op, type, index->value());
}

std::string PredExpr::InvalidRegOperand() const {
  PredExpr *index = operands_[0];
  auto op = negate() ? "!=" : "==";
  return formatv("{0} {1} 0", GetOperand(index), op);
}

std::string PredExpr::RegOperand(const std::string &family) const {
  PredExpr *index = operands_[0];
  PredExpr *reg = operands_[1];
  auto func = (operands_.size() == 3) ? operands_[2]->value() : "";
  if (!func.empty())
    func = func.substr(1, func.length() - 2);

  auto getReg = GetOperand(index);
  if (!func.empty())
    getReg = formatv("{0}({1})", func, getReg);
  auto op = negate() ? "!=" : "==";
  auto val = formatv("{0}::{1}", family, reg->value());
  if (reg->value().empty())
    val = "0";
  return formatv("{0} {1} {2}", getReg, op, val);
}

std::string PredExpr::SameRegOperand() const {
  PredExpr *reg0 = operands_[0];
  PredExpr *reg1 = operands_[1];

  auto op = negate() ? "!=" : "==";
  return formatv("{0} {1} {2}", GetOperand(reg0), op, GetOperand(reg1));
}

std::string PredExpr::ImmOperand() const {
  PredExpr *index = operands_[0];
  PredExpr *value = operands_[1];
  auto func = (operands_.size() == 3) ? operands_[2]->value() : "";
  if (!func.empty())
    func = func.substr(1, func.length() - 2);

  auto getImm = GetOperand(index);
  if (!func.empty())
    getImm = formatv("{0}({1})", func, getImm);
  auto val = value->value();
  if (val[0] == '\"')
    val = val.substr(1, val.length() - 2);
  if (val.empty())
    return formatv("{0}{1}", negate() ? "!" : "", getImm);

  auto op = negate() ? "!=" : "==";
  return formatv("{0} {1} {2}", getImm, op, val);
}

std::string PredExpr::ImmZeroOperand() const {
  PredExpr *index = operands_[0];

  auto getImm = GetOperand(index);
  auto op = negate() ? "!=" : "==";
  return formatv("{0} {1} 0", getImm, op);
}

std::string PredExpr::NumOperands() const {
  auto op = negate() ? "!=" : "==";
  return formatv("MI->getMI()->getNumOperands() {0} {1}", op,
                 operands_[0]->value());
}

std::string PredExpr::CheckCompound(OutputState *spec) {
  std::string out;
  std::string sep = "";
  std::string op = (opcode() == PredOp::kCheckAll) ? " && " : " || ";

  for (auto *operand : operands()) {
    out += formatv("{0}{1}", sep, spec->FormatPredicateFunc(operand));
    sep = op;
  }
  return formatv("({0})", out);
}

//----------------------------------------------------------------------
// This is a huge kludge to cope with the "PredicateProlog" tablegen
// hack to communicate the target base class name to the predicate
// function.  Currently only three targets use this feature.
//----------------------------------------------------------------------
// Note an alternative approach would be to require a "using" clause
// in the <TARGET>Subtarget.cpp (that includes the generated file) that
// specifies the target object name, ie ARMBaseInstrInfo, etc.
//----------------------------------------------------------------------
// Another approach would be to parse the PredicateProlog record in
// TdScan and pass that information through the generated MDL file.
// Since its potentially arbitrary C++ code, that could be tricky.
//----------------------------------------------------------------------
static std::string InstrInfoName(const std::string &family) {
  if (family == "ARM")
    return "ARMBaseInstrInfo";
  if (family == "AArch64")
    return "AArch64InstrInfo";
  if (family == "AMDGPU")
    return "SIInstrInfo";
  return formatv("{0}InstrInfo", family);
}

// Given an input string and an offset, find the next identifier in the string
// and return it. The "loc" parameter points to the end of the identifier
static std::string FindId(std::string input, size_t &loc) {
  // Find next alphabetic character.
  char ch;
  std::string result;
  for (ch = input[loc]; ch && !(isalpha(ch) || ch == '_'); ch = input[++loc]) {
  }
  for (; ch && (isalnum(ch) || ch == '_'); ch = input[++loc])
    result.push_back(ch);
  return result;
}

static std::string ExpandVariables(std::string body, std::string family,
                                   bool &TII_seen) {
  // Fetch the target's InstrInfo name (from the PredicateProlog record).
  std::string tii =
      formatv("static_cast<const {0}*>(MI->getTII())", InstrInfoName(family));

  // Replace references to MI with Instr object references.  Replace TII
  // with the target's InstrInfo name.
  size_t loc = 0;
  for (auto id = FindId(body, loc); !id.empty(); id = FindId(body, loc)) {
    if (id == "MI") {
      body = body.insert(loc, "->getMI()");
      loc += 6;
    } else if (id == "TII") {
      body = body.replace(loc - 3, 3, tii);
      loc += tii.size() - 3;
      TII_seen = true;
    }
  }
  return body;
}

// Code predicates must work with MachineInstr AND MCInst objects. We need
// to replace references to (*MI) and (MI) with a reference to the object's
// machine instruction pointer.
std::string PredExpr::CheckCode(OutputState *spec) const {
  bool TII_seen = false;
  std::string input = value();
  std::string body = input.substr(2, input.length() - 4);

  body = ExpandVariables(body, spec->spec().family_name(), TII_seen);

  // Create the body of the virtual function, add it to the virtual function
  // table, and generate a call to that function via its index.
  std::string neg = negate() ? "!" : "";
  std::string out = formatv("{0}({1})", neg, body);
  if (!TII_seen)
    return "(MI->isMI() && " + out + ")";
  auto vfunc = formatv("  return {0};", out);
  auto index = OutputState::AddEntry(spec->virtual_ref_predicates(), vfunc);
  return formatv("(MI->isMI() && MI->evaluatePredicate({0}))", index);
}

std::string PredExpr::FunctionPredicate(bool withTII, OutputState *spec) const {
  // If withTII is specified, we need to pass target information to the
  // function.  For Machine instructions, this is a TII-> prefix.  For MCInst
  // versions, we pass an extra parameter.
  std::string tii;
  std::string mcii = withTII ? ", MI->getMCII())" : ")";

  if (withTII) {
    if (operands_.size() == 3 && operands_[2]->value() != "\"TII\"") {
      tii = operands_[2]->value();
      tii = tii.substr(1, tii.length() - 2); // Strip quotes
      tii += "->";
    } else {
      // Fetch the target's InstrInfo name (from the PredicateProlog record).
      tii = formatv("static_cast<const {0}*>(MI->getTII())->",
                    InstrInfoName(spec->spec().family_name()));
    }
  }

  std::string neg = negate() ? "!" : "";

  auto MCfunc = operands_[0]->value(); // MCInst function
  if (!MCfunc.empty())
    MCfunc = MCfunc.substr(1, MCfunc.length() - 2);
  if (!MCfunc.empty())
    MCfunc = MCfunc + "(*MI->getMC()" + mcii;

  auto MIfunc = operands_[1]->value(); // MachineInstr function
  if (!MIfunc.empty())
    MIfunc = MIfunc.substr(1, MIfunc.length() - 2);
  if (!MIfunc.empty())
    MIfunc = tii + MIfunc + "(*MI->getMI())";

  if (MIfunc.empty())
    return formatv("(MI->isMC() && {0}{1})", neg, MCfunc);

  // Create the body of the virtual function, add it to the virtual function
  // table, and generate a call to that function via its index.
  auto vfunc = formatv("  return {0};", MIfunc);
  auto index = OutputState::AddEntry(spec->virtual_ref_predicates(), vfunc);
  MIfunc = formatv("MI->evaluatePredicate({0})", index);

  if (MCfunc.empty())
    return "(MI->isMI() && " + MIfunc + ")";

  return neg + "(MI->isMC() ? " + MCfunc + " : " + MIfunc + ")";
}

} // namespace mdl
} // namespace mpact

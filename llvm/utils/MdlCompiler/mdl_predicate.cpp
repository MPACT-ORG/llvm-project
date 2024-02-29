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
using PredFunc = PredExpr *(MdlSpec::*)(PredExpr *Pred,
                                        const InstructionDef *Def);

// Table mapping predicate expression types to string names.
std::string PredExpr::getPredName() {
  static auto *PredName = new std::unordered_map<PredOp, std::string>(
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
  return (*PredName)[Opcode];
}

PredExpr *MdlSpec::evaluatePredicate(std::string Name,
                                     const InstructionDef *Instr) {
  if (PredicateTable.count(Name) == 0)
    return nullptr;
  return evaluatePredicate(PredicateTable[Name], Instr);
}

PredExpr *MdlSpec::evaluatePredicate(PredExpr *Pred,
                                     const InstructionDef *Instr) {
  // Table of operation-to-evaluation functions.
  static auto *PredOps = new std::unordered_map<PredOp, PredFunc>({
      {PredOp::kTrue, &MdlSpec::predSimple},
      {PredOp::kFalse, &MdlSpec::predSimple},
      {PredOp::kEmpty, &MdlSpec::predSimple},
      {PredOp::kName, &MdlSpec::predEvalName},
      {PredOp::kCheckAny, &MdlSpec::predCheckAny},
      {PredOp::kCheckAll, &MdlSpec::predCheckAll},
      {PredOp::kCheckNot, &MdlSpec::predCheckNot},
      {PredOp::kCheckOpcode, &MdlSpec::predCheckOpcode},
      {PredOp::kCheckIsRegOperand, &MdlSpec::predCheckIsReg},
      {PredOp::kCheckRegOperand, &MdlSpec::predCheckReg},
      {PredOp::kCheckInvalidRegOperand, &MdlSpec::predCheckInvalidReg},
      {PredOp::kCheckSameRegOperand, &MdlSpec::predCheckSameReg},
      {PredOp::kCheckNumOperands, &MdlSpec::predCheckNumOperand},
      {PredOp::kCheckIsImmOperand, &MdlSpec::predCheckIsImm},
      {PredOp::kCheckImmOperand, &MdlSpec::predCheckImm},
      {PredOp::kCheckZeroOperand, &MdlSpec::predCheckZero},
      {PredOp::kCheckFunctionPredicate, &MdlSpec::predSimple},
      {PredOp::kCheckFunctionPredicateWithTII, &MdlSpec::predSimple},
      {PredOp::kOpcodeSwitchStmt, &MdlSpec::predOpcodeSwitchStmt},
      {PredOp::kReturnStatement, &MdlSpec::predReturnStatement},
  });

  PredOp Opcode = Pred->getOpcode();
  if (PredOps->count(Opcode))
    return (this->*(*PredOps)[Opcode])(Pred, Instr);

  return Pred;
}

// Look up a predicate by name, and return the associated predicate.
// If the predicate maps to a name, recur on that name.
PredExpr *MdlSpec::lookupPredicate(PredExpr *Pred) {
  if (!isValidInstructionPredicate(Pred->getValue())) {
    ErrorLog(Pred, "Undefined predicate: {0}", Pred->getValue());
    return new PredExpr(PredOp::kFalse);
  }

  auto *Item = PredicateTable[Pred->getValue()];
  if (Item->getOpcode() == PredOp::kName)
    return lookupPredicate(Item);
  return Item;
}

// Evaluate a named predicate.  Since CheckNots aren't propagated through
// named predicates, we need to handle negates explicitly.
PredExpr *MdlSpec::predEvalName(PredExpr *Pred, const InstructionDef *Instr) {
  auto *Item = lookupPredicate(Pred);
  auto *Result = evaluatePredicate(Item, Instr);
  if (Pred->getNegate())
    return predSimplify(new PredExpr(*Pred, PredOp::kCheckNot, Result));

  return Result;
}

// Logical OR operator on child predicates:
//   - immediately return True on a predicate that evaluates to True.
//   - discard any predicates that evaluate to False.
//   - if the predicate can't be completely evaluated, add to result set.
PredExpr *MdlSpec::predCheckAny(PredExpr *Pred, const InstructionDef *Instr) {
  std::vector<PredExpr *> Result;
  for (auto *OrOp : Pred->getOperands()) {
    auto *Item = evaluatePredicate(OrOp, Instr);
    if (Item->isTrue())
      return Item;
    if (!Item->isFalse())
      Result.push_back(Item);
  }
  // If we didn't find True or partially evaluated predicates, return False.
  if (Result.empty())
    return new PredExpr(PredOp::kFalse);
  // If we only found one partially evaluated predicate, just return it.
  if (Result.size() == 1)
    return Result[0];
  // If there is more than one predicate, return an OR of them.
  return new PredExpr(*Pred, PredOp::kCheckAny, Result);
}

// Logical AND operator on child predicates:
//   - immediately return False on a predicate that evaluates to False.
//   - discard any predicates that evaluate to True.
//   - if the predicate can't be completely evaluated, add to result set.
PredExpr *MdlSpec::predCheckAll(PredExpr *Pred, const InstructionDef *Instr) {
  std::vector<PredExpr *> Result;
  for (auto *AndOp : Pred->getOperands()) {
    auto *Item = evaluatePredicate(AndOp, Instr);
    if (Item->isFalse())
      return Item;
    if (!Item->isTrue())
      Result.push_back(Item);
  }
  // If we didn't find True or partially evaluated predicates, return True.
  if (Result.empty())
    return new PredExpr(PredOp::kTrue);
  // If we only found one partially evaluated predicate, just return it.
  if (Result.size() == 1)
    return Result[0];
  // If there is more than one predicate, return an AND of them.
  return new PredExpr(*Pred, PredOp::kCheckAll, Result);
}

// Logical NOT operator on the child predicate.
// kCheckNot operators are almost always simplified away, so when evaluating one
// we need to preserve it in the expression unless the child is simplified to
// true or false.
PredExpr *MdlSpec::predCheckNot(PredExpr *Pred, const InstructionDef *Instr) {
  auto *Item = evaluatePredicate(Pred->getOperands()[0], Instr);
  if (Item->isFalse())
    return new PredExpr(PredOp::kTrue);
  if (Item->isTrue())
    return new PredExpr(PredOp::kFalse);
  return new PredExpr(*Pred, PredOp::kCheckNot, Item);
}

// Check for a particular opcode.  This always return either true or false.
PredExpr *MdlSpec::predCheckOpcode(PredExpr *Pred,
                                   const InstructionDef *Instr) {
  std::vector<PredExpr *> Result;
  for (auto *Opcode : Pred->getOperands()) {
    if (!Opcode->isName())
      ErrorLog(Opcode, "Instruction name expected");
    else if (InstructionMap.count(Opcode->getValue()) == 0)
      ErrorLog(Opcode, "Invalid instruction name: {0}", Opcode->getValue());
    if (Opcode->getValue() == Instr->getName())
      return new PredExpr(PredOp::kTrue, Pred->getNegate());
  }
  return new PredExpr(PredOp::kFalse, Pred->getNegate());
}

// If a predicate operand is a predicate index, look up the operand and
// check it for validity.  Return -1 if its invalid.
// If the operand is an operand reference, look it up and return its index.
int MdlSpec::predOperandIndex(const PredExpr *Pred,
                              const InstructionDef *Instr) {
  // Predicate operand indexes are flattened operand indexes.
  if (Pred->getOpcode() == PredOp::kNumber) {
    int Index = Pred->getIntValue();
    int NumOperands = Instr->getNumFlatOperands();
    if (Index < 0) {
      ErrorLog(Pred, "Invalid operand index: {0}", Index);
      return -1;
    }
    return (Index < NumOperands || Instr->hasEllipsis()) ? Index : -1;
  }

  if (Pred->getOpcode() == PredOp::kOperandRef)
    return findOperandName(Instr, *Pred->getOpnd()->getOpNames(),
                           RefTypes::kNull);

  ErrorLog(Pred, "Operand index expected");
  return -1;
}

// Check if a specified operand is a register operand.  We look for register
// class operands or a register name. If we reference a defined operand, we
// can always determine if it's a register or not. If it refers to a variadic
// operand, we have to generate a compile-time test.
PredExpr *MdlSpec::predCheckIsReg(PredExpr *Pred, const InstructionDef *Instr) {
  int Index = predOperandIndex(Pred->getOperands()[0], Instr);
  if (Index == -1)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  if (Index >= Instr->getNumFlatOperands()) {
    if (Instr->hasEllipsis())
      return Pred;
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  }

  // If it's a valid operand, we can always determine whether or not it is a
  // register operand.
  auto *Opnd = (*Instr->getFlatOperands())[Index];
  auto type = Opnd->getBaseType()->getName();
  if (!RegisterClassMap.count(type) && FindItem(Registers, type) == nullptr)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  return new PredExpr(PredOp::kTrue, Pred->getNegate());
}

// Check if a specific register operand is an invalid register.  We usually
// need to generate a compile-time check for this, but can do some sanity
// checking at compiler build time.
PredExpr *MdlSpec::predCheckInvalidReg(PredExpr *Pred,
                                       const InstructionDef *Instr) {
  int Index = predOperandIndex(Pred->getOperands()[0], Instr);
  if (Index == -1)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  if (Index >= Instr->getNumFlatOperands()) {
    if (Instr->hasEllipsis())
      return Pred;
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  }

  // If the operand type is a named register, then it can't be invalid.
  auto *Opnd = (*Instr->getFlatOperands())[Index];
  auto Type = Opnd->getBaseType()->getName();
  if (FindItem(Registers, Type))
    return new PredExpr(PredOp::kFalse, Pred->getNegate());

  return Pred;
}

// Check if an operand is a specific register. There are several cases we
// can handle at compiler build time:
// - If the declared operand has a register name, we can match it against the
//   specified register name.
// - If the declared operand is a register class, we can check whether the
//   specified register name is NOT in that class.
// - If it's an invalid operand index, we can return kFalse.
// In all other cases, we need to generate a compile-time test.
PredExpr *MdlSpec::predCheckReg(PredExpr *Pred, const InstructionDef *Instr) {
  int Index = predOperandIndex(Pred->getOperands()[0], Instr);
  if (Index == -1)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  if (Index >= Instr->getNumFlatOperands()) {
    if (Instr->hasEllipsis())
      return Pred;
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  }

  // If we have a custom function to call, we can't evaluate it.
  if (Pred->getOperands().size() == 3)
    return Pred;

  // Check that its actually a register name, treat invalid names as an error.
  auto RegName = Pred->getOperands()[1]->getValue();
  if (!FindItem(Registers, RegName)) {
    ErrorLog(Pred->getOperands()[1], "Invalid register name: {0}", RegName);
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  }

  auto *Opnd = (*Instr->getFlatOperands())[Index];
  auto Type = Opnd->getBaseType()->getName();

  // If the operand type is a register, see if it matches the specified name.
  if (FindItem(Registers, Type)) {
    auto Opcode = (Type == RegName) ? PredOp::kTrue : PredOp::kFalse;
    return new PredExpr(Opcode, Pred->getNegate());
  }

  // If the operand type is a register class, see if the specified name is NOT
  // in the class.
#ifdef HAVE_REGISTER_OVERLAP_INFORMATION
  // We currently can't do this if the target has overlapping classes.
  // This is specified in the td files, but we don't currently reflect this
  // information in the machine description.
  // TODO(tdb): Scrape overlapping register information from the td files,
  // and use that information here.
  if (RegisterClassMap().count(type) &&
      !FindItem(*RegisterClassMap()[type]->members(), reg_name))
    return new PredExpr(PredOp::kFalse, Pred->negate());
#endif

  return Pred;
}

// In general, we need to do a runtime test unless the indexes are invalid.
// We -could- check for cases involving literal register operands and/or
// non-intersecting register classes.
PredExpr *MdlSpec::predCheckSameReg(PredExpr *Pred,
                                    const InstructionDef *Instr) {
  int Index0 = predOperandIndex(Pred->getOperands()[0], Instr);
  if (Index0 == -1)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  if (Index0 >= Instr->getNumFlatOperands())
    return Instr->hasEllipsis() ? Pred
                      : new PredExpr(PredOp::kFalse, Pred->getNegate());
  int Index1 = predOperandIndex(Pred->getOperands()[1], Instr);
  if (Index1 == -1)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  if (Index1 >= Instr->getNumFlatOperands())
    return Instr->hasEllipsis() ? Pred
                           : new PredExpr(PredOp::kFalse, Pred->getNegate());

  // Make sure they're both register operands.
  auto *Opnd0 = (*Instr->getFlatOperands())[Index0];
  auto Type0 = Opnd0->getBaseType()->getName();
  auto *Opnd1 = (*Instr->getFlatOperands())[Index1];
  auto Type1 = Opnd1->getBaseType()->getName();

  bool Reg0 = FindItem(Registers, Type0) || RegisterClassMap.count(Type0);
  bool Reg1 = FindItem(Registers, Type1) || RegisterClassMap.count(Type1);
  if (!Reg0 || !Reg1)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  return Pred;
}

// Check that an instruction has a specified number of operands.
// If the instruction has variadic operands, we generally need to generate a
// compile time test.
PredExpr *MdlSpec::predCheckNumOperand(PredExpr *Pred,
                                       const InstructionDef *Instr) {
  if (!Pred->getOperands()[0]->isInteger() ||
       Pred->getOperands()[0]->getIntValue() < 0) {
    ErrorLog(Pred, "Operand count expected");
    return Pred;
  }
  int Index = Pred->getOperands()[0]->getIntValue();
  int NumOperands = Instr->getNumFlatOperands();

  if (Index < NumOperands)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  if (Instr->hasEllipsis())
    return Pred;
  auto Opcode = (Index != NumOperands) ? PredOp::kFalse : PredOp::kTrue;
  return new PredExpr(Opcode, Pred->getNegate());
}

// Check that an operand has a specific immediate value. There are several
// things we can check:
// - If the operand is a register operand, we can return kFalse.
// - If it's an invalid operand index, we can return kFalse.
// Otherwise we generate a compile-time check.
PredExpr *MdlSpec::predCheckIsImm(PredExpr *Pred, const InstructionDef *Instr) {
  int Index = predOperandIndex(Pred->getOperands()[0], Instr);
  if (Index == -1)
    return new PredExpr(PredOp::kFalse, Pred->getNegate());
  if (Index >= Instr->getNumFlatOperands())
    return Instr->hasEllipsis() ? Pred
                            : new PredExpr(PredOp::kFalse, Pred->getNegate());
  // Check for register operands?
  return Pred;
}

// We generally need a compile-time check to look for specific immediate values,
// so for now we just check that it's a valid immedidate operand.
PredExpr *MdlSpec::predCheckImm(PredExpr *Pred, const InstructionDef *Instr) {
  return predCheckIsImm(Pred, Instr);
}

// Ditto for PredCheckIsImm.
PredExpr *MdlSpec::predCheckZero(PredExpr *Pred, const InstructionDef *Instr) {
  return predCheckIsImm(Pred, Instr);
}

// When we evaluate an OpcodeSwitchStmt against a single instruction we can
// trivally simplify the opcode-based switch statement to a single case and
// return statement.
PredExpr *MdlSpec::predOpcodeSwitchStmt(PredExpr *Pred,
                                        const InstructionDef *Instr) {
  for (auto *Opnd : Pred->getOperands()) {
    // If we encounter a named predicate, find its associated predicate, which
    // needs to be either a switch case or a return statement.
    auto *Cases = Opnd;       // Don't overwrite the iterator.
    if (Cases->getOpcode() == PredOp::kName)
      Cases = lookupPredicate(Cases);

    // We expect just SwitchCases and ReturnStatements. We handle these two
    // cases inline, since they have a particular semantic we need
    // to implement.
    if (Cases->getOpcode() == PredOp::kOpcodeSwitchCase) {
      if (evaluatePredicate(Cases->getOperands()[0], Instr)->isTrue())
        return evaluatePredicate(Cases->getOperands()[1], Instr);
      continue;
    }
    // A ReturnStatement is the switch Default.  Just evaluate and return its
    // underlying predicate.
    if (Cases->getOpcode() == PredOp::kReturnStatement)
      return evaluatePredicate(Cases->getOperands()[0], Instr);

    // If the predicate isn't a SwitchCase or return statement, we have a
    // poorly defined switch statement, so complain.
    ErrorLog(Pred, "Malformed switch predicate");
  }
  return new PredExpr(PredOp::kFalse, Pred->getNegate());
}

// Trivially return the first operand of a Return statement.
PredExpr *MdlSpec::predReturnStatement(PredExpr *Pred,
                                        const InstructionDef *Instr) {
  return evaluatePredicate(Pred->getOperands()[0], Instr);
}

// Write out a predicate expression for debug.
std::string PredExpr::ToString(int Indent) {
  auto Sep = "";
  std::string Out = formatv("{0}{1}{2}", std::string(Indent * 2 + 2, ' '),
                            getNegate() ? "!" : "", getPredName());

  switch (getOpcode()) {
  case PredOp::kTrue:
  case PredOp::kFalse:
  case PredOp::kEmpty:
    return Out;

  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kOpcodeSwitchCase:
  case PredOp::kReturnStatement:
  case PredOp::kCheckAny:
  case PredOp::kCheckAll:
  case PredOp::kCheckNot:
    Out += "<";
    for (auto *Opnd : getOperands())
      Out += formatv("\n{0}", Opnd->ToString(Indent + 1));
    return Out + ">";

  case PredOp::kCheckOpcode:
    Out += " [";
    for (auto *Opnd : getOperands()) {
      Out += formatv("{0}{1}", Sep, Opnd->ToString(-1));
      Sep = ", ";
    }
    Out += "]";
    return Out;

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
    Out += "<";
    for (auto *Opnd : getOperands()) {
      Out += formatv("{0}{1}", Sep, Opnd->ToString(-1));
      Sep = ", ";
    }
    return Out + ">";

  case PredOp::kOperandRef:
    return getOpnd()->ToString();
  case PredOp::kString:
  case PredOp::kNumber:
  case PredOp::kName:
  case PredOp::kCode:
    return Out + getValue();
  }
  return "Error";
}

// Helper functions to find recusively defined predicates.
bool findPredicateCycle(PredExpr *Root, PredExpr *Pred,
                        std::set<PredExpr *> &Visited, MdlSpec *Spec) {
  Visited.insert(Pred);

  // For predicate types that have predicate operands, recur over all
  // predicate operands. For kName operands, explicitly check for recursion.
  switch (Pred->getOpcode()) {
    case PredOp::kCheckAll:
    case PredOp::kCheckAny:
    case PredOp::kCheckNot:
    case PredOp::kReturnStatement:
    case PredOp::kOpcodeSwitchStmt:
    case PredOp::kOpcodeSwitchCase:
      for (auto *Opnd : Pred->getOperands())
        if (Opnd->isPred() &&
            findPredicateCycle(Root, Opnd, Visited, Spec))
          return true;
      return false;

    case PredOp::kName: {
        if (Spec->getPredicateTable().count(Pred->getValue()) == 0)
          return false;
        auto *Child = Spec->getPredicateTable()[Pred->getValue()];
        if (Child == Root)
          return true;
        if (Visited.count(Child) == 0)
           return findPredicateCycle(Root, Child, Visited, Spec);
        return false;
      }
    default: return false;
  }
  return false;
}

bool findPredicateCycles(std::string Name, PredExpr *Pred, MdlSpec *Spec) {
  std::set<PredExpr *> Visited;
  if (findPredicateCycle(Pred, Pred, Visited, Spec)) {
    Spec->ErrorLog(Pred, "Recursively defined predicate: {0}", Name);
    return true;
  }
  return false;
}

// Simplify all predicates registered in the predicate table.
void MdlSpec::simplifyPredicates() {
  for (auto [Name, Pred] : PredicateTable)
    if (!findPredicateCycles(Name, Pred, this))
      PredicateTable[Name] = predSimplify(Pred);
}

// Simplify predicates if possible. In particular we want to propagate
// negate operators (kCheckNot) down the expression.
PredExpr *MdlSpec::predSimplify(PredExpr *Expr) {
  auto &Operands = Expr->getOperands();

  switch (Expr->getOpcode()) {
  // For Any/All case, if negated, reverse opcode and negate all operands.
  case PredOp::kCheckAny:
  case PredOp::kCheckAll:
    if (Expr->getNegate()) {
      PredOp Op = (Expr->getOpcode() == PredOp::kCheckAll) ? PredOp::kCheckAny
                                                        : PredOp::kCheckAll;
      Expr->setOpcode(Op);
      for (auto *Opnd : Expr->getOperands())
        Opnd->setNegate();
    }
    for (unsigned I = 0; I < Operands.size(); I++)
      Operands[I] = predSimplify(Operands[I]);
    Expr->resetNegate();

    // If they only have one operand, just return the single operand.
    if (Expr->getOperands().size() == 1)
      return Expr->getOperands()[0];
    return Expr;

  // Note that we don't simplify across kNames, since the expressions
  // associated with names are shared across all uses of the kName.  We could,
  // but we'd have to recursively clone the predicate expression for each
  // use, and its just not particuarly compelling.
  case PredOp::kName:
    return Expr;

  // For NOT case, negate operand, and simplify it.
  case PredOp::kCheckNot:
    if (!Expr->getNegate())
      Operands[0]->setNegate();
    Expr->resetNegate();
    return predSimplify(Operands[0]);

  case PredOp::kTrue:
    if (Expr->getNegate())
      Expr->setOpcode(PredOp::kFalse);
    Expr->resetNegate();
    return Expr;
  case PredOp::kFalse:
    if (Expr->getNegate())
      Expr->setOpcode(PredOp::kTrue);
    Expr->resetNegate();
    return Expr;

  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kOpcodeSwitchCase:
  case PredOp::kReturnStatement:
    for (unsigned I = 0; I < Operands.size(); I++)
      Operands[I] = predSimplify(Operands[I]);
    return Expr;

  default:
    return Expr;
  }
  return Expr;
}

//-----------------------------------------------------------------------------
// Predicate function generation code.
//-----------------------------------------------------------------------------

// Top level interface for generating a function to evaluate a predicate.
std::string OutputState::formatPredicateFunc(PredExpr *Expr) {
  auto &Operands = Expr->getOperands();

  switch (Expr->getOpcode()) {
  case PredOp::kCheckAny:
    return Expr->fmtCheckCompound(this);
  case PredOp::kCheckAll:
    return Expr->fmtCheckCompound(this);
  case PredOp::kCheckNot:
    return formatv("!({0})", formatPredicateFunc(Operands[0]));

  case PredOp::kCheckIsRegOperand:
  case PredOp::kCheckIsImmOperand:
    return Expr->fmtOperandType();

  case PredOp::kCheckInvalidRegOperand:
    return Expr->fmtInvalidRegOperand();
  case PredOp::kCheckRegOperand:
    return Expr->fmtRegOperand(getSpec().getFamilyName());
  case PredOp::kCheckSameRegOperand:
    return Expr->fmtSameRegOperand();
  case PredOp::kCheckImmOperand:
    return Expr->fmtImmOperand();
  case PredOp::kCheckZeroOperand:
    return Expr->fmtImmZeroOperand();
  case PredOp::kCheckFunctionPredicate:
    return Expr->fmtFunctionPredicate(false, this);
  case PredOp::kCheckFunctionPredicateWithTII:
    return Expr->fmtFunctionPredicate(true, this);
  case PredOp::kCheckNumOperands:
    return Expr->fmtNumOperands();

  case PredOp::kCode:
    return Expr->fmtCheckCode(this);
  case PredOp::kName: {
    std::string Out =
        formatPredicateFunc(getSpec().getPredicateTable()[Expr->getValue()]);
    if (Expr->getNegate())
      return formatv("!({0})", Out);
    return Out;
  }

  // These should be all resolved, and don't need to be formatted.
  case PredOp::kOpcodeSwitchStmt:
  case PredOp::kOpcodeSwitchCase:
  case PredOp::kCheckOpcode:
  case PredOp::kReturnStatement:
  case PredOp::kNumber:
  case PredOp::kString:
  default:
    return formatv("ERROR {0}: {1}", Expr->getLocation(), Expr->ToString(0));

  case PredOp::kTrue:
    return "true";
  case PredOp::kFalse:
    return "false";
  case PredOp::kEmpty:
    return "empty";
  }
  return "";
}

std::string PredExpr::fmtGetOperand(PredExpr *Index) const {
  return formatv("MI->getOperand({0})", Index->getValue());
}

std::string PredExpr::fmtOperandType() const {
  PredExpr *Index = Operands[0];
  auto Type = (Opcode == PredOp::kCheckIsRegOperand) ? "isOpndRegister"
                                                      : "isOpndLiteral";
  auto Op = getNegate() ? "!" : "";
  return formatv("{0}MI->{1}({2})", Op, Type, Index->getValue());
}

std::string PredExpr::fmtInvalidRegOperand() const {
  PredExpr *Index = Operands[0];
  auto Op = getNegate() ? "!=" : "==";
  return formatv("{0} {1} 0", fmtGetOperand(Index), Op);
}

std::string PredExpr::fmtRegOperand(const std::string &Family) const {
  PredExpr *Index = Operands[0];
  PredExpr *Reg = Operands[1];
  auto Func = (Operands.size() == 3) ? Operands[2]->getValue() : "";

  auto GetReg = fmtGetOperand(Index);
  if (!Func.empty())
    GetReg = formatv("{0}({1})", Func, GetReg);
  auto Op = getNegate() ? "!=" : "==";
  auto Val = formatv("{0}::{1}", Family, Reg->getValue());
  if (Reg->getValue().empty())
    Val = "0";
  return formatv("{0} {1} {2}", GetReg, Op, Val);
}

std::string PredExpr::fmtSameRegOperand() const {
  PredExpr *Reg0 = Operands[0];
  PredExpr *Reg1 = Operands[1];

  auto Op = getNegate() ? "!=" : "==";
  return formatv("{0} {1} {2}", fmtGetOperand(Reg0), Op, fmtGetOperand(Reg1));
}

std::string PredExpr::fmtImmOperand() const {
  PredExpr *Index = Operands[0];
  PredExpr *Value = Operands[1];
  auto Func = (Operands.size() == 3) ? Operands[2]->getValue() : "";

  auto GetImm = fmtGetOperand(Index);
  if (!Func.empty())
    GetImm = formatv("{0}({1})", Func, GetImm);
  auto Val = Value->getValue();
  if (Val.empty())
    return formatv("{0}{1}", getNegate() ? "!" : "", GetImm);

  auto Op = getNegate() ? "!=" : "==";
  return formatv("{0} {1} {2}", GetImm, Op, Val);
}

std::string PredExpr::fmtImmZeroOperand() const {
  PredExpr *Index = Operands[0];

  auto GetImm = fmtGetOperand(Index);
  auto Op = getNegate() ? "!=" : "==";
  return formatv("{0} {1} 0", GetImm, Op);
}

std::string PredExpr::fmtNumOperands() const {
  auto Op = getNegate() ? "!=" : "==";
  return formatv("MI->getMI()->getNumOperands() {0} {1}", Op,
                 Operands[0]->getValue());
}

std::string PredExpr::fmtCheckCompound(OutputState *Outspec) {
  std::string Out;
  std::string Sep = "";
  std::string Op = (getOpcode() == PredOp::kCheckAll) ? " && " : " || ";

  for (auto *operand : getOperands()) {
    Out += formatv("{0}{1}", Sep, Outspec->formatPredicateFunc(operand));
    Sep = Op;
  }
  return formatv("({0})", Out);
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
static std::string InstrInfoName(const std::string &Family) {
  if (Family == "ARM")
    return "ARMBaseInstrInfo";
  if (Family == "AArch64")
    return "AArch64InstrInfo";
  if (Family == "AMDGPU")
    return "SIInstrInfo";
  return formatv("{0}InstrInfo", Family);
}

// Given an input string and an offset, find the next identifier in the string
// and return it. The "loc" parameter points to the end of the identifier
static std::string FindId(std::string Input, size_t &Loc) {
  // Find next alphabetic character.
  char Ch;
  std::string Result;
  for (Ch = Input[Loc]; Ch && !(isalpha(Ch) || Ch == '_'); Ch = Input[++Loc]) {
  }
  for (; Ch && (isalnum(Ch) || Ch == '_'); Ch = Input[++Loc])
    Result.push_back(Ch);
  return Result;
}

static std::string expandVariables(std::string Body, std::string Family,
                                   bool &TII_seen) {
  // Fetch the target's InstrInfo name (from the PredicateProlog record).
  std::string TII =
      formatv("static_cast<const {0}*>(MI->getTII())", InstrInfoName(Family));

  // Replace references to MI with Instr object references.  Replace TII
  // with the target's InstrInfo name.
  size_t Loc = 0;
  for (auto Id = FindId(Body, Loc); !Id.empty(); Id = FindId(Body, Loc)) {
    if (Id == "MI") {
      Body = Body.insert(Loc, "->getMI()");
      Loc += 6;
    } else if (Id == "TII") {
      Body = Body.replace(Loc - 3, 3, TII);
      Loc += TII.size() - 3;
      TII_seen = true;
    }
  }
  return Body;
}

// Code predicates must work with MachineInstr AND MCInst objects. We need
// to replace references to (*MI) and (MI) with a reference to the object's
// machine instruction pointer.
std::string PredExpr::fmtCheckCode(OutputState *Outspec) const {
  bool TII_seen = false;
  std::string Input = getValue();
  std::string Body = Input.substr(2, Input.length() - 4);

  Body = expandVariables(Body, Outspec->getSpec().getFamilyName(), TII_seen);

  // Create the Body of the virtual function, add it to the virtual function
  // table, and generate a call to that function via its index.
  std::string Neg = getNegate() ? "!" : "";
  std::string Out = formatv("{0}({1})", Neg, Body);
  if (!TII_seen)
    return "(MI->isMI() && " + Out + ")";
  auto Vfunc = formatv("  return {0};", Out);
  auto Index = OutputState::AddEntry(Outspec->getVirtualRefPredicates(), Vfunc);
  return formatv("(MI->isMI() && MI->evaluatePredicate({0}))", Index);
}

std::string
PredExpr::fmtFunctionPredicate(bool WithTII, OutputState *Outspec) const {
  // If WithTII is specified, we need to pass target information to the
  // function.  For Machine instructions, this is a TII-> prefix.  For MCInst
  // versions, we pass an extra parameter.
  std::string TII;
  std::string MCII = WithTII ? ", MI->getMCII())" : ")";

  if (WithTII) {
    if (Operands.size() == 3 && Operands[2]->getValue() != "TII") {
      TII = Operands[2]->getValue();
      TII += "->";
    } else {
      // Fetch the target's InstrInfo name (from the PredicateProlog record).
      TII = formatv("static_cast<const {0}*>(MI->getTII())->",
                    InstrInfoName(Outspec->getSpec().getFamilyName()));
    }
  }

  std::string Neg = getNegate() ? "!" : "";

  auto MCfunc = Operands[0]->getValue(); // MCInst function
  if (!MCfunc.empty())
    MCfunc = MCfunc + "(*MI->getMC()" + MCII;

  auto MIfunc = Operands[1]->getValue();        // MachineInstr function
  if (!MIfunc.empty())
    MIfunc = TII + MIfunc + "(*MI->getMI())";

  if (MIfunc.empty())
    return formatv("(MI->isMC() && {0}{1})", Neg, MCfunc);

  // Create the body of the virtual function, add it to the virtual function
  // table, and generate a call to that function via its index.
  auto Vfunc = formatv("  return {0};", MIfunc);
  auto Index = OutputState::AddEntry(Outspec->getVirtualRefPredicates(), Vfunc);
  MIfunc = formatv("MI->evaluatePredicate({0})", Index);

  if (MCfunc.empty())
    return "(MI->isMI() && " + MIfunc + ")";

  return Neg + "(MI->isMC() ? " + MCfunc + " : " + MIfunc + ")";
}

} // namespace mdl
} // namespace mpact

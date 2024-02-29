//===- mdl_output.cpp - Write out the MDL database ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Produce the instruction information database for LLVM as a C++ file.
//
// The overall schema for the database looks like this:
//
//     <A table of CPU definitions>
//        <for each CPU, a table of instructions>
//           <for each instruction, a set of subunit entries>
//              <for each subunit, sets of...
//                 <operand latency records>
//                 <resource reference records>
//                 <pooled resource references>
//                 <register constraints>
//     > > > >
//
// A key aspect of the design is that there is -enormous- duplication of
// information across CPUs, Functional Units, and Instructions, and we
// want to share this information across the database as much as possible.
// The formatting functions automatically share output objects.
//
// The organization of the generated C++ output looks like this:
//
// <generated functions that implement non-trivial instruction predicates>
// <generated functions that implement non-trivial pipeline phase expressions>
// <a table of operand reference lists (operand latencies)>
// <a table of conditional operand reference lists>
// <tables of resource reference lists>
// <a pooled resource allocation table>
// <a pooled resource reference table>
// <operand constraint tables>
// <instruction tables for each CPU definition>
// <an instruction name table (not really necessary...)>
// <a table of CPU definitions>
//
// For each table of shared objects, we create a dictionary of the output
// representation of each entry in the table. Once an entry is in the table
// it is referred to by its unique identifier in the table.
//
// The majority of the code in this file simply handles the formatting of
// the object to generate C++ code.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Error.h"

#include "mdl.h"
#include "mdl_output.h"

namespace mpact {
namespace mdl {

// We generate a LOT of nullptrs in the output, and would prefer something
// that adds less clutter.  So we use __ rather than "nullptr".
constexpr auto kNull = "__";

static const char *Divider =
    "\n//-------------------------------------------------------------------\n";

// Functions for creating shared object names.
static std::string TableName(int Index, std::string Prefix) {
  if (Index == -1)
    return kNull;
  return Prefix + std::to_string(Index);
}
static std::string PredicateName(int Index) {
  return TableName(Index, "PRED_");
}
static std::string VirtualPredicateName(int Index) {
  return TableName(Index, "MI_PRED_");
}
static std::string OperandListName(int Index) {
  return TableName(Index, "OPND_");
}
static std::string CondReferenceName(int Index) {
  return TableName(Index, "COND_");
}
static std::string CondResourceReferenceName(int Index) {
  return TableName(Index, "CRES_");
}
static std::string CondPooledResourceReferenceName(int Index) {
  return TableName(Index, "CPOOL_");
}
static std::string CondConstraintName(int Index) {
  return TableName(Index, "CREG_");
}
static std::string UsedResourceListName(int Index) {
  return TableName(Index, "URES_");
}
static std::string HeldResourceListName(int Index) {
  return TableName(Index, "HRES_");
}
static std::string ReservedResourceListName(int Index) {
  return TableName(Index, "RRES_");
}
static std::string ResourceGroupName(int Index) {
  return TableName(Index, "GROUP_");
}
static std::string PoolDescriptorName(int Index) {
  return TableName(Index, "POOL_");
}
static std::string PooledResourceListName(int Index) {
  return TableName(Index, "PRES_");
}
static std::string PooledCountFuncName(int Index) {
  return TableName(Index, "COUNT_");
}
static std::string PoolValueFuncName(int Index) {
  return TableName(Index, "VALUE_");
}
static std::string ConstraintListName(int Index) {
  return TableName(Index, "REG_");
}
static std::string PhaseName(int Index) { return TableName(Index, "PIPE_"); }
static std::string SubunitListName(int Index) {
  return TableName(Index, "SU_");
}
static std::string SubunitsName(const std::string &Cpu,
                                const std::string &Name) {
  return formatv("SU__{0}__{1}", Cpu, Name);
}
static std::string ForwardSetName(int Index) {
  return TableName(Index, "FWD_");
}

// For non-trivial phase expressions, create a C++ expression to evaluate
// the arithmetic and fetch operands from the instruction if needed.
std::string OutputState::formatPhaseExpr(const PhaseExpr *Expr) const {
  std::string Left = Expr->getLeft() ? formatPhaseExpr(Expr->getLeft()) : "";
  std::string Right = Expr->getRight() ? formatPhaseExpr(Expr->getRight()) : "";

  switch (Expr->getOperation()) {
  case kPlus:
    return formatv("({0} + {1})", Left, Right);
  case kMinus:
    return formatv("({0} - {1})", Left, Right);
  case kMult:
    return formatv("({0} * {1})", Left, Right);
  case kDiv:
    if (Expr->getRight()->isExpressionConstant())
      return formatv("({0} / {1})", Left, Right);
    else
      return formatv("({0} / ({1} ?: 1))", Left, Right);
  case kNeg:
    return formatv("-({0})", Left);
  case kInt:
    return std::to_string(Expr->getNumber());
  case kPhase:
    return std::to_string(Expr->getPhaseId());
  case kOpnd:
    return formatv("static_cast<int32_t>(ins->GetOperand({0}))",
                   Expr->getOperand()->getOperandIndex());
  case kPositive:
    return formatv("std::max(0, {0})", Left);
  }
  return "Error";
}

// Format a pipeline phase reference. A phase can either evaluate to an
// integer, or the address of a function that calculates the phase.
// If a function is required, generate the body of that function (it's just a
// return statement), and enter it into a table, and return the function name.
std::string OutputState::formatPhase(const PhaseExpr *Expr) {
  if (Expr->isExpressionConstant()) {
    auto Result = Expr->evaluateConstantExpression();
    if (Result)
      return std::to_string(*Result) + "," + kNull;
    getSpec().ErrorLog(Expr, "Invalid phase: divide by zero");
    return "0";
  }

  std::string Out = formatv("  return {0};", formatPhaseExpr(Expr));
  auto Index = AddEntry(Phases, Out);
  return formatv("-1,&{0}", PhaseName(Index));
}

// Format reference flags field.
std::string formatReferenceFlags(const Reference *Ref) {
  std::string Out;
  if (Ref == nullptr || Ref->getPhaseExpr()->isDefaultLatency())
    return "0";
  if (Ref->isProtected())
    Out = std::to_string(RefFlags::kProtected);

  if (Ref->isDuplicate())
    Out += formatv(Out.empty() ? "{0}" : "|{0}",
                   std::to_string(RefFlags::kDuplicate));

  if (Out.empty())
    return "0";
  return Out;
}

// Format resource reference flags field.
std::string formatResourceReferenceFlags(const ResourceEvent &Ref) {
  std::string Out;
  // Handle operand and resource references.
  if (Ref.getRefType() != RefTypes::kFus) {
    if (Ref.getPhaseExpr()->isProtected())
      Out = std::to_string(RefFlags::kProtected);
    if (Ref.getReference() && Ref.getReference()->isDuplicate())
      Out += formatv(Out.empty() ? "{0}" : "|{0}", RefFlags::kDuplicate);
    return Out;
  }

  // Handle explicit functional unit reference flags.
  if (Ref.getReference()->isUnbuffered())
    Out += formatv("{0}|", RefFlags::kUnbuffered);
  if (Ref.getReference()->isOutOfOrder())
    Out += formatv("{0}|", RefFlags::kOutOfOrder);
  if (Ref.getReference()->isInOrder())
    Out += formatv("{0}|", RefFlags::kInOrder);

  if (RefFlags::isBeginGroup(Ref.getReference()->getFuFlags()))
    Out += formatv("{0}|", RefFlags::kBeginGroup);
  if (RefFlags::isEndGroup(Ref.getReference()->getFuFlags()))
    Out += formatv("{0}|", RefFlags::kEndGroup);
  if (RefFlags::isSingleIssue(Ref.getReference()->getFuFlags()))
    Out += formatv("{0}|", RefFlags::kSingleIssue);
  if (RefFlags::isRetireOOO(Ref.getReference()->getFuFlags()))
    Out += formatv("{0}|", RefFlags::kRetireOOO);

  if (!Out.empty()) Out.pop_back();   // remove last "|"
  return Out.empty() ? "0" : Out;
}

// Format a predicate function, add it to the table, and return its index.
int OutputState::formatPredicate(PredExpr *Pred) {
  if (Pred == nullptr)
    return -1;
  auto Func = formatv("return {0};", formatPredicateFunc(Pred));

  // If we're generating a standalone database, check to see if the function
  // includes LLVM definitions for the target. This is conservative, but safe.
  if (GenerateLLVMDefs) {
    if (Func.find(Database->getSpec().getFamilyName()) != std::string::npos ||
        Func.find("evaluatePredicate") != std::string::npos)
      Func = "return true;";
  }

  return AddEntry(ReferencePredicates, Func);
}

// This function is used to generate a phase function for an if/then/else
// tree where its been determined (in IsSingleOperand) that all clauses of
// the conditional reference access the same operand with various latencies.
// We handle 2 special cases:
//   - All the latencies are exactly the same integer value, so just return it.
//   - There was only one phase function, so just return that function.
// Its possible to see duplicate predicates in different clauses. and there's
// no reason to write Out a predicate more than once.
std::string OutputState::formatSingleConditionalOperand(ConditionalRef *Cond) {
  std::string Out;
  std::set<int> ConstantExprs;
  std::set<int> NontrivialExprs;
  std::set<int> SeenPredicates;

  for (; Cond; Cond = Cond->getElseClause()) {
    auto *Pred = Cond->getInstrPredicate();
    if (Pred != nullptr && !Pred->isTrue()) {
      auto Index = formatPredicate(Pred);
      if (SeenPredicates.count(Index))
        continue; // Skip duplicate predicates
      SeenPredicates.insert(Index);
      Out += formatv("  if ({0}(ins))", PredicateName(Index));
    }

    auto *Ref = Cond->findSingleRef();
    PhaseExpr *Expr = Ref->getPhaseExpr();
    if (Expr->isExpressionConstant()) {
      auto Value = Expr->evaluateConstantExpression();
      if (Value) {
        Out += formatv("  return {0};\n", *Value);
        ConstantExprs.insert(*Value);
      }
    } else {
      std::string Func = formatv("  return {0};", formatPhaseExpr(Expr));
      int Index = AddEntry(Phases, Func);
      Out += PhaseName(Index) + ";\n";
      NontrivialExprs.insert(Index);
      ForwardPhases.insert(Index);
    }

    if (Pred == nullptr || Pred->isTrue())
      break;
  }

  // If we only saw one unique value generated, just return that value.
  if (ConstantExprs.size() + NontrivialExprs.size() == 1) {
    if (!ConstantExprs.empty())
      return std::to_string(*ConstantExprs.begin()) + "," + kNull;
    return formatv("-1,&{0}", PhaseName(*NontrivialExprs.begin()));
  }

  // Otherwise insert the new function into the table and return it.
  auto Index = AddEntry(Phases, Out);
  return formatv("-1,&{0}", PhaseName(Index));
}

// Generate a predicated operand reference. This is the top level function for
// handling if/then/else references. This returns a constructor for an
// operand reference, which is either a "normal" constructor if the predicates
// can be folded into phase functions, or a conditional operand reference for
// non-trivial if/then/else references.
std::string OutputState::formatConditionalOperandRef(ConditionalRef *Cond) {
  // If this set of conditionals only references a single operand, we can
  // fold all the predicates into a phase function.
  if (Cond->isSingleOperand()) {
    auto *Ref = Cond->findSingleRef();
    int Index = Ref->getOperand() ? Ref->getOperand()->getOperandIndex() : -1;
    return formatv("{{{0},{1},{2},{3}}", formatReferenceType(Ref->getRefType()),
                   formatReferenceFlags(Ref),
                   formatSingleConditionalOperand(Cond), Index);
  }

  // If it is a set of if/then/else clauses that we can't simplify, generate
  // a conditional operand reference record.
  return formatv("{{{0}}", formatIfElseOperandRef(Cond));
}

// Else clauses are handled by creating a single OperandRef which is initialized
// with an optional predicate, a pointer to an operand reference list, and an
// optional else clause.  Empty else clauses simply return "nullptr".
std::string OutputState::formatIfElseOperandRef(ConditionalRef *Cond) {
  std::string Out;
  if (Cond == nullptr)
    return kNull;

  // Add the operand list id to the list of OperandRef forward references.
  auto Opnds = formatOperandReferenceList(&Cond->getRefs());
  if (Opnds != kNull)
    ForwardOpndRefs.insert(Opnds.substr(1, Opnds.size() - 1));

  // If the predicate is null and the operand list is empty, just return null.
  if (Opnds == kNull && Cond->getInstrPredicate() == nullptr)
    return kNull;

  // If the predicate is null it's an unconditional set of references.
  if (Cond->getInstrPredicate() == nullptr) {
    Out = formatv("{1},{0},{1}", Opnds, kNull);
  } else {
    auto Index = formatPredicate(Cond->getInstrPredicate());
    auto ElseRefs = formatIfElseOperandRef(Cond->getElseClause());
    Out = formatv("&{0},{1},{2}", PredicateName(Index), Opnds, ElseRefs);
    // Add else clauses to ConditionalRefs forward references.
    if (ElseRefs != kNull)
      ForwardCondOpndRefs.insert(ElseRefs.substr(1, ElseRefs.size() - 1));
  }

  auto Index = AddEntry(CondOperandRefs, Out);
  return formatv("&{0}", CondReferenceName(Index));
}

// Format an operand reference. We generate an autoinitialization for the type:
//     struct OperandRef {
//       ReferenceType Type;        // type of the reference (use, def, ...)
//       ReferenceFlags Flags;      // reference flags
//       PipePhase Phase;           // pipeline phase of the reference
//       PipeFunc PhaseFunc;        // optional pointer to phase function
//       OperandId OperandIndex;    // operand index
//     };
std::string OutputState::formatOperandReference(const Reference *Ref) {
  int Index = Ref->getOperand() ? Ref->getOperand()->getOperandIndex() : -1;
  return formatv("{{{0},{1},{2},{3}}", formatReferenceType(Ref->getRefType()),
                 formatReferenceFlags(Ref), formatPhase(Ref->getPhaseExpr()),
                 Index);
}

// Format an operand reference list. Create a vector of operand references,
// and enter into a table so it can be shared between subunits.
std::string OutputState::formatOperandReferenceList(const ReferenceList *Refs) {
  std::string Out;
  std::string Previous;
  for (const auto *Ref : *Refs) {
    if (Ref->isConditionalRef()) {
      if (Ref->getConditionalRef()->hasOperandRefs()) {
        Out += formatConditionalOperandRef(Ref->getConditionalRef()) + ",";
      }
    } else if (Ref->getOperand() && Ref->isOperandRefType()) {
      auto Current = formatOperandReference(Ref);
      if (Current != Previous) {
        Out += Current + ",";
        Previous = Current;
      }
    }
  }
  if (Out.empty())
    return kNull;
  Out.pop_back(); // throw away trailing comma.

  auto Index = AddEntry(OperandRefs, Out);
  return formatv("&{0}", OperandListName(Index));
}

// Format a single resource reference. We generate an autoinitialization of
// the type:
//    struct ResourceRef {
//      ReferenceType Type;         // type of the reference (use, def, ...)
//      ReferenceFlags Flags;       // reference flags
//      PipePhase Phase;            // pipeline phase of the reference
//      PipeFunc PhaseFunc;         // optional pointer to phase function
//      unsigned int UseCycles;     // # cycles a resource is used
//      ResourceId ResourceId;      // the resource we're referencing
//      OperandId OperandIndex;     // operand index for shared resources
//      PoolBits Width;             // number of bits in shared value (or -1)
//    };

std::string OutputState::formatResourceReference(const ResourceEvent &Ref) {
  auto *Res = Ref.getResource();
  auto Type = formatReferenceType(Ref.getRefType());
  auto Flags = formatResourceReferenceFlags(Ref);
  auto Phase = formatPhase(Ref.getPhaseExpr());
  int Opnd = Res->getOperandIndex();
  int Cycles = Ref.getUseCycles();
  int Size = Res->getDefinition()->getBitSize();
  std::string Out;

  // If this reference is a duplicate and it doesn't have a valid operand id,
  // there's no reason to write it Out.
  if (Ref.getReference() && Ref.getReference()->isDuplicate() &&
      !Res->hasOperandIndex())
    return "";

  // If this was a functional unit reference, write Out a FU constructor.
  if (Ref.isFuncUnitRef())
    return formatv("{{{0},{1},{2},{3},{4}}", Type, Flags, Cycles,
                   Res->getFinalResourceId(), Ref.getMicroOps());

  // If this was a reference to an entire group, write Out a reference for
  // each group member.
  if (Res->isGroupRef()) {
    for (auto *Member : Res->getDefinition()->getMemberDefs()) {
      Out += formatv("{{{0},{1},{2},{3},{4},{5},{6}},", Type, Flags, Phase,
                     Cycles, Member->getResourceId(), Opnd, Size);
    }
    Out.pop_back(); // throw away trailing comma.
    return Out;
  }

  // If this is a reference to a single resource, write Out a single reference.
  if (!Res->isGroupRef() || Res->getFirst() == Res->getLast())
    return formatv("{{{0},{1},{2},{3},{4},{5},{6}}", Type, Flags, Phase, Cycles,
                   Res->getFinalResourceId(), Opnd, Size);

  // If the reference was for a range of pool entries, write Out a reference
  // for each resource in the range. (This is rare.)
  if (Res->isArrayDef()) {
    for (int Id = Res->getFirst(); Id <= Res->getLast(); Id++) {
      Out += formatv("{{{0},{1},{2},{3},{4},{5},{6}},", Type, Flags, Phase,
                     Cycles, Res->getResourceId() + Id, Opnd, Size);
    }
  }
  Out.pop_back(); // throw away trailing comma.
  return Out;
}

// Given a list of possibly predicated references, generate the objects for
// each reference.  This can handle pooled or not-pooled reference lists.
std::string OutputState::formatResourceReferenceList(
    SubUnitInstantiation *Subunit, ReferenceList &Refs, RefType Type,
    OutputSet &OutputList, FormatName Name, bool FormatPooledRef) {
  std::string Out;
  auto *Cpu = Subunit->getCpu();

  for (auto *Ref : Refs) {
    if (Ref->isFuncUnitRef()) {
      if (Type == RefTypes::kUse) // if we're filtering for kUses.
        Out += formatFuncUnitReference(Subunit, Ref, FormatPooledRef);
      continue;
    }

    if (Ref->isConditionalRef()) {
      auto Res = formatIfElseResourceRef(Subunit, Ref->getConditionalRef(),
                                 Type, OutputList, Name, FormatPooledRef);
      if (Res != kNull)
        Out += "{&" + Res + "},";
      continue;
    }

    // Handle normal case of a reference that may contain resource refs.
    auto RefType = Ref->adjustResourceReferenceType();
    if (RefType != Type)
      continue;
    for (auto *Res : *Ref->getResources()) {
      if (!Res->isNull() && (Res->hasCount() == FormatPooledRef)) {
        PhaseExpr *Phase = Ref->getPhaseExpr();
        if (auto *Start = Res->getDefinition()->getStartPhase())
          Phase =
              new PhaseExpr(Subunit->getSpec()->findPipeReference(Start, Cpu));
        if (Ref->getOperand())
          Res->setOperandIndex(Ref->getOperand()->getOperandIndex());
        auto Event = ResourceEvent(RefType, Phase, Ref->getUseCycles(), Res,
                                   Ref, Subunit);
        if (!Res->hasCount())
          Out += formatResourceReference(Event) + ",";
        else
          Out += formatPooledResourceReference(Event) + ",";
      }
    }
  }

  if (Out.empty())
    return kNull;
  Out.pop_back(); // throw away trailing comma.

  // Enter it into a table, and return the name of the table entry.
  return Name(AddEntry(OutputList, Out));
}

// Format an explicit functional unit reference. The resources in Fus
// records refer to the name of either a functional unit template or a
// functional unit group.  If its a functional unit template, we find each
// occurance of that template in the current CPU - if there is more than
// one we generate a pooled reference, otherwise we generate a single
// reference of the instantiated FU.  If its a functional unit group, we
// find all occurances of all members of the group, and generate references
// for each individual instantiation.
std::string OutputState::formatFuncUnitReference(SubUnitInstantiation *Subunit,
                                                 Reference *Ref,
                                                 bool FormatPooledRef) {
  std::string Out;
  auto *Cpu = Subunit->getCpu();

  // If the resource list is empty, this is a plain micro-ops statement.
  if (Ref->getResources()->empty() && Ref->getMicroOps() > 0 &&
      !FormatPooledRef)
    return formatv("{{RefFus,{0},{1}},", Ref->getFuFlags(), Ref->getMicroOps());

  // For each named FU, create resource events and write them Out. If an
  // instance is a sub-functional-unit, return its root parent resource.
  for (auto *Res : *Ref->getResources()) {
    // Generate an entry for a functional unit template reference.
    if (getSpec().isFuncUnitTemplate(Res->getName()) &&
        !FormatPooledRef && !Res->isFuPool()) {
      auto &Funits = Cpu->getFuncUnitInstances()[Res->getName()];
      if (Funits.size() == 0) continue;

      auto *Fu = new ResourceRef(Funits[0]->getRootResource());
      ResourceEvent Event(RefTypes::kFus, Ref, Fu);
      Out += formatResourceReference(Event) + ",";
      continue;
    }

    if (FormatPooledRef && Res->getFuPool()) {
      ResourceRef Pool(Res->getFuPool());
      Pool.setPoolCount(1);
      ResourceEvent Event(RefTypes::kFus, Ref, &Pool);
      Res->getFuPool()->addReferenceSizeToPool(&Pool, Ref, Subunit);
      Out += formatPooledResourceReference(Event) + ",";
    }
  }

  return Out;
}

// Conditionally format a predicated set of resource references.  The input
// reference list may or may not contain resource references, so we need to
// handle the (common) case that none are found.
std::string OutputState::formatIfElseResourceRef(
    SubUnitInstantiation *Subunit, ConditionalRef *Cond, RefType Type,
    OutputSet &OutputList, FormatName Name, bool FormatPooledRef) {
  if (Cond == nullptr)
    return kNull;

  // Find resource references and generate entries for each. If any are found,
  // add the resource list id to the list of ResourceRef forward references.
  auto ThenRefs = formatResourceReferenceList(
      Subunit, Cond->getRefs(), Type, OutputList, Name, FormatPooledRef);
  if (ThenRefs != kNull) {
    if (FormatPooledRef)
      ForwardPooledRefs.insert(ThenRefs);
    else
      ForwardResourceRefs.insert(ThenRefs);
  }

  // If no resource references were found, and the predicate is null, abort.
  if (ThenRefs == kNull && Cond->getInstrPredicate() == nullptr)
    return kNull;

  // If the predicate is null it's an unconditional set of references.
  std::string Out;
  if (Cond->getInstrPredicate() == nullptr) {
    Out = formatv("{1},&{0},{1}", ThenRefs, kNull);
  } else {
    auto ElseRefs =
        formatIfElseResourceRef(Subunit, Cond->getElseClause(), Type,
                                OutputList, Name, FormatPooledRef);
    // Add else clauses to ConditionalRefs forward references.
    if (ElseRefs != kNull) {
      if (FormatPooledRef)
        ForwardCondPoolRefs.insert(ElseRefs);
      else
        ForwardCondResRefs.insert(ElseRefs);
    }

    if (ElseRefs == kNull && ThenRefs == kNull)
      return kNull;
    if (ElseRefs != kNull)
      ElseRefs = "&" + ElseRefs;
    if (ThenRefs != kNull)
      ThenRefs = "&" + ThenRefs;

    auto Index = formatPredicate(Cond->getInstrPredicate());
    Out = formatv("&{0},{1},{2}", PredicateName(Index), ThenRefs, ElseRefs);
  }

  if (FormatPooledRef) {
    auto Index = AddEntry(CondPooledResourceRefs, Out);
    return CondPooledResourceReferenceName(Index);
  } else {
    auto Index = AddEntry(CondResourceRefs, Out);
    return CondResourceReferenceName(Index);
  }
}

// Format a resource reference list. Create a vector of resource references,
// and enter into a table so it can be shared between subunits.
std::string OutputState::formatResourceReferences(InstrInfo *Info, RefType Type,
                                                  OutputSet &OutputList,
                                                  FormatName Name) {
  // First write Out entries for all the unconditional resource references.
  std::string Out, Previous;
  auto *Subunit = Info->getSubunit();
  for (auto &Ref : Info->getResources())
    if (!Ref.getResource()->hasCount() && Ref.getRefType() == Type) {
      auto Resource = formatResourceReference(Ref);
      if (!Resource.empty() && Resource != Previous) {
        Out += Resource + ",";
        Previous = Resource;
      }
    }

  // Format conditional resource references and FU references.
  for (auto *Ref : Info->getResourceRefs()) {
    if (Ref->isConditionalRef()) {
      auto Res = formatIfElseResourceRef(Subunit, Ref->getConditionalRef(),
                                         Type, OutputList, Name, false);
      if (Res != kNull)
        Out += "{&" + Res + "},";
    } else if (Ref->isFuncUnitRef() && Type == RefTypes::kUse) {
      Out += formatFuncUnitReference(Subunit, Ref, false);
    }
  }

  if (Out.empty())
    return kNull;
  Out.pop_back(); // throw away trailing comma.

  // Enter it into a table, and return a reference to the table entry.
  auto Index = AddEntry(OutputList, Out);
  return formatv("&{0}", Name(Index));
}

// Search for a named attribute in an operand definition. If it has an
// attribute predicate, make sure it's valid for this operand.
// Note: Don't check bases here - that is done at a higher level.
OperandAttribute *findAttribute(const std::string &Name, const OperandDef *Opnd,
                                const SubUnitInstantiation *Subunit) {
  if (Opnd == nullptr || Opnd->getAttributes() == nullptr)
    return nullptr;
  for (auto *Attr : *Opnd->getAttributes())
    if (Name == Attr->getName())
      if (Subunit->isValidPredicate(Attr->getPredicate()))
        return Attr;
  return nullptr;
}

// Find the derivation between an operand and a base operand.
// Since we checked derivations earlier, this should always succeed.
bool findDerivation(OperandDef *Ref, const OperandDef *decl,
                    OperandDefList &Opnds) {
  Opnds.push_back(Ref);
  if (Ref == decl)
    return true;
  if (Ref->getBaseOperands())
    for (auto *Base : *Ref->getBaseOperands())
      if (findDerivation(Base, decl, Opnds))
        return true;

  Opnds.pop_back();
  return false;
}

// Generate the conditional code that implements an attribute predicate.
std::string formatAttributePredicate(const OperandAttribute *Attr) {
  std::string Out;
  for (auto *Pred : *Attr->getPredicateValues()) {
    if (!Out.empty())
      Out += " ||\n        ";
    if (Pred->isValue())
      Out += formatv("(value == {0})", Pred->formatValue(Pred->getValue()));
    else if (Pred->isRange())
      Out += formatv("(value >= {0} && value <= {1})",
                     Pred->formatValue(Pred->getLow()),
                     Pred->formatValue(Pred->getHigh()));
    else if (Pred->isMask())
      Out += formatv("((value & ~{0:X8}UL) == 0)", Pred->getMask());
  }

  if (Attr->getPredicateValues()->size() > 1)
    return formatv("    if ({0})", Out);
  return formatv("    if {0}", Out);
}

// Generate a function that returns the appropriate attribute value for the
// given operand derivation.
std::string formatPooledCountFunction(std::string const &Attr,
                                      OperandDefList const &Opnds,
                                      const SubUnitInstantiation *Subunit) {
  // The generated function has the following declaration:
  //    void COUNT_#(Instr *ins, int operand_id, int size, int values[]) {}
  std::string Out;
  std::vector<std::string> Lits;
  std::string Addr, Label, None;
  bool LitEnd = false;

  int64_t MaxValue = 0;
  for (auto *Opnd : Opnds) {
    for (auto OpAttr : *Opnd->getAttributes()) {
      if (OpAttr->getName() == Attr &&
          Subunit->isValidPredicate(OpAttr->getPredicate())) {
        int64_t Value = OpAttr->getValues(0);
        std::string Result = formatv("  return {0};", Value);

        std::string Attr = OpAttr->getType();
        if (Attr.empty())
          None = Result + "  // none\n";
        if (Attr == "address" && Addr.empty())
          Addr = formatv("  {0}  // addr\n", Result);
        if (Attr == "label" && Label.empty())
          Label = formatv("  {0}  // label\n", Result);

        if (Attr == "lit" && !LitEnd) {
          std::string Out;
          LitEnd = OpAttr->getPredicateValues()->empty();
          if (!LitEnd) {
            Out =
                formatv("{0}{1}\n", formatAttributePredicate(OpAttr), Result);
            Lits.push_back(Out);
          } else {
            Lits.push_back(formatv("  {0}\n", Result));
          }
        }
        MaxValue = std::max(MaxValue, Value);
      }
    }
  }

  if (!Lits.empty()) {
    Out += "  if (ins->isOpndLiteral(operand_index)) {\n"
           "    int64_t value = ins->GetOperand(operand_index);\n";
    for (const auto &Lit : Lits)
      Out += Lit;
    Out += "  }\n";
  }
  if (!Label.empty())
    Out += formatv("  if (ins->isOpndLabel(operand_index))\n{0}", Label);
  if (!Addr.empty())
    Out += formatv("  if (ins->isOpndAddress(operand_index))\n{0}", Addr);
  if (!None.empty())
    Out += None;

  if (None.empty())
    Out += formatv("  return {0};  // default\n", MaxValue);
  return Out;
}

// Return a string that encodes the pool count and the address of a function
// to call if a symbolic size was specified: <count, &function>.
// If the provided name matches an operand attribute, generate a function to
// calculate the right attribute value.  If it does't match an attribute,
// generate the address of a user-define function to call.
std::string OutputState::formatPooledCount(const ResourceEvent &Ref) {
  auto *Res = Ref.getResource();
  int PoolCount = Res->getPoolCount();

  // If there's no symbolic pooled count specified, just return the
  // specified pool count.
  if (Res->getPoolCountName() == nullptr)
    return formatv("{0},{1}", PoolCount, kNull);

  auto CountName = Res->getPoolCountName()->getName();
  auto Out = formatv("{0},&PoolCount_{1}", PoolCount, CountName);

  // If there is a symbolic pooled count, but no reference or no operand,
  // then just generate a reference to the user-defined function.
  if (Ref.getReference() == nullptr ||
      Ref.getReference()->getOperand() == nullptr)
    return Out;

  // If there is an operand reference, check to see if the pool count name
  // matches an operand attribute with a valid predicate.
  auto *OpndRef = Ref.getReference()->getOperand();
  auto *OpndBase = OpndRef->getOperandDecl()->getOperand();
  auto *OpndDef = OpndRef->getOperand();

  // If we can't find a derivation, there's a problem, just return.
  OperandDefList Opnds;
  if (!findDerivation(OpndDef, OpndBase, Opnds)) {
    getSpec().ErrorLog(Ref.getReference(),
                       "Panic: operand derivation failed\n\n");
    return Out;
  }

  // If we don't find the attribute name, generate the reference to the
  // user-defined function.
  OperandAttribute *Attr = nullptr;
  for (auto *Opnd : Opnds)
    if ((Attr = findAttribute(CountName, Opnd, Ref.getSubunit())) != nullptr)
      break;
  if (Attr == nullptr)
    return Out;

  // If the attribute doesn't have predicate values associated with it, just
  // return the attribute value.
  if (Attr->getPredicateValues()->empty())
    return formatv("{0},{1}", Attr->getValues(0), kNull);

  // Finally, we can generate code for the attribute lookup, and return
  // the name of the function.
  auto Func = formatPooledCountFunction(CountName, Opnds, Ref.getSubunit());
  auto Index = AddEntry(PooledCountFunctions, Func);
  return formatv("{0},&{1}", PoolCount, PooledCountFuncName(Index));
}

// Generate a function that returns the appropriate attribute value(s) for the
// given operand derivation.
const int kMaxPoolCount = 20;

static int findLSB(int64_t Val) {
  int Lsb = 0;
  if (Val == 0)
    return 0;
  for (; (Val & 1) == 0; Val >>= 1)
    Lsb++;
  return Lsb;
}

std::string formatPoolValuesFunction(std::string const &Attr,
                                     ResourceEvent const &Ref,
                                     OperandDefList const &Opnds,
                                     const SubUnitInstantiation *Subunit) {
  // The generated function has the following declaration:
  //    void VALUE_#(Instr *ins, int operand_id, int size, int values[]) {}
  std::string Sizes[kMaxPoolCount];
  bool Unconditional[kMaxPoolCount] = {false};

  // Collect the attributes for each size found in the operands.
  for (auto *Opnd : Opnds)
    if (Opnd->getAttributes()) {
      for (auto OpAttr : *Opnd->getAttributes()) {
        if (OpAttr->getName() == Attr &&
            Subunit->isValidPredicate(OpAttr->getPredicate())) {
          int TupleSize = OpAttr->getValues()->size();
          if (Unconditional[TupleSize])
            continue;

          std::string Pred;
          if (OpAttr->getPredicateValues()->empty())
            Unconditional[TupleSize] = true;
          else
            Pred = formatAttributePredicate(OpAttr);

          std::string Item;
          for (int I = 0; I < TupleSize; I++) {
            uint32_t Val = OpAttr->getValues(I);
            if (int Lsb = findLSB(Val))
              Item += formatv("      values[{0}] = (value & {1:X8}) >> {2};\n",
                              I, Val, Lsb);
            else
              Item += formatv("      values[{0}] = value & {1:X8};\n", I, Val);
          }

          if (!OpAttr->getPredicateValues()->empty())
            Item = formatv("  {{\n{0}      return true;\n    }\n", Item);
          else
            Item += "      return true;\n";
          Sizes[TupleSize] += formatv("{0}{1}", Pred, Item);
        }
      }
    }

  // If the resource reference indicated a specific size (res:1), make sure
  // we found at least one attribute that satisfied that size.  If not, it's
  // an error.  This is a panic error - it should never happen.
  auto *Res = Ref.getResource();
  if (Res->getPoolCount() > 0)
    if (Sizes[Res->getPoolCount()].empty())
      Subunit->ErrorLog(Ref.getReference(),
                        "Panic: Incompatible pool size specifier: {0}:{1}",
                        Res->getId()->ToString(), Res->getPoolCount());

  std::string Out = "  uint64_t value = ins->GetOperand(operand_index);\n";

  for (int Size = 1; Size < kMaxPoolCount; Size++) {
    if (!Sizes[Size].empty()) {
      Out += formatv("  if (size == {0}) {{\n{1}", Size, Sizes[Size]);
      if (!Unconditional[Size]) {
        for (int I = 0; I < Size; I++)
          Out += formatv("    values[{0}] = value;\n", I);
        Out += "    return true;\n";
      }
      Out += "  }\n";
    }
  }
  return Out + "  return false;\n";
}

// Return a string that encodes the name of a function to call if a reference
// specifies an optional mask operation on an operand allocated to a pool.
// Generate the function to fetch, shift and mask the parts of the operand
// that are shared.
std::string OutputState::formatPoolValues(const ResourceEvent &Ref) {
  auto *Res = Ref.getResource();
  auto *Def = Res->getDefinition();
  // Some sanity checking.
  if (Res->getValueName() == nullptr) {
    if (Def->hasSharedBits() && Res->hasCount()) {
      if (Res->getPoolCount() > 1 || Res->getPoolCountName() != nullptr)
        getSpec().ErrorLog(Ref.getReference(),
                        "Missing value mask attribute on shared resource");
    }
    return kNull;
  }

  auto Value = Res->getValueName()->getName();

  // If there is an operand reference, check to see if the mask name
  // matches an operand attribute with a valid predicate.
  auto *OpndRef = Ref.getReference()->getOperand();
  auto *OpndBase = OpndRef->getOperandDecl()->getOperand();
  auto *OpndDef = OpndRef->getOperand();

  // If we can't find a derivation, there's a problem, just return.
  OperandDefList Opnds;
  if (!findDerivation(OpndDef, OpndBase, Opnds)) {
    getSpec().ErrorLog(Ref.getReference(),
                       "Panic: operand derivation failed\n\n");
    return kNull;
  }

  // If we don't find the attribute name, it's an error.
  OperandAttribute *Attr = nullptr;
  for (auto *Opnd : Opnds)
    if ((Attr = findAttribute(Value, Opnd, Ref.getSubunit())) != nullptr)
      break;

  if (Attr == nullptr) {
    getSpec().ErrorLog(Ref.getReference(),
                       "Invalid value mask name: {0}", Value);
    return kNull;
  }

  // Generate code for the attribute lookup, and return the function name.
  auto Func = formatPoolValuesFunction(Value, Ref, Opnds, Ref.getSubunit());
  auto Index = AddEntry(PoolMaskFunctions, Func);
  return formatv("&{0}", PoolValueFuncName(Index));
}

// Format a single pool descriptor.
// Generate an autoinitialization for the type:
//    struct PoolDescriptor {
//      uint8_t PoolId;               // which pool to allocate from
//      uint8_t PoolSize;             // how many different allocations sizes
//      uint8_t Count;                // how many pool elements we need
//      PoolFunc PoolFunc;            // optional pointer to pool size func
//      OpndValueFunc ValueFunc;      // optional pointer to pool values func
//      uint8_t First;                // index of first legal element id
//      uint8_t Last;                 // index of last legal element id
//      uint8_t Width;                // width in bits
//    };
std::string OutputState::formatPoolDescriptor(const ResourceEvent &Ref) {
  auto *Res = Ref.getResource();
  SubPool Pool(Res);
  auto &SubpoolInfo = Res->getDefinition()->getSubPool(Pool);
  int SubpoolSize = *SubpoolInfo.getCounts().rbegin();

  std::string Out =
      formatv("{0},{1},{2},{3},{4},{5},{6}", SubpoolInfo.getSubpoolId(),
              SubpoolSize, formatPooledCount(Ref), formatPoolValues(Ref),
              Pool.getFirst(), Pool.getLast(),
              Res->getDefinition()->getBitSize());

  // Enter it into a table of Pool descriptors, and return a reference to it.
  auto Index = AddEntry(PoolDescriptors, Out);
  return formatv("&{0}", PoolDescriptorName(Index));
}

// Format a group of resources used in a pool request.  This is simply a list
// of resource ids for groups or arrays. Enter them into a table so they can
// be shared across pool requests.
std::string OutputState::formatResourceGroup(const ResourceEvent &Ref) {
  std::string Out;
  auto *Res = Ref.getResource();

  if (Res->isGroupRef()) {
    for (auto *Member : Res->getDefinition()->getMemberDefs())
      Out += std::to_string(Member->getResourceId()) + ",";
  } else if (Res->isArrayDef()) {
    for (int Id = Res->getFirst(); Id <= Res->getLast(); Id++)
      Out += std::to_string(Res->getResourceId() + Id) + ",";
  }
  Out.pop_back(); // throw away trailing comma.

  // Enter it into a table of resource groups, and return a reference to it.
  auto Index = AddEntry(ResourceGroups, Out);
  return formatv("{0}", ResourceGroupName(Index));
}

// Format a single pooled resource reference.
// Generate an autoinitialization for the type:
//    struct PooledResourceRef {
//      struct ResourceRef {
//        ReferenceType Type;         // type of the reference (use, def, ...)
//        ReferenceFlags Flags;       // reference flags
//        PipePhase Phase;            // pipeline phase of the reference
//        unsigned int UseCycles;     // # cycles a resource is used
//        PipeFunc PhaseFunc;         // optional pointer to phase function
//        ResourceId &ResourceId[];   // the resources we're referencing
//        OperandId OperandIndex;     // operand index for shared resources
//        int MicroOps;               // MicroOps for an FU entry
//      };
//      PoolDescriptor *Pool;         // pointer to pool descriptor
//    };
std::string
OutputState::formatPooledResourceReference(const ResourceEvent &Ref) {
  auto *Res = Ref.getResource();

  auto Pool = formatPoolDescriptor(Ref);
  auto Group = formatResourceGroup(Ref);

  if (Ref.isFuncUnitRef())
    return formatv("{{{0},{1},{2},{3},{4},{5}}",
                   formatReferenceType(Ref.getRefType()),
                   formatResourceReferenceFlags(Ref), Ref.getUseCycles(), Group,
                   Pool, Ref.getMicroOps());

  return formatv(
      "{{{0},{1},{2},{3},{4},{5},{6}}", formatReferenceType(Ref.getRefType()),
      formatResourceReferenceFlags(Ref), formatPhase(Ref.getPhaseExpr()),
      Ref.getUseCycles(), Group, Res->getOperandIndex(), Pool);
}

// Format a pooled operand reference list. Enter it into a table so it can
// be shared with other subunits.
std::string OutputState::formatPooledResourceReferences(InstrInfo *Info,
                                                        OutputSet &OutputList,
                                                        FormatName Name) {
  // First write Out entries for all unconditional pooled references.
  std::string Out, Previous;
  auto *Subunit = Info->getSubunit();
  for (auto &Ref : Info->getResources())
    if (Ref.getResource()->hasCount()) { // Only pooled references
      auto Resource = formatPooledResourceReference(Ref);
      if (Resource != Previous) {
        Out += Resource + ",";
        Previous = Resource;
      }
    }

  // Format conditional pooled resource references and FU references.
  for (auto *Ref : Info->getResourceRefs()) {
    if (Ref->isConditionalRef()) {
      auto Res = formatIfElseResourceRef(Subunit, Ref->getConditionalRef(),
                                        RefTypes::kUse, OutputList, Name, true);
      if (Res != kNull)
        Out += "{&" + Res + "},";
    } else if (Ref->isFuncUnitRef()) {
      Out += formatFuncUnitReference(Subunit, Ref, true);
    }
  }

  if (Out.empty())
    return kNull;
  Out.pop_back(); // throw away trailing comma.

  auto Index = AddEntry(PooledResourceRefs, Out);
  return formatv("&{0}", Name(Index));
}

// Format a single constraint. Return an empty string if no constraint found,
// or if the MDL constraint doesn't further constrain the operand (ie, it is a
// superset of the operand constraint).
std::string OutputState::formatConstraint(const Reference *Ref) {
  std::string Family = getSpec().getFamilyName();
  if (auto *Opnd = Ref->getOperand())
    if (auto *Port = Ref->getPort())
      if (auto *RegClass = Port->getRegClass())
        if (auto *OperandClass = Opnd->getOperandDecl()->getRegClass())
          if (!RegClass->isSupersetOf(OperandClass))
            return formatv("{{{0},{1}::{2}RegClassId}", Opnd->getOperandIndex(),
                           Family, RegClass->getName());
  return "";
}

// Find and format a list of constraints. Not all operands have constraints,
// so the resulting string could be empty.
std::string OutputState::formatConstraintList(ReferenceList *Refs) {
  std::string Out;
  for (auto *Ref : *Refs) {
    auto Constraint = formatConstraint(Ref);
    if (!Constraint.empty())
      Out += Constraint + ",";
  }
  if (Out.empty())
    return kNull;
  Out.pop_back(); // throw away trailing comma.

  auto Index = AddEntry(Constraints, Out);
  return ConstraintListName(Index);
}

std::string OutputState::formatIfElseConstraint(ConditionalRef *Cond) {
  if (Cond == nullptr)
    return kNull;

  auto ThenRefs = formatConstraintList(&Cond->getRefs());
  if (ThenRefs != kNull)
    ForwardConstraintRefs.insert(ThenRefs);

  // If no constraints were found, and the predicate is null, abort.
  if (ThenRefs == kNull && Cond->getInstrPredicate() == nullptr)
    return kNull;

  std::string Out;
  if (Cond->getInstrPredicate() == nullptr) {
    Out = formatv("{1},&{0},{1}", ThenRefs, kNull);
  } else {
    auto ElseRefs = formatIfElseConstraint(Cond->getElseClause());
    if (ElseRefs != kNull)
      ForwardCondConstraintRefs.insert(ElseRefs);

    if (ElseRefs == kNull && ThenRefs == kNull)
      return kNull;
    if (ElseRefs != kNull)
      ElseRefs = "&" + ElseRefs;
    if (ThenRefs != kNull)
      ThenRefs = "&" + ThenRefs;

    auto Index = formatPredicate(Cond->getInstrPredicate());
    Out = formatv("&{0},{1},{2}", PredicateName(Index), ThenRefs, ElseRefs);
  }
  auto Index = AddEntry(CondConstraints, Out);
  return CondConstraintName(Index);
}

std::string OutputState::formatPortReferences(InstrInfo *Info) {
  std::string Out;
  for (auto *Ref : *Info->getReferences()) {
    if (Ref->isConditionalRef()) {
      auto Constraint = formatIfElseConstraint(Ref->getConditionalRef());
      if (Constraint != kNull)
        Out += "{&" + Constraint + "},";
    } else {
      auto Constraint = formatConstraint(Ref);
      if (!Constraint.empty())
        Out += Constraint + ",";
    }
  }

  if (Out.empty())
    return kNull;
  Out.pop_back(); // throw away trailing comma.

  auto Index = AddEntry(Constraints, Out);
  return formatv("&{0}", ConstraintListName(Index));
}

// Scan a reference list, marking resource uses which are identical to earlier
// references. This enables a single instruction to reference resources
// several times - in different operands - without impacting hazards and
// bundle packing.
void MarkDuplicateReferences(ResourceList &Refs) {
  for (auto &Ref : Refs)
    for (auto &OldRef : Refs) {
      if (&Ref == &OldRef)
        break;
      auto *Res = Ref.getResource();
      auto *OldRes = OldRef.getResource();
      if (Ref.getPhaseExpr()->ToString() != OldRef.getPhaseExpr()->ToString())
        continue;
      if (Res->getFinalResourceId() != OldRes->getFinalResourceId())
        continue;
      if (Ref.getRefType() != OldRef.getRefType())
        continue;
      if (Res->hasCount() || OldRes->hasCount())
        continue;
      int RefOperandIndex = Res->getOperandIndex();
      int OldOeprandIndex = OldRes->getOperandIndex();
      if (RefOperandIndex != OldOeprandIndex) {
        Ref.getReference()->setDuplicate();
        break;
      }
    }
}

// Format a single subunit.
std::string OutputState::formatSubunit(InstrInfo *Info) {
  // First format all the operand references.
  auto Operands = formatOperandReferenceList(Info->getReferences());

  // Sort the references so that they are ordered by phase, then resource id.
  // This will speed up bundle packing, since functional units and issue slots
  // are the lowest-numbered resources.
  std::stable_sort(Info->getResources().begin(), Info->getResources().end());

  MarkDuplicateReferences(Info->getResources());

  auto Used = formatResourceReferences(
      Info, RefTypes::kUse, UsedResourceRefs, &UsedResourceListName);
  auto Held = formatResourceReferences(
      Info, RefTypes::kHold, HeldResourceRefs, &HeldResourceListName);
  auto Rsvd = formatResourceReferences(Info, RefTypes::kReserve,
                                       ReservedResourceRefs,
                                       &ReservedResourceListName);
  auto Pooled = formatPooledResourceReferences(Info, PooledResourceRefs,
                                               &PooledResourceListName);
  auto Constraint = formatPortReferences(Info);

  // If everything is null (common case with CPU predicates), return null.
  if (Operands == kNull && Used == kNull && Rsvd == kNull && Held == kNull &&
      Pooled == kNull && Constraint == kNull)
    return kNull;

  // Its pretty common that most of these fields are null, so write Out shorter
  // initializations if that's the case (this just saves disk space).
  if (Held == kNull && Rsvd == kNull && Pooled == kNull && Constraint == kNull)
    return formatv("{{{0},{1}}", Operands, Used);
  return formatv("{{{0},{1},{2},{3},{4},{5}}", Operands, Used, Held, Rsvd,
                 Pooled, Constraint);
}

// Format a subunit set for an instruction on a single CPU.
std::string OutputState::formatSubunits(const std::string &Instr,
                                        const InstrInfoList &InfoList,
                                        const std::string &CpuName) {
  // Generate each subunit and eliminate duplicates.
  std::vector<std::string> Units;
  for (auto *Info : InfoList)
    if (Info->getSubunit()->getFuncUnit()->getCpu()->getName() == CpuName) {
      auto Unit = formatSubunit(Info);
      if (Unit != kNull &&
          std::find(Units.begin(), Units.end(), Unit) == Units.end())
        Units.push_back(Unit);
    }

  if (Units.empty())
    return kNull; // We didn't find subunits for this CPU.

  // Format the list of strings.
  std::string Out;
  for (auto &Unit : Units) Out += Unit + ",";
  Out.pop_back(); // throw away trailing comma.

  // Not sure if its worthwhile actually sharing these, but its pretty easy.
  auto Index = AddEntry(Subunits, Out);
  CpuInstrSubunits[SubunitsName(CpuName, Instr)] = Index;
  return Out;
}

void OutputState::writeTable(const OutputSet &Objects, const std::string &Type,
                             const std::string &Suffix, FormatName Name,
                             const std::string &Title,
                             const std::string &Info) {
  if (!Title.empty())
    outputC() << formatv("{0}// {1} ({2} entries){3}{0}", Divider, Title,
                          Objects.size(), Info);
  for (auto &[Out, Index] : Objects)
    outputC() << formatv("{0} {1}{2} {{{3}};\n", Type, Name(Index), Suffix,
                          Out);
}

// Helper to figure Out how many constructors are included on a a vector
// initialization line. We're just counting leading '{'.  Note that some
// vectors don't have any braces.
static int countVectorInitItems(std::string Input) {
  int Count = 0;
  for (auto C : Input)
    if (C == '{')
      Count++;
  return std::max(Count, 1); // In case there's just one initializer
}

void OutputState::writeVectorTable(const OutputSet &Objects,
                                   const std::string &Type, FormatName Name,
                                   const std::string &Title,
                                   const std::string &Info) {
  if (!Title.empty())
    outputC() << formatv("{0}// {1} ({2} entries){3}{0}", Divider, Title,
                          Objects.size(), Info);
  for (auto &[Out, Index] : Objects) {
    outputC() << formatv("{0} {1}_data[] = {{{2}};\n", Type, Name(Index), Out);
    outputC() << formatv("{0}Vec {1} = {{ {2}, {1}_data };\n", Type,
                          Name(Index), countVectorInitItems(Out));
  }
}

void OutputState::writePhases(const OutputSet &Phases, FormatName Name) const {
  if (Phases.empty())
    return;
  outputC() << formatv(
      "{0}// Functions to compute non-trivial pipeline phase expressions{0}",
      Divider);

  // Write Out any forward declarations that might be needed.
  for (auto Index : ForwardPhases)
    outputC() << formatv("unsigned {0}(Instr *ins);\n", Name(Index));
  outputC() << "\n";

  for (auto &[Phase, Index] : Phases)
    outputC() << formatv("unsigned {0}(Instr *ins) {{\n{1} }\n", Name(Index),
                          Phase);
}

void OutputState::writePoolCountFunctions(const OutputSet &Funcs,
                                          FormatName Name) const {
  if (Funcs.empty())
    return;
  outputC() << formatv(
      "{0}{1}{0}", Divider,
      "// Functions to compute attribute-based pool size counts");

  for (auto &[Func, Index] : Funcs)
    outputC() << formatv("int {0}(Instr *ins, int operand_index) {{\n{1}}\n",
                          Name(Index), Func);
}

void OutputState::writePoolValueFunctions(const OutputSet &Funcs,
                                          FormatName Name) const {
  if (Funcs.empty())
    return;
  outputC() << formatv(
      "{0}{1}{0}", Divider,
      "// Functions to fetch and normalize operand values for sharing");

  for (auto &[Func, Index] : Funcs)
    outputC() << formatv("bool {0}(Instr *ins, int operand_index, "
                          "int size, int values[]) {{\n{1}}\n",
                          Name(Index), Func);
}

void OutputState::writePredicateFunctions(const OutputSet &Funcs,
                                          FormatName Name,
                                          const std::string &Type,
                                          std::fstream &Output) const {
  if (Funcs.empty())
    return;
  Output << formatv("{0}// {1}{0}", Divider, Type);
  for (auto &[Func, Index] : Funcs) {
    Output << formatv("bool {0}(Instr *MI) {{\n{1}\n}\n", Name(Index), Func);
  }
}

void OutputState::writeVirtualPredicateTable(const OutputSet &Funcs) const {
  outputT() << formatv("{0}// Virtual predicate function table{0}", Divider);
  outputT() << "std::vector<PredFunc> InstrPredicates { ";
  for (unsigned I = 0; I < Funcs.size(); I++) {
    if (I > 0)
      outputT() << ", ";
    outputT() << "&" << VirtualPredicateName(I);
  }
  outputT() << "};\n\n";
}

// TODO(tbd): Write Out a representation of register classes.
// No current architecture currently requires this...
void OutputState::writeClasses(const OutputSet &RegClasses, FormatName Name) {}

// Write Out some information about each llvm instruction definition.
// This is only necessary when generating a stand-alone database.
void OutputState::writeInstructionInfo() const {
  auto &Spec = getSpec();
  if (!GenerateLLVMDefs)
    return;
  if (Spec.getCpus().empty())
    return;

  std::string Out;
  std::string Family = Spec.getFamilyName();
  for (auto *Ins : Spec.getInstructions()) {
    Out += formatv("{{::llvm::{0}::{1},\"{1}\"},\n", Family, Ins->getName());
    if (Ins->getDerived())
      for (auto *Derived : *Ins->getDerived())
        Out += formatv("{{::llvm::{0}::{1},\"{1}\"},\n", Family,
                       Derived->getName());
  }

  outputC() << formatv("{0}// Instruction name table ({1} entries){0}",
                        Divider, Spec.getInstructions().size());
  outputC() << formatv("InstructionNameMap InstructionNames = {{\n{0}};\n",
                        Out);
}

// Write Out instruction tables for each defined CPU.
void OutputState::writeInstructionTables() const {
  std::string Family = getSpec().getFamilyName();
  for (auto *Cpu : getSpec().getCpus()) {
    std::string Out;
    int InstrCount = 0; //  Number of instructions for this CPU.
    Out = formatv("  static SubunitTable table;\n"
                  "  static sys::SmartMutex<true> Mutex;\n"
                  "  sys::SmartScopedLock<true> Lock(Mutex);\n"
                  "  if (table.size() != 0) return &table;\n"
                  "  table.resize(::llvm::{0}::INSTRUCTION_LIST_END, {1});\n",
                  Family, kNull);

    for (auto &[Iname, Info] : Database->getInstructionInfo()) {
      std::string SuName = SubunitsName(Cpu->getName(), Iname);
      if (CpuInstrSubunits.count(SuName)) {
        int Id = CpuInstrSubunits.at(SuName);
        Out += formatv("  table[::llvm::{0}::{1}] = &{2};\n", Family, Iname,
                       SubunitListName(Id));
        if (Info[0]->getInstruct()->getDerived())
          for (auto *Derived : *Info[0]->getInstruct()->getDerived())
            Out += formatv("  table[::llvm::{0}::{1}] = &{2};\n", Family,
                           Derived->getName(), SubunitListName(Id));

        InstrCount++;
      }
    }
    // If there's no instruction info for a CPU, don't write Out object.
    Cpu->setInstrCount(InstrCount);
    if (InstrCount == 0) continue;

    outputC() << formatv(
        "{0}// Instruction table initialization for {1} ({2} valid entries){0}",
        Divider, Cpu->getName(), InstrCount);
    outputC() << formatv(
        "__attribute__((optnone))\n"
        "SubunitTable *SUNITS_{0}() {{\n{1}  return &table;\n}\n",
        Cpu->getName(), Out);
  }
}

// Generate the forwarding table for a single CPU.
std::string OutputState::formatForwardingInfo(const CpuInstance *Cpu,
                                              FwdNetwork &Network) {
  std::string Out;
  for (int From = 0; From < Network.getUnits(); From++) {
    std::string FwdSetOut;
    for (int To = 0; To < Network.getUnits(); To++)
      FwdSetOut += formatv("{0},", Network.get(From, To));

    auto Index = AddEntry(ForwardSets, FwdSetOut);
    Out += formatv("{0},", ForwardSetName(Index));
  }

  return formatv("int8_t *FWD_{0}[{1}] = {{ {2} };\n", Cpu->getName(),
                 Network.getUnits(), Out);
}

// Given a forwarding statement functional unit specifier, look for a match
// for the name in functional unit templates, functional unit groups, and
// functional unit instances.
// Forwarding statements within a cluster definition apply only to that
// cluster. Cpu-level statements apply across all clusters.
std::vector<int> OutputState::findUnitIds(const CpuInstance *Cpu,
                                          const ClusterInstance *FwdCluster,
                                          const Identifier *Name) const {
  std::vector<int> Units;
  auto &Spec = getSpec();
  // If it's a functional unit template, find all instances of that template.
  if (Spec.getFuMap().count(Name->getName())) {
    for (auto *Cluster : *Cpu->getClusters())
      if (Cluster == FwdCluster || FwdCluster == nullptr) {
        for (auto *Fu : Cluster->getFuInstantiations())
          if (Fu->getFuncType()->getName() == Name->getName())
            Units.push_back(Fu->getFuResource()->getResourceId());
      }
    return Units;
  }

  // If it's a group, find all uses of every member functional unit template.
  if (Spec.getFuGroupMap().count(Name->getName())) {
    for (auto *Member : *Spec.getFuGroupMap()[Name->getName()]->getMembers()) {
      auto Gunits = findUnitIds(Cpu, FwdCluster, Member);
      Units.insert(Units.end(), Gunits.begin(), Gunits.end());
    }
    return Units;
  }

  // If it's not a template or a group, find a functional unit instance of
  // the name.  There potentially could be one in each cluster.
  for (auto *Cluster : *Cpu->getClusters())
    if (Cluster == FwdCluster || FwdCluster == nullptr) {
      for (auto *Fu : Cluster->getFuInstantiations())
        if (Fu->getInstance()->getName() == Name->getName())
          Units.push_back(Fu->getFuResource()->getResourceId());
    }
  return Units;
}

void OutputState::expandForwardStmt(FwdNetwork &Network, const CpuInstance *Cpu,
                                    const ClusterInstance *Cluster,
                                    const ForwardStmt *Fwd) const {
  auto &Spec = getSpec();
  auto From = Fwd->getFromUnit();
  auto Defs = findUnitIds(Cpu, Cluster, From);
  if (Defs.empty()) {
    Spec.ErrorLog(From, "Invalid functional unit: {0}", From->getName());
    return;
  }
  for (const auto &[To, Cycles] : Fwd->getToUnits()) {
    auto Uses = findUnitIds(Cpu, Cluster, To);
    if (!Uses.empty()) {
      for (auto Def : Defs)
        for (auto Use : Uses)
          Network.set(Def, Use, Cycles);
    } else {
      Spec.ErrorLog(To, "Invalid functional unit: {0}", To->getName());
    }
  }
}

// Generate forwarding information tables. For each CPU, use all the
// forward clauses to build a representation of the forwarding network, then
// write Out a dense representation of each network.
void OutputState::generateForwardingInfo() {
  std::vector<std::string> Networks;
  for (auto *Cpu : getSpec().getCpus())
    if (!Cpu->getForwardStmts()->empty()) {
      FwdNetwork Network(Cpu->getMaxFuId() + 1);
      for (auto *Fwd : *Cpu->getForwardStmts())
        expandForwardStmt(Network, Cpu, nullptr, Fwd);
      for (auto *Cluster : *Cpu->getClusters())
        for (auto *Fwd : *Cluster->getForwardStmts())
          expandForwardStmt(Network, Cpu, Cluster, Fwd);
      Networks.push_back(formatForwardingInfo(Cpu, Network));
    }

  if (Networks.empty())
    return;
  outputC() << formatv("{0}// Functional unit forwarding tables.{0}", Divider);

  // Write Out the networks for each processor.
  writeTable(ForwardSets, "int8_t", "[]", &ForwardSetName, "");
  for (auto &Item : Networks)
    outputC() << Item;
}

// Calculate a "resource factor".  This is used by LLVM generic scheduler
// to relate functional unit usage to cycles (unique for each CPU). We
// calculate it here so we don't have to in LLVM.
int CalcResourceFactor(CpuInstance *Cpu) {
  int Factor = std::max(1, Cpu->getMaxIssue());
  for (auto Size : Cpu->getFuPoolSizes())
    Factor = std::lcm(Factor, Size);
  return Factor;
}

// Write Out the top-level CPU table, which contains pointers to instruction
// tables for each CPU.
void OutputState::writeCpuList() const {
  std::string Out, CpuDefs;
  for (auto *Cpu : getSpec().getCpus()) {
    int ExeStage = getSpec().findFirstExecutePhase(Cpu)->getIndex();
    std::string Fwd = Cpu->getForwardStmts()->empty()
                          ? kNull
                          : formatv("&FWD_{0}[0]", Cpu->getName());
    int ResourceFactor = CalcResourceFactor(Cpu);
    auto SUnitsName = Cpu->getInstrCount() ?
                       formatv("&SUNITS_{0}", Cpu->getName()) : kNull;
    CpuDefs +=
        formatv("CpuConfig<CpuParams<{0},{1},{2}, {3},{4}, {5},{6}, "
                "{7},{8},{9},{10}>> CPU_{11}({12},{13},{14},NAMES_{15});\n",
                Cpu->getAllResources().back()->getResourceId(),
                Cpu->getMaxUsedResourceId(), Cpu->getMaxFuId(),
                Cpu->getPoolCount(), Cpu->getMaxPoolAllocation(),
                std::max(1, Cpu->getMaxIssue()), Cpu->getReorderBufferSize(),
                ExeStage, Cpu->getLoadPhase(), Cpu->getHighLatencyDefPhase(),
                Cpu->getMaxResourcePhase(), Cpu->getName(), SUnitsName, Fwd,
                ResourceFactor, Cpu->getName());

    for (const auto &LLVMName : Cpu->getLlvmNames())
      Out += formatv("  {{\"{0}\", &CPU_{1} },\n", LLVMName, Cpu->getName());
  }

  // Write Out CPU configurations for each subtarget in the family.
  outputC() << formatv("{0}// Family CPU Descriptions.\n"
                        "//  CpuParams:\n"
                        "//    - Total number of defined resources\n"
                        "//    - Maximum \"used\" resource id\n"
                        "//    - Maximum functional unit id\n"
                        "//\n"
                        "//    - Number of distinct allocation pools\n"
                        "//    - Largest resource pool allocation size\n"
                        "//\n"
                        "//    - Instruction issue width\n"
                        "//    - Instruction reorder buffer size\n"
                        "//\n"
                        "//    - First execution pipeline phase\n"
                        "//    - Default load phase\n"
                        "//    - \"High-latency instruction\" write phase\n"
                        "//    - Latest resource use pipeline phase"
                        "{0}{1}",
                        Divider, CpuDefs);

  // Write Out the top-level cpu table for this family.
  outputC() << formatv("{0}// Top-level {1} Subtarget Description Table.{0}",
                        Divider, getSpec().getFamilyName());
  outputC() << formatv("CpuTableDict CpuDict = {{\n{0}};\n\n", Out);
  outputC() << formatv("CpuTableDef CpuTable = CpuTableDef(CpuDict);\n");
}

// Open the output files, abort if unable to do that.
void OutputState::openOutputFiles() {
  // Split Out the input filename and directory.
  auto Infile = std::filesystem::path(Database->getFileName());
  std::string DirName = Infile.parent_path();
  std::string BaseName = Infile.stem();
  if (!Database->getDirectoryName().empty())
    DirName = Database->getDirectoryName();

  auto AddSlash = [](std::string PathName) {
    if (!PathName.empty() && PathName.back() != '/')
      PathName += "/";
    return PathName;
  };

  // Open the main database output file.
  FileNameC = formatv("{0}{1}GenMdlInfo.inc", AddSlash(DirName), BaseName);
  OutputC = new std::fstream(FileNameC, std::fstream::out);
  if (!outputC().is_open()) {
    llvm::errs() << formatv("Cannot open output file \"{0}\", aborting\n",
                            FileNameC);
    exit(EXIT_FAILURE);
  }

  // Open the Target library component of the database.
  FileNameT =
      formatv("{0}{1}GenMdlTarget.inc", AddSlash(DirName), BaseName);
  OutputT = new std::fstream(FileNameT, std::fstream::out);
  if (!outputT().is_open()) {
    llvm::errs() << formatv("Cannot open output file \"{0}\", aborting\n",
                            FileNameT);
    exit(EXIT_FAILURE);
  }

  // Open output header file filename.
  FileNameH = formatv("{0}{1}GenMdlInfo.h", AddSlash(DirName), BaseName);
  OutputH = new std::fstream(FileNameH, std::fstream::out);
  if (!outputH().is_open()) {
    llvm::errs() << formatv("Cannot open output file \"{0}\", aborting\n",
                            FileNameH);
    exit(EXIT_FAILURE);
  }
}

// Write Out headers to the C and H output files.
void OutputState::writeHeader() {
  auto Infile = std::filesystem::path(Database->getFileName());
  std::string CpuName = Infile.stem();
  outputC() << formatv("{0}// Machine Description Database.\n"
                        "// This file is auto-generated, do not edit.{1}\n",
                        Divider + 1, Divider);

  if (!GenerateLLVMDefs) {
    outputC() << "#include \"llvm/Support/Mutex.h\"\n";
    outputC() << "#include \"llvm/MC/MDLInfo.h\"\n";
    outputC() << "#include \"llvm/MC/MDLInstrInfo.h\"\n";
    outputC() << "#include \"" + CpuName + "InstrInfo.h\"\n";
  }
  outputC() << formatv("#include \"{0}\"\n\n", FileNameH);

  outputT() << formatv(
      "{0}// Machine Description Database: Target library components\n"
      "// This file is auto-generated, do not edit.{1}",
      Divider + 1, Divider);
  outputT()
      << "// This file contains MDL predicate functions that call Target\n"
         "// library functions. Since MDL lives in MC, and MC is included\n"
         "// in programs that may NOT include the Target library, we need\n"
         "// to virtualize these."
      << Divider;
  outputT() << "#include \"llvm/MC/MDLInfo.h\"\n";
  outputT() << "#include \"llvm/MC/MDLInstrInfo.h\"\n\n";
  outputT() << "#include \"" + CpuName + "InstrInfo.h\"\n";

  outputH() << formatv("#ifndef {0}_MACHINE_DESCRIPTION_DATABASE\n", CpuName);
  outputH() << formatv("#define {0}_MACHINE_DESCRIPTION_DATABASE\n", CpuName);
  outputH() << formatv("{0}// Machine Description Database.\n"
                        "// This file is auto-generated, do not edit.{1}",
                        Divider + 1, Divider);
  outputH() << "#include <map>\n";
  outputH() << "#include <string>\n";
}

void OutputState::writeTrailer() {
  auto Infile = std::filesystem::path(Database->getFileName());
  std::string CpuName = Infile.stem();
  outputH() << formatv("\n#endif  // {0}_MACHINE_DESCRIPTION_DATABASE\n",
                        CpuName);
}

// Write out some global statistics about the spec:
//   - Maximum number of resources (across CPUs, for each CPU).
//   - Maximum resource id used in RefUse operations.
//   - Maximum pipeline phase used by RefUse operations.
//   - Maximum number of instructions that can be issued in parallel.
//   - Maximum number of pools (across CPUs, for each CPU).
//   - Maximum pool allocation size across CPUs.
// NOTE: These are worst-case numbers across all family members, we may
// want to write out CPU-specific versions for compiler performance reasons.
void OutputState::writeSpecDefinitions() {
  MdlSpec &Spec = getSpec();
  int MaxRes = 0;
  int MaxUseRes = 0;
  int MaxPhase = 0;
  int MaxIssue = 0;
  int MaxPools = 0;
  int MaxPoolAlloc = 0;

  // Find that absolute worst-case pipeline phase.
  int MaxPhaseWorstCase = 0;
  for (auto *Pipe : Spec.getPipePhases())
    MaxPhaseWorstCase =
      std::max(MaxPhaseWorstCase, Pipe->getPhaseNames()->back()->getIndex());

  // Find maximum number of resources across CPUs.
  for (auto *Cpu : Spec.getCpus())
    MaxRes = std::max(MaxRes, Cpu->getAllResources().back()->getResourceId());

  // Find maximum number of pools across CPUs.
  for (auto *Cpu : Spec.getCpus())
    MaxPools = std::max(MaxPools, Cpu->getPoolCount());

  // Find the maximum pool allocation size across CPUs.
  for (auto *Cpu : Spec.getCpus()) {
    int CpuMaxAlloc = 0;
    for (auto *Def : Cpu->getPoolResources())
      if (!Def->getAllocSizes().empty())
        CpuMaxAlloc = std::max(CpuMaxAlloc, *Def->getAllocSizes().rbegin());
    Cpu->setMaxPoolAllocation(CpuMaxAlloc);
    MaxPoolAlloc = std::max(MaxPoolAlloc, CpuMaxAlloc);
  }

  // Find maximum resource use phase for each CPU.
  // Also find the maximum resource id used in RefUse rules (for all CPUs).
  for (auto *Cpu : Spec.getCpus()) {
    int MaxCpuPhase = 0;
    int MaxCpuUseRes = 0;
    for (auto *Res : Cpu->getAllResources())
      if (Res->getRefTypes() & (RefTypes::kUse | RefTypes::kFus)) {
        if (Res->phaseExprSeen())
          MaxCpuPhase = MaxPhaseWorstCase;
        else
          MaxCpuPhase = std::max(MaxCpuPhase, Res->getLatestRef());

        // If referencing a group or pool, we need to note all the pool or
        // group members as explicitly used.
        if (Res->isGroupDef()) {
          for (auto *Member : Res->getMemberDefs())
            MaxCpuUseRes =
                std::max(MaxCpuUseRes, Member->getResourceId());
        } else {
          int ResId = Res->getResourceId();
          if (Res->getPoolSize() > 0)
            ResId += Res->getPoolSize() - 1;
          MaxCpuUseRes = std::max(MaxCpuUseRes, ResId);
        }
      }

    Cpu->setMaxResourcePhase(MaxCpuPhase);
    MaxPhase = std::max(MaxPhase, MaxCpuPhase);

    Cpu->setMaxUsedResourceId(MaxCpuUseRes);
    MaxUseRes = std::max(MaxUseRes, MaxCpuUseRes);
  }

  // Find maximum issue size for each CPU.
  // Count issue slots for each cluster. If a cluster doesn't have issue
  // slots, count functional unit instantiations.
  for (auto *Cpu : Spec.getCpus()) {
    int Issue = 0;
    for (auto *Clst : *Cpu->getClusters())
      Issue += !Clst->getIssues()->empty() ? Clst->getIssues()->size()
                                        : Clst->getFuncUnits()->size();

    if (Issue == 0) Issue = MCSchedModel::DefaultIssueWidth;
    Cpu->setMaxIssue(Issue);
    MaxIssue = std::max(MaxIssue, Issue);
  }

  // Find the earliest pipeline phase referenced by name in a "use" clause
  // for each CPU.
  for (auto &[InstrName, InfoList] : Database->getInstructionInfo()) {
    for (auto *Info : InfoList) {
      auto *Cpu = Info->getSubunit()->getFuncUnit()->getCpu();
      int MinUse = Cpu->getEarlyUsePhase();
      for (auto *Ref : *Info->getReferences()) {
        if (Ref->getOperand() && Ref->getRefType() == RefTypes::kUse) {
          if (auto *Phase = Ref->getPhaseExpr()->getPhaseName()) {
            if (MinUse == -1)
              MinUse = Phase->getIndex();
            else
              MinUse = std::min(MinUse, Phase->getIndex());
          }
        }
      }
      Cpu->setEarlyUsePhase(MinUse);
    }
  }
  outputH() << formatv("\nnamespace llvm {{\nnamespace {0} {{\n",
                        Spec.getFamilyName());

  // Use globally default values for undefined attributes.
  outputH() << formatv("{0}// Global constant definitions{0}", Divider);
  outputH() << formatv("const int kMaxResourceId = {0};\n", MaxRes);
  outputH() << formatv("const int kMaxUsedResourceId = {0};\n", MaxUseRes);
  outputH() << formatv("const int kMaxPipePhase = {0};\n", MaxPhase);
  outputH() << formatv("const int kMaxIssue = {0};\n", MaxIssue);
  outputH() << formatv("const int kMaxPools = {0};\n", MaxPools);
  outputH() << formatv("const int kMaxPoolCount = {0};\n", MaxPoolAlloc);
}

// Write out definitions we expect LLVM tablegen to create:
//    - instruction ids.
//    - register ids.
//    - register class ids.
// This function will be unnecessary after integration with LLVM.
void OutputState::writeLLVMDefinitions() {
  if (!GenerateLLVMDefs)
    return;

  MdlSpec &Spec = getSpec();
  int Id = 0;
  outputH() << formatv("{0}// LLVM Instruction defs{0}", Divider);
  outputH() << "  enum {\n";

  for (auto *Instr : Spec.getInstructions()) {
    outputH() << formatv("    {0},  // {1}\n", Instr->getName(), Id++);
    if (Instr->getDerived())
      for (auto *DInstr : *Instr->getDerived())
        outputH() << formatv("    {0},  // {1}\n", DInstr->getName(), Id++);
  }
  outputH() << formatv("    INSTRUCTION_LIST_END,  // {0}\n", Id++);

  outputH() << formatv("{0}// Register defs{0}", Divider);
  for (auto *Reg : Spec.getRegisters())
    outputH() << formatv("  {0},  // {1}\n", Reg->getName(), Id++);

  outputH() << formatv("{0}// Register class def{0}", Divider);
  for (auto *RegClass : Spec.getRegClasses())
    outputH() << formatv("  {0}RegClassId,  // {1}\n", RegClass->getName(),
                          Id++);

  outputH() << formatv("\n  }; // enum\n");
}

// Format the fully-qualified C++ name of a resource.
std::string ResourceCppName(MdlSpec &Spec, CpuInstance *Cpu,
                         ClusterInstance *Cluster, ResourceDef *Res) {
  std::string Name = formatv("{0}::Mdl::{1}::", Spec.getFamilyName(),
                             Cpu->getName());
  if (Cluster && !Cluster->isNull())
    Name += formatv("{0}::", Cluster->getName());
  Name += Res->getName();
  return Name;
}

// Format a debug name for a used resource.
std::string ResourceName(MdlSpec &Spec, ClusterInstance *Cluster,
                         ResourceDef *Res) {
  std::string Name;
  if (Cluster && !Cluster->isNull())
    Name += formatv("{0}:", Cluster->getName());

  if (auto *Instance = Res->getFu())
    Name += formatv("{0}:", Instance->getFuncType()->getName());

  return Name + Res->getName();
}

void SaveResourceName(ResourceDef *Res, std::string Name,
                      std::vector<std::string> &ResNames) {
  int Id = Res->getResourceId();
  if (Res->getPoolSize() <= 1)
    ResNames[Id] = Name;
  else
    for (int I = 0; I < Res->getPoolSize(); I++)
      ResNames[Id + I] = formatv("{0}[{1}]", Name, I);
}

// Write out a single resource definition.
void AddResourceDef(std::string *Out, MdlSpec &Spec, CpuInstance *Cpu,
                    ClusterInstance *Cluster, ResourceDef *Res,
                    std::string note, std::vector<std::string> &ResNames) {
  std::string prefix = "  ";
  if (Cluster && !Cluster->isNull())
    prefix += "  ";

  // Note: we don't need to print out groups.
  if (!Res->isGroupDef()) {
    auto Name = ResourceCppName(Spec, Cpu, Cluster, Res);
    *Out += formatv("{0}  const int {1} = {2};      // {3} ({4})\n", prefix,
                    Res->getName(), Res->getResourceId(), Name, note);
    SaveResourceName(Res, ResourceName(Spec, Cluster, Res), ResNames);
  }
}

// Write out definitions for functional unit and issue slot resources.
// We also capture the names of each resource and create a table of them
// indexed by id.
void OutputState::writeResourceDefinitions() {
  MdlSpec &Spec = getSpec();
  std::string Out;
  outputC() << formatv("{0}// Resource name tables{0}", Divider);

  for (auto *Cpu : Spec.getCpus()) {
    std::vector<std::string> ResNames;
    ResNames.resize(Cpu->getMaxResourceId() + 1);

    Out += formatv("{0}// Resource Definitions for {1}{0}", Divider,
                   Cpu->getName());
    Out += formatv("  namespace {0} {{\n", Cpu->getName());
    for (auto *Res : *Cpu->getResources())
      AddResourceDef(&Out, Spec, Cpu, nullptr, Res, "resource", ResNames);

    // Write out C++ definitions for resources associated with a cluster.
    // Note that we DON'T write definitions for resources associated with
    // functional unit instances. (Its messy dealing with resource scoping).
    for (auto *Cluster : *Cpu->getClusters()) {
      if (!Cluster->isNull())
        Out += formatv("\n    namespace {0} {{\n", Cluster->getName());

      // Write out functional unit resource definitions. We only write names
      // for top-level functional unit resources, and don't write out
      // catchall units. We also add functional unit instance resources to the
      // name table (even though we don't write out definitions).
      for (auto *Fu : Cluster->getFuInstantiations()) {
        if (Fu->getParent() == nullptr && Fu->getFuResource()->isUsed() &&
            !Fu->getInstance()->isCatchallUnit()) {
          auto *Res = Fu->getFuResource();
          AddResourceDef(&Out, Spec, Cpu, Cluster, Res, "func unit", ResNames);
        }
        for (auto *Res : Fu->getResources())
          if (!Res->isGroupDef())
            SaveResourceName(Res, ResourceName(Spec, Cluster, Res), ResNames);
      }

      for (auto *Issue : *Cluster->getIssues())
        AddResourceDef(&Out, Spec, Cpu, Cluster, Issue, "issue", ResNames);
      for (auto *Res : *Cluster->getResources())
        AddResourceDef(&Out, Spec, Cpu, Cluster, Res, "resource", ResNames);
      if (!Cluster->isNull())
        Out += formatv("    }  // namespace {0}\n", Cluster->getName());
    }
    Out += formatv("  }  // namespace {0}\n", Cpu->getName());

    // Write out resource names for this CPU.
    std::string Names;
    for (unsigned idx = 1; idx < ResNames.size(); idx++)
      Names += "\"" + ResNames[idx] + "\",";
    if (!Names.empty()) Names.pop_back();
    outputC() << formatv("std::string NAMES_{0}[] = {{\"\",{1}};\n",
                          Cpu->getName(), Names);
  }

  outputH() << "\nnamespace Mdl {\n";
  outputH() << Out;
  outputH() << "}  // namespace Mdl\n";
}

// Write external definitions to the output header file.
void OutputState::writeExterns() {
  std::string Family = getSpec().getFamilyName();
  outputH() << formatv("{0}// External definitions{0}", Divider);
  if (GenerateLLVMDefs) {
    outputH() << "using InstructionNameMap = std::map<int, std::string>;\n";
    outputH() << formatv("extern InstructionNameMap InstructionNames;\n",
                          Family);
  }
  outputH() << formatv("extern llvm::mdl::CpuTableDef CpuTable;\n\n", Family);
  outputH() << formatv("}  // namespace {0}\n}  // namespace llvm\n", Family);
}

// Top level function for writing out the machine description.
void OutputState::writeCpuTable() {
  for (auto [InstrName, Info] : Database->getInstructionInfo())
    for (auto *Cpu : getSpec().getCpus())
      formatSubunits(InstrName, Info, Cpu->getName());

  // Collect and write out overall spec parameters after processing all the
  // CPUs' subunits.
  writeSpecDefinitions();

  outputC() << formatv("\nnamespace llvm {{\nnamespace {0} {{\n",
                        getSpec().getFamilyName());
  outputT() << formatv("\nnamespace llvm {{\nnamespace {0} {{\n",
                        getSpec().getFamilyName());
  outputC() << formatv("using namespace ::llvm::mdl;\n");
  outputT() << formatv("using namespace ::llvm::mdl;\n");
  outputC() << formatv("constexpr auto {0} = nullptr;\n", kNull);

  // Generate some constants for reference types.
  outputC() << "constexpr auto RefPred    = ReferenceTypes::RefPred;\n";
  outputC() << "constexpr auto RefUse     = ReferenceTypes::RefUse;\n";
  outputC() << "constexpr auto RefDef     = ReferenceTypes::RefDef;\n";
  outputC() << "constexpr auto RefKill    = ReferenceTypes::RefKill;\n";
  outputC() << "constexpr auto RefUseDef  = ReferenceTypes::RefUseDef;\n";
  outputC() << "constexpr auto RefHold    = ReferenceTypes::RefHold;\n";
  outputC() << "constexpr auto RefReserve = ReferenceTypes::RefReserve;\n";
  outputC() << "constexpr auto RefFus     = ReferenceTypes::RefFus;\n";

  writePredicateFunctions(ReferencePredicates, &PredicateName,
                          "Predicate functions", outputC());
  writePredicateFunctions(VirtualRefPredicates, &VirtualPredicateName,
                          "Virtual predicate functions", outputT());
  writeVirtualPredicateTable(VirtualRefPredicates);

  writePhases(Phases, &PhaseName);
  writePoolCountFunctions(PooledCountFunctions, &PooledCountFuncName);
  writePoolValueFunctions(PoolMaskFunctions, &PoolValueFuncName);

  // Write out forward references for conditional references.
  outputC() << formatv(
      "{0}// Forward references for conditional references{0}", Divider);
  for (auto Name : ForwardOpndRefs)
    outputC() << formatv("extern OperandRefVec {0};\n", Name);
  if (!ForwardOpndRefs.empty())
    outputC() << "\n";
  for (auto Name : ForwardCondOpndRefs)
    outputC() << formatv("extern ConditionalRef<OperandRef> {0};\n", Name);
  if (!ForwardCondOpndRefs.empty())
    outputC() << "\n";

  for (auto Name : ForwardResourceRefs)
    outputC() << formatv("extern ResourceRefVec {0};\n", Name);
  if (!ForwardResourceRefs.empty())
    outputC() << "\n";
  for (auto Name : ForwardCondResRefs)
    outputC() << formatv("extern ConditionalRef<ResourceRef> {0};\n", Name);
  if (!ForwardCondResRefs.empty())
    outputC() << "\n";

  for (auto Name : ForwardPooledRefs)
    outputC() << formatv("extern PooledResourceRefVec {0};\n", Name);
  if (!ForwardPooledRefs.empty())
    outputC() << "\n";
  for (auto Name : ForwardCondPoolRefs)
    outputC() << formatv("extern ConditionalRef<PooledResourceRef> {0};\n",
                          Name);
  if (!ForwardCondPoolRefs.empty())
    outputC() << "\n";

  for (auto Name : ForwardConstraintRefs)
    outputC() << formatv("extern OperandConstraintVec {0};\n", Name);
  if (!ForwardConstraintRefs.empty())
    outputC() << "\n";
  for (auto Name : ForwardCondConstraintRefs)
    outputC() << formatv("extern ConditionalRef<OperandConstraint> {0};\n",
                          Name);
  if (!ForwardCondConstraintRefs.empty())
    outputC() << "\n";

  outputC()
      << Divider
      << "// Conditional Reference Tables:\n"
         "//     - Predicate function (optional)\n"
         "//     - References (operands, resource or pooled resource refs\n"
         "//     - \"Else\" clause conditional reference (optional)"
      << Divider;

  writeTable(CondOperandRefs, "ConditionalRef<OperandRef>", "",
             &CondReferenceName, "Conditional Operand Reference Table");
  writeTable(CondResourceRefs, "ConditionalRef<ResourceRef>", "",
             &CondResourceReferenceName,
             "Conditional Resource Reference Table");
  writeTable(CondPooledResourceRefs, "ConditionalRef<PooledResourceRef>",
             "", &CondPooledResourceReferenceName,
             "Conditional Pooled Resource Reference Table");
  writeTable(CondConstraints, "ConditionalRef<OperandConstraint>", "",
             &CondConstraintName, "Conditional Constraints Table");

  writeVectorTable(OperandRefs, "OperandRef", &OperandListName,
                   "Operand Reference Table",
                   "\n//     - Resource type(use, def, cond)\n"
                   "//     - Reference flags (protected, unprotected)\n"
                   "//     - Pipeline phase\n"
                   "//     - Pipeline phase function (optional)\n"
                   "//     - Operand index\n"
                   "//   or (for conditional references)\n"
                   "//     - Conditional reference");

  outputC()
      << Divider
      << "// Resource Reference Tables:\n"
         "//     - Reference type (use, hold, reserve)\n"
         "//     - Reference flags (protected, unprotected, reserved)\n"
         "//     - Pipeline phase\n"
         "//     - Pipeline phase function (optional)\n"
         "//     - Used cycles\n"
         "//     - Resource id\n"
         "//     - Operand index (for shared resources)\n"
         "//     - Width in bits (for shared resources)\n"
         "//   or (for functional unit descriptors)\n"
         "//     - Reference type (fus)\n"
         "//     - Reference flags (reserved, buffered, begin_group, ...)\n"
         "//     - Used cycles\n"
         "//     - Resource id\n"
         "//     - Number of MicroOps\n"
         "//   or (for \"unitless\" micro-ops)\n"
         "//     - Reference type (fus)\n"
         "//     - Number of MicroOps\n"
         "//   or (for conditional references)\n"
         "//     - Conditional reference"
      << Divider;

  writeVectorTable(UsedResourceRefs, "ResourceRef", &UsedResourceListName,
                   "Used Resource Reference Table");
  writeVectorTable(HeldResourceRefs, "ResourceRef", &HeldResourceListName,
                   "Held Resource Reference Table");
  writeVectorTable(ReservedResourceRefs, "ResourceRef",
                   &ReservedResourceListName,
                   "Reserved Resource Reference Table");

  writeTable(ResourceGroups, "ResourceIdType", "[]", &ResourceGroupName,
             "Resource Group Table");

  auto PoolDescriptorTable =
      "\n"
      "//      pool_id -    which pool to allocate from\n"
      "//      pool_size -  how many different allocations sizes\n"
      "//      count -      how many pool elements we need\n"
      "//      pool_func -  optional pointer to pool size func\n"
      "//      value_func - optional pointer to pool values func\n"
      "//      first -      index of first legal element id\n"
      "//      last -       index of last legal element id\n"
      "//      width -      width in bits";

  writeTable(PoolDescriptors, "PoolDescriptor", "", &PoolDescriptorName,
             "Pool Descriptor Table", PoolDescriptorTable);

  auto PooledRefsHeader =
      "\n"
      "//     - Resource type (use, hold, reserve)\n"
      "//     - Reference flags (protected, unprotected, reserved\n"
      "//     - Pipeline phase\n"
      "//     - Pipeline phase function (optional)\n"
      "//     - Used cycles\n"
      "//     - Resource group\n"
      "//     - Operand index (for shared resources)\n"
      "//     - Pool descriptor\n"
      "//   or (for functional unit descriptors)\n"
      "//     - Reference type (fus)\n"
      "//     - Reference flags (reserved, buffered, begin_group, ...)\n"
      "//     - Used cycles\n"
      "//     - Group id\n"
      "//     - Pool id\n"
      "//     - Number of MicroOps";

  writeVectorTable(PooledResourceRefs, "PooledResourceRef",
                   &PooledResourceListName, "Pooled Resource Reference Table",
                   PooledRefsHeader);

  auto ConstraintTableHeader = "\n"
                                 "//     - Operand Index\n"
                                 "//     - Constraint id\n"
                                 "//   or (for conditional reference)\n"
                                 "//     - Conditional constraint name\n";

  writeVectorTable(Constraints, "OperandConstraint", &ConstraintListName,
                   "Operand Constraint Table", ConstraintTableHeader);

  writeVectorTable(Subunits, "Subunit", &SubunitListName, "Subunit Table");

  writeInstructionTables();
  writeInstructionInfo();
  generateForwardingInfo();

  writeResourceDefinitions();      // Write out resource names
  writeCpuList();

  outputC() << formatv("}  // namespace {0}\n}  // namespace llvm\n\n",
                        getSpec().getFamilyName());
  outputT() << formatv("}  // namespace {0}\n}  // namespace llvm\n\n",
                        getSpec().getFamilyName());
}

} // namespace mdl
} // namespace mpact

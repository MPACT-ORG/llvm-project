//===- mdl_generate.cpp - Generate the mdl database -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These functions perform the second half of the architecture expansion,
// where we specialize each generated subunit instance for each instruction.
// From this, we can generate the internal representation of the complete
// database for the spec.
//
// The general algorithm for this is:
//
//    for each llvm instruction description:
//       for each subunit it qualifies for:
//          for each specialized instance of that subunit:
//             further specialize the subunit for the current instruction
//             add the final specialized subunit to the instruction definition
//
// After this pass, each instruction will have a set of subunit instances,
// each of which has the following information:
//   - a cpu and functional unit combination the instruction can run on,
//   - the resources it uses (on that cpu/functional unit), and when,
//   - any resource pool requirements for the instruction,
//   - the latencies of all operand reads and writes,
//   - any CPU and/or Functional unit register constraints imposed on the ins.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "mdl_output.h"

namespace mpact {
namespace mdl {

// Check that each operand in the insteuction is mentioned in at least
// one reference record. Unmentioned operands are a likely error.
void InstrInfo::checkUnreferencedOperands(bool CheckAllOperands) {
  std::set<int> ReferencedOperands;
  for (auto *Ref : *References)
    if (Ref->getOperand())
      ReferencedOperands.insert(Ref->getOperand()->getOperandIndex());

  for (int OpId = 0; OpId < Instruct->getNumFlatOperands(); OpId++)
    if ((CheckAllOperands ||
         Instruct->getOperandDecl(OpId)->getRegClass()) &&
        !ReferencedOperands.count(OpId))
      Subunit->WarningLog(
          Instruct, "Operand{0}in instruction \"{1}\" is unreferenced",
          StringVec(Instruct->getOperandDecl(OpId)->getOpNames(),
                    " \"", ".", "\" "), Instruct->getName());
}

// Stringify a InstrInfo object, suitable for writing debug information.
std::string InstrInfo::ToString() const {
  std::string Out = formatv("{0}    Subunit: {1}.{2}\n", Instruct->ToString(),
                            Subunit->getFuncUnit()->getCpu()->getName(),
                            Subunit->getFuncUnit()->getInstance()->getName());

  Out += "      Operand references:\n";
  for (auto *Ref : *References)
    if ((Ref->getOperand() && Ref->isOperandRefType()) ||
         Ref->isConditionalRef())
      Out += formatv("      ===>  {0}\n", Ref->ToString());

  if (!ResourceRefs.empty())
    Out += "      FU references:\n";
  for (auto *Ref : ResourceRefs)
    if (Ref->isFuncUnitRef())
      Out += formatv("      --->  {0}\n", Ref->ToString());

  if (Resources.empty())
    return Out;
  Out += "      Resources:\n";
  for (auto &Res : Resources)
    if (!Res.getResource()->hasCount())
      Out += formatv("            {0}\n", Res.ToString());

  Out += "      Pool Resources:\n";
  for (auto &Res : Resources)
    if (Res.getResource()->hasCount()) {
      Out += formatv("            {0} ", Res.ToString());
      SubPool Subpool(Res.getResource());
      auto &SpInfo = Res.getResource()->getDefinition()->getSubPool(Subpool);
      Out += formatv(" subpool id: {0}", SpInfo.getSubpoolId());
      Out += " size requests: ";
      for (auto Request : SpInfo.getCounts())
        Out += formatv("{0},", Request);
      Out.pop_back();      // delete trailing comma
      Out += "\n";
    }

  Out += "      Architectural Register Constraints:\n";
  for (auto *Ref : *References)
    if (auto *Opnd = Ref->getOperand())
      if (auto *Port = Ref->getPort())
        if (auto *RegClass = Port->getRegClass())
          Out += formatv("            operand {0}: {1}\n",
                         Opnd->getOperandIndex(), RegClass->getName());
  return Out;
}

//----------------------------------------------------------------------------
// Find all functional-unit-related resources associated with this subunit
// instance, including the implied functional unit resource (and its bases).
// Note that we don't include any explicit functional units specified by an
// fus() clause, these are handled separately.
//----------------------------------------------------------------------------
void getFuncUnitResources(SubUnitInstantiation *Subunit, ResourceSets &ResSet,
                          PhaseName *Phase) {
  // Add all the implicit functional unit resources, including parent fu ids.
  // Note: Each fu is added as an independent resource in the res set.
  auto fu_resources = Subunit->getFuncUnitResources();
  for (auto *def : fu_resources) {
    // Don't write out catchall units or unreserved functional units.
    if (isCatchallName(def->getName()) ||
        def->getFu()->getInstance()->isUnreserved())
      continue;
    auto *Fu = new ResourceRef(def);
    ResourceEvent FuRes(RefTypes::kUse, new PhaseExpr(Phase), 1, Fu);
    std::vector<ResourceEvent> Items{FuRes};
    ResSet.push_back(Items);
  }
}

//----------------------------------------------------------------------------
// Build a resource set for this instruction instance.
//----------------------------------------------------------------------------
ResourceSets
InstructionDatabase::buildResourceSets(ResourceList &Resources,
                                       SubUnitInstantiation *Subunit) {
  ResourceSets ResSet;
  for (auto &Item : Resources) {
    std::vector<ResourceEvent> Items;

    // Resource pools can be managed along with "Normal" resources, or
    // separately. Its more computationally expensive at compile time to manage
    // them with other resources ((N*P)^2 vs (N^2 + P^2)), but vastly more
    // convenient to consider them together.  So if N+P is "small enough", we
    // should just add pooled resources in with the normal resources (which the
    // code below will accomplish).  But currently I don't think thats typically
    // the case, so for now I've just commented this (working) code out.  We'll
    // get much fewer resource sets, and have to model all pools in the compiler
    // at compile time (which will typically be much faster).
    // TODO(tbd) - Determine what kinds of pools this makes sense for.
#if 0
    // Handle resource pools.
    ResourceRef *Ref = Item.getResource();
    if (Ref->IsPool() && !has_shared_bits() &&
        (Ref->getLast() - Ref->getFirst()) < 4  &&
        (Ref->HasCount() == 1 || Ref->getOperandIndex() == -1)) {
      for (int Id = Ref->getFirst(); Id <= Ref->getLast();
           Id += Ref->getPoolCount()) {
        auto *Newref = new ResourceRef(*Ref);
        Newref->setSubrange(Id, Id + Ref->pool_count() - 1);
        Newref->setPoolCount(-1);
        ResourceEvent Newevent(Item.getType(), Item.getPhaseExpr(), Newref);
        Items.push_back(Newevent);
      }
      ResSet.push_back(Items);
      continue;
    }
#endif
#if 0
    // Handle resource groups.
    if (Item.getResource()->IsGroupRef() && !hasSharedBits() &&
        Item.getResource()->getDefinition()->getMembers().size() < 4) {
      for (auto Mem : *Item.getResource()->definition()->member_defs()) {
        auto *Newref = new ResourceRef(*Mem);
        ResourceEvent Newevent(Item.Type(), Item.phase_expr(), Newref);
        Items.push_back(Newevent);
      }
      ResSet.push_back(Items);
      continue;
    }
#endif

    // Handle single resource items.
    Items.push_back(Item);
    ResSet.push_back(Items);
  }

  // We need to identify a "default" phase to use for implicit resources.
  auto *Phase0 = getSpec().findFirstPhase();

  // Add all the implicit functional unit resources, including parent fu ids.
  // Note: Each fu is added as an independent resource in the res set.
  getFuncUnitResources(Subunit, ResSet, Phase0);

  // If we determined we don't need to model resource slots, we're done.
  auto *Cpu = Subunit->getCpu();
  if (!Cpu->needsSlotResources())
    return ResSet;

  // Add "any" slot resources associated with this functional unit instance.
  // Note: we add all of them as a single pooled entry in the res_set.
  auto *SlotsAny = Subunit->getSlotResourcesAny();
  if (SlotsAny && !SlotsAny->empty()) {
    std::vector<ResourceEvent> Items;
    for (auto *PinAny : *SlotsAny) {
      auto *Slot = new ResourceRef(*PinAny);
      auto *Phase = Phase0;
      if (Slot->getDefinition()->getStartPhase())
        Phase =
           getSpec().findPipeReference(Slot->getDefinition()->getStartPhase(),
                                       Cpu);
      Items.emplace_back(RefTypes::kUse, new PhaseExpr(Phase), Slot);
    }
    ResSet.push_back(Items);
  }

  // Add "all" slot resources associated with this functional unit instance.
  // Note: Each slot is added as an independent resource in the res_set.
  // TODO: we might want to try just doing pooled allocations for these - all
  // the mechanism exists, we just need to create a pooled allocation
  // ResourceEvent, and the backend does the rest.
  auto *SlotsAll = Subunit->getSlotResourcesAll();
  if (SlotsAll && !SlotsAll->empty()) {
    for (auto *PinAll : *SlotsAll) {
      auto *Slot = new ResourceRef(*PinAll);
      auto *Phase = Phase0;
      if (Slot->getDefinition()->getStartPhase())
        Phase =
           getSpec().findPipeReference(Slot->getDefinition()->getStartPhase(),
                                       Cpu);
      ResourceEvent PinRes(RefTypes::kUse, new PhaseExpr(Phase), Slot);
      std::vector<ResourceEvent> Items{PinRes};
      ResSet.push_back(Items);
    }
  }

  return ResSet;
}

//----------------------------------------------------------------------------
// Build a set of all possible resource combinations found in the input
// resource set.
//----------------------------------------------------------------------------
void buildResourceCombos(ResourceSets &ResSet, unsigned Index,
                         std::vector<ResourceEvent> &Current,
                         ResourceSets &Result) {
  if (Index == ResSet.size()) {
    Result.push_back(Current);
    return;
  }

  for (auto &resource : ResSet[Index]) {
    Current.push_back(resource);
    buildResourceCombos(ResSet, Index + 1, Current, Result);
    Current.pop_back();
  }
}

//----------------------------------------------------------------------------
// Annotate phase expressions with instruction-specific operand information.
//----------------------------------------------------------------------------
void annotatedPhaseExpr(const InstructionDef *Instr, PhaseExpr *Expr,
                        MdlSpec &Spec, CpuInstance *Cpu) {
  if (Expr->getOperation() == kOpnd) {
    int Index = Spec.getOperandIndex(Instr, Expr->getOperand(), RefTypes::kUse);
    Expr->getOperand()->setOperandIndex(Index);
    Expr->getOperand()->setOperandDecl(Instr->getOperandDecl(Index));
    return;
  }
  if (Expr->getOperation() == kPhase) {
    Spec.specializePhaseExpr(Expr, Cpu);
    return;
  }
  if (Expr->getLeft())
    annotatedPhaseExpr(Instr, Expr->getLeft(), Spec, Cpu);
  if (Expr->getRight())
    annotatedPhaseExpr(Instr, Expr->getRight(), Spec, Cpu);
}

//----------------------------------------------------------------------------
// Annotate a reference with instruction-specific operand information.
//----------------------------------------------------------------------------
Reference *annotatedReference(const InstructionDef *Instr, Reference *Ref,
                              int Delay, MdlSpec &Spec, CpuInstance *Cpu) {
  auto *Newref = new Reference(*Ref, Delay); // Make a private copy.
  Ref->setUsed(); // Note that we used this reference.
  if (auto *Opnd = Newref->getOperand()) {
    int Index = Spec.getOperandIndex(Instr, Opnd, Ref->getRefType());
    Opnd->setOperandIndex(Index);
    Opnd->setOperandDecl(Instr->getOperandDecl(Index));
  }

  annotatedPhaseExpr(Instr, Newref->getPhaseExpr(), Spec, Cpu);
  Newref->setConstantPhase(); // Evaluate constant phase expressions.
  return Newref;
}

// Return true if two register class have any common registers.
bool doClassesOverlap(const RegisterDefList *a, const RegisterDefList *b) {
  for (auto *item_a : *a)
    for (auto *item_b : *b)
      if (item_a->getName() == item_b->getName())
        return true;
  return false;
}

// Return true if any of any instruction's port constraints are incompatible
// with operand constraints.  This is a nice optimization to prune subunits
// whose port constraints are incompatible with an instruction's operand
// constraints.  It is ok to be conservative. We skip conditional references
// since the predicates could impact whether a reference is used or not.
bool hasIncompatibleConstraints(const ReferenceList *References) {
  for (const auto *Ref : *References)
    if (!Ref->isConditionalRef())
      if (const auto *Opnd = Ref->getOperand())
        if (const auto OpDecl = Opnd->getOperandDecl())
          if (const auto *OpClass = OpDecl->getRegClass())
            if (const auto *Port = Ref->getPort())
              if (const auto *PClass = Port->getRegClass())
                if (!doClassesOverlap(PClass->getMembers(),
                                     OpClass->getMembers()))
                  return true;
  return false;
}

// Filter conditional references, and return a filtered copy.
// If there's no predicate, just filter the refs (this is an "else" clause).
// if there's a predicate, evaluate it:
//   - if its true, filter and return its associated reference list.
//   - if its false, filter and return its "else" clause (if there is one)
//   - if it cannot be fully evaluated, create a copy with filtered references
//     and else clause.
ConditionalRef *InstructionDatabase::filterConditionalRef(
    const InstructionDef *Instr, ConditionalRef *Cond, CpuInstance *Cpu) {
  auto &Spec = getSpec();
  // If the entire clause is missing, just return nullptr.
  if (Cond == nullptr)
    return nullptr;
  auto *Pred = Cond->getPredicate();
  // If the predicate is missing, it evaluates to True, so return filtered refs.
  if (Pred == nullptr)
    return new ConditionalRef(
        *Cond, nullptr, filterReferences(Instr, Cond->getRefs(), Cpu), nullptr);

  // Evaluate the predicate, and if it is True or False, we can simplify.
  auto *Value = Spec.evaluatePredicate(Pred->getName(), Instr);
  if (Value->isTrue())
    return new ConditionalRef(
        *Cond, nullptr, filterReferences(Instr, Cond->getRefs(), Cpu), nullptr);
  if (Value->isFalse())
    return filterConditionalRef(Instr, Cond->getElseClause(), Cpu);

  // If we can't completely evaluate the predicate, create a copy, filter its
  // references, and recur on its else clause.
  auto *ThenClause = filterReferences(Instr, Cond->getRefs(), Cpu);
  auto *ElseClause = filterConditionalRef(Instr, Cond->getElseClause(), Cpu);
  auto *NewCond = new ConditionalRef(*Cond, Pred, ThenClause, ElseClause);
  NewCond->setInstrPredicate(Value);
  return NewCond;
}

// Given a list of references, determine if each reference is valid for the
// specified instruction.  Return a filtered list of references.
ReferenceList *InstructionDatabase::filterReferences(
    const InstructionDef *Instr, ReferenceList &Candidates, CpuInstance *Cpu) {
  auto *Refs = new ReferenceList;

  for (auto *Ref : Candidates) {
    Ref->setSeen(); // Note that we've seen a possible reference to this.

    // If it's not a conditional reference, check to see if its valid for
    // this instruction, and if it is valid add to the reference list.
    // Expand WriteSequences to discrete references.
    if (!Ref->isConditionalRef()) {
      if (isReferenceValid(Instr, Ref)) {
        Refs->push_back(annotatedReference(Instr, Ref, 0, Spec, Cpu));
        int Delay = Ref->getDelay();
        for (int Repeat = 1; Repeat < Ref->getRepeat(); Repeat++) {
          Refs->push_back(annotatedReference(Instr, Ref, Delay, Spec, Cpu));
          Delay += Ref->getDelay();
        }
      }
      continue;
    }

    // Recursively filter each conditional reference.  If the result is a
    // single unconditional ConditionalRef object, just add all of its
    // references to the list. Otherwise, add the conditional reference to
    // the list.
    auto *Cond = filterConditionalRef(Instr, Ref->getConditionalRef(), Cpu);
    if (Cond == nullptr)
      continue;
    if (Cond->getPredicate() != nullptr)
      Refs->push_back(new Reference(*Cond, nullptr, Cond));
    else
      Refs->insert(Refs->end(), Cond->getRefs().begin(), Cond->getRefs().end());
  }

  return Refs;
}

//----------------------------------------------------------------------------
// If there are unreferenced output operands in an instruction, we need at
// least one Def in the database to use for the default latency.  This is
// a hack to handle cases where LLVM has unmentioned defs or dynamically adds
// defs to an instruction instance. Scan a reference list looking for the
// kFu with the largest latency, and create a default def for that list.
// These will provide the compiler with "default" def phases.
// Return the number of default defs inserted.
//----------------------------------------------------------------------------
int AddDefaultDefs(ReferenceList &Refs, CpuInstance *Cpu, MdlSpec &Spec) {
  ReferenceList defs;
  int Count = 0;

  // Scan conditional reference list for defs.
  for (auto *Ref : Refs) {
    for (auto *Cond = Ref->getConditionalRef(); Cond;
         Cond = Cond->getElseClause())
      Count += AddDefaultDefs(Cond->getRefs(), Cpu, Spec);
  }

  // Scan the references looking for the latest Def or Use. If no defs are
  // found, add a default def.
  Reference *Latest = nullptr;
  Reference *LatestDef = nullptr;
  int LatestLatency = -1;

  for (auto *Ref : Refs) {
    if (Ref->isDef() || Ref->isUse()) {
      if (Ref->getPhaseExpr()->isExpressionConstant()) {
        auto Latency = Ref->getPhaseExpr()->evaluateConstantExpression();
        if (!Latency) {
          Spec.ErrorLog(Ref, "Invalid latency: divide by zero");
          continue;
        }
        if (Latest == nullptr || *Latency > LatestLatency ||
            (*Latency == LatestLatency && Latest->isDef())) {
          Latest = Ref;
          LatestLatency = *Latency;
        }
      }
    }
  }

  // If we haven't seen a def, create a default reference, either the latest
  // Use, or the first execute phase for the CPU.
  if (LatestDef == nullptr) {
    auto *Opnd = new OperandRef("<default_operand>");
    if (Latest)
      Refs.push_back(
               new Reference(RefTypes::kDef, Latest->getPhaseExpr(), Opnd));
    else
      Refs.push_back(
          new Reference(RefTypes::kDef, Spec.findFirstExecutePhase(Cpu), Opnd));
    Count++;
  }
  return Count;
}

//----------------------------------------------------------------------------
// Given an instruction and a reference list, create a set of referenced
// operand indexes.
//----------------------------------------------------------------------------
void InstructionDatabase::findCondReferencedOperands(
    const InstructionDef *Instr, ConditionalRef *Cond, CpuInstance *Cpu,
    std::set<int> &Found) {
  if (Cond == nullptr)
    return;
  findReferencedOperands(Instr, &Cond->getRefs(), Cpu, Found);
  findCondReferencedOperands(Instr, Cond->getElseClause(), Cpu, Found);
}

void InstructionDatabase::findReferencedOperands(const InstructionDef *Instr,
                                                 ReferenceList *Refs,
                                                 CpuInstance *Cpu,
                                                 std::set<int> &Found) {
  if (Refs == nullptr)
    return;
  for (const auto *Ref : *Refs) {
    if (Ref->isConditionalRef())
      findCondReferencedOperands(Instr, Ref->getConditionalRef(), Cpu, Found);
    else if (Ref->getOperand() != nullptr)
      Found.insert(Ref->getOperand()->getOperandIndex());
  }
}

//----------------------------------------------------------------------------
// Create pool names for functional unit pools and groups.  This allows us to
// manage and reuse these pools. We sort and concatenate the names to create
// a canonical name.
//----------------------------------------------------------------------------
std::string fuPoolName(ResourceRef *Res, CpuInstance *Cpu, MdlSpec &Spec) {
  std::set<std::string> Names;
  if (Spec.isFuncUnitTemplate(Res->getName())) {
    auto &Funits = Cpu->getFuncUnitInstances()[Res->getName()];
    if (Funits.size() == 1) return Res->getName();

    for (auto *Unit : Funits)
      Names.insert(Unit->getRootResource()->getName());
    return StringSet(&Names, "", ":", "");
  }
  if (Spec.isFuncUnitGroup(Res->getName())) {
    auto *Group = Spec.getFuGroupMap()[Res->getName()];
    for (auto *FuTemplate : Group->getFuMembers()) {
      auto &Funits = Cpu->getFuncUnitInstances()[FuTemplate->getName()];
      for (auto *Unit : Funits)
        Names.insert(Unit->getRootResource()->getName());
    }
    return StringSet(&Names, "", ":", "");
  }
  return "";     // Can't get here...
}

//----------------------------------------------------------------------------
// Helper for recording fu references.
//----------------------------------------------------------------------------
void InstructionDatabase::recordConditionallyUsedFus(
                ConditionalRef *Cond, SubUnitInstantiation *Subunit) {
  recordUsedFus(Cond->getRefs(), Subunit);
  if (Cond->getElseClause())
    recordConditionallyUsedFus(Cond->getElseClause(), Subunit);
}

//----------------------------------------------------------------------------
// Look for FU references in resource lists, and mark those FUs as used, so
// that they will be assigned resource ids.
//----------------------------------------------------------------------------
void InstructionDatabase::recordUsedFus(ReferenceList &ResourceRefs,
                                        SubUnitInstantiation *Subunit) {
  auto *Cpu = Subunit->getCpu();
  for (auto *Ref : ResourceRefs) {
    if (Ref->isConditionalRef()) {
      recordConditionallyUsedFus(Ref->getConditionalRef(), Subunit);
      continue;
    }
    if (!Ref->isFuncUnitRef()) continue;

    // Functional unit references that reference more than one FU are split up
    // into multiple refs, so this loop will only iterate once. We need to
    // handle three cases:
    // - a reference to a single functional unit template,
    // - a reference to an FU template that has multiple instances,
    // - a reference to a functional unit group.
    for (auto *Res : *Ref->getResources()) {
      ResourceDef *Pool = nullptr;
      // Handle the case of a functional unit template.
      if (getSpec().isFuncUnitTemplate(Res->getName())) {
        auto &Funits = Cpu->getFuncUnitInstances()[Res->getName()];

        // If there's just a single unit, record it as used.
        if (Funits.size() == 1) {
          auto *Def = Funits[0]->getRootResource();
          Def->recordFuReference();
          Ref->setBufferSize(
                    Funits[0]->getInstance()->getBufferSize(Res->getName()));
          continue;
        }

        // If there are multiple instances of the functional unit, create a
        // create a pool of all instances of that template. If the pool
        // already exists, use the existing one.
        auto Name = fuPoolName(Res, Cpu, getSpec());
        if (auto *Pool = FindItem(Cpu->getAllResources(), Name)) {
          Res->setFuPool(Pool);
          Ref->setBufferSize(
                    Funits[0]->getInstance()->getBufferSize(Res->getName()));
          continue;
        }
        // Create a new ResourceDef Pool, and add it to the CPUs list of
        // FU pools (so it can be reused).
        std::set<int> BufferSizes;
        int Size = -1;
        Pool = new ResourceDef(Name);
        for (auto *Funit : Funits) {
          auto *Def = Funit->getRootResource();
          Pool->addMemberDef(Def);
          Pool->getMembers().push_back(Def->getId());
          Size = Funit->getInstance()->getBufferSize(Res->getName());
          BufferSizes.insert(Size);    // count different sizes
          Def->recordFuReference();
        }
        if (BufferSizes.size() > 1)
          getSpec().WarningLog(Ref,
                               "Inconsistent functional unit buffer sizes");
        Ref->setBufferSize(Size);
      }

      // If the reference is a functional unit group, create a pool of all
      // instances of all group members.
      else if (getSpec().isFuncUnitGroup(Res->getName())) {
        auto Name = fuPoolName(Res, Cpu, getSpec());
        auto *Group = getSpec().getFuGroupMap()[Res->getName()];
        if (auto *Pool = FindItem(Cpu->getAllResources(), Name)) {
          Res->setFuPool(Pool);
          Ref->setBufferSize(Group->getBufferSize());
          continue;
        }
        // Create a new ResourceDef Pool, and add it to the CPUs list of
        // FU pools (so it can be reused), and annotate the resource reference.
        Pool = new ResourceDef(Name);
        Ref->setBufferSize(Group->getBufferSize());
        for (auto *FuTemplate : Group->getFuMembers()) {
          auto &Funits = Cpu->getFuncUnitInstances()[FuTemplate->getName()];
          for (auto *Funit : Funits) {
            auto *Def = Funit->getRootResource();
            Pool->addMemberDef(Def);
            Pool->getMembers().push_back(Def->getId());
            Def->recordFuReference();
          }
        }
      }
      else continue;     // Skip CPU entries

      // Add the new pool to the CPU's pool table, and annotate the reference.
      Pool->addAllocSize(1);
      Pool->recordReference(RefTypes::kFus, Ref->getPhaseExpr(), nullptr,
                                Ref, Subunit);
      Cpu->addCpuResource(Pool, "FuncPool", nullptr, nullptr, nullptr);
      Cpu->addFuPoolSize(Pool->getMembers().size());
      Res->setFuPool(Pool);         // Annotate the fu reference.

      auto *PoolRef = new ResourceRef(Pool);
      PoolRef->setPoolCount(1);
      Pool->addReferenceSizeToPool(PoolRef, Ref, Subunit);
    }
  }
}

//----------------------------------------------------------------------------
// Find unreferenced output operands, and create default references for them.
//----------------------------------------------------------------------------
void InstructionDatabase::addUnreferencedOperandDefs(
    const InstructionDef *Instr, ReferenceList *Refs, CpuInstance *Cpu) {
  // First find all the referenced operands.
  std::set<int> ReferencedOpnds;
  findReferencedOperands(Instr, Refs, Cpu, ReferencedOpnds);

  // Find register operands that have no references, create a vector of them.
  std::vector<OperandRef *> Opnds;
  for (int OpId = 0; OpId < Instr->getNumFlatOperands(); OpId++)
    if (ReferencedOpnds.count(OpId) == 0) {
      auto *Opnd = Instr->getOperandDecl(OpId);
      if (Opnd->isInput())
        continue;
      auto *Back = Opnd->getTypes()->back();
      auto *Front = Opnd->getTypes()->front();

      // See if the operand declaration is a register or a register class.
      if (auto *Cls = FindItem(getSpec().getRegClasses(), Back->getName())) {
        Opnds.push_back(new OperandRef(Front, Opnd->getOpNames(), Cls, OpId));
      } else if (FindItem(getSpec().getRegisters(), Back->getName()) != nullptr) {
        Opnds.push_back(new OperandRef(nullptr, new IdList(1, Back), OpId));
      }
    }

  // If we found unreferenced output operands, add "default" defs to
  // represent the worst-case pipeline phase for unspecified defs.
  // If only one default operand was added, and it's the last item in the
  // reference list, just use its latency and remove it from the list.
  PhaseExpr *DefLatency = nullptr;
  if (!Opnds.empty() && AddDefaultDefs(*Refs, Cpu, getSpec()) == 1 &&
      Refs->back()->isDefaultOperandRef()) {
    DefLatency = Refs->back()->getPhaseExpr();
    Refs->pop_back();
  } else {
    DefLatency = PhaseExpr::getDefaultLatency();
  }

  // We found unreferenced register-based output operands, so create a
  // references for them.
  for (auto *Opnd : Opnds)
    Refs->push_back(new Reference(RefTypes::kDef, DefLatency, Opnd));
}

//----------------------------------------------------------------------------
// Generate all instruction information records for a target instruction.
// Instructions can have more than one subunit.  If so, instantiate them all.
//----------------------------------------------------------------------------
void InstructionDatabase::generateInstructionInfo(InstructionDef *Instr) {
  // For each Subunit, create reference records for this instruction.
  for (auto *Subunit : *Instr->getSubunits())
    for (auto *Unit : *getSpec().getSuInstantiations()[Subunit->getName()]) {
      // Mark this subunit as used.
      getSpec().getSuMap()[Subunit->getName()]->incUse();

      // Create a list of valid references for this subunit.
      // Check each reference to see if its valid for this instruction.
      auto *Cpu = Unit->getCpu();
      auto *Refs = filterReferences(Instr, Unit->getReferences(), Cpu);

      // Check each reference for incompatible constraints imposed
      // by ports. These are not valid subunits, and we don't want to add
      // this subunit instance to the database.
      if (hasIncompatibleConstraints(Refs))
        continue;

      // Sort the references by pipeline phase. This is primarily to order
      // operand references by type and phase for cosmetic reasons.
      std::stable_sort(
          Refs->begin(), Refs->end(),
          [](const Reference *A, const Reference *B) { return *A < *B; });

      // Add defs for unreferenced register operand defs.  This isn't
      // necessary, so its currently disabled.
      // AddUnreferencedOperandDefs(Instr, Refs, Cpu);

      // Given a list of validated references, create a list of events for
      // unconditional resource references. At this point, we don't add
      // FUs and conditional refs - these are added later for each combination
      // of unconditional resource refs.
      // For each port reference, add it and its associated resources.
      // For each pooled reference, annotate it with its operand index.
      // For resources associated with operands:
      //      - they are always "used".
      //      - tag the resource reference with its associate operand index.
      //      - If the resource has a defined cycle id, use it.
      ReferenceList ResourceRefs;
      ResourceList Resources;
      for (auto *Ref : *Refs) {
        // Don't add functional unit and conditional references, just add them
        // to the resource reference list for this instruction/subunit.
        if (Ref->isFuncUnitRef() ||
            (Ref->isConditionalRef() &&
             Ref->getConditionalRef()->hasResourceRefs())) {
          ResourceRefs.push_back(Ref);
          continue;
        }
        // Add all other resource references.
        auto Type = Ref->adjustResourceReferenceType();
        for (auto *Res : *Ref->getResources()) {
          if (!Res->isNull()) {
            PhaseExpr *Phase = Ref->getPhaseExpr();

            // If the resource definition has a specified Phase, use it instead.
            if (auto *start = Res->getDefinition()->getStartPhase())
              Phase = new PhaseExpr(getSpec().findPipeReference(start, Cpu));
            if (Ref->getOperand())
              Res->setOperandIndex(Ref->getOperand()->getOperandIndex());
            Resources.emplace_back(Type, Phase, Ref->getUseCycles(), Res,
                                   Ref, Unit);
          }
        }
      }

      // Create sets of reference resource combinations.
      ResourceSets ResSet = buildResourceSets(Resources, Unit);
      ResourceList Current;
      ResourceSets ResourceCombos;
      buildResourceCombos(ResSet, 0, Current, ResourceCombos);

      // Find FU references, mark them as used.  If pooled, add them to
      // the CPU's resource pool list.
      recordUsedFus(ResourceRefs, Unit);

      //----------------------------------------------------------------------
      // AND FINALLY: For the current instruction, for each subunit, for each
      // resource combination, create an instruction record that captures all
      // of this information and add it to the instruction database.
      //----------------------------------------------------------------------
      for (auto &Res : ResourceCombos) {
        auto *NewInstr = new InstrInfo(Instr, Unit, Res, Refs, ResourceRefs);
        InstructionInfo[Instr->getName()].push_back(NewInstr);
      }
    }
}

//----------------------------------------------------------------------------
// Check all instruction records for operands that don't have explicit
// references referring to them - these are likely errors.
//----------------------------------------------------------------------------
void InstructionDatabase::checkUnreferencedOperands(bool CheckAllOperands) {
  for (auto &[Name, InfoSet] : InstructionInfo)
    for (auto *Info : InfoSet)
      Info->checkUnreferencedOperands(CheckAllOperands);
}

//----------------------------------------------------------------------------
// Given a Reference operand, determine if it is valid for this instruction.
// If the reference operand is null, its always valid.
// Return true if its valid.
//----------------------------------------------------------------------------
bool InstructionDatabase::isOperandValid(const InstructionDef *Instr,
                                         const OperandRef *Opnd,
                                         RefType Type) const {
  if (Opnd == nullptr) return true;
  int OpIndex = Spec.getOperandIndex(Instr, Opnd, Type);
  if (OpIndex == -1) return false;

  // For holds and reserves, we don't have to check the reference type.
  int IType = static_cast<int>(Type);
  if ((IType & RefTypes::kAnyUseDef) == 0)
    return true;

  // If the reference is any use or def, make sure it matches the type of the
  // operand declaration in the instruction.  Input operands must be "used",
  // and output operands must be "defed".
  // Occasionally td files give input and output operands the same name/type
  // (in different instructions), and latency rules must provide "defs" and
  // "uses" for those operands, but we don't have an obvious way to decide
  // whether a particular def or use matches an operand reference. So we use
  // an operand's I/O designator to differentiate. (These are -always- there
  // for definitions scraped from llvm).  If an operand doesn't have an I/O
  // designator, we can skip this check.
  auto *Op = Instr->getOperandDecl(OpIndex);
  if (Op == nullptr) return true;
  if (Op->isInput() && (IType & RefTypes::kAnyUse) == 0) return false;
  if (Op->isOutput() && (IType & RefTypes::kAnyDef) == 0) return false;
  return true;
}

// Look for operand references in phase expressions, and make sure the
// operand exists in the current instruction.
// Return true if the expression is valid.
bool InstructionDatabase::isPhaseExprValid(const InstructionDef *Instr,
                                           const PhaseExpr *Expr) const {
  if (!Expr) return true;
  if (Expr->getOperation() == kOpnd)
    return isOperandValid(Instr, Expr->getOperand(), RefTypes::kNull);
  return isPhaseExprValid(Instr, Expr->getLeft()) &&
         isPhaseExprValid(Instr, Expr->getRight());
}

// Return true if this reference is valid for this instruction.
// - If it has an operand reference, then check that the instuction
//   definition has that operand.
// - If the phase expression contains operand references, check them too.
bool InstructionDatabase::isReferenceValid(const InstructionDef *Instr,
                                           const Reference *Ref) const {
  return isOperandValid(Instr, Ref->getOperand(), Ref->getRefType()) &&
         isPhaseExprValid(Instr, Ref->getPhaseExpr());
}

//----------------------------------------------------------------------------
// Dump everything we know about all the instructions.
//----------------------------------------------------------------------------
void InstructionDatabase::dumpInstructions() {
  std::cout << "\n---------------------------------------------------------\n";
  std::cout << " Instruction info for \"" << FileName << "\"";
  std::cout << "\n---------------------------------------------------------\n";

  // Debug: dump out all the instruction information we've generated.
  for (auto &InstrList : InstructionInfo)
    for (auto &Instr : InstrList.second)
      Instr->Dump();
}

//----------------------------------------------------------------------------
// Start the process of generating the final instruction information.
//----------------------------------------------------------------------------
InstructionDatabase::InstructionDatabase(std::string DirectoryName,
                                         std::string FileName,
                                         bool GenMissingInfo, MdlSpec &Spec)
    : DirectoryName(DirectoryName), FileName(FileName),
      GenMissingInfo(GenMissingInfo), Spec(Spec) {
  // Add all the target instructions to the instruction database.
  for (auto *Instr : Spec.getInstructions())
    if (!Instr->getSubunits()->empty())
      generateInstructionInfo(Instr);
}

//----------------------------------------------------------------------------
// Write out the entire database to the output C++ file.
//----------------------------------------------------------------------------
void InstructionDatabase::write(bool GenerateLLVMDefs) {
  OutputState output(this, GenerateLLVMDefs);
  output.writeHeader();
  output.writeLLVMDefinitions();
  // output.writeResourceDefinitions();
  output.writeCpuTable();
  output.writeExterns();
  output.writeTrailer();
}

} // namespace mdl
} // namespace mpact

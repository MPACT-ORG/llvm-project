//===- MDLInstrInfo.cpp - MDL-based instruction modeling ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a set of APIs between the MDL database and the CodeGen
// and MC libraries.  The MDL database uses the Instr class to access
// information about MachineInstr and MCInst objects, and the CodeGen/MC
// libraries use these interfaces to calculate various types of instruction
// latencies and resource management.
//
// While this is part of the MC library, it uses interfaces into the Codegen
// headers so that one interface can provide MDL information for both MCInst
// and MachineInstr objects.  Note that we don't include code or data from
// the Codegen library.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "llvm/ADT/bit.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MDLInfo.h"
#include "llvm/MC/MDLInstrInfo.h"

namespace llvm {
namespace mdl {

/// A SlotDesc represents a single issue slot, containing the entire context
/// of how an instruction is bundled.
SlotDesc::SlotDesc(const MCInst *MC, const MCSubtargetInfo *STI,
                   const MCInstrInfo *MCII)
    : Inst(MC, STI, MCII),
      Subunits((*STI->getCpuInfo()->getSubunits())[Inst.getOpcode()]),
      SubunitId(0) {}
SlotDesc::SlotDesc(MachineInstr *MI, const TargetSubtargetInfo *STI)
    : Inst(MI, STI),
      Subunits((*STI->getCpuInfo()->getSubunits())[Inst.getOpcode()]),
      SubunitId(0) {}

/// The Instr object provides a common interface to the MDL compiler for
/// accessing information in EITHER MachineInstrs and MCInsts.
Instr::Instr(const MachineInstr *MI, const TargetSubtargetInfo *STI)
    : MI(MI), TII(STI->getInstrInfo()), Cpu(STI->getCpuInfo()) {}
Instr::Instr(const MachineInstr *MI, const TargetInstrInfo *TII, CpuInfo *Cpu)
    : MI(MI), TII(TII), Cpu(Cpu) {}

Instr::Instr(const MCInst *MC, const MCSubtargetInfo *STI,
             const MCInstrInfo *MCII)
    : MC(MC), STI(STI), MCII(MCII), Cpu(STI->getCpuInfo()) {}

// Get the LLVM name for this instruction.
std::string Instr::getName() { return TII->getName(getOpcode()).str(); }
// Get the opcode for an instruction.
int Instr::getOpcode() { return isMC() ? MC->getOpcode() : MI->getOpcode(); }

// Evaluate a Target library instruction predicate for this instruction.
bool Instr::evaluatePredicate(int PredId) {
  return Cpu->evaluatePredicate(PredId, this);
}

// Return the set of subunits for an instruction and CPU combination.
SubunitVec *Instr::getSubunit() { return Cpu->getSubunit(getOpcode()); }

/// Return the raw bits associated with an operand.
int64_t Instr::getOperand(int OperandIndex) {
  if (isMC()) {
    const MCOperand &MO = MC->getOperand(OperandIndex);
    if (MO.isImm())
      return MO.getImm();
    if (MO.isSFPImm())
      return MO.getSFPImm();
    if (MO.isDFPImm())
      return MO.getDFPImm();
    if (MO.isReg())
      return MO.getReg();
  } else {
    const MachineOperand &MO = MI->getOperand(OperandIndex);
    if (MO.isImm())
      return MO.getImm();
    if (MO.isFPImm())
      return llvm::bit_cast<uint32_t>(
          MO.getFPImm()->getValueAPF().convertToFloat());
    if (MO.isReg())
      return MO.getReg();
  }
  return 0;
}

// Return true if a specific operand is a literal (immediate of some form).
bool Instr::isOpndLiteral(int OperandIndex) {
  if (isMC()) {
    const MCOperand &MO = MC->getOperand(OperandIndex);
    return MO.isImm() || MO.isSFPImm() || MO.isDFPImm();
  } else {
    const MachineOperand &MO = MI->getOperand(OperandIndex);
    return MO.isImm() || MO.isFPImm();
  }
}

// Return true if a specific operand is a relocatable address.
bool Instr::isOpndAddress(int OperandIndex) {
  if (isMC())
    return MC->getOperand(OperandIndex).isExpr();
  else
    return MI->getOperand(OperandIndex).isGlobal() ||
           MI->getOperand(OperandIndex).isMBB();
}

// Return true if a specific operand is a code label.
bool Instr::isOpndLabel(int OperandIndex) {
  if (isMC())
    return MC->getOperand(OperandIndex).isExpr();
  else
    return MI->getOperand(OperandIndex).isMBB();
}

// Return true if a specific operand is a register.
bool Instr::isOpndRegister(int OperandIndex) {
  if (isMC())
    return MC->getOperand(OperandIndex).isReg();
  else
    return MI->getOperand(OperandIndex).isReg();
}

// Return true if a specific operand is a virtual register.
bool Instr::isOpndVirtualRegister(int OperandIndex) {
  if (isMC()) {
    const MCOperand &MO = MC->getOperand(OperandIndex);
    if (!MO.isReg())
      return false;
    return false; // MO.getReg().isVirtual();
  } else {
    const MachineOperand &MO = MI->getOperand(OperandIndex);
    if (!MO.isReg())
      return false;
    return MO.getReg().isVirtual();
  }
}

/// Return true if a MachineInstr has more operands than described in its
/// MCInst description.
bool Instr::hasExtraOperands() {
  if (isMC())
    return false;
  return MI->getNumOperands() != MI->getDesc().getNumOperands() + 
                                 MI->getDesc().NumImplicitUses +
                                 MI->getDesc().NumImplicitDefs;
}

/// Since LLVM may arbitrarily insert and append operands onto instruction
/// instances, we can't always rely on the operand indexes that the model
/// provides. To deal with this, we create a sorted list of the defs or
/// uses for an instruction, then determine if the requested operand has
/// information, or not.
/// NOTE: This isn't as onerous as it sounds: operand insertions are rare,
/// and typically instructions only have a few explicit references.
inline OperandRef const *findOrderedReference(Instr *Inst, ReferenceType Type,
                                              int OpndId, OperandRefVec *Refs) {
  // Find the set of defs OR uses for this instruction, and sort them by
  // operand index and pipeline phase. We want the latest defs and the
  // earliest uses, so that when we're searching the sorted list below,
  // we find the right reference first.
  std::vector<const OperandRef *> FoundRefs;
  for (const auto &Opnd : ReferenceIter<OperandRef>(Refs, Inst))
    if (Opnd.getType() == Type)
      FoundRefs.push_back(&Opnd);

  int RefIdx = 0;
  if (Type == ReferenceTypes::RefDef) {
    std::stable_sort(FoundRefs.begin(), FoundRefs.end(),
                     [Inst](const OperandRef *a, const OperandRef *b) {
                       return a->getOperandIndex() < b->getOperandIndex() ||
                              (a->getOperandIndex() == b->getOperandIndex() &&
                               a->getPhase(Inst) > b->getPhase(Inst));
                     });
    for (int i = 0; i < OpndId; i++) {
      const MachineOperand &MO = Inst->getMI()->getOperand(i);
      if (MO.isReg() && MO.isDef())
        RefIdx++;
    }
  } else {
    std::stable_sort(FoundRefs.begin(), FoundRefs.end(),
                     [Inst](const OperandRef *a, const OperandRef *b) {
                       return a->getOperandIndex() < b->getOperandIndex() ||
                              (a->getOperandIndex() == b->getOperandIndex() &&
                               a->getPhase(Inst) < b->getPhase(Inst));
                     });
    for (int i = 0; i < OpndId; i++) {
      const MachineOperand &MO = Inst->getMI()->getOperand(i);
      if (MO.isReg() && MO.readsReg() && !MO.isDef())
        RefIdx++;
    }
  }

  // Find the "nth" operand id in the sorted reference list and return it.
  int LastIndex = -1;
  int Seen = 0;
  for (const auto *Item : FoundRefs)
    if (Item->getOperandIndex() != LastIndex) {
      if (++Seen > RefIdx)
        return Item;
      else
        LastIndex = Item->getOperandIndex();
    }

  return nullptr;
}

/// Given two references, return the "best" one.  In general, we want the
/// latest defs and the earliest uses.
static const OperandRef *bestRef(const OperandRef *Best, const OperandRef *Item,
                                 Instr *Inst) {
  if (Best == nullptr)
    return Item;
  if (Best->getType() == ReferenceTypes::RefDef &&
      Item->getPhase(Inst) > Best->getPhase(Inst))
    return Item;
  if (Best->getType() == ReferenceTypes::RefUse &&
      Item->getPhase(Inst) < Best->getPhase(Inst))
    return Item;
  return Best;
}

/// Search an operand reference list for a reference to a particular operand.
inline OperandRef const *findReference(Instr *Inst, ReferenceType Type,
                                       int OpndId, OperandRefVec *Refs) {
  if (Inst->hasExtraOperands())
    return findOrderedReference(Inst, Type, OpndId, Refs);

  const OperandRef *BestRef = nullptr;
  for (const auto &Opnd : ReferenceIter<OperandRef>(Refs, Inst))
    if (Opnd.getType() == Type && Opnd.getOperandIndex() == OpndId)
      BestRef = bestRef(BestRef, &Opnd, Inst);

  return BestRef; // Return the best reference found, or nullptr.
}

// Given two sets of resource references, look up the forwarding adjustment
// in the forwarding table.
// TODO: When there more than one functional unit, we need a heuristic
// to determine if forwarding occurs.
int calculateForwardingAdjustment(CpuInfo &Cpu, Instr *Def, Subunit &DefUnit,
                                  Instr *Use, Subunit &UseUnit) {
  // No point doing this if there isn't a forwarding table.
  auto *FwdTable = Cpu.getForwardTable();
  if (FwdTable == nullptr)
    return 0; // No forwarding table

  // Find the first functional unit resource for the Def instruction.
  int DefFuId = -1;
  if (auto *DefRes = DefUnit.getUsedResourceReferences())
    for (auto &DefFu : ReferenceIter<ResourceRef>(DefRes, Def))
      if (DefFu.isFus() || Cpu.isFuncUnitId(DefFu.getResourceId())) {
        DefFuId = DefFu.getResourceId();
        break;
      }

  // If we didn't find a functional unit in resources, check resource pools.
  // We'll take the first member of the first pool we find, with the assumption
  // that all the functional units in the pool have similar forwarding.
  if (DefFuId == -1) {
    if (auto *DefRes = DefUnit.getPooledResourceReferences())
      for (auto &DefFu : ReferenceIter<PooledResourceRef>(DefRes, Def))
        if (DefFu.isFus()) {
          DefFuId = DefFu.getResourceIds()[0];
          break;
        }
  }
  if (DefFuId == -1)
    return 0;

  // Find the first functional unit resource for the Use instruction.
  int UseFuId = -1;
  if (auto *UseRes = UseUnit.getUsedResourceReferences())
    for (auto &UseFu : ReferenceIter<ResourceRef>(UseRes, Use))
      if (UseFu.isFus() || Cpu.isFuncUnitId(UseFu.getResourceId())) {
        UseFuId = UseFu.getResourceId();
        break;
      }

  // If we didn't find a functional unit in resources, check resource pools.
  // We'll take the first member of the first pool we find, with the assumption
  // that all the functional units in the pool have similar forwarding.
  if (UseFuId == -1) {
    if (auto *UseRes = UseUnit.getPooledResourceReferences())
      for (auto &UseFu : ReferenceIter<PooledResourceRef>(UseRes, Use))
        if (UseFu.isFus()) {
          UseFuId = UseFu.getResourceIds()[0];
          break;
        }
  }
  if (UseFuId == -1)
    return 0;

  return FwdTable[DefFuId][UseFuId];
}

// This is essentially a clone of MachineInstr::mayLoad, except that it
// doesn't depend on code in the CodeGen library, and doesn't handle bundles.
static bool mayLoad(const MachineInstr *MI) {
  return MI->getDesc().getFlags() & (1 << MCID::MayLoad);
}

// Return a default expected latency for a Def'ed register which has no
// MDL information about its latency.  Note that this simply replicates
// the "normal" TargetInstrInfo default latency api.
static unsigned defaultDefLatency(Instr *Def, CpuInfo &Cpu) {
  if (auto *DefMI = Def->getMI()) {
    if (isTransient(DefMI))
      return 0;
    if (mayLoad(DefMI))
      return Cpu.getLoadPhase();
    if (Def->getTII()->isHighLatencyDef(Def->getOpcode()))
      return Cpu.getHighLatencyDefPhase();
  }
  return 1;
}

/// Calculate the latency between two instructions' operands.
/// This function will always receive a Def, but doesn't always get a Use.
/// For Defs or Uses, if we don't have a subunit description, or the subunit
/// doesn't mention the operand, we use the earliest reference cycle for
/// the current CPU.
int calculateOperandLatency(Instr *Def, unsigned DefOpId, Instr *Use,
                            unsigned UseOpId) {
  CpuInfo &Cpu = *Def->getCpuInfo();

  int DefPhase = -1;
  int UsePhase = Cpu.getEarlyUsePhase(); // Default use stage
  int FwdAdjust = 0;

  int DefSuId = Def->getSubunitId();
  int UseSuId = Use ? Use->getSubunitId() : 0;
  SubunitVec *DefSubunit = nullptr;
  SubunitVec *UseSubunit = nullptr;

  if (Cpu.IsInstruction(Def->getOpcode(), DefOpId))
    if ((DefSubunit = Def->getSubunit()))
      if (auto *DefRefs = (*DefSubunit)[DefSuId].getOperandReferences())
        if (auto *DefRef =
                findReference(Def, ReferenceTypes::RefDef, DefOpId, DefRefs))
          DefPhase = DefRef->getPhase(Def);

  // If we don't find the def, choose a reasonable latency.
  if (DefPhase == -1)
    return defaultDefLatency(Def, Cpu);

  // Find the phase for a Use instruction, if provided.
  if (Use && Cpu.IsInstruction(Use->getOpcode(), UseOpId))
    if ((UseSubunit = Use->getSubunit()))
      if (auto *UseRefs = (*UseSubunit)[UseSuId].getOperandReferences())
        if (auto *UseRef =
                findReference(Use, ReferenceTypes::RefUse, UseOpId, UseRefs))
          UsePhase = UseRef->getPhase(Use);

  // If we have subunits for the def and the use, check for forwarding
  // information and adjust the latency. If we can't identify a subunit, we
  // can't know anything about forwarding.
  if (DefSubunit && UseSubunit) {
    auto &UseUnit = (*UseSubunit)[UseSuId];
    auto &DefUnit = (*DefSubunit)[DefSuId];
    FwdAdjust = calculateForwardingAdjustment(Cpu, Def, DefUnit, Use, UseUnit);
  }

  return std::max(DefPhase - UsePhase - FwdAdjust + 1, 0);
}

/// Wrapper for MachineInstr Objects.
/// This function will always receive a Def, but doesn't always get a Use.
int calculateOperandLatency(const MachineInstr *Def, unsigned DefOpId,
                            const MachineInstr *Use, unsigned UseOpId,
                            const TargetSubtargetInfo *STI) {
  Instr instDef(Def, STI);
  if (Use) {
    Instr instUse(Use, STI);
    return calculateOperandLatency(&instDef, DefOpId, &instUse, UseOpId);
  }
  return calculateOperandLatency(&instDef, DefOpId, nullptr, UseOpId);
}

/// Find the maximum cycle that resources are reserved for an instruction.
/// This is typically used for models that use itineraries.
int calculateResourceLatency(const MachineInstr *MI,
                             const TargetSubtargetInfo *STI) {
  Instr Ins(MI, STI);
  auto *Subunit = Ins.getSubunit();
  if (Subunit == nullptr) return 0;

  int SubunitId = Ins.getSubunitId();
  auto *Refs = (*Subunit)[SubunitId].getUsedResourceReferences();
  if (Refs == nullptr)
    return 0;

  int MaxPhase = isTransient(MI) ? 0 : 1;
  for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
    MaxPhase = std::max(MaxPhase, Ref.getPhase(&Ins));

  return MaxPhase;
}

/// Search a list of operand references for the maximum DEF latency.
inline int findMaxLatency(Instr *Inst, OperandRefVec *Refs) {
  int Max = 0;
  if (Refs == nullptr)
    return Max;
  for (auto &Ref : ReferenceIter<OperandRef>(Refs, Inst))
    if (Ref.isDef())
      Max = std::max(Max, Ref.getPhase(Inst));

  return Max;
}

/// Find the maximum latency of an instruction based on operand references.
int calculateInstructionLatency(Instr *Inst) {
  CpuInfo &Cpu = *Inst->getCpuInfo();
  if (!Cpu.IsInstruction(Inst->getOpcode(), 0))
    return defaultDefLatency(Inst, Cpu);

  // Handle instructions that don't have subunit information, or whose subunits
  // don't mention all operands. If the instructions were annotated with
  // assigned subunits, we wouldn't have to check all of them.
  int Max = Cpu.getEarlyUsePhase();
  if (auto *Subunits = Inst->getSubunit())
    for (auto &Subunit : *Subunits)
      Max = std::max(Max, findMaxLatency(Inst, Subunit.getOperandReferences()));
#if 0
  return Max;
#else
  return std::max(Max - static_cast<int>(Cpu.getEarlyUsePhase()) + 1, 0);
#endif
}

/// Wrapper for MCInst objects.
int calculateInstructionLatency(const MCInst *Inst, const MCSubtargetInfo *STI,
                                const MCInstrInfo *MCII) {
  Instr Ins(Inst, STI, MCII);
  return calculateInstructionLatency(&Ins);
}

/// Wrapper for MachineInstr objects.
int calculateInstructionLatency(const MachineInstr *Inst,
                                const TargetSubtargetInfo *STI) {

  Instr Ins(Inst, STI);
  return calculateInstructionLatency(&Ins);
}

/// Calculate the latency between two instructions that hold or reserve the
/// same resource.
int calculateHazardLatency(Instr *Reserve, Instr *Hold) {
  auto *ReserveSu = Reserve->getSubunit();
  auto *HoldSu = Hold->getSubunit();
  if (ReserveSu == nullptr || HoldSu == nullptr)
    return -1;

  int ResId = Reserve->getSubunitId();
  int HoldId = Hold->getSubunitId();
  auto *ReserveRefs = (*ReserveSu)[ResId].getReservedResourceReferences();
  auto *HoldRefs = (*HoldSu)[HoldId].getHeldResourceReferences();
  if (HoldRefs == nullptr || ReserveRefs == nullptr)
    return -1;

  int MaxLatency = -1;
  for (auto const &ResRef : *ReserveRefs) {
    for (auto const &HoldRef : *HoldRefs) {
      if (ResRef.getResourceId() == HoldRef.getResourceId()) {
        int Latency = ResRef.getPhase(Reserve) - HoldRef.getPhase(Hold) + 1;
        MaxLatency = std::max(MaxLatency, std::max(Latency, 0));
      }
    }
  }
  return MaxLatency;
}

/// Wrapper for MCInst objects.
int calculateHazardLatency(const MCInst *Reserve, const MCInst *Hold,
                           const MCSubtargetInfo *STI,
                           const MCInstrInfo *MCII) {
  Instr ReserveInst(Reserve, STI, MCII);
  Instr HoldInst(Hold, STI, MCII);
  return calculateHazardLatency(&ReserveInst, &HoldInst);
}

/// Wrapper for MachineInstr objects.
int calculateHazardLatency(MachineInstr *Reserve, MachineInstr *Hold,
                           TargetSubtargetInfo *STI) {
  Instr ReserveInst(Reserve, STI);
  Instr HoldInst(Reserve, STI);
  return calculateHazardLatency(&ReserveInst, &HoldInst);
}

} // namespace mdl
} // namespace llvm

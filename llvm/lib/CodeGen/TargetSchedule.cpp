//===- llvm/Target/TargetSchedule.cpp - Sched Machine Model ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a wrapper around MCSchedModel that allows the interface
// to benefit from information currently only available in TargetInstrInfo.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MDLInfo.h"
#include "llvm/MC/MDLInstrInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <numeric>

using namespace llvm;
using namespace mdl;

static cl::opt<bool> EnableSchedModel("schedmodel", cl::Hidden, cl::init(true),
  cl::desc("Use TargetSchedModel for latency lookup"));

static cl::opt<bool> EnableSchedItins("scheditins", cl::Hidden, cl::init(true),
  cl::desc("Use InstrItineraryData for latency lookup"));

static cl::opt<bool> ForceEnableIntervals(
    "sched-model-force-enable-intervals", cl::Hidden, cl::init(false),
    cl::desc("Force the use of resource intervals in the schedule model"));

bool TargetSchedModel::hasInstrSchedModel() const {
  return EnableSchedModel && SchedModel.hasInstrSchedModel();
}

bool TargetSchedModel::hasInstrItineraries() const {
  return EnableSchedItins && !InstrItins.isEmpty();
}

mdl::CpuInfo *TargetSchedModel::getCpuInfo() const { return STI->getCpuInfo(); }

bool TargetSchedModel::hasMdlModel() const
   { return STI->hasMdlModel(); }
bool TargetSchedModel::hasInstrMdlModel() const
   { return STI->hasInstrMdlModel(); }
bool TargetSchedModel::hasInstrMdlModel(const MachineInstr *MI) const
   { return MI && STI->hasInstrMdlModel(MI->getOpcode()); }
bool TargetSchedModel::hasInstrMdlModel(const MCInst *MI) const
   { return MI && STI->hasInstrMdlModel(MI->getOpcode()); }

void TargetSchedModel::init(const TargetSubtargetInfo *TSInfo) {
  STI = TSInfo;
  SchedModel = TSInfo->getSchedModel();
  TII = TSInfo->getInstrInfo();
  STI->initInstrItins(InstrItins);

  // ResourceLCM is computed in the MDL compiler for each CPU.
  if (hasMdlModel()) {
    MdlResourceLCM = STI->getCpuInfo()->getResourceFactor();
    MdlMicroOpFactor = MdlResourceLCM / STI->getCpuInfo()->getMaxIssue();

    // MDL functional unit resources don't have "NumUnits", instead we use
    // explicit "pools" to allocate them.  So here we pre-compute all
    // feasible pool size factors into ResourceFactors, then index it with
    // the pool size.
    ResourcePoolFactors.resize(MdlResourceLCM + 1);
    for (unsigned PoolSize = 1; PoolSize <= MdlResourceLCM; PoolSize++)
      ResourcePoolFactors[PoolSize] = MdlResourceLCM / PoolSize;
  }

  unsigned NumRes = SchedModel.getNumProcResourceKinds();
  ResourceFactors.resize(NumRes);

  ResourceLCM = SchedModel.IssueWidth;
  for (unsigned Idx = 0; Idx < NumRes; ++Idx) {
    unsigned NumUnits = SchedModel.getProcResource(Idx)->NumUnits;
    if (NumUnits > 0)
      ResourceLCM = std::lcm(ResourceLCM, NumUnits);
  }
  MicroOpFactor = ResourceLCM / SchedModel.IssueWidth;
  for (unsigned Idx = 0; Idx < NumRes; ++Idx) {
    unsigned NumUnits = SchedModel.getProcResource(Idx)->NumUnits;
    ResourceFactors[Idx] = NumUnits ? (ResourceLCM / NumUnits) : 0;
  }
}

/// Returns true only if instruction is specified as beginning an issue group.
bool TargetSchedModel::mustBeginGroup(const MachineInstr *MI,
                                     const MCSchedClassDesc *SC) const {
  if (hasInstrMdlModel(MI))
    return STI->getCpuInfo()->mustBeginGroup(MI, STI);
  if (hasInstrSchedModel()) {
    if (!SC)
      SC = resolveSchedClass(MI);
    if (SC->isValid())
      return SC->BeginGroup;
  }
  return false;
}

/// Returns true only if instruction is specified as ending an issue group.
bool TargetSchedModel::mustEndGroup(const MachineInstr *MI,
                                     const MCSchedClassDesc *SC) const {
  if (hasInstrMdlModel(MI))
    return STI->getCpuInfo()->mustEndGroup(MI, STI);
  if (hasInstrSchedModel()) {
    if (!SC)
      SC = resolveSchedClass(MI);
    if (SC->isValid())
      return SC->EndGroup;
  }
  return false;
}

unsigned TargetSchedModel::getNumMicroOps(const MachineInstr *MI,
                                          const MCSchedClassDesc *SC) const {
  if (hasInstrMdlModel(MI) && !hasInstrItineraries())
    return STI->getCpuInfo()->numMicroOps(MI, STI);

  if (hasInstrItineraries()) {
    int UOps = InstrItins.getNumMicroOps(MI->getDesc().getSchedClass());
    return (UOps >= 0) ? UOps : TII->getNumMicroOps(&InstrItins, *MI);
  }
  if (hasInstrSchedModel()) {
    if (!SC)
      SC = resolveSchedClass(MI);
    if (SC->isValid())
      return SC->NumMicroOps;
  }
  return MI->isTransient() ? 0 : 1;
}

// The machine model may explicitly specify an invalid latency, which
// effectively means infinite latency. Since users of the TargetSchedule API
// don't know how to handle this, we convert it to a very large latency that is
// easy to distinguish when debugging the DAG but won't induce overflow.
static unsigned capLatency(int Cycles) {
  return Cycles >= 0 ? Cycles : 1000;
}

/// Return the MCSchedClassDesc for this instruction. Some SchedClasses require
/// evaluation of predicates that depend on instruction operands or flags.
const MCSchedClassDesc *TargetSchedModel::
resolveSchedClass(const MachineInstr *MI) const {
  // Get the definition's scheduling class descriptor from this machine model.
  unsigned SchedClass = MI->getDesc().getSchedClass();
  const MCSchedClassDesc *SCDesc = SchedModel.getSchedClassDesc(SchedClass);
  if (!SCDesc->isValid())
    return SCDesc;

#ifndef NDEBUG
  unsigned NIter = 0;
#endif
  while (SCDesc->isVariant()) {
    assert(++NIter < 6 && "Variants are nested deeper than the magic number");

    SchedClass = STI->resolveSchedClass(SchedClass, MI, this);
    SCDesc = SchedModel.getSchedClassDesc(SchedClass);
  }
  return SCDesc;
}

/// Find the def index of this operand. This index maps to the machine model and
/// is independent of use operands. Def operands may be reordered with uses or
/// merged with uses without affecting the def index (e.g. before/after
/// regalloc). However, an instruction's def operands must never be reordered
/// with respect to each other.
static unsigned findDefIdx(const MachineInstr *MI, unsigned DefOperIdx) {
  unsigned DefIdx = 0;
  for (unsigned i = 0; i != DefOperIdx; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (MO.isReg() && MO.isDef())
      ++DefIdx;
  }
  return DefIdx;
}

/// Find the use index of this operand. This is independent of the instruction's
/// def operands.
///
/// Note that uses are not determined by the operand's isUse property, which
/// is simply the inverse of isDef. Here we consider any readsReg operand to be
/// a "use". The machine model allows an operand to be both a Def and Use.
static unsigned findUseIdx(const MachineInstr *MI, unsigned UseOperIdx) {
  unsigned UseIdx = 0;
  for (unsigned i = 0; i != UseOperIdx; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (MO.isReg() && MO.readsReg() && !MO.isDef())
      ++UseIdx;
  }
  return UseIdx;
}

static unsigned compareLatencies(const TargetInstrInfo *TII,
                                 const MachineInstr *Def, int DefOpIdx,
                                 const MachineInstr *Use, int UseOpIdx,
                                 int MdlLatency, unsigned BaseLatency,
                                 int Advance) {
  // Adjust LLVM Schedule-based latency by the Advance amount.
  unsigned Latency = BaseLatency - Advance;
  if (Advance > 0 && (unsigned)Advance > BaseLatency) // unsigned wrap
    Latency = 0;

  if (MdlLatency == -1 || MdlLatency == (int)Latency)
    return Latency;

  DEBUG_WITH_TYPE("MdlLatency",
    if (Use == nullptr || UseOpIdx == -1) {
      llvm::dbgs() << formatv("[{0}<{1}> nullptr <-1> ] ",
                              Def->getOpcode(), DefOpIdx);
    } else {
      llvm::dbgs() << formatv("[{0}<{1}> {2}<{3}> ] ",
                  Def->getOpcode(), DefOpIdx, Use->getOpcode(), UseOpIdx);
    }

    llvm::dbgs() << formatv("{0}-->", TII->getName(Def->getOpcode()));

    if (Use == nullptr || UseOpIdx == -1)
      llvm::dbgs() << "<none> == ";
    else
      llvm::dbgs() << formatv("{0} == ", TII->getName(Use->getOpcode()));

    llvm::dbgs() << formatv("Expected: {0}({1}) Calculated: {2}\n",
                Latency, Advance, MdlLatency);
    // llvm_unreachable("MDLCheckLatency :: Different calculated latencies");
  );
  return MdlLatency;
}

#include <stdio.h>

// Top-level API for clients that know the operand indices. This doesn't need to
// return std::optional<unsigned>, as it always returns a valid latency.
unsigned TargetSchedModel::computeOperandLatency(
  const MachineInstr *DefMI, unsigned DefOperIdx,
  const MachineInstr *UseMI, unsigned UseOperIdx) const {

  const unsigned InstrLatency = computeInstrLatency(DefMI);
  const unsigned DefaultDefLatency = TII->defaultDefLatency(SchedModel, *DefMI);

  if (!hasAnySchedModel())
    return DefaultDefLatency;

  // If we have an MDL model use it to compute a latency.  Itineraries use
  // a target-specific method to override the model if we're missing any
  // information about either instruction, so revert to that when we don't
  // have instruction information (rather than recreate that here).
  int MdlLatency = -1;
  if (hasInstrMdlModel())
    if (!hasInstrItineraries() &&
       (hasInstrMdlModel(DefMI) && hasInstrMdlModel(UseMI)))
      MdlLatency = llvm::mdl::calculateOperandLatency(DefMI, DefOperIdx, UseMI,
                                              UseOperIdx, STI);

  // Use itineraries to calculate the latency.
  if (hasInstrItineraries()) {
    std::optional<unsigned> OperLatency;
    if (UseMI) {
      OperLatency = TII->getOperandLatency(&InstrItins, *DefMI, DefOperIdx,
                                           *UseMI, UseOperIdx);
    }
    else {
      unsigned DefClass = DefMI->getDesc().getSchedClass();
      OperLatency = InstrItins.getOperandCycle(DefClass, DefOperIdx);
    }
    unsigned ItinLatency = OperLatency ? *OperLatency
                       : std::max(InstrLatency, DefaultDefLatency);
    return compareLatencies(TII, DefMI, DefOperIdx, UseMI, UseOperIdx,
                              MdlLatency, ItinLatency, 0);
  }

  // hasInstrSchedModel()
  if (!hasInstrSchedModel())
    return TII->defaultDefLatency(SchedModel, *DefMI);

  const MCSchedClassDesc *SCDesc = resolveSchedClass(DefMI);
  unsigned DefIdx = findDefIdx(DefMI, DefOperIdx);
  if (DefIdx < SCDesc->NumWriteLatencyEntries) {
    // Lookup the definition's write latency in SubtargetInfo.
    const MCWriteLatencyEntry *WLEntry =
      STI->getWriteLatencyEntry(SCDesc, DefIdx);
    unsigned WriteID = WLEntry->WriteResourceID;
    unsigned Latency = capLatency(WLEntry->Cycles);
    if (!UseMI)
      return compareLatencies(TII, DefMI, DefOperIdx, UseMI, UseOperIdx,
                              MdlLatency, Latency, 0);

    // Lookup the use's latency adjustment in SubtargetInfo.
    const MCSchedClassDesc *UseDesc = resolveSchedClass(UseMI);
    if (UseDesc->NumReadAdvanceEntries == 0)
      return compareLatencies(TII, DefMI, DefOperIdx, UseMI, UseOperIdx,
                              MdlLatency, Latency, 0);
    unsigned UseIdx = findUseIdx(UseMI, UseOperIdx);
    int Advance = STI->getReadAdvanceCycles(UseDesc, UseIdx, WriteID);
    return compareLatencies(TII, DefMI, DefOperIdx, UseMI, UseOperIdx,
                            MdlLatency, Latency, Advance);
  }
  // If DefIdx does not exist in the model (e.g. implicit defs), then return
  // unit latency (defaultDefLatency may be too conservative).
#ifndef NDEBUG
  if (SCDesc->isValid() && !DefMI->getOperand(DefOperIdx).isImplicit() &&
      !DefMI->getDesc().operands()[DefOperIdx].isOptionalDef() &&
      SchedModel.isComplete()) {
    errs() << "DefIdx " << DefIdx << " exceeds machine model writes for "
           << *DefMI << " (Try with MCSchedModel.CompleteModel set to false)";
    llvm_unreachable("incomplete machine model");
  }
#endif
  // FIXME: Automatically giving all implicit defs defaultDefLatency is
  // undesirable. We should only do it for defs that are known to the MC
  // desc like flags. Truly implicit defs should get 1 cycle latency.
  unsigned Latency =
      DefMI->isTransient() ? 0 : DefaultDefLatency;
  return compareLatencies(TII, DefMI, DefOperIdx, UseMI, UseOperIdx, MdlLatency,
                          Latency, 0);
}

unsigned
TargetSchedModel::computeInstrLatency(const MCSchedClassDesc &SCDesc) const {
  return capLatency(MCSchedModel::computeInstrLatency(*STI, SCDesc));
}

unsigned TargetSchedModel::computeInstrLatency(unsigned Opcode) const {
  assert(hasInstrSchedModel() && "Only call this function with a SchedModel");
  unsigned SCIdx = TII->get(Opcode).getSchedClass();
  return capLatency(SchedModel.computeInstrLatency(*STI, SCIdx));
}

unsigned TargetSchedModel::computeInstrLatency(const MCInst &Inst) const {
  if (hasInstrMdlModel(&Inst)) {
    return llvm::mdl::calculateInstructionLatency(&Inst, STI, TII);
  }

  if (hasInstrSchedModel())
    return capLatency(SchedModel.computeInstrLatency(*STI, *TII, Inst));
  return computeInstrLatency(Inst.getOpcode());
}

unsigned
TargetSchedModel::computeInstrLatency(const MachineInstr *MI,
                                      bool UseDefaultDefLatency) const {

  // If we have an MDL model, use it to determine the latency. Itineraries
  // define latencies based on stages (resources), so we do it differently
  // for those models.
  if (hasInstrMdlModel(MI) && !hasInstrItineraries())
    return llvm::mdl::calculateInstructionLatency(MI, STI);

  // For the itinerary model, fall back to the old subtarget hook.
  // Allow subtargets to compute Bundle latencies outside the machine model.
  if (hasInstrItineraries() || MI->isBundle() ||
      (!hasInstrSchedModel() && !UseDefaultDefLatency))
    return TII->getInstrLatency(&InstrItins, *MI);

  if (hasInstrSchedModel()) {
    const MCSchedClassDesc *SCDesc = resolveSchedClass(MI);
    if (SCDesc->isValid())
      return computeInstrLatency(*SCDesc);
  }
  return TII->defaultDefLatency(SchedModel, *MI);
}

unsigned TargetSchedModel::
computeOutputLatency(const MachineInstr *DefMI, unsigned DefOperIdx,
                     const MachineInstr *DepMI) const {
  if (!SchedModel.isOutOfOrder())
    return 1;

  // Out-of-order processor can dispatch WAW dependencies in the same cycle.

  // Treat predication as a data dependency for out-of-order cpus. In-order
  // cpus do not need to treat predicated writes specially.
  //
  // TODO: The following hack exists because predication passes do not
  // correctly append imp-use operands, and readsReg() strangely returns false
  // for predicated defs.
  Register Reg = DefMI->getOperand(DefOperIdx).getReg();
  const MachineFunction &MF = *DefMI->getMF();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  if (!DepMI->readsRegister(Reg, TRI) && TII->isPredicated(*DepMI))
    return computeInstrLatency(DefMI);

  // If we have a per operand scheduling model, check if this def is writing
  // an unbuffered resource. If so, it treated like an in-order cpu.
  // Note that Itineraries always assume output latencies are 0.
  if (hasInstrMdlModel(DefMI) && !hasInstrItineraries()) {
    Instr Ins(DefMI, getSubtargetInfo());
    if (auto *Subunit = Ins.getSubunit()) {
      if (auto *Refs = (*Subunit)[0].getUsedResourceReferences())
        for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
          if (Ref.isFus() && Ref.getCycles() && Ref.isUnbuffered())
            return 1;

      if (auto *Prefs = (*Subunit)[0].getPooledResourceReferences())
        for (const auto &Ref : ReferenceIter<PooledResourceRef>(Prefs, &Ins))
          if (Ref.isFus() && Ref.isUnbuffered())
            return 1;
    }
    return 0;
  }

  if (hasInstrSchedModel()) {
    const MCSchedClassDesc *SCDesc = resolveSchedClass(DefMI);
    if (SCDesc->isValid()) {
      for (const MCWriteProcResEntry *PRI = STI->getWriteProcResBegin(SCDesc),
             *PRE = STI->getWriteProcResEnd(SCDesc); PRI != PRE; ++PRI) {
        if (!SchedModel.getProcResource(PRI->ProcResourceIdx)->BufferSize)
          return 1;
      }
    }
  }
  return 0;
}

double
TargetSchedModel::computeReciprocalThroughput(const MachineInstr *MI) const {
  if (hasInstrMdlModel(MI))
    return STI->getCpuInfo()->getReciprocalThroughput(STI, MI);

  if (hasInstrItineraries()) {
    unsigned SchedClass = MI->getDesc().getSchedClass();
    return MCSchedModel::getReciprocalThroughput(SchedClass,
                                                 *getInstrItineraries());
  }
  if (hasInstrSchedModel())
    return MCSchedModel::getReciprocalThroughput(*STI, *resolveSchedClass(MI));

  return 0.0;
}

double
TargetSchedModel::computeReciprocalThroughput(unsigned Opcode) const {
  unsigned SchedClass = TII->get(Opcode).getSchedClass();
  if (hasInstrItineraries())
    return MCSchedModel::getReciprocalThroughput(SchedClass,
                                                 *getInstrItineraries());
  if (hasInstrSchedModel()) {
    const MCSchedClassDesc &SCDesc = *SchedModel.getSchedClassDesc(SchedClass);
    if (SCDesc.isValid() && !SCDesc.isVariant())
      return MCSchedModel::getReciprocalThroughput(*STI, SCDesc);
  }

  return 0.0;
}

double
TargetSchedModel::computeReciprocalThroughput(const MCInst &MI) const {
  if (hasInstrMdlModel(&MI))
    return STI->getCpuInfo()->getReciprocalThroughput(STI, TII, &MI);
  if (hasInstrSchedModel())
    return SchedModel.getReciprocalThroughput(*STI, *TII, MI);
  return computeReciprocalThroughput(MI.getOpcode());
}

bool TargetSchedModel::enableIntervals() const {
  if (ForceEnableIntervals)
    return true;

  return SchedModel.EnableIntervals;
}

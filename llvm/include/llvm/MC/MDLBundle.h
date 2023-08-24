//===- MDLBundle.h  MDL-based instruction bundling implementation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MDL-based Bundle Packer.  This file defines the APIs and implementation
// for an MDL-based parallel-issue instruction bundler, appropriate for both
// VLIW processors and OOO, superscalar processors. It can bundle MCInsts or
// MachineInstrs. The general approach is to incrementally build up bundles
// one instruction at a time, adding the highest priority instructions first.
// This is a non-ejecting bundler: when adding a new instruction to a bundle,
// we don't eject instructions in the bundle to allow a new instruction fit,
// although we do reconsider all functional unit and resource assignments each
// time we add an instruction.
//
// A bundle is self-contained - it contains all the functional unit and
// resource assignments for the instructions in the bundle.
//
// We also define a resource reservation table which can be used to track
// scheduled instructions' resource uses over time, and it can be used as an
// additional external constraint to the bundle packer.
//
// The "heavy lifting" of this is performed in templatized member functions
// which are specialized for each subtarget, so that most of the objects
// can be statically sized and allocated.
//
//===----------------------------------------------------------------------===//

#ifndef MDL_BUNDLE_H_
#define MDL_BUNDLE_H_

#include "llvm/MC/MDLInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"

namespace llvm {
namespace mdl {

/// Given a set of LLVM instructions, create a set of bundling candidates.
inline SlotSet createCandidateSet(const InstrSet &Insts,
                                  const TargetSubtargetInfo *STI) {
  SlotSet Candidates;
  Candidates.reserve(Insts.size());
  for (unsigned i = 0; i < Insts.size(); i++)
    Candidates.emplace_back(Insts[i], STI);
  return Candidates;
}

/// Given a set of LLVM instructions, create a set of bundling candidates.
inline SlotSet createCandidateSet(const MCInstrSet &Insts,
                                  const MCSubtargetInfo *STI,
                                  const MCInstrInfo *MCII) {
  SlotSet Candidates;
  Candidates.reserve(Insts.size());
  for (unsigned i = 0; i < Insts.size(); i++)
    Candidates.emplace_back(Insts[i], STI, MCII);
  return Candidates;
}

/// Given a set of instructions ordered by priority, attempt to bundle as
/// many of them as possible, and return the set of bundled instructions.
/// Note that this method doesn't consider external resource conflicts.
template <typename CpuParams>
SlotSet CpuConfig<CpuParams>::bundleCandidates(const SlotSet *Candidates) {
  SlotSet Bundle; // Start with an empty bundle.
  Bundle.reserve(CpuParams::MaxIssue);

  // One at a time, attempt to add instuctions to a bundle.
  ReservationsConfig<CpuParams> Res; // Start with an empty reservation table
  for (auto &Cand : *Candidates)
    addToBundle(Bundle, Cand, Res);

  return Bundle;
}

/// Determine if an instruction can be added to a bundle.  Return true if
/// it can be, else return false. Note: we don't modify the SlotSet!
/// This requires all the work of adding to a bundle without actually adding
/// to the bundle.  Normally you should just call AddToBundle directly.
template <typename CpuParams>
bool CpuConfig<CpuParams>::canAddToBundle(SlotSet &Bundle,
                                          const SlotDesc &Candidate,
                                          const Reservations &Res) {
  SlotSet LocalBundle = Bundle; // We don't want to modify the bundle.
  return addToBundle(LocalBundle, Candidate, Res);
}

/// Try to add an instruction to a bundle:
///   - First check its resources against already-scheduled instructions.
///   - Make sure it can allocate resources for any pool requests.
/// Return true if we successfully added the instruction.
template <typename CpuParams>
bool CpuConfig<CpuParams>::addToBundle(SlotSet &Bundle,
                                       const SlotDesc &Candidate,
                                       const Reservations &Res) {
  // MDL-TODO: we need to differentiate instructions that have no resource use
  // from instructions that are pseudo instructions, or have "zero cost", as
  // Itineraries do.
  // if (Candidate.getSubunits() == nullptr) return true;
  Bundle.push_back(Candidate);
  if (Candidate.getSubunits() == nullptr)
    return true;

  // Starting with the existing bundle and its subunit assignments, recursively
  // find a set of subunit assignments and resource allocations that
  // accomodates the new instruction. So first make a copy of the original,
  // since we don't want to modify that.
  auto LocalRes = static_cast<const ReservationsConfig<CpuParams> &>(Res);
  if (attemptToBundle(Bundle, LocalRes, 0, false) == BundleStatus::kSuccess)
    return true;

  Bundle.pop_back();
  return false;
}

// Given a "final" bundle, add it to the resource reservation table.
template <typename CpuParams>
void CpuConfig<CpuParams>::addBundleToReservation(SlotSet &Bundle,
                                                  Reservations &Res) {
  auto &LocalRes = static_cast<ReservationsConfig<CpuParams> &>(Res);
  for (auto &Slot : Bundle) {
    if (Slot.getSubunits())
      addResources(Slot, (*Slot.getSubunits())[Slot.getSubunitId()], LocalRes);

    // Add allocated resources
    for (auto &Item : Slot.getResources())
      LocalRes.set(Item.ResId, Item.Phase, Item.Cycles);
  }
}

/// Given a proposed bundle, find a permutation of the candidate instructions'
/// subunits that don't have interfering resource usage.
/// If some of the instructions have already been bundled (they have an
/// existing subunit id), use that set of subunits as a starting point.
/// If any of assignments need to be reconsidered, reset all the remaining
/// unbundled instructions' subunit ids.
template <typename CpuParams>
BundleStatus
CpuConfig<CpuParams>::attemptToBundle(SlotSet &Bundle,
                                      ReservationsConfig<CpuParams> &Res,
                                      int WhichSlot, bool Reset) {
  // If we've found a valid bundle assignment for all the candidates, attempt
  // to allocate resources for the entire bundle.
  // NOTE: Currently, if the pool allocation fails, the current bundle of
  // instructions fails. Backtracking over subunit assignments typically won't
  // impact whether the pool allocation succeeds, and its EXTREMELY expensive.
  // This is based on the observation that an instruction's pool requests are
  // typically identical across all subunit candidates.
  if (WhichSlot == (int)Bundle.size())
    return allocatePools(Bundle, Res);

  SlotDesc &Item = Bundle[WhichSlot];

  // If this is some kind of pseudo-instruction, we don't need to check
  // its resources.
  if (Item.getSubunits() == nullptr)
    return attemptToBundle(Bundle, Res, WhichSlot + 1, Reset);

  // Iterate over this instruction's subunits, and try to find a slot
  // allocation that is compatible with earlier bundled instructions. If
  // we succeed, recursively try the rest of the slots.
  // If this is the first attempt to bundle this instruction, we iterate over
  // all of its subunits, otherwise we start with the previous subunit
  // assignment.
  SubunitVec &subunits = *Item.getSubunits();
  int OriginalId = Item.getSubunitId();
  if (Reset)
    Item.setSubunitId(0);

  for (int Id = Item.getSubunitId(); Id < (int)subunits.size();
       Id++, Reset = true) {
    auto LocalRes = Res;
    if (addResources(Item, subunits[Id], LocalRes)) {
      Item.setSubunitId(Id);
      auto Status = attemptToBundle(Bundle, LocalRes, WhichSlot + 1, Reset);
      // If we succeeded, update the resource set, and return success.
      if (Status == BundleStatus::kSuccess) {
        Res = LocalRes;
        return Status;
      }
      // If we failed allocation for the bundle, return that we failed.
      // Note: if a target has specific instructions which have subunits with
      // very different pool allocation requests, we may want to simply
      // delete the next statement, and the packing will try much harder to
      // find allocations for different combinations of subunits.  This is
      // VERY expensive, and probably not terribly common. This might happen
      // if a set of equivalent candidate functional units have separate pools
      // of resources to allocate from.
      if (Status == BundleStatus::kAllocationFailed) {
        Item.setSubunitId(OriginalId); // Back out of subunit assignments.
        return Status;
      }
    }
  }
  Item.setSubunitId(OriginalId); // Back out of subunit assignments.
  return BundleStatus::kBundleFailed;
}

/// Add resource reference to current resource set.  Return true if there were
/// no conflicts.  Note that we don't attempt to share these resources, even
/// if the resource is shareable. (Its quite expensive, even if done
/// efficiently)
template <typename CpuParams>
bool CpuConfig<CpuParams>::addResources(SlotDesc &Slot, Subunit &WhichSubunit,
                                        ReservationsConfig<CpuParams> &Res) {
  if (auto *Refs = WhichSubunit.getUsedResourceReferences()) {
    for (auto const &Ref : ReferenceIter<ResourceRef>(Refs, Slot.getInst())) {
      if (Ref.isUse() && !Ref.isDuplicate())
        if (Res.testSet(Ref.getResourceId(), Ref.getPhase(Slot.getInst()),
                        Ref.getCycles()))
          return false;
    }
  }
  return true;
}

/// Find statically allocated resource uses and add them to the values set.
template <typename CpuParams>
void CpuConfig<CpuParams>::findStaticResources(
    SlotSet &Bundle, ResourceValues<CpuParams> &Values) {
  for (auto &Slot : Bundle)
    if (Slot.getSubunits())
      if (auto *Refs = Slot.getSubunit()->getUsedResourceReferences())
        for (auto &Ref : ReferenceIter<ResourceRef>(Refs, Slot.getInst()))
          if (Ref.isUse() && Ref.isValidOperandIndex() && Ref.isShared()) {
            int value = Slot.getInst()->getOperand(Ref.getOperandIndex());
            Values.set(Ref.getResourceId(), &value, 1);
          }
}

/// Given a candidate bundle and a proposed set of subunits, attempt to find
/// a resource allocation for all the pool requests.  Note that we save any
/// prior allocations, and restore them if this allocation fails.
template <typename CpuParams>
BundleStatus
CpuConfig<CpuParams>::allocatePools(SlotSet &Bundle,
                                    ReservationsConfig<CpuParams> &Res) {
  std::vector<AllocatedResourceSet> SavedResources;

  // Collate all pool requests into lists for each subpool and size combination.
  PoolRequests<CpuParams> Pools;
  int RequestCount = 0;     // Keep track of the number of requests.
  for (auto &Slot : Bundle) {
    SavedResources.push_back(std::move(Slot.getResources()));
    if (Slot.getSubunits()) {
      if (auto *Refs = Slot.getSubunit()->getPooledResourceReferences())
        for (auto &Ref : ReferenceIter<PooledResourceRef>(Refs, Slot.getInst()))
          if (Ref.isUse()) {
            Pools.AddPoolRequest(&Slot, &Ref);
            RequestCount++;
          }
    }
  }

  // If there were no pooled requests, we've succeeded!
  if (RequestCount == 0) return BundleStatus::kSuccess;
  
  // Find the set of shared resources used by the bundle.
  ResourceValues<CpuParams> Values;
  findStaticResources(Bundle, Values);

  // Allocate each pool independently.  If any pool fails, we fail, and
  // restore all the old resource allocations.
  for (int PoolId = 0; PoolId < CpuParams::PoolCount; PoolId++) {
    if (!allocatePool(Pools.getPool(PoolId), Res, Values)) {
      int Id = 0;
      for (auto &Slot : Bundle)
        Slot.setResources(std::move(SavedResources[Id++]));
      return BundleStatus::kAllocationFailed;
    }
    // If we've serviced all the requests, we can quit.
    RequestCount -= Pools.getPool(PoolId).size();
    if (RequestCount <= 0) break;
  }
  return BundleStatus::kSuccess;
}

/// Given a pool request, fetch operand values from the instruction, and return
/// the normalized values in the "values" array.  Return false if we can't
/// share values.
inline bool getOperandValues(PoolRequest &Item, int Count, int Values[]) {
  Instr *Inst = Item.getInst();
  int OperandId = Item.getOperandId();
  auto *Pool = Item.getRef()->getPool();
  if (Pool->hasValueFunc() && Inst->isOpndLiteral(OperandId))
    return Pool->getValues(Inst, OperandId, Count, Values);

  // For now, we don't attempt to share virtual register-based resources.
  if (Inst->isOpndVirtualRegister(OperandId))
    return false;

  // Handle simple literals and registers.
  if (Count == 1) {
    Values[0] = Inst->getOperand(OperandId);
    return true;
  }
  return false;
}

/// Attempt to allocate a set of pool resources for a reference.
/// Check to see if we can share resources with existing resource allocations.
/// Note that for multi-cycle pool allocations, we don't have enough information
/// to share these kinds of resources.
template <typename CpuParams>
bool CpuConfig<CpuParams>::allocateResource(PoolRequest &Item, int Id,
                                            int Count,
                                            ReservationsConfig<CpuParams> &Res,
                                            ResourceValues<CpuParams> &Values) {
  // Check to see if we can share a resource with another operand.
  int OpndValues[CpuParams::MaxPoolAllocation + 1] = {0};

  int Phase = Item.getPhase();
  int Cycles = Item.getCycles();

  // If this item has shared bits (width), fetch the operands' normalized
  // values from the operand, and check them against currently shared values.
  bool Shared = Cycles == 1 && Item.isShared() &&
                getOperandValues(Item, Count, OpndValues);
  if (Shared) {
    if (Values.check(Id, OpndValues, Count)) {
      for (int Off = 0; Off < Count; Off++) {
        int Opnd = Item.getRef()->getOperandIndex();
        Item.getSlot()->getResources()
            .emplace_back(Id + Off, Phase, Cycles, Opnd, OpndValues[Off], Count);
      }
      return true;
    }
  }

  // Non-sharing case - see if all the specified resources are available.
  for (int Off = 0; Off < Count; Off++)
    if (Res.test(Id + Off, Phase, Cycles))
      return false;

  // If the entire allocation succeeds, mark all the resources as reserved.
  // Add the resources used to the slot they're allocated for.
  for (int Off = 0; Off < Count; Off++) {
    Res.set(Id + Off, Phase, Cycles);
    Item.getSlot()->getResources().emplace_back(Id + Off, Phase, Cycles);
  }

  // If the item is shared, store off the operand values.
  if (Shared)
    Values.set(Id, OpndValues, Count);
  return true;
}

/// Given a set of pool requests for a single pool, determine if we can
/// allocate all of them. Each pool request list has the following attributes:
///    - all members of the list are requesting the same subpool.
///    - all requests are for the same (non-zero) number of resources.
///    - this set of pool requests is less constrained than previous pool
///      request sets, and more constrained than future pool request sets.
template <typename CpuParams>
bool CpuConfig<CpuParams>::allocatePool(PoolRequestSet &Pool,
                                        ReservationsConfig<CpuParams> &Res,
                                        ResourceValues<CpuParams> &Values) {
  if (Pool.empty())
    return true;

  ResourceIdType *Base = Pool[0].getResourceIds(); // ptr to resource id pool
  int Count = Pool[0].getCount();                  // # of resources requested
  int First = Pool[0].getFirst();           // offset from first resource id
  int Last = Pool[0].getLast() - Count + 1; // offset from last resource id

  // For multi-resource allocations, make sure first and last are reasonable.
  if (Count != 1) {
    if (Last % Count != 0)
      Last -= Last % Count;
    if (First % Count != 0)
      First += Count - (First % Count);
  }

  // Choose an allocation order for all requests for this pool (or subpool).
  // If the request is for any member of a pool, order doesn't matter.
  // If the request is for the head of the pool, allocate in forward order.
  // If the request is for the tail of the pool, allocate in backwards order.
  if (First == 0) {
    for (auto &Item : Pool) {
      bool Allocated = false;
      for (int id = First; id <= Last; id += Count)
        if ((Allocated = allocateResource(Item, Base[id], Count, Res, Values)))
          break;
      if (!Allocated)
        return false;
    }
  } else {
    for (auto &Item : Pool) {
      bool Allocated = false;
      for (int Id = Last; Id >= First; Id -= Count)
        if ((Allocated = allocateResource(Item, Base[Id], Count, Res, Values)))
          break;
      if (!Allocated)
        return false;
    }
  }

  return true;
}

// Write out all the instructions in a bundle.
inline void CpuInfo::dumpBundle(std::string Cpu, std::string Msg,
                                SlotSet &Bundle) {
  std::cout << "\n"
            << Msg << " " << Cpu << "  " << Bundle.size()
            << " -----------------------------------------\n";
  for (auto &Slot : Bundle)
    std::cout << dumpSlot("  ", Slot);
}

/// Write out a single bundled instruction, and what resources it uses.
inline std::string CpuInfo::dumpSlot(std::string Msg, SlotDesc &Slot) {
  std::string Out = Msg;
  int Id = Slot.getInst()->getOpcode();
  auto Name = Slot.getInst()->getName();
  Out += formatv("su: {0}/{1} {2} : {3}", Slot.getSubunitId(),
                 Slot.getSubunits()->size(), Id, Name);

  if (!Slot.getSubunits())
    return Out + "\n";

  if (auto *Refs = Slot.getSubunit()->getUsedResourceReferences()) {
    Out += "\t <";
    for (auto &ref : ReferenceIter<ResourceRef>(Refs, Slot.getInst()))
      Out += formatv("{0},", ref.getResourceId());
    Out += ">";
  }

  if (!Slot.getResources().empty()) {
    Out += "\t [";
    for (auto &Res : Slot.getResources())
      Out += formatv("{{{0},{1},{2}},", Res.ResId, Res.OpndId, Res.Value);
    Out += "]";
  }

  return Out + "\n";
}

// Validate that a bundle doesn't oversubscribe resources.
template <typename CpuParams>
bool CpuConfig<CpuParams>::validateBundle(std::string Cpu, SlotSet &Bundle) {
  std::map<int, int> Resources[CpuParams::MaxUsedResourceId + 1];

  // First check subunit static resources.
  for (auto &Slot : Bundle) {
    if (!Slot.getSubunits())
      continue;
    if (auto *Refs = Slot.getSubunit()->getUsedResourceReferences()) {
      for (auto &Ref : ReferenceIter<ResourceRef>(Refs, Slot.getInst())) {
        if (Ref.isFus())
          continue;
        int Phase = Ref.getPhase(Slot.getInst());
        int Id = Ref.getResourceId();
        if (Ref.isValidOperandIndex() && Ref.isShared()) {
          int OpndValue = Slot.getInst()->getOperand(Ref.getOperandIndex());
          if (!Resources[Phase].emplace(Id, OpndValue).second) {
            if (Resources[Phase][Id] != OpndValue) {
              dumpBundle(Cpu,
                         formatv("Failed:{0},{1},{2}", Id, OpndValue, Phase),
                         Bundle);
              return false;
            }
          }
        }
      }
    }

    // Check allocated resources (all are in the same pipe phase).
    for (auto &Res : Slot.getResources())
      if (!Resources[0].emplace(Res.ResId, Res.Value).second) {
        if (Resources[0][Res.ResId] != Res.Value) {
          dumpBundle(Cpu,
                     formatv("Failed:{0},{1} ", Res.ResId, Res.Value), Bundle);
          return false;
        }
      }
  }
  return true;
}

} // namespace mdl
} // namespace llvm

#endif // MDL_BUNDLE_H_

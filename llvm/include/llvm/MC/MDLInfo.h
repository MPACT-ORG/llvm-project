//===- MDLInfo.h - MDL-based instructions modeling ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions that describe the generated machine
// description database. These definitions must stay in sync with what the
// mdl compiler produces. The overall schema of the database looks like this:
//
// The top-level object for each CPU family is the CpuTable, which is a
// dictionary of subtarget descriptors. Each entry in the dictionary is a
// subtarget name and a CpuInfo object that describes a single subtarget.
//
// A CpuInfo object captures a basic set of architectural parameters, and
// includes a pointer to the CPU's subunit table and optional forwarding
// information table.
//
// Each subunit table contains a pointer to a vector of valid subunits for
// each instruction valid on that CPU.
//
// Each subunit object is a vector of tuples. Each tuple represents one
// possible behavior of an instruction (or a set of instructions), including
// all of its operand references, its resource requirements, its pooled
// resource requirements, and any additional operand constraints to apply
// to the instruction. Each subunit object therefore contains all feasible
// behaviors of a client instruction.
//
// The four subunit components are described in separate tables, and heavily
// shared across subunits and CPUs.
//
//===----------------------------------------------------------------------===//

#ifndef MDL_INFO_H
#define MDL_INFO_H

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {

// Declarations of LLVM types that describe targets and instructions.
class MachineInstr;
class TargetSubtargetInfo;
class TargetInstrInfo;
class MCInst;
class MCSubtargetInfo;
struct MCSchedModel;
class MCInstrInfo;
class TargetSchedModel;

namespace mdl {

// Fundamental type of a reference to an operand or resource.
// These are powers of two so that we can quickly check for subsets of them.
struct ReferenceTypes {
  using Item = int16_t;
  static constexpr Item RefNull = 0;
  static constexpr Item RefPred = 1;     // use of a predicate operand
  static constexpr Item RefUse = 2;      // use of an operand and/or resource
  static constexpr Item RefDef = 4;      // operand def (resource use)
  static constexpr Item RefKill = 8;     // kill of an operand
  static constexpr Item RefUseDef = 16;  // operand use/def (use of operand)
  static constexpr Item RefHold = 32;    // wait on availability of resource
  static constexpr Item RefReserve = 64; // reserve resource until some cycle
  static constexpr Item RefFus = 128;    // use a functional unit
  static constexpr Item RefCond = 256;   // conditional reference

  static constexpr Item AnyUse = RefUse | RefUseDef | RefPred;
  static constexpr Item AnyDef = RefDef | RefUseDef;
};
using ReferenceType = ReferenceTypes::Item;

// The index of an operand into an instruction.
using OperandId = int8_t; // These start at 0, so < 0 means invalid.

// Reference flags field.  Values are powers of 2 so we can combine them.
struct ReferenceFlags {
  using Item = int8_t;
  static constexpr int kNone = 0;
  // Reference flags for operand and resource references.
  static constexpr int kProtected = 1;   // Reference is hardware-protected.
  static constexpr int kUnprotected = 2; // Reference is not h/w protected.
  static constexpr int kDuplicate = 4;   // Reference is duplicate reference

  // Reference flags for explicit functional unit references.
  static constexpr int kUnbuffered = 1;   // Funcunit doesn't have a queue.
  static constexpr int kInOrder = 2;      // Funcunit is in-order.
  static constexpr int kOutOfOrder = 4;   // Funcunit is out-of-order.
  static constexpr int kBeginGroup = 8;   // Instr must begin issue group.
  static constexpr int kEndGroup = 16;    // Instr must end issue group.
  static constexpr int kSingleIssue = 32; // Instr must issue alone.
  static constexpr int kRetireOOO = 64;   // Instr may retire out of order.

  static bool is_protected(Item flag) { return flag & kProtected; }
  static bool is_unprotected(Item flag) { return flag & kUnprotected; }
  static bool is_duplicate(Item flag) { return flag & kDuplicate; }
  static bool is_unbuffered(Item flag) { return flag & kUnbuffered; }
  static bool is_in_order(Item flag) { return flag & kInOrder; }
  static bool is_out_of_order(Item flag) { return flag & kOutOfOrder; }
  static bool is_buffered(Item flag) { return flag & (kInOrder | kOutOfOrder); }
  static bool is_begin_group(Item flag) { return flag & kBeginGroup; }
  static bool is_end_group(Item flag) { return flag & kEndGroup; }
  static bool is_single_issue(Item flag) { return flag & kSingleIssue; }
  static bool is_retire_ooo(Item flag) { return flag & kRetireOOO; }
};
using ReferenceFlag = ReferenceFlags::Item;

// The index of a reference resource or resource pool.
using ResourceIdType = int16_t; // These start at 0, so < 0 means invalid.

// CPU-specific id of each resource pool.
using PoolIdType = int8_t;

// The number of resources in a pool.
using PoolSizeType = int8_t;

// The number of bits represented by a resource, if shared.
using PoolBitsType = int8_t; // -1 means resource is not shared.

// An integer that represents a pipeline stage.
using PipePhaseType = int16_t; // These start at 0, so < 0 means invalid.

// An integer that represents the number of stages a resource is used.
using UseCyclesType = uint16_t;

// An index into the register class table.
using RegisterClassIndexType = int8_t; // These start at 0, so < 0 is invalid.

// Definitions of objects in the target database.
class Instr;             // MDL abstract description of an instruction
class OperandRef;        // A single operand reference
class ResourceRef;       // A single resource reference
class PoolDescriptor;    // An allocation pool descriptor
class PooledResourceRef; // A pooled resource allocation descriptor
class OperandConstraint; // An operand constraint descriptor
class Subunit;           // A subunit descriptor
class CpuInfo;           // Information about a single CPU/Subtarget
class CpuTableDef;       // A table of all CPUs/Subtargets

template <class T> class ConditionalRef;

// Some compilers don't allow specialization of a type alias (like this):
//      using ConditionalRefPool = class ConditionalRef<PooledResourceRef>;
// So we need to do this workaround:
template <class T> struct TypeAlias {
  using type = ConditionalRef<T>;
};

using ConditionalRefOpnd = typename TypeAlias<OperandRef>::type;
using ConditionalRefRes = typename TypeAlias<ResourceRef>::type;
using ConditionalRefPool = typename TypeAlias<PooledResourceRef>::type;

// Function definitions used to evaluate predicates, calculating pipeline
// phases, determining resource pool sizes, and fetching values from
// instructions
using PredFunc = bool (*)(Instr *ins);
using PipeFunc = unsigned (*)(Instr *ins);
using PoolFuncType = int (*)(Instr *ins, int operand_index);
using OpndValueFunc = bool (*)(Instr *ins, int operand_index, int count,
                               int values[]);

// A simple wrapper to check the range for calculated resource latencies.
inline int getResourcePhase(PipeFunc Func, Instr *Ins);

//----------------------------------------------------------------------------
// We initialize a *LOT* of vectors of objects, which incurs a significant
// runtime overhead when the compiler autoinitialization occurs. So rather than
// use vectors, we use an "InitializationVector" instead, which incurs zero
// overhead**.  This is a limited "vector" substitute with limited iteration
// capabilities, but is sufficient for all uses of these objects.
//----------------------------------------------------------------------------
// **Note: If the client type has a constructor, the compiler by default
// generates code to call the constructor, and the translation unit must be
// compiled with optimization to eliminate the code and only produce
// initialized data.  Alternatively, we can delete all the constructors so
// that we don't -have- to compile with optimization and still avoid the
// initialization time overhead.
//----------------------------------------------------------------------------
template <typename T> class InitializationVector {
public:
  unsigned char Size; // Number of entries in the vector.
  T *Data;            // Pointer to the data.

public:
  class Iterator {
    T *Iter;

  public:
    T &operator*() const { return *Iter; }
    T *operator->() { return Iter; }
    Iterator &operator++() {
      ++Iter;
      return *this;
    }
    Iterator operator++(int) {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator==(const Iterator &a, const Iterator &b) {
      return a.Iter == b.Iter;
    }
    friend bool operator!=(const Iterator &a, const Iterator &b) {
      return a.Iter != b.Iter;
    }
    Iterator(T *Data) : Iter(Data) {}
  };
  Iterator begin() { return Iterator(&Data[0]); }
  Iterator end() { return Iterator(&Data[Size]); }
  unsigned size() const { return Size; }
  T &operator[](int index) { return Data[index]; }
};

//-----------------------------------------------------------------------------
// ReferenceIter lets us wrap a vector (or InitializationVector) of
// reference lists (that may include nested predicated sublists) and iterate
// over all the members transparently.
// The client T type must have 2 methods:
//     isCond() - returns true if this is a "conditional" reference
//     getIfElse() - Return the pointer to a "conditional reference object".
// The associated "conditional reference object" must have 3 methods:
//     evalPredicate(Ins) - Evaluate the predicate, return true/false.
//     getElseClause() - Return the else clause associated with the reference.
//     getRefs() - Return the predicated reference vector pointer.
//-----------------------------------------------------------------------------
// Since references lists can have arbitrarily deeply nested conditionals, the
// "iterator" needs to dynamically keep track of nested conditional reference
// list iterators.  We use vectors of input_iterators to implement a stack.
// Since conditional reference lists are the exception, we want the "normal"
// case to run as fast as possible, so we only use the iterator vectors when
// we encounter a conditional reference.
//-----------------------------------------------------------------------------
// Note that these client objects are generally PODs because they are
// auto-initialized by the MDL compiler.
//-----------------------------------------------------------------------------
template <typename T> class ReferenceIter {
private:
  InitializationVector<T> *Refs; // The top-level vector of references.
  Instr *Ins;                    // Instruction to use with predicates.

  struct Iterator {
    Instr *Ins;
    typename InitializationVector<T>::Iterator Iter, End;
    std::vector<typename InitializationVector<T>::Iterator> Iters, Ends;

    Iterator(Instr *Ins, InitializationVector<T> *Refs)
        : Ins(Ins), Iter(Refs->begin()), End(Refs->end()) {
      advance();
    }

    Iterator(Instr *Ins, typename InitializationVector<T>::Iterator End)
        : Ins(Ins), Iter(End), End(End) {}

    T &operator*() const { return *Iter; }
    T *operator->() { return Iter; }

    Iterator &operator++() {
      ++Iter;
      advance();
      return *this;
    }
    Iterator operator++(int) {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    // When an iterator is incremented, if we've reached the end of the
    // vector, we pop the stack of reference-lists (or just return).
    void advance() {
      // If we've reached the end of a vector, pop it off the stack.
      if (Iter == End) {
        if (Iters.empty())
          return;
        Iter = Iters.back();
        Iters.pop_back();
        End = Ends.back();
        Ends.pop_back();
        ++Iter;
        return advance();
      }

      // If the entry is a value, we're done advancing.
      auto &Ref = *Iter;
      if (!Ref.isCond())
        return;

      // Evaluate predicates until we find a true (or missing) one.
      // When we counter a TRUE predicate, push the current list onto the
      // reference-list stack, and start iterating over the new one.
      for (auto *cond = Ref.getIfElse(); cond; cond = cond->getElseClause()) {
        if (cond->evalPredicate(Ins)) {
          if (cond->getRefs() == nullptr)
            break;
          Iters.push_back(Iter);
          Iter = cond->getRefs()->begin();
          Ends.push_back(End);
          End = cond->getRefs()->end();
          return advance();
        }
      }
      Iter++; // Advance past a conditional ref with an empty clause.
      return advance();
    }

    friend bool operator==(const Iterator &a, const Iterator &b) {
      return a.Iter == b.Iter;
    }
    friend bool operator!=(const Iterator &a, const Iterator &b) {
      return a.Iter != b.Iter;
    }
  };

public:
  ReferenceIter(InitializationVector<T> *Refs, Instr *Ins)
      : Refs(Refs), Ins(Ins) {}

  Iterator begin() { return Iterator(this->Ins, Refs); }
  Iterator end() { return Iterator(this->Ins, Refs->end()); }
};

// Containers of initialized reference objects.
using OperandRefVec = InitializationVector<OperandRef>;
using ResourceRefVec = InitializationVector<ResourceRef>;
using PooledResourceRefVec = InitializationVector<PooledResourceRef>;
using OperandConstraintVec = InitializationVector<OperandConstraint>;

// A set of subunits for a particular instruction/CPU combination
using SubunitVec = InitializationVector<Subunit>;

// A mapping of instructions to subunit lists.
using SubunitTable = std::vector<SubunitVec *>;

//-----------------------------------------------------------------------------
/// A description of a single conditional reference object.
/// Used for operand, resource, and pooled resource references.
//-----------------------------------------------------------------------------
template <class T> class ConditionalRef {
  PredFunc Predicate;            // function to evaluate the predicate
  InitializationVector<T> *Refs; // conditional refs
  ConditionalRef<T> *ElseClause; // optional else clause
public:
  ConditionalRef(PredFunc Predicate, InitializationVector<T> *Refs,
                 ConditionalRef<T> *ElseClause)
      : Predicate(Predicate), Refs(Refs), ElseClause(ElseClause) {}

  bool hasPredicate() const { return Predicate != nullptr; }
  bool evalPredicate(Instr *ins) const {
    return Predicate == nullptr || Predicate(ins);
  }
  InitializationVector<T> *getRefs() const { return Refs; }
  ConditionalRef<T> *getElseClause() const { return ElseClause; }
};

//-----------------------------------------------------------------------------
/// A reference to an instruction's operand.
//-----------------------------------------------------------------------------
class OperandRef {
  ReferenceType Type;  // type of the reference
  ReferenceFlag Flags; // protected or unprotected
  PipePhaseType Phase; // pipeline phase of the reference
  union {
    PipeFunc PhaseFunc;                 // optional pointer to phase function
    ConditionalRef<OperandRef> *IfElse; // conditional reference descriptor
  };
  OperandId OperandIndex; // operand index
public:
  // Construct a normal unconditional reference.
  OperandRef(ReferenceType Type, ReferenceFlag Flags, PipePhaseType Phase,
             PipeFunc PhaseFunc, OperandId OperandIndex)
      : Type(Type), Flags(Flags), Phase(Phase), PhaseFunc(PhaseFunc),
        OperandIndex(OperandIndex) {}
  // Construct a conditional reference.
  OperandRef(ConditionalRef<OperandRef> *IfElse)
      : Type(ReferenceTypes::RefCond), IfElse(IfElse) {}

  ReferenceType getType() const { return Type; }
  bool isDef() const { return Type & ReferenceTypes::AnyDef; }
  bool isUse() const { return Type & ReferenceTypes::AnyUse; }
  bool isCond() const { return Type == ReferenceTypes::RefCond; }
  bool isDefaultDef() const { return isDef() && OperandIndex == -1; }

  ReferenceFlag getFlags() const { return Flags; }
  bool isProtected() const { return Flags & ReferenceFlags::kProtected; }
  bool isUnprotected() const { return Flags & ReferenceFlags::kUnprotected; }
  bool isDuplicate() const { return Flags & ReferenceFlags::kDuplicate; }
  int getPhase(Instr *Ins) const { return PhaseFunc ? PhaseFunc(Ins) : Phase; }
  int getOperandIndex() const { return OperandIndex; }
  ConditionalRef<OperandRef> *getIfElse() const { return IfElse; }
};

//-----------------------------------------------------------------------------
/// A reference to a single resource.
//-----------------------------------------------------------------------------
class ResourceRef {
  ReferenceType Type;        // type of the reference (def, use, etc)
  ReferenceFlag Flags;       // protected, unprotected, or duplicate ref
  PipePhaseType Phase;       // pipeline phase of the reference
  PipeFunc PhaseFunc;        // optional pointer to phase function
  UseCyclesType UseCycles;   // number of cycles a resource is "used"
  ResourceIdType ResourceId; // the resource we're referencing
  union {
    OperandId OperandIndex; // operand index for shared resources.
    unsigned MicroOps;      // number of microops for this resource.
  };
  PoolBitsType Width; // how many bits in value (-1 if not shared)
  ConditionalRef<ResourceRef> *IfElse; // conditional reference descriptor
public:
  ResourceRef(ReferenceType Type, ReferenceFlag Flags, PipePhaseType Phase,
              PipeFunc PhaseFunc, UseCyclesType UseCycles,
              ResourceIdType ResourceId, OperandId OperandIndex,
              PoolBitsType Width)
      : Type(Type), Flags(Flags), Phase(Phase), PhaseFunc(PhaseFunc),
        UseCycles(UseCycles), ResourceId(ResourceId),
        OperandIndex(OperandIndex), Width(Width) {}

  // Construct a conditional reference.
  ResourceRef(ConditionalRef<ResourceRef> *IfElse)
      : Type(ReferenceTypes::RefCond), IfElse(IfElse) {}

  // Construct a fus reference
  ResourceRef(ReferenceType Type, ReferenceFlag Flags, UseCyclesType UseCycles,
              ResourceIdType ResourceId, int MicroOps)
      : Type(Type), Flags(Flags), Phase(0), PhaseFunc(nullptr),
        UseCycles(UseCycles), ResourceId(ResourceId), MicroOps(MicroOps) {}
  // Construct a micro-ops reference with no functional unit resource.
  ResourceRef(ReferenceType Type, ReferenceFlag Flags, int MicroOps)
      : Type(Type), Flags(Flags), Phase(0), PhaseFunc(nullptr), UseCycles(0),
        ResourceId(-1), MicroOps(MicroOps) {}

  ReferenceType getType() const { return Type; }
  ReferenceFlag getFlags() const { return Flags; }
  bool isUse() const { return Type == ReferenceTypes::RefUse; }
  bool isFus() const { return Type == ReferenceTypes::RefFus; }
  bool isCond() const { return Type == ReferenceTypes::RefCond; }
  bool isProtected() const { return Flags & ReferenceFlags::kProtected; }
  bool isUnprotected() const { return Flags & ReferenceFlags::kUnprotected; }
  bool isDuplicate() const { return Flags & ReferenceFlags::kDuplicate; }

  bool isUnbuffered() const { return ReferenceFlags::is_unbuffered(Flags); }
  bool isInOrder() const { return ReferenceFlags::is_in_order(Flags); }
  bool isOutOfOrder() const { return ReferenceFlags::is_out_of_order(Flags); }
  bool isBuffered() const { return ReferenceFlags::is_buffered(Flags); }
  bool isBeginGroup() const { return Flags & ReferenceFlags::kBeginGroup; }
  bool isEndGroup() const { return Flags & ReferenceFlags::kEndGroup; }
  bool isSingleIssue() const { return Flags & ReferenceFlags::kSingleIssue; }
  bool isRetireOOO() const { return Flags & ReferenceFlags::kRetireOOO; }

  int getPhase(Instr *Ins) const {
    return PhaseFunc ? getResourcePhase(PhaseFunc, Ins) : Phase;
  }
  int getCycles() const { return UseCycles; }
  int getResourceId() const { return ResourceId; }
  bool hasResourceId() const { return ResourceId != -1; }
  int getMicroOps() const { return MicroOps; }
  int getOperandIndex() const { return OperandIndex; }
  int getWidth() const { return Width; }
  bool isValidOperandIndex() const { return OperandIndex >= 0; }
  bool isShared() const { return Width > 0; }
  ConditionalRef<ResourceRef> *getIfElse() const { return IfElse; }
};

//-----------------------------------------------------------------------------
/// A descriptor of a single resource pool.
//-----------------------------------------------------------------------------
class PoolDescriptor {
  PoolIdType PoolId;       // base pool id for this subpool
  PoolIdType PoolSize;     // how many different allocation sizes in pool
  PoolSizeType Count;      // number of entries needed
  PoolFuncType PoolFunc;   // optional pointer to pool count func
  OpndValueFunc ValueFunc; // optional pointer to fetch operand values
  ResourceIdType First;    // index of first legal id
  ResourceIdType Last;     // index of last legal id
  PoolBitsType Width;      // how many bits in value (-1 if not shared)
public:
  PoolDescriptor(PoolIdType PoolId, PoolIdType PoolSize, PoolSizeType Count,
                 PoolFuncType PoolFunc, OpndValueFunc ValueFunc,
                 ResourceIdType First, ResourceIdType Last, PoolBitsType Width)
      : PoolId(PoolId), PoolSize(PoolSize), Count(Count), PoolFunc(PoolFunc),
        ValueFunc(ValueFunc), First(First), Last(Last), Width(Width) {}

  // Return the number of individual resources needed. This is either a
  // constant value, or we can call a function to determine it based on
  // the instruction instance.
  int getCount(Instr *Inst, int OperandId) const {
    return PoolFunc ? PoolFunc(Inst, OperandId) : Count;
  }

  // Fetch operand values from an instruction, used to facilitate
  // sharing resources of shared values.
  bool getValues(Instr *Inst, int OperandId, int Count, int Values[]) {
    return ValueFunc ? ValueFunc(Inst, OperandId, Count, Values) : false;
  }
  bool hasValueFunc() const { return ValueFunc != nullptr; }
  int getFirst() const { return First; }
  int getLast() const { return Last; }
  int getSize() const { return Last - First + 1; }
  int getWidth() const { return Width; }
  int getPoolSize() const { return PoolSize; }
  int getPoolId() const { return PoolId; }
  int isShared() const { return Width > 0; }
};

//-----------------------------------------------------------------------------
/// A reference to a resource pool.
//-----------------------------------------------------------------------------
class PooledResourceRef {
  ReferenceType Type;          // type of the reference
  ReferenceFlag Flags;         // protected, or unprotected
  PipePhaseType Phase;         // pipeline phase of the reference
  PipeFunc PhaseFunc;          // optional pointer to phase function
  UseCyclesType Cycles;        // number of cycles resource is used
  ResourceIdType *ResourceIds; // the resources we're referencing
  OperandId OperandIndex;      // operand index for shared resources
  int MicroOps = 0;            // number of microops for an Fus entry
  union {
    PoolDescriptor *Pool; // pointer to pool descriptor object
    ConditionalRef<PooledResourceRef> *IfElse; // conditional ref descriptor
  };

public:
  PooledResourceRef(ReferenceType Type, ReferenceFlag Flags,
                    PipePhaseType Phase, PipeFunc PipeFunc,
                    UseCyclesType Cycles, ResourceIdType *ResourceIds,
                    OperandId OperandIndex, PoolDescriptor *Pool)
      : Type(Type), Flags(Flags), Phase(Phase), PhaseFunc(PipeFunc),
        Cycles(Cycles), ResourceIds(ResourceIds), OperandIndex(OperandIndex),
        Pool(Pool) {}
  // Construct a conditional reference.
  PooledResourceRef(ConditionalRef<PooledResourceRef> *IfElse)
      : Type(ReferenceTypes::RefCond), IfElse(IfElse) {}
  // Constructor for a pooled functional unit reference.
  PooledResourceRef(ReferenceType Type, ReferenceFlag Flags,
                    UseCyclesType Cycles, ResourceIdType *ResourceIds,
                    PoolDescriptor *Pool, int MicroOps)
      : Type(Type), Flags(Flags), Phase(0), PhaseFunc(nullptr), Cycles(Cycles),
        ResourceIds(ResourceIds), OperandIndex(0), MicroOps(MicroOps),
        Pool(Pool) {}

  ReferenceType getType() const { return Type; }
  ReferenceFlag getFlags() const { return Flags; }
  bool isUse() const { return Type == ReferenceTypes::RefUse; }
  bool isFus() const { return Type == ReferenceTypes::RefFus; }
  bool isCond() const { return Type == ReferenceTypes::RefCond; }
  bool IsProtected() const { return Flags & ReferenceFlags::kProtected; }
  bool IsUnprotected() const { return Flags & ReferenceFlags::kUnprotected; }
  bool IsDuplicate() const { return Flags & ReferenceFlags::kDuplicate; }

  bool isInOrder() const { return ReferenceFlags::is_in_order(Flags); }
  bool isOutOfOrder() const { return ReferenceFlags::is_out_of_order(Flags); }
  bool isUnbuffered() const { return ReferenceFlags::is_unbuffered(Flags); }
  bool isBuffered() const { return ReferenceFlags::is_buffered(Flags); }
  bool isBeginGroup() const { return Flags & ReferenceFlags::kBeginGroup; }
  bool isEndGroup() const { return Flags & ReferenceFlags::kEndGroup; }
  bool isSingleIssue() const { return Flags & ReferenceFlags::kSingleIssue; }
  bool isRetireOOO() const { return Flags & ReferenceFlags::kRetireOOO; }

  int getPhase(Instr *Ins) const {
    return PhaseFunc ? getResourcePhase(PhaseFunc, Ins) : Phase;
  }
  unsigned getCycles() const { return Cycles; }
  ResourceIdType *getResourceIds() const { return ResourceIds; }
  int getOperandIndex() const { return OperandIndex; }
  int getMicroOps() const { return MicroOps; }
  PoolDescriptor *getPool() const { return Pool; }
  int getPoolId() const { return Pool->getPoolId(); }
  int getPoolSize() const { return Pool->getPoolSize(); }
  int getCount(Instr *Inst, int OperandId) const {
    return Pool->getCount(Inst, OperandId);
  }
  int getFirst() const { return Pool->getFirst(); }
  int getLast() const { return Pool->getLast(); }
  int getSize() const { return Pool->getSize(); }
  int getWidth() const { return Pool->getWidth(); }
  bool isShared() const { return Pool->isShared(); }
  ConditionalRef<PooledResourceRef> *getIfElse() const { return IfElse; }
};

/// A register constraint on a single operand.
class OperandConstraint {
  OperandId OperandIndex;
  RegisterClassIndexType ClassIndex;
  ConditionalRef<OperandConstraint> *IfElse; // conditional constraint
public:
  OperandConstraint(OperandId OperandIndex, RegisterClassIndexType ClassIndex)
      : OperandIndex(OperandIndex), ClassIndex(ClassIndex), IfElse(nullptr) {}
  // Construct a conditional reference.
  OperandConstraint(ConditionalRef<OperandConstraint> *IfElse)
      : IfElse(IfElse) {}

  int getOperandIndex() const { return OperandIndex; }
  int getClassIndex() const { return ClassIndex; }

  ConditionalRef<OperandConstraint> *getIfElse() const { return IfElse; }
  bool isCond() { return IfElse != nullptr; }
};

/// A single subunit definition.  A subunit completely describes the register
/// and resource behavior of the instance of an instruction (or a set of
/// instructions).
class Subunit {
  OperandRefVec *OperandReferences = nullptr;
  ResourceRefVec *UsedResourceReferences = nullptr;
  ResourceRefVec *HeldResourceReferences = nullptr;
  ResourceRefVec *ReservedResourceReferences = nullptr;
  PooledResourceRefVec *PooledResourceReferences = nullptr;
  OperandConstraintVec *Constraints = nullptr;

public:
  Subunit(OperandRefVec *OperandReferences,
          ResourceRefVec *UsedResourceReferences,
          ResourceRefVec *HeldResourceReferences,
          ResourceRefVec *ReservedResourceReferences,
          PooledResourceRefVec *PooledResourceReferences,
          OperandConstraintVec *Constraints)
      : OperandReferences(OperandReferences),
        UsedResourceReferences(UsedResourceReferences),
        HeldResourceReferences(HeldResourceReferences),
        ReservedResourceReferences(ReservedResourceReferences),
        PooledResourceReferences(PooledResourceReferences),
        Constraints(Constraints) {}
  // Simpler constructor for the common case of empty parameters.
  Subunit(OperandRefVec *OperandReferences,
          ResourceRefVec *UsedResourceReferences)
      : OperandReferences(OperandReferences),
        UsedResourceReferences(UsedResourceReferences) {}

  OperandRefVec *getOperandReferences() const { return OperandReferences; }
  ResourceRefVec *getUsedResourceReferences() const {
    return UsedResourceReferences;
  }
  ResourceRefVec *getHeldResourceReferences() const {
    return HeldResourceReferences;
  }
  ResourceRefVec *getReservedResourceReferences() const {
    return ReservedResourceReferences;
  }
  PooledResourceRefVec *getPooledResourceReferences() const {
    return PooledResourceReferences;
  }
  OperandConstraintVec *getConstraints() const { return Constraints; }
};

// CPU configuration parameters, determined by the MDL compiler, based on the
// machine description.  This is used to specialize CpuInfo methods for
// bundle packing and scheduling.
template <int MRI, int MURI, int MFUI, int PC, int MPA, int MI, int RBS,
          int EUP, int LP, int HLDP, int MRP>
struct CpuParams {
  static const int MaxResourceId = MRI;        // maximum resource id
  static const int MaxUsedResourceId = MURI;   // maximum "used" resource
  static const int MaxFuncUnitId = MFUI;       // maximum functional unit id
  static const int PoolCount = PC;             // number of pools defined
  static const int MaxPoolAllocation = MPA;    // biggest pool allocation
  static const int MaxIssue = MI;              // maximum parallel issue
  static const int ReorderBufferSize = RBS;    // instr reorder buffer size
  static const int EarlyUsePhase = EUP;        // earliest operand use phase
  static const int LoadPhase = LP;             // default phase for loads
  static const int HighLatencyDefPhase = HLDP; // high latency def phase
  static const int MaxResourcePhase = MRP;     // latest resource "use" phase
};

// An abstract type that describes the interface to a CPU-specific resource
// reservation table.
class Reservations {
public:
  Reservations(){};
  virtual ~Reservations() = default;
  virtual Reservations *allocate() = 0;
  virtual Reservations *allocate(unsigned II) = 0;
  virtual void advance() = 0;
  virtual void recede() = 0;
  virtual void merge(Reservations *input) = 0;
  virtual Reservations *clone() = 0;
  virtual void reset() = 0;
  virtual void setCycle(unsigned InsertCycle) = 0;
  virtual void dump() = 0;

  static constexpr unsigned power_of_2(unsigned number) {
    unsigned result = 1;
    while (number > result)
      result <<= 1;
    return result;
  }
};

/// A 2D bitset representing resources used by a window of instructions over
/// the pipeline phases of an instruction execution. This does not necessarily
/// include the entire pipeline, or all declared resources, but just the
/// resources and phases representing issue, pool, and hazard resources
/// (determined by the mdl compiler).
/// In addition to testing, setting, and removing members, you can also
/// "advance" the pipeline forward and backward in time and OR two sets.
/// For efficiency, this object is implemented as a power-of-2-sized circular
/// buffer of bitsets. Since its specialized for each CPU, the sizes are all
/// constant, so everything is statically allocated.
template <typename CpuParams> class ReservationsConfig : public Reservations {
  static constexpr unsigned Size = power_of_2(CpuParams::MaxResourcePhase + 1);
  static constexpr unsigned Mask = Size - 1;
  unsigned Head = 0;
  using Resources = std::bitset<CpuParams::MaxUsedResourceId + 1>;
  Resources Bits[Size];
  // std::array<Resources, Size> Bits; 

  Resources &Item(unsigned Phase) { return Bits[(Head + Phase) & Mask]; }

public:
  ReservationsConfig() { reset(); }
  Reservations *allocate() override {
    return static_cast<Reservations *>(new ReservationsConfig());
  }
  Reservations *allocate(unsigned II) override { return nullptr; }  // Unused
  Reservations *clone() override {
    return static_cast<Reservations *>(new ReservationsConfig(*this));
  }

  void reset() override {
    for (unsigned i = 0; i < Size; i++)
      Bits[i].reset();
    Head = 0;
  }

  void advance() override {
    Bits[Head].reset();
    Head = (Head + 1) & Mask;
  }
  void recede() override {
    Head = (Head - 1) & Mask;
    Bits[Head].reset();
  }
  void setCycle(unsigned InsertCycle) override { }   // Unused

  void set(int Bit, unsigned Phase) { Item(Phase).set(Bit); }
  void clr(int Bit, unsigned Phase) { Item(Phase).reset(Bit); }
  bool test(int Bit, unsigned Phase) { return Item(Phase).test(Bit); }
  bool testSet(int Bit, unsigned Phase) {
    if (test(Bit, Phase))
      return true;
    set(Bit, Phase);
    return false;
  }

  void set(int Bit, unsigned Phase, unsigned Cycles) {
    for (unsigned i = 0; i < Cycles; i++)
      set(Bit, Phase + i);
  }
  void clr(int Bit, unsigned Phase, unsigned Cycles) {
    for (unsigned i = 0; i < Cycles; i++)
      clr(Bit, Phase + i);
  }
  bool test(int Bit, unsigned Phase, unsigned Cycles) {
    for (unsigned i = 0; i < Cycles; i++)
      if (test(Bit, Phase + i))
        return true;
    return false;
  }
  bool testSet(int Bit, unsigned Phase, unsigned Cycles) {
    if (test(Bit, Phase, Cycles))
      return true;
    set(Bit, Phase, Cycles);
    return false;
  }

  void merge(Reservations *input) override {
    auto *my_input = static_cast<ReservationsConfig<CpuParams> *>(input);
    for (unsigned i = 0; i < Size; i++)
      Item(i) |= my_input->Item(i);
  }

  // Return the count of resources used in a set of phases.
  unsigned popl(unsigned Early, unsigned Late) {
    unsigned Count = 0;
    for (unsigned time = Early; time <= Late; time++)
      Count += Item(time).count();
    return Count;
  }

  void dump() override {
    DEBUG_WITH_TYPE("MdlResource",
      int last = CpuParams::MaxResourcePhase;
      while (last && Item(last).none()) last--;
      dbgs() << "Scoreboard:\n";
      for (int i = 0; i <= last; i++)
        dbgs() << "  " << i << ": " << Item(i).to_string() << "\n";
      dbgs() << "\n";
    );
  }
};

/// A 2D bitset representing a modulo resource table for software pipelining.
/// Like ReservationsConfig, this only needs to model resources used
/// for issue, slot allocation, pools, and hazards.
/// Unlike ReservationsConfig, these objects have a "current cycle" that
/// resource references occur in. Since the reservation table needs to model
/// the entire loop, there's no method for "advancing" or "receding" the
/// pipeline mode, but clients do need to set the insertion when attempting
/// to schedule an instruction.
/// Note: the II can be larger than the MDL-generated maximum pipeline depth.
template <typename CpuParams>
class ModuloReservationsConfig : public Reservations {
  unsigned int II = 0;    // The II we're attempting to schedule at.
  unsigned int Cycle = 0; // Current cycle to insert at.
  using Resources = std::bitset<CpuParams::MaxUsedResourceId + 1>;
  Resources *Bits;

  Resources &Item(unsigned Phase) { return Bits[(Cycle + Phase) % II]; }

public:
  ModuloReservationsConfig(unsigned II) : II(II), Bits(new Resources[II]) {}

  ~ModuloReservationsConfig() { delete[] Bits; }

  Reservations *allocate(unsigned II) override {
    return static_cast<Reservations *>(new ModuloReservationsConfig(II));
  }
  Reservations *allocate() override { return nullptr; }    // Unused

  Reservations *clone() override {
    return static_cast<Reservations *>(new ModuloReservationsConfig(*this));
  }

  void setCycle(unsigned InsertCycle) override { Cycle = InsertCycle; }
  void reset() override {
    for (unsigned i = 0; i < II; i++)
      Bits[i].reset();
  }

  void set(int Bit, unsigned Phase) { Item(Phase).set(Bit); }
  void clr(int Bit, unsigned Phase) { Item(Phase).reset(Bit); }
  bool test(int Bit, unsigned Phase) { return Item(Phase).test(Bit); }
  bool testSet(int Bit, unsigned Phase) {
    if (test(Bit, Phase))
      return true;
    set(Bit, Phase);
    return false;
  }

  void set(int Bit, unsigned Phase, unsigned Cycles) {
    for (unsigned i = 0; i < Cycles; i++)
      set(Bit, Phase + i);
  }
  void clr(int Bit, unsigned Phase, unsigned Cycles) {
    for (unsigned i = 0; i < Cycles; i++)
      clr(Bit, Phase + i);
  }
  bool test(int Bit, unsigned Phase, unsigned Cycles) {
    for (unsigned i = 0; i < Cycles; i++)
      if (test(Bit, Phase + i))
        return true;
    return false;
  }
  bool testSet(int Bit, unsigned Phase, unsigned Cycles) {
    if (test(Bit, Phase, Cycles))
      return true;
    set(Bit, Phase, Cycles);
    return false;
  }

  void merge(Reservations *input) override { }      // Unused
  void advance() override { }                       // Unused
  void recede() override { }                        // Unused

  void dump() override {
    DEBUG_WITH_TYPE("MdlResource",
      unsigned last = II;
      while (last && Bits[(Cycle + last) & II].none()) last--;
      for (unsigned i = 0; i <= last; i++)
        dbgs() << i << ": " << Bits[(Cycle + i) % II].to_string() << "\n";
    );
  }
};

///----------------------------------------------------------------------------
/// Abstract interface to an llvm instruction. This object provides
/// a common interface to the MDL compiler for accessing information in
/// EITHER MachineInstrs and MCInsts.
///----------------------------------------------------------------------------
class Instr {
  // Descriptors for MachineInst records.
  const MachineInstr *MI = nullptr;
  const TargetInstrInfo *TII = nullptr;

  // Descriptors for MCInstr records.
  const MCInst *MC = nullptr;
  const MCSubtargetInfo *STI = nullptr;
  const MCInstrInfo *MCII = nullptr;

  CpuInfo *Cpu = nullptr;

public:
  Instr(const MachineInstr *MI, const TargetSubtargetInfo *STI);
  Instr(const MachineInstr *MI, const TargetInstrInfo *TII, CpuInfo *Cpu);
  Instr(const MCInst *MC, const MCSubtargetInfo *STI, const MCInstrInfo *MCII);

  const MachineInstr *getMI() const { return MI; }
  const TargetInstrInfo *getTII() const { return TII; }

  const MCInst *getMC() const { return MC; }
  const MCSubtargetInfo *getSTI() const { return STI; }
  const MCInstrInfo *getMCII() const { return MCII; }
  CpuInfo *getCpuInfo() { return Cpu; }

  bool isMC() const { return MC != nullptr; }
  bool isMI() const { return MI != nullptr; }

  // Get the LLVM name for this instruction.
  std::string getName();
  /// Fetch the instruction's opcode.
  int getOpcode();

  /// Evaluate a Target-library instruction predicate for this instruction.
  bool evaluatePredicate(int PredId);

  /// Return the raw bits associated with an operand.
  int64_t getOperand(int OperandIndex);
  /// Return various attributes of an instruction's operand.
  bool isOpndLiteral(int OperandIndex);
  bool isOpndAddress(int OperandIndex);
  bool isOpndLabel(int OperandIndex);
  bool isOpndRegister(int OperandIndex);
  bool isOpndVirtualRegister(int OperandIndex);

  /// Return true if a MachineInstr has more operands than described in its
  /// MCInst description.
  bool hasExtraOperands();

  /// Fetch the instruction's currently assigned subunit.  TODO: We don't have
  /// a way to record the selected subunit in an instruction, so for now just
  /// return 0 (ie, the first subunit).
  int getSubunitId() { return 0; }

  /// Return the set of subunits for an instruction and CPU combination.
  SubunitVec *getSubunit();
};

///----------------------------------------------------------------------------
/// MDL-based Bundle Packer definitions.  This provides object definitions that
/// are needed for the bundle packing implementation.  Since the implmentation
/// is specialized for each CPU, we need to define these separately from the
/// implementation.
///----------------------------------------------------------------------------
class SlotDesc;
class PoolRequest;

/// An allocated resource contains a resource id, a phase, the number of cycles.
/// Shared resources additionally have an operand id, a value, and a resource
/// count. An AllocatedResourceSet contains the set of all the resource
/// allocations (vs statically assigned resources) for a single slot.
struct AllocatedResource {
  int ResId;                // Resource reserved
  int Phase;                // First phase its reserved for
  int Cycles;               // Number of phases its reserved for
  int OpndId = -1;          // Operand id (for shared values)
  int Value = 0;            // The shared value, if any
  int ResCount = 0;         // Number of resources (if shared)

  AllocatedResource(int ResId, int Phase, int Cycles) :
    ResId(ResId), Phase(Phase), Cycles(Cycles) {}
  AllocatedResource(int ResId, int Phase, int Cycles, int OpndId, int Value,
                    int ResCount) :
    ResId(ResId), Phase(Phase), Cycles(Cycles),
    OpndId(OpndId), Value(Value), ResCount(ResCount) {}
};

using AllocatedResourceSet = std::vector<AllocatedResource>;

using SlotSet = std::vector<SlotDesc>;
using InstrSet = std::vector<MachineInstr *>;
using MCInstrSet = std::vector<MCInst *>;
using PoolRequestSet = std::vector<PoolRequest>;

/// When we attempt to bundle an instruction, there are three possible
/// (internal) outcomes. Either we succeed in the bundling, or we fail to
/// find a valid bundle, or we fail to allocate pooled resources.
enum class BundleStatus {
  kSuccess,          // Bundling and Resource Allocation succeeded.
  kBundleFailed,     // Bundling failed.
  kAllocationFailed, // Bundling worked, Resource Allocation failed.
};

/// A working set of values allocated to resources, used by bundling to
/// allocate shared resources in a bundle.
template <typename CpuParams> class ResourceValues {
  int Values[CpuParams::MaxUsedResourceId + 1];
  bool Valid[CpuParams::MaxUsedResourceId + 1] = {false};

public:
  bool check(int resource_id, int Values[], int count) {
    for (int id = 0; id < count; id++, resource_id++)
      if (!Valid[resource_id] || Values[resource_id] != Values[id])
        return false;
    return true;
  }
  void set(int ResourceId, int Values[], int Count) {
    for (int id = 0; id < Count; id++, ResourceId++) {
      Valid[ResourceId] = true;
      Values[ResourceId] = Values[id];
    }
  }
};

/// Representation of a single issue slot.  A slot contains the entire context
/// of how an instruction is bundled: the instruction itself, all the subunits
/// it qualifies for, the selected subunit id, and the resources assigned to
/// the instruction in the current bundle.
class SlotDesc {
  Instr Inst;                     // instruction description
  SubunitVec *Subunits;           // pointer to vector of legal subunits
  int SubunitId;                  // currently selected subunit id
  AllocatedResourceSet Resources; // resources reserved for instruction
public:
  SlotDesc(const MCInst *MC, const MCSubtargetInfo *STI,
           const MCInstrInfo *MCII);
  SlotDesc(MachineInstr *MI, const TargetSubtargetInfo *STI);

  Instr *getInst() { return &Inst; }
  const MachineInstr *getMI() const { return Inst.getMI(); }
  SubunitVec *getSubunits() const { return Subunits; }
  int getSubunitId() const { return SubunitId; }
  void setSubunitId(int Id) { SubunitId = Id; }

  Subunit *getSubunit() const { return &(*Subunits)[SubunitId]; }
  AllocatedResourceSet &getResources() { return Resources; }
  void setResources(const AllocatedResourceSet &Res) { Resources = Res; }
};

/// Specify a single pool request for a candidate instruction. This object
/// is internal to the bundle packer, and is used to fulfill instructions'
/// pooled allocation requests.
class PoolRequest {
  SlotDesc *WhichSlot;    // Slot/instruction making request
  PooledResourceRef *Ref; // the pooled resource request
  int Count;              // how many resources requested
  int Phase;              // what pipeline phase
public:
  PoolRequest(SlotDesc *WhichSlot, PooledResourceRef *Ref)
      : WhichSlot(WhichSlot), Ref(Ref) {
    Count = Ref->getCount(WhichSlot->getInst(), Ref->getOperandIndex());
    Phase = Ref->getPhase(WhichSlot->getInst());
  }

  int getFirst() const { return Ref->getFirst(); }
  int getLast() const { return Ref->getLast(); }
  int getSize() const { return Ref->getSize(); }
  int getCount() const { return Count; }
  int getWidth() const { return Ref->getWidth(); }
  int getPhase() const { return Phase; }
  int getCycles() const { return Ref->getCycles(); }
  ResourceIdType *getResourceIds() const { return Ref->getResourceIds(); }
  int getPoolId() const { return Ref->getPoolId(); }
  int getSubpoolId() const {
    return getPoolId() + Ref->getPool()->getPoolSize() - getCount();
  }
  Instr *getInst() const { return WhichSlot->getInst(); }
  int getOperandId() const { return Ref->getOperandIndex(); }
  PooledResourceRef *getRef() const { return Ref; }
  SlotDesc *getSlot() const { return WhichSlot; }
  bool isShared() const { return getRef()->isShared(); }
};

/// Collection of all pool requests for a set of candidate instructions,
/// organized by pool id. Pools are ordered (by the MDL compiler) by the
/// number of entries in the pool, with smaller more constrained pools
/// allocated first.
template <typename CpuParams> class PoolRequests {
  PoolRequestSet Pools[CpuParams::PoolCount ? CpuParams::PoolCount : 1];

public:
  PoolRequests() : Pools() {}
  auto &getPool(int Index) { return Pools[Index]; }
  void AddPoolRequest(SlotDesc *WhichSlot, PooledResourceRef *Item) {
    PoolRequest request(WhichSlot, Item);
    if (request.getCount() != 0)
      Pools[request.getSubpoolId()].push_back(request);
  }
};

// This is essentially a clone of MachineInstr::isTransient, except that it
// doesn't depend on code in the CodeGen library, and doesn't handle bundles.
inline bool isTransient(const MachineInstr *MI) {
  switch (MI->getOpcode()) {
  default:
    return MI->getDesc().getFlags() & (1 << MCID::Meta);
  case TargetOpcode::PHI:
  case TargetOpcode::G_PHI:
  case TargetOpcode::COPY:
  case TargetOpcode::INSERT_SUBREG:
  case TargetOpcode::SUBREG_TO_REG:
  case TargetOpcode::REG_SEQUENCE:
    return true;
  }
}

///----------------------------------------------------------------------------
/// Information for each defined CPU. Each MDL CPU corresponds to a single
/// LLVM target or, roughly, a single SchedMachineModel. CpuInfo contains
/// instruction behaviors specific to that SchedMachineModel, as well as
/// forwarding information and some "worst-case" instruction behaviors.
///----------------------------------------------------------------------------
class CpuInfo {
  unsigned MaxResourceId = 0;       // maximum resource id
  unsigned MaxUsedResourceId = 0;   // maximum "used" resource
  unsigned MaxFuncUnitId = 0;       // max functional unit resource id
  unsigned PoolCount = 0;           // number of pools defined
  unsigned MaxPoolAllocation = 0;   // max resources alloced for a pool
  unsigned MaxIssue = 0;            // maximum parallel issue
  unsigned ReorderBufferSize = 0;   // instruction reorder buffer size
  unsigned EarlyUsePhase = 0;       // earliest phase of operand uses
  unsigned LoadPhase = 0;           // default phase for load instructions
  unsigned HighLatencyDefPhase = 0; // high latency def instruction phase
  unsigned MaxResourcePhase = 0;    // latest resource "use" phase
  SubunitTable *(*InitSubunitTable)() = nullptr;
  int8_t **ForwardTable = nullptr;  // forwarding info table, or null
  SubunitTable *Subunits = nullptr; // instruction-to-subunit mapping
  unsigned ResourceFactor = 1;      // Cpu-specific resource factor
  std::string *ResourceNames = nullptr;  // Names of resources

  // A CPU can have a set of Target-library predicates, which are only used
  // if the LLVM Target library is included in an application. This vector is
  // generated by the MDL compiler, and is initialized here when the Subtarget
  // object is initialized.
  std::vector<PredFunc> *InstrPredicates = nullptr;

public:
  CpuInfo(unsigned MaxResourceId, unsigned MaxUsedResourceId,
          unsigned MaxFuncUnitId, unsigned PoolCount,
          unsigned MaxPoolAllocation, unsigned MaxIssue,
          unsigned ReorderBufferSize, unsigned EarlyUsePhase,
          unsigned LoadPhase, unsigned HighLatencyDefPhase,
          unsigned MaxResourcePhase, SubunitTable *(*InitSubunitTable)(),
          int8_t **ForwardTable, unsigned ResourceFactor,
          std::string *ResourceNames)
      : MaxResourceId(MaxResourceId), MaxUsedResourceId(MaxUsedResourceId),
        MaxFuncUnitId(MaxFuncUnitId), PoolCount(PoolCount),
        MaxPoolAllocation(MaxPoolAllocation), MaxIssue(MaxIssue),
        ReorderBufferSize(ReorderBufferSize), EarlyUsePhase(EarlyUsePhase),
        LoadPhase(LoadPhase), HighLatencyDefPhase(HighLatencyDefPhase),
        MaxResourcePhase(MaxResourcePhase), InitSubunitTable(InitSubunitTable),
        ForwardTable(ForwardTable), ResourceFactor(ResourceFactor),
        ResourceNames(ResourceNames) {}
  CpuInfo() {}
  virtual ~CpuInfo() = default;

  //------------------------------------------------------------------------
  // These functions return all the top-level attributes of the CPU.
  //------------------------------------------------------------------------
  unsigned getMaxResourceId() const { return MaxResourceId; }
  unsigned getMaxUsedResourceId() const { return MaxUsedResourceId; }
  unsigned getMaxFuncUnitId() const { return MaxFuncUnitId; }
  bool isFuncUnitId(int id) const { return (unsigned)id <= MaxFuncUnitId; }

  unsigned getPoolCount() const { return PoolCount; }
  unsigned getMaxPoolAllocation() const { return MaxPoolAllocation; }

  unsigned getMaxIssue() const { return MaxIssue; }
  unsigned getReorderBufferSize() const { return ReorderBufferSize; }

  unsigned getEarlyUsePhase() const { return EarlyUsePhase; }
  unsigned getLoadPhase() const { return LoadPhase; }
  unsigned getHighLatencyDefPhase() const { return HighLatencyDefPhase; }
  unsigned getMaxResourcePhase() const { return MaxResourcePhase; }
  int8_t **getForwardTable() const { return ForwardTable; }
  unsigned getResourceFactor() const { return ResourceFactor; }
  std::string &getResourceName(unsigned Id) { return ResourceNames[Id]; }

  //------------------------------------------------------------------------
  // Functions for managing the subunit and predicate tables.
  // If a cpu description doesn't have instruction information, its subunit
  // table will be empty.
  //------------------------------------------------------------------------
  bool hasSubunits() const { return Subunits; }
  SubunitTable *getSubunits() const { return Subunits; }
  SubunitVec *getSubunit(int opcode) const {
    if (Subunits == nullptr) return nullptr;
    return (*Subunits)[opcode];
  }
  bool IsInstruction(int Opcode, int OperandId) const {
    if (OperandId == -1)
      return false;
    return getSubunit(Opcode) != nullptr;
  }

  // A subunit table is only initialized once, when it is selected for use.
  // Call the MDL-generated function to initialize it.
  void InitSubunits() {
    if (Subunits == nullptr && InitSubunitTable)
      Subunits = InitSubunitTable();
  }

  // Register a set of Subtarget-specific predicates for this subtarget.
  void SetInstrPredicates(std::vector<PredFunc> *Preds) {
    InstrPredicates = Preds;
  }

  // Optionally evaluate a Subtarget-specific predicate function (generated
  // by the MDL compiler).
  bool evaluatePredicate(int Index, Instr *MI) {
    if (InstrPredicates == nullptr)
      return false;
    return (*InstrPredicates)[Index](MI);
  }

  //------------------------------------------------------------------------
  // These functions look for various attributes on explicit functional unit
  // references. Note that these reference lists typically have only a single
  // entry, so this should be very fast.
  //------------------------------------------------------------------------
  // Return true if an instruction must begin an issue group.
  bool mustBeginGroup(const MachineInstr *MI, const TargetSubtargetInfo *STI) {
    Instr Ins(MI, STI);
    if (auto *Subunit = Ins.getSubunit()) {
      if (auto *Refs = (*Subunit)[0].getUsedResourceReferences())
        for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
          if (Ref.isFus() && (Ref.isBeginGroup() || Ref.isSingleIssue()))
            return true;
      if (auto *Refs = (*Subunit)[0].getPooledResourceReferences())
        for (const auto &Ref : ReferenceIter<PooledResourceRef>(Refs, &Ins))
          if (Ref.isFus() && (Ref.isBeginGroup() || Ref.isSingleIssue()))
            return true;
    }
    return false;
  }

  // Return true if an instruction must end an issue group.
  bool mustEndGroup(const MachineInstr *MI, const TargetSubtargetInfo *STI) {
    Instr Ins(MI, STI);
    if (auto *Subunit = Ins.getSubunit()) {
      if (auto *Refs = (*Subunit)[0].getUsedResourceReferences())
        for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
          if (Ref.isFus() && (Ref.isEndGroup() || Ref.isSingleIssue()))
            return true;
      if (auto *Refs = (*Subunit)[0].getPooledResourceReferences())
        for (const auto &Ref : ReferenceIter<PooledResourceRef>(Refs, &Ins))
          if (Ref.isFus() && (Ref.isEndGroup() || Ref.isSingleIssue()))
            return true;
    }
    return false;
  }

  // Return true if an instruction must be single-issued.
  bool isSingleIssue(const MachineInstr *MI, const TargetSubtargetInfo *STI) {
    Instr Ins(MI, STI);
    if (auto *Subunit = Ins.getSubunit()) {
      if (auto *Refs = (*Subunit)[0].getUsedResourceReferences())
        for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
          if (Ref.isFus() && Ref.isSingleIssue())
            return true;
      if (auto *Refs = (*Subunit)[0].getPooledResourceReferences())
        for (const auto &Ref : ReferenceIter<PooledResourceRef>(Refs, &Ins))
          if (Ref.isFus() && Ref.isSingleIssue())
            return true;
    }
    return false;
  }

  // Return true if an instruction has the RetireOOO attribute.
  bool isRetireOOO(const MachineInstr *MI, const TargetSubtargetInfo *STI) {
    Instr Ins(MI, STI);
    if (auto *Subunit = Ins.getSubunit()) {
      if (auto *Refs = (*Subunit)[0].getUsedResourceReferences())
        for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
          if (Ref.isFus() && Ref.isRetireOOO())
            return true;
      if (auto *Refs = (*Subunit)[0].getPooledResourceReferences())
        for (const auto &Ref : ReferenceIter<PooledResourceRef>(Refs, &Ins))
          if (Ref.isFus() && Ref.isRetireOOO())
            return true;
    }
    return false;
  }

  //------------------------------------------------------------------------
  // Return the total number of micro-ops for an instruction.
  //------------------------------------------------------------------------
  int numMicroOps(Instr Ins) const {
    auto *Subunit = Ins.getSubunit();
    if (Subunit == nullptr)
      return isTransient(Ins.getMI()) ? 0 : 1;

    int MicroOps = 0;
    if (auto *Refs = (*Subunit)[0].getUsedResourceReferences()) {
      for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
        if (Ref.isFus())
          MicroOps += Ref.getMicroOps();
    }
    if (auto *Prefs = (*Subunit)[0].getPooledResourceReferences())
      for (const auto &Ref : ReferenceIter<PooledResourceRef>(Prefs, &Ins))
        if (Ref.isFus()) 
          MicroOps += Ref.getMicroOps();

    // If the instruction had subunits, its at least one microop.
    return MicroOps ? MicroOps : 1;
  }

  int numMicroOps(const MachineInstr *MI, const TargetSubtargetInfo *STI) {
    return numMicroOps(Instr(MI, STI));
  }
  int numMicroOps(const MCInst *MI, const MCSubtargetInfo *STI,
                  const MCInstrInfo *MCII) {
    return numMicroOps(Instr(MI, STI, MCII));
  }

  //------------------------------------------------------------------------
  // Calculate the reciprocal throughput for an instruction.
  //------------------------------------------------------------------------
  double getReciprocalThroughput(Instr Ins) const {
    double Throughput = 0.0;
    if (auto *Subunit = Ins.getSubunit()) {
      if (auto *Refs = (*Subunit)[0].getUsedResourceReferences())
        for (const auto &Ref : ReferenceIter<ResourceRef>(Refs, &Ins))
          if (Ref.isFus() && Ref.getCycles()) {
            double Temp = 1.0 / Ref.getCycles();
            Throughput = Throughput ? std::min(Throughput, Temp) : Temp;
          }
      if (auto *Prefs = (*Subunit)[0].getPooledResourceReferences())
        for (const auto &Ref : ReferenceIter<PooledResourceRef>(Prefs, &Ins))
          if (Ref.isFus()) { // Pools always have non-zero cycles
            double Temp = (Ref.getSize() * 1.0) / Ref.getCycles();
            Throughput = Throughput ? std::min(Throughput, Temp) : Temp;
          }
    }
    if (Throughput != 0.0)
      return 1.0 / Throughput;
    return (numMicroOps(Ins) * 1.0) / getMaxIssue();
  }

  double getReciprocalThroughput(const TargetSubtargetInfo *STI,
                                 const MachineInstr *MI) {
    return getReciprocalThroughput(Instr(MI, STI));
  }
  double getReciprocalThroughput(const MCSubtargetInfo *STI,
                                 const MCInstrInfo *MCII, const MCInst *MI) {
    return getReciprocalThroughput(Instr(MI, STI, MCII));
  }

  //------------------------------------------------------------------------
  // Abstract interface to bundle packing infrastructure.
  //------------------------------------------------------------------------
  virtual bool addToBundle(SlotSet &Bundle, const SlotDesc &Candidate,
                           const Reservations &Res) = 0;
  virtual bool canAddToBundle(SlotSet &Bundle, const SlotDesc &Candidate,
                              const Reservations &Res) = 0;
  virtual void addBundleToReservation(SlotSet &Bundle, Reservations &Res) = 0;
  virtual void deleteBundleFromReservation(SlotSet &Bundle, Reservations &Res) {
  }
  virtual SlotSet bundleCandidates(const SlotSet *Candidates) = 0;
  virtual Reservations *allocReservations() const = 0;
  virtual Reservations *allocModuloReservations(int II) const = 0;

  // Bundle packing debug functions.
  void dumpBundle(std::string Cpu, std::string Msg, SlotSet &Bundle);
  void dumpBundle(SlotSet &Bundle) { dumpBundle("", "", Bundle); }
  std::string dumpSlot(std::string Msg, SlotDesc &Slot);
  virtual bool validateBundle(std::string Cpu, SlotSet &Bundle) = 0;
};

///----------------------------------------------------------------------------
/// CPU-specific object that describes parameters of the target.
/// The primary role of this object is to provide a bundle packing API that
/// is specialized for each target and subtarget.
///----------------------------------------------------------------------------
template <typename CpuParams> class CpuConfig : public CpuInfo {
public:
  CpuConfig(SubunitTable *(*InitSubunitTable)(), int8_t **ForwardTable,
            unsigned ResourceFactor, std::string *ResourceNames)
      : CpuInfo(CpuParams::MaxResourceId, CpuParams::MaxUsedResourceId,
                CpuParams::MaxFuncUnitId, CpuParams::PoolCount,
                CpuParams::MaxPoolAllocation, CpuParams::MaxIssue,
                CpuParams::ReorderBufferSize, CpuParams::EarlyUsePhase,
                CpuParams::LoadPhase, CpuParams::HighLatencyDefPhase,
                CpuParams::MaxResourcePhase, InitSubunitTable, ForwardTable,
                ResourceFactor, ResourceNames) {}

  // CPU-specialized bundle packing functions.
  bool addToBundle(SlotSet &Bundle, const SlotDesc &Candidate,
                   const Reservations &Res) override;
  bool canAddToBundle(SlotSet &Bundle, const SlotDesc &Candidate,
                      const Reservations &Res) override;
  SlotSet bundleCandidates(const SlotSet *Candidates) override;
  Reservations *allocReservations() const override {
    return new ReservationsConfig<CpuParams>;
  }
  Reservations *allocModuloReservations(int II) const override {
    return new ModuloReservationsConfig<CpuParams>(II);
  }
  void addBundleToReservation(SlotSet &Bundle, Reservations &Res) override;

  // Internal functions to help with bundle packing.
  BundleStatus attemptToBundle(SlotSet &Bundle,
                               ReservationsConfig<CpuParams> &Res,
                               int WhichSlot, bool Reset);
  bool addResources(SlotDesc &Slot, Subunit &WhichSubunit,
                    ReservationsConfig<CpuParams> &res);
  void findStaticResources(SlotSet &Bundle, ResourceValues<CpuParams> &Values);

  // Internal pool allocation functions.
  BundleStatus allocatePools(SlotSet &Bundle,
                             ReservationsConfig<CpuParams> &Res);
  bool allocateResource(PoolRequest &Item, int Id, int Count,
                        ReservationsConfig<CpuParams> &Res,
                        ResourceValues<CpuParams> &Values);
  bool allocatePool(PoolRequestSet &Pool, ReservationsConfig<CpuParams> &Res,
                    ResourceValues<CpuParams> &Values);
  bool validateBundle(std::string Cpu, SlotSet &Bundle) override;
};

// A simple wrapper to check the range for calculated resource latencies.
inline int getResourcePhase(PipeFunc Func, Instr *Ins) {
  return std::min(Func(Ins), Ins->getCpuInfo()->getMaxResourcePhase());
}

///----------------------------------------------------------------------------
/// A CPU dictionary is the top-level object in the database, and describes
/// each defined CPU in the family. Each CPU object in this table corresponds
/// to a single SchedMachineModel.
///----------------------------------------------------------------------------
using CpuTableDict = std::map<std::string, CpuInfo *>;

class CpuTableDef {
  // A dictionary of all CPUs defined in the description, indexed by name.
  CpuTableDict &Cpus;

public:
  explicit CpuTableDef(CpuTableDict &Cpus) : Cpus(Cpus) {}

  CpuInfo *getCpu(std::string name) const {
    if (!Cpus.count(name))
      return nullptr;
    auto *cpu = Cpus[name];
    cpu->InitSubunits();
    return cpu;
  }

  bool hasCpus() const { return !Cpus.empty(); }
  int getCpuCount() const { return Cpus.size(); }

  // Register a set of Subtarget-specific predicates with each subtarget.
  void SetInstrPredicates(std::vector<PredFunc> *Preds) {
    for (auto [Name, Cpu] : Cpus)
      Cpu->SetInstrPredicates(Preds);
  }
};

} // namespace mdl
} // namespace llvm

///----------------------------------------------------------------------------
/// MDLBundle.h contains template function definitions that provide the
/// implementations of all bundle packing infrastructure which are based on
/// the CpuConfig templatized object.  We include it here to avoid having to
/// include it everywhere that we include MDLInfo.h.
///----------------------------------------------------------------------------
#include "llvm/MC/MDLBundle.h"

#endif // MDL_INFO_H

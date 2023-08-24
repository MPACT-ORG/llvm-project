//-------------------------------------------------------------------
// Machine Description Database.
// This file is auto-generated, do not edit.
//-------------------------------------------------------------------

#include "llvm/Support/Mutex.h"
#include "llvm/MC/MDLInfo.h"
#include "llvm/MC/MDLInstrInfo.h"
#include "test_fus_8InstrInfo.h"
#include "/usr/local/google/home/tatge/clone/llvm-project/llvm/test/Mdl/test_fus_8GenMdlInfo.h"


namespace llvm {
namespace x {
using namespace ::llvm::mdl;
constexpr auto __ = nullptr;
constexpr auto RefPred    = ReferenceTypes::RefPred;
constexpr auto RefUse     = ReferenceTypes::RefUse;
constexpr auto RefDef     = ReferenceTypes::RefDef;
constexpr auto RefKill    = ReferenceTypes::RefKill;
constexpr auto RefUseDef  = ReferenceTypes::RefUseDef;
constexpr auto RefHold    = ReferenceTypes::RefHold;
constexpr auto RefReserve = ReferenceTypes::RefReserve;
constexpr auto RefFus     = ReferenceTypes::RefFus;

//-------------------------------------------------------------------
// Forward references for conditional references
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Conditional Reference Tables:
//     - Predicate function (optional)
//     - References (operands, resource or pooled resource refs
//     - "Else" clause conditional reference (optional)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Conditional Operand Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Conditional Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Conditional Pooled Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Conditional Constraints Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Operand Reference Table (2 entries)
//     - Resource type(use, def, cond)
//     - Reference flags (protected, unprotected)
//     - Pipeline phase
//     - Pipeline phase function (optional)
//     - Operand index
//   or (for conditional references)
//     - Conditional reference
//-------------------------------------------------------------------
OperandRef OPND_0_data[] = {{RefDef,1,1,__,0}};
OperandRefVec OPND_0 = { 1, OPND_0_data };
OperandRef OPND_1_data[] = {{RefDef,1,4,__,0}};
OperandRefVec OPND_1 = { 1, OPND_1_data };

//-------------------------------------------------------------------
// Resource Reference Tables:
//     - Reference type (use, hold, reserve)
//     - Reference flags (protected, unprotected, reserved)
//     - Pipeline phase
//     - Pipeline phase function (optional)
//     - Used cycles
//     - Resource id
//     - Operand index (for shared resources)
//     - Width in bits (for shared resources)
//   or (for functional unit descriptors)
//     - Reference type (fus)
//     - Reference flags (reserved, buffered, begin_group, ...)
//     - Used cycles
//     - Resource id
//     - Number of MicroOps
//   or (for "unitless" micro-ops)
//     - Reference type (fus)
//     - Number of MicroOps
//   or (for conditional references)
//     - Conditional reference
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Used Resource Reference Table (2 entries)
//-------------------------------------------------------------------
ResourceRef URES_0_data[] = {{RefFus,0,1,1,1}};
ResourceRefVec URES_0 = { 1, URES_0_data };
ResourceRef URES_1_data[] = {{RefFus,0,1,1,2}};
ResourceRefVec URES_1 = { 1, URES_1_data };

//-------------------------------------------------------------------
// Held Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Reserved Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Resource Group Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Pool Descriptor Table (0 entries)
//      pool_id -    which pool to allocate from
//      pool_size -  how many different allocations sizes
//      count -      how many pool elements we need
//      pool_func -  optional pointer to pool size func
//      value_func - optional pointer to pool values func
//      first -      index of first legal element id
//      last -       index of last legal element id
//      width -      width in bits
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Pooled Resource Reference Table (0 entries)
//     - Resource type (use, hold, reserve)
//     - Reference flags (protected, unprotected, reserved
//     - Pipeline phase
//     - Pipeline phase function (optional)
//     - Used cycles
//     - Resource group
//     - Operand index (for shared resources)
//     - Pool descriptor
//   or (for functional unit descriptors)
//     - Reference type (fus)
//     - Reference flags (reserved, buffered, begin_group, ...)
//     - Used cycles
//     - Group id
//     - Pool id
//     - Number of MicroOps
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Operand Constraint Table (0 entries)
//     - Operand Index
//     - Constraint id
//   or (for conditional reference)
//     - Conditional constraint name

//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Subunit Table (2 entries)
//-------------------------------------------------------------------
Subunit SU_0_data[] = {{&OPND_0,&URES_0}};
SubunitVec SU_0 = { 1, SU_0_data };
Subunit SU_1_data[] = {{&OPND_1,&URES_1}};
SubunitVec SU_1 = { 1, SU_1_data };

//-------------------------------------------------------------------
// Instruction table initialization for x1 (1 valid entries)
//-------------------------------------------------------------------
__attribute__((optnone))
SubunitTable *SUNITS_x1() {
  static SubunitTable table;
  static sys::SmartMutex<true> Mutex;
  sys::SmartScopedLock<true> Lock(Mutex);
  if (table.size() != 0) return &table;
  table.resize(::llvm::x::INSTRUCTION_LIST_END, __);
  table[::llvm::x::xxx] = &SU_0;
  return &table;
}

//-------------------------------------------------------------------
// Instruction table initialization for x2 (1 valid entries)
//-------------------------------------------------------------------
__attribute__((optnone))
SubunitTable *SUNITS_x2() {
  static SubunitTable table;
  static sys::SmartMutex<true> Mutex;
  sys::SmartScopedLock<true> Lock(Mutex);
  if (table.size() != 0) return &table;
  table.resize(::llvm::x::INSTRUCTION_LIST_END, __);
  table[::llvm::x::xxx] = &SU_1;
  return &table;
}

//-------------------------------------------------------------------
// Resource name tables
//-------------------------------------------------------------------
std::string NAMES_x1[] = {"","F:f","",""};
std::string NAMES_x2[] = {"","F:f","",""};

//-------------------------------------------------------------------
// Family CPU Descriptions.
//  CpuParams:
//    - Total number of defined resources
//    - Maximum "used" resource id
//    - Maximum functional unit id
//
//    - Number of distinct allocation pools
//    - Largest resource pool allocation size
//
//    - Instruction issue width
//    - Instruction reorder buffer size
//
//    - First execution pipeline phase
//    - Default load phase
//    - "High-latency instruction" write phase
//    - Latest resource use pipeline phase
//-------------------------------------------------------------------
CpuConfig<CpuParams<2,1,1, 0,0, 2,0, 0,4,10,0>> CPU_x1(&SUNITS_x1,__,2,NAMES_x1);
CpuConfig<CpuParams<2,1,1, 0,0, 2,0, 0,4,10,0>> CPU_x2(&SUNITS_x2,__,2,NAMES_x2);

//-------------------------------------------------------------------
// Top-level x Subtarget Description Table.
//-------------------------------------------------------------------
CpuTableDict CpuDict = {
  {"x1", &CPU_x1 },
  {"x2", &CPU_x2 },
};

CpuTableDef CpuTable = CpuTableDef(CpuDict);
}  // namespace x
}  // namespace llvm


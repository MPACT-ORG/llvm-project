//-------------------------------------------------------------------
// Machine Description Database.
// This file is auto-generated, do not edit.
//-------------------------------------------------------------------

#include "llvm/Support/Mutex.h"
#include "llvm/MC/MDLInfo.h"
#include "llvm/MC/MDLInstrInfo.h"
#include "test_fus_9InstrInfo.h"
#include "/usr/local/google/home/tatge/clone/llvm-project/llvm/test/Mdl/test_fus_9GenMdlInfo.h"


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
// Operand Reference Table (3 entries)
//     - Resource type(use, def, cond)
//     - Reference flags (protected, unprotected)
//     - Pipeline phase
//     - Pipeline phase function (optional)
//     - Operand index
//   or (for conditional references)
//     - Conditional reference
//-------------------------------------------------------------------
OperandRef OPND_1_data[] = {{RefDef,1,1,__,0}};
OperandRefVec OPND_1 = { 1, OPND_1_data };
OperandRef OPND_2_data[] = {{RefDef,1,3,__,0}};
OperandRefVec OPND_2 = { 1, OPND_2_data };
OperandRef OPND_0_data[] = {{RefDef,1,4,__,0}};
OperandRefVec OPND_0 = { 1, OPND_0_data };

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
// Used Resource Reference Table (4 entries)
//-------------------------------------------------------------------
ResourceRef URES_0_data[] = {{RefFus,0,1,1,1}};
ResourceRefVec URES_0 = { 1, URES_0_data };
ResourceRef URES_1_data[] = {{RefFus,1,1,2,1}};
ResourceRefVec URES_1 = { 1, URES_1_data };
ResourceRef URES_2_data[] = {{RefFus,2,1,3,1}};
ResourceRefVec URES_2 = { 1, URES_2_data };
ResourceRef URES_3_data[] = {{RefFus,4,1,4,1}};
ResourceRefVec URES_3 = { 1, URES_3_data };

//-------------------------------------------------------------------
// Held Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Reserved Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Resource Group Table (4 entries)
//-------------------------------------------------------------------
ResourceIdType GROUP_0[] {1,5,8};
ResourceIdType GROUP_1[] {5,8};
ResourceIdType GROUP_2[] {6,8};
ResourceIdType GROUP_3[] {7,8};

//-------------------------------------------------------------------
// Pool Descriptor Table (4 entries)
//      pool_id -    which pool to allocate from
//      pool_size -  how many different allocations sizes
//      count -      how many pool elements we need
//      pool_func -  optional pointer to pool size func
//      value_func - optional pointer to pool values func
//      first -      index of first legal element id
//      last -       index of last legal element id
//      width -      width in bits
//-------------------------------------------------------------------
PoolDescriptor POOL_1 {0,1,1,__,__,0,1,-1};
PoolDescriptor POOL_2 {1,1,1,__,__,0,1,-1};
PoolDescriptor POOL_3 {2,1,1,__,__,0,1,-1};
PoolDescriptor POOL_0 {3,1,1,__,__,0,2,-1};

//-------------------------------------------------------------------
// Pooled Resource Reference Table (8 entries)
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
PooledResourceRef PRES_6_data[] = {{RefFus,0,1,GROUP_0,&POOL_0,2}};
PooledResourceRefVec PRES_6 = { 1, PRES_6_data };
PooledResourceRef PRES_7_data[] = {{RefFus,1,1,GROUP_0,&POOL_0,2}};
PooledResourceRefVec PRES_7 = { 1, PRES_7_data };
PooledResourceRef PRES_2_data[] = {{RefFus,1,1,GROUP_1,&POOL_1,0},{RefFus,2,1,GROUP_2,&POOL_2,0},{RefFus,4,1,GROUP_3,&POOL_3,0},{RefFus,4,1,GROUP_0,&POOL_0,0}};
PooledResourceRefVec PRES_2 = { 4, PRES_2_data };
PooledResourceRef PRES_3_data[] = {{RefFus,1,1,GROUP_1,&POOL_1,1}};
PooledResourceRefVec PRES_3 = { 1, PRES_3_data };
PooledResourceRef PRES_0_data[] = {{RefFus,2,1,GROUP_0,&POOL_0,2}};
PooledResourceRefVec PRES_0 = { 1, PRES_0_data };
PooledResourceRef PRES_4_data[] = {{RefFus,2,1,GROUP_2,&POOL_2,2}};
PooledResourceRefVec PRES_4 = { 1, PRES_4_data };
PooledResourceRef PRES_1_data[] = {{RefFus,4,1,GROUP_0,&POOL_0,2}};
PooledResourceRefVec PRES_1 = { 1, PRES_1_data };
PooledResourceRef PRES_5_data[] = {{RefFus,4,1,GROUP_3,&POOL_3,2}};
PooledResourceRefVec PRES_5 = { 1, PRES_5_data };

//-------------------------------------------------------------------
// Operand Constraint Table (0 entries)
//     - Operand Index
//     - Constraint id
//   or (for conditional reference)
//     - Conditional constraint name

//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Subunit Table (12 entries)
//-------------------------------------------------------------------
Subunit SU_0_data[] = {{&OPND_0,__,__,__,&PRES_0,__}};
SubunitVec SU_0 = { 1, SU_0_data };
Subunit SU_1_data[] = {{&OPND_0,__,__,__,&PRES_1,__}};
SubunitVec SU_1 = { 1, SU_1_data };
Subunit SU_7_data[] = {{&OPND_0,__,__,__,&PRES_3,__}};
SubunitVec SU_7 = { 1, SU_7_data };
Subunit SU_8_data[] = {{&OPND_0,__,__,__,&PRES_4,__}};
SubunitVec SU_8 = { 1, SU_8_data };
Subunit SU_9_data[] = {{&OPND_0,__,__,__,&PRES_5,__}};
SubunitVec SU_9 = { 1, SU_9_data };
Subunit SU_10_data[] = {{&OPND_0,__,__,__,&PRES_6,__}};
SubunitVec SU_10 = { 1, SU_10_data };
Subunit SU_11_data[] = {{&OPND_0,__,__,__,&PRES_7,__}};
SubunitVec SU_11 = { 1, SU_11_data };
Subunit SU_2_data[] = {{&OPND_1,&URES_0,__,__,&PRES_2,__}};
SubunitVec SU_2 = { 1, SU_2_data };
Subunit SU_3_data[] = {{&OPND_2,&URES_0}};
SubunitVec SU_3 = { 1, SU_3_data };
Subunit SU_4_data[] = {{&OPND_2,&URES_1}};
SubunitVec SU_4 = { 1, SU_4_data };
Subunit SU_5_data[] = {{&OPND_2,&URES_2}};
SubunitVec SU_5 = { 1, SU_5_data };
Subunit SU_6_data[] = {{&OPND_2,&URES_3}};
SubunitVec SU_6 = { 1, SU_6_data };

//-------------------------------------------------------------------
// Instruction table initialization for x1 (12 valid entries)
//-------------------------------------------------------------------
__attribute__((optnone))
SubunitTable *SUNITS_x1() {
  static SubunitTable table;
  static sys::SmartMutex<true> Mutex;
  sys::SmartScopedLock<true> Lock(Mutex);
  if (table.size() != 0) return &table;
  table.resize(::llvm::x::INSTRUCTION_LIST_END, __);
  table[::llvm::x::x10] = &SU_0;
  table[::llvm::x::x11] = &SU_1;
  table[::llvm::x::x12] = &SU_2;
  table[::llvm::x::xx1] = &SU_3;
  table[::llvm::x::xx2] = &SU_4;
  table[::llvm::x::xx3] = &SU_5;
  table[::llvm::x::xx4] = &SU_6;
  table[::llvm::x::xx5] = &SU_7;
  table[::llvm::x::xx6] = &SU_8;
  table[::llvm::x::xx7] = &SU_9;
  table[::llvm::x::xx8] = &SU_10;
  table[::llvm::x::xx9] = &SU_11;
  return &table;
}

//-------------------------------------------------------------------
// Resource name tables
//-------------------------------------------------------------------
std::string NAMES_x1[] = {"","A:U1","AR:U2","AB:U3","AU:U4","B:U5","C:U6","D:U7","B:C:D:U8","",""};

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
CpuConfig<CpuParams<9,8,8, 4,1, 9,0, 0,4,10,0>> CPU_x1(&SUNITS_x1,__,18,NAMES_x1);

//-------------------------------------------------------------------
// Top-level x Subtarget Description Table.
//-------------------------------------------------------------------
CpuTableDict CpuDict = {
  {"x1", &CPU_x1 },
};

CpuTableDef CpuTable = CpuTableDef(CpuDict);
}  // namespace x
}  // namespace llvm


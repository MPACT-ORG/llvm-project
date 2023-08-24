//-------------------------------------------------------------------
// Machine Description Database.
// This file is auto-generated, do not edit.
//-------------------------------------------------------------------

#include "llvm/Support/Mutex.h"
#include "llvm/MC/MDLInfo.h"
#include "llvm/MC/MDLInstrInfo.h"
#include "test_fus_1InstrInfo.h"
#include "/usr/local/google/home/tatge/clone/llvm-project/llvm/test/Mdl/test_fus_1GenMdlInfo.h"


namespace llvm {
namespace test {
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
// Operand Reference Table (1 entries)
//     - Resource type(use, def, cond)
//     - Reference flags (protected, unprotected)
//     - Pipeline phase
//     - Pipeline phase function (optional)
//     - Operand index
//   or (for conditional references)
//     - Conditional reference
//-------------------------------------------------------------------
OperandRef OPND_0_data[] = {{RefDef,1,3,__,0}};
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
// Used Resource Reference Table (5 entries)
//-------------------------------------------------------------------
ResourceRef URES_0_data[] = {{RefFus,0,1,1,1}};
ResourceRefVec URES_0 = { 1, URES_0_data };
ResourceRef URES_1_data[] = {{RefFus,0,10,1,2}};
ResourceRefVec URES_1 = { 1, URES_1_data };
ResourceRef URES_2_data[] = {{RefFus,0,10,1,3},{RefFus,1,2,2,0}};
ResourceRefVec URES_2 = { 2, URES_2_data };
ResourceRef URES_4_data[] = {{RefFus,4,2,5,3}};
ResourceRefVec URES_4 = { 1, URES_4_data };
ResourceRef URES_3_data[] = {{RefFus,4|8,1,5,5}};
ResourceRefVec URES_3 = { 1, URES_3_data };

//-------------------------------------------------------------------
// Held Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Reserved Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Resource Group Table (2 entries)
//-------------------------------------------------------------------
ResourceIdType GROUP_0[] {3,4};
ResourceIdType GROUP_1[] {5,6};

//-------------------------------------------------------------------
// Pool Descriptor Table (2 entries)
//      pool_id -    which pool to allocate from
//      pool_size -  how many different allocations sizes
//      count -      how many pool elements we need
//      pool_func -  optional pointer to pool size func
//      value_func - optional pointer to pool values func
//      first -      index of first legal element id
//      last -       index of last legal element id
//      width -      width in bits
//-------------------------------------------------------------------
PoolDescriptor POOL_0 {0,1,1,__,__,0,1,-1};
PoolDescriptor POOL_1 {1,1,1,__,__,0,1,-1};

//-------------------------------------------------------------------
// Pooled Resource Reference Table (4 entries)
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
PooledResourceRef PRES_2_data[] = {{RefFus,0,1,GROUP_0,&POOL_0,0}};
PooledResourceRefVec PRES_2 = { 1, PRES_2_data };
PooledResourceRef PRES_0_data[] = {{RefFus,0,1,GROUP_0,&POOL_0,4}};
PooledResourceRefVec PRES_0 = { 1, PRES_0_data };
PooledResourceRef PRES_1_data[] = {{RefFus,0,1,GROUP_1,&POOL_1,6}};
PooledResourceRefVec PRES_1 = { 1, PRES_1_data };
PooledResourceRef PRES_3_data[] = {{RefFus,4,2,GROUP_1,&POOL_1,0}};
PooledResourceRefVec PRES_3 = { 1, PRES_3_data };

//-------------------------------------------------------------------
// Operand Constraint Table (0 entries)
//     - Operand Index
//     - Constraint id
//   or (for conditional reference)
//     - Conditional constraint name

//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Subunit Table (1 entries)
//-------------------------------------------------------------------
Subunit SU_0_data[] = {{&OPND_0,&URES_0},{&OPND_0,&URES_1},{&OPND_0,&URES_2},{&OPND_0,__,__,__,&PRES_0,__},{&OPND_0,&URES_3},{&OPND_0,__,__,__,&PRES_1,__},{&OPND_0,&URES_2,__,__,&PRES_2,__},{&OPND_0,&URES_4,__,__,&PRES_3,__}};
SubunitVec SU_0 = { 8, SU_0_data };

//-------------------------------------------------------------------
// Instruction table initialization for cpu (1 valid entries)
//-------------------------------------------------------------------
__attribute__((optnone))
SubunitTable *SUNITS_cpu() {
  static SubunitTable table;
  static sys::SmartMutex<true> Mutex;
  sys::SmartScopedLock<true> Lock(Mutex);
  if (table.size() != 0) return &table;
  table.resize(::llvm::test::INSTRUCTION_LIST_END, __);
  table[::llvm::test::abc] = &SU_0;
  return &table;
}

//-------------------------------------------------------------------
// Resource name tables
//-------------------------------------------------------------------
std::string NAMES_cpu[] = {"","FU1:fu_1","FU2:fu_2","FU3:fu_3","FU3:fu_3a","FU4:FU6:fu_4","FU5:FU6:fu_5","s1","s2","s3","",""};

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
CpuConfig<CpuParams<10,6,6, 2,1, 3,42, 1,4,10,12>> CPU_cpu(&SUNITS_cpu,__,6,NAMES_cpu);

//-------------------------------------------------------------------
// Top-level test Subtarget Description Table.
//-------------------------------------------------------------------
CpuTableDict CpuDict = {
  {"cpu", &CPU_cpu },
};

CpuTableDef CpuTable = CpuTableDef(CpuDict);
}  // namespace test
}  // namespace llvm


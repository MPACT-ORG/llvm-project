//-------------------------------------------------------------------
// Machine Description Database.
// This file is auto-generated, do not edit.
//-------------------------------------------------------------------

#include "llvm/Support/Mutex.h"
#include "llvm/MC/MDLInfo.h"
#include "llvm/MC/MDLInstrInfo.h"
#include "test_cond_refsInstrInfo.h"
#include "test_cond_refsGenMdlInfo.h"


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
// Predicate functions
//-------------------------------------------------------------------
bool PRED_0(Instr *MI) {
return MI->getOperand(0) == test::r0;
}
bool PRED_1(Instr *MI) {
return MI->getOperand(0) == test::r1;
}

//-------------------------------------------------------------------
// Functions to compute non-trivial pipeline phase expressions
//-------------------------------------------------------------------

unsigned PIPE_0(Instr *ins) {
  if (PRED_0(ins))  return 17;
  if (PRED_1(ins))  return 29;
 }

//-------------------------------------------------------------------
// Forward references for conditional references
//-------------------------------------------------------------------
extern OperandRefVec OPND_1;
extern OperandRefVec OPND_2;

extern ConditionalRef<OperandRef> COND_0;

extern ResourceRefVec URES_0;
extern ResourceRefVec URES_1;
extern ResourceRefVec URES_3;
extern ResourceRefVec URES_4;
extern ResourceRefVec URES_6;
extern ResourceRefVec URES_7;

extern ConditionalRef<ResourceRef> CRES_0;
extern ConditionalRef<ResourceRef> CRES_2;
extern ConditionalRef<ResourceRef> CRES_4;
extern ConditionalRef<ResourceRef> CRES_5;

extern PooledResourceRefVec PRES_0;
extern PooledResourceRefVec PRES_2;
extern PooledResourceRefVec PRES_3;

extern ConditionalRef<PooledResourceRef> CPOOL_1;
extern ConditionalRef<PooledResourceRef> CPOOL_2;

extern OperandConstraintVec REG_0;

extern ConditionalRef<OperandConstraint> CREG_0;


//-------------------------------------------------------------------
// Conditional Reference Tables:
//     - Predicate function (optional)
//     - References (operands, resource or pooled resource refs
//     - "Else" clause conditional reference (optional)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Conditional Operand Reference Table (2 entries)
//-------------------------------------------------------------------
ConditionalRef<OperandRef> COND_1 {&PRED_0,&OPND_1,&COND_0};
ConditionalRef<OperandRef> COND_0 {&PRED_1,&OPND_2,__};

//-------------------------------------------------------------------
// Conditional Resource Reference Table (7 entries)
//-------------------------------------------------------------------
ConditionalRef<ResourceRef> CRES_1 {&PRED_0,&URES_0,&CRES_0};
ConditionalRef<ResourceRef> CRES_3 {&PRED_0,&URES_3,&CRES_2};
ConditionalRef<ResourceRef> CRES_6 {&PRED_0,__,&CRES_5};
ConditionalRef<ResourceRef> CRES_0 {&PRED_1,&URES_1,__};
ConditionalRef<ResourceRef> CRES_2 {&PRED_1,&URES_4,__};
ConditionalRef<ResourceRef> CRES_5 {&PRED_1,&URES_6,&CRES_4};
ConditionalRef<ResourceRef> CRES_4 {__,&URES_7,__};

//-------------------------------------------------------------------
// Conditional Pooled Resource Reference Table (4 entries)
//-------------------------------------------------------------------
ConditionalRef<PooledResourceRef> CPOOL_0 {&PRED_0,&PRES_0,__};
ConditionalRef<PooledResourceRef> CPOOL_3 {&PRED_0,__,&CPOOL_2};
ConditionalRef<PooledResourceRef> CPOOL_2 {&PRED_1,&PRES_2,&CPOOL_1};
ConditionalRef<PooledResourceRef> CPOOL_1 {__,&PRES_3,__};

//-------------------------------------------------------------------
// Conditional Constraints Table (2 entries)
//-------------------------------------------------------------------
ConditionalRef<OperandConstraint> CREG_1 {&PRED_0,__,&CREG_0};
ConditionalRef<OperandConstraint> CREG_0 {&PRED_1,&REG_0,__};

//-------------------------------------------------------------------
// Operand Reference Table (4 entries)
//     - Resource type(use, def, cond)
//     - Reference flags (protected, unprotected)
//     - Pipeline phase
//     - Pipeline phase function (optional)
//     - Operand index
//   or (for conditional references)
//     - Conditional reference
//-------------------------------------------------------------------
OperandRef OPND_1_data[] = {{RefDef,1,17,__,0}};
OperandRefVec OPND_1 = { 1, OPND_1_data };
OperandRef OPND_2_data[] = {{RefDef,1,29,__,0}};
OperandRefVec OPND_2 = { 1, OPND_2_data };
OperandRef OPND_3_data[] = {{RefDef,1,42,__,1},{&COND_1}};
OperandRefVec OPND_3 = { 2, OPND_3_data };
OperandRef OPND_0_data[] = {{RefDef,1,42,__,1},{RefDef,1,-1,&PIPE_0,0}};
OperandRefVec OPND_0 = { 2, OPND_0_data };

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
// Used Resource Reference Table (9 entries)
//-------------------------------------------------------------------
ResourceRef URES_8_data[] = {{&CRES_6}};
ResourceRefVec URES_8 = { 1, URES_8_data };
ResourceRef URES_7_data[] = {{RefFus,0,1,1,12}};
ResourceRefVec URES_7 = { 1, URES_7_data };
ResourceRef URES_6_data[] = {{RefFus,0,1,1,14}};
ResourceRefVec URES_6 = { 1, URES_6_data };
ResourceRef URES_2_data[] = {{RefUse,1,0,__,1,1,-1,-1},{RefUse,1,42,__,1,2,1,-1},{&CRES_1}};
ResourceRefVec URES_2 = { 3, URES_2_data };
ResourceRef URES_5_data[] = {{RefUse,1,0,__,1,1,-1,-1},{RefUse,1,42,__,1,3,1,-1},{&CRES_3}};
ResourceRefVec URES_5 = { 3, URES_5_data };
ResourceRef URES_0_data[] = {{RefUse,1,17,__,1,2,0,-1}};
ResourceRefVec URES_0 = { 1, URES_0_data };
ResourceRef URES_3_data[] = {{RefUse,1,17,__,1,3,0,-1}};
ResourceRefVec URES_3 = { 1, URES_3_data };
ResourceRef URES_1_data[] = {{RefUse,1,29,__,1,2,0,-1}};
ResourceRefVec URES_1 = { 1, URES_1_data };
ResourceRef URES_4_data[] = {{RefUse,1,29,__,1,3,0,-1}};
ResourceRefVec URES_4 = { 1, URES_4_data };

//-------------------------------------------------------------------
// Held Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Reserved Resource Reference Table (0 entries)
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// Resource Group Table (2 entries)
//-------------------------------------------------------------------
ResourceIdType GROUP_1[] {1,2};
ResourceIdType GROUP_0[] {4,5,6,7};

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
PoolDescriptor POOL_0 {-1,2,2,__,__,0,3,-1};
PoolDescriptor POOL_1 {0,1,1,__,__,0,1,-1};

//-------------------------------------------------------------------
// Pooled Resource Reference Table (5 entries)
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
PooledResourceRef PRES_1_data[] = {{&CPOOL_0}};
PooledResourceRefVec PRES_1 = { 1, PRES_1_data };
PooledResourceRef PRES_4_data[] = {{&CPOOL_3}};
PooledResourceRefVec PRES_4 = { 1, PRES_4_data };
PooledResourceRef PRES_3_data[] = {{RefFus,0,1,GROUP_1,&POOL_1,12}};
PooledResourceRefVec PRES_3 = { 1, PRES_3_data };
PooledResourceRef PRES_2_data[] = {{RefFus,0,1,GROUP_1,&POOL_1,14}};
PooledResourceRefVec PRES_2 = { 1, PRES_2_data };
PooledResourceRef PRES_0_data[] = {{RefUse,1,17,__,1,GROUP_0,0,&POOL_0}};
PooledResourceRefVec PRES_0 = { 1, PRES_0_data };

//-------------------------------------------------------------------
// Operand Constraint Table (2 entries)
//     - Operand Index
//     - Constraint id
//   or (for conditional reference)
//     - Conditional constraint name

//-------------------------------------------------------------------
OperandConstraint REG_0_data[] = {{0,test::LOWRegClassId}};
OperandConstraintVec REG_0 = { 1, REG_0_data };
OperandConstraint REG_1_data[] = {{1,test::LOWRegClassId},{&CREG_1}};
OperandConstraintVec REG_1 = { 2, REG_1_data };

//-------------------------------------------------------------------
// Subunit Table (3 entries)
//-------------------------------------------------------------------
Subunit SU_0_data[] = {{&OPND_0,&URES_2,__,__,&PRES_1,&REG_1},{&OPND_0,&URES_5}};
SubunitVec SU_0 = { 2, SU_0_data };
Subunit SU_1_data[] = {{&OPND_3,&URES_8}};
SubunitVec SU_1 = { 1, SU_1_data };
Subunit SU_2_data[] = {{&OPND_3,__,__,__,&PRES_4,__}};
SubunitVec SU_2 = { 1, SU_2_data };

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
// Instruction table initialization for cpu1 (1 valid entries)
//-------------------------------------------------------------------
__attribute__((optnone))
SubunitTable *SUNITS_cpu1() {
  static SubunitTable table;
  static sys::SmartMutex<true> Mutex;
  sys::SmartScopedLock<true> Lock(Mutex);
  if (table.size() != 0) return &table;
  table.resize(::llvm::test::INSTRUCTION_LIST_END, __);
  table[::llvm::test::abc] = &SU_1;
  return &table;
}

//-------------------------------------------------------------------
// Instruction table initialization for cpu2 (1 valid entries)
//-------------------------------------------------------------------
__attribute__((optnone))
SubunitTable *SUNITS_cpu2() {
  static SubunitTable table;
  static sys::SmartMutex<true> Mutex;
  sys::SmartScopedLock<true> Lock(Mutex);
  if (table.size() != 0) return &table;
  table.resize(::llvm::test::INSTRUCTION_LIST_END, __);
  table[::llvm::test::abc] = &SU_2;
  return &table;
}

//-------------------------------------------------------------------
// Resource name tables
//-------------------------------------------------------------------
std::string NAMES_cpu[] = {"","fu:fu_a","a","b","c[0]","c[1]","c[2]","c[3]","",""};
std::string NAMES_cpu1[] = {"","FU:fu1","",""};
std::string NAMES_cpu2[] = {"","FU:fu1","FU:fu2","",""};

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
CpuConfig<CpuParams<8,7,1, 0,2, 1,0, 1,4,10,42>> CPU_cpu(&SUNITS_cpu,__,1,NAMES_cpu);
CpuConfig<CpuParams<2,1,1, 0,0, 2,0, 1,4,10,1>> CPU_cpu1(&SUNITS_cpu1,__,2,NAMES_cpu1);
CpuConfig<CpuParams<3,2,2, 1,1, 3,0, 1,4,10,1>> CPU_cpu2(&SUNITS_cpu2,__,6,NAMES_cpu2);

//-------------------------------------------------------------------
// Top-level test Subtarget Description Table.
//-------------------------------------------------------------------
CpuTableDict CpuDict = {
  {"cpu", &CPU_cpu },
  {"cpu1", &CPU_cpu1 },
  {"cpu2", &CPU_cpu2 },
};

CpuTableDef CpuTable = CpuTableDef(CpuDict);
}  // namespace test
}  // namespace llvm


#ifndef test_cond_refs_MACHINE_DESCRIPTION_DATABASE
#define test_cond_refs_MACHINE_DESCRIPTION_DATABASE
//-------------------------------------------------------------------
// Machine Description Database.
// This file is auto-generated, do not edit.
//-------------------------------------------------------------------
#include <map>
#include <string>

namespace llvm {
namespace test {

//-------------------------------------------------------------------
// Global constant definitions
//-------------------------------------------------------------------
const int kMaxResourceId = 8;
const int kMaxUsedResourceId = 7;
const int kMaxPipePhase = 42;
const int kMaxIssue = 3;
const int kMaxPools = 1;
const int kMaxPoolCount = 2;

namespace Mdl {

//-------------------------------------------------------------------
// Resource Definitions for cpu
//-------------------------------------------------------------------
  namespace cpu {
    const int a = 2;      // test::Mdl::cpu::a (resource)
    const int b = 3;      // test::Mdl::cpu::b (resource)
    const int c = 4;      // test::Mdl::cpu::c (resource)
    const int fu_a = 1;      // test::Mdl::cpu::fu_a (func unit)
  }  // namespace cpu

//-------------------------------------------------------------------
// Resource Definitions for cpu1
//-------------------------------------------------------------------
  namespace cpu1 {
    const int fu1 = 1;      // test::Mdl::cpu1::fu1 (func unit)
  }  // namespace cpu1

//-------------------------------------------------------------------
// Resource Definitions for cpu2
//-------------------------------------------------------------------
  namespace cpu2 {
    const int fu1 = 1;      // test::Mdl::cpu2::fu1 (func unit)
    const int fu2 = 2;      // test::Mdl::cpu2::fu2 (func unit)
  }  // namespace cpu2
}  // namespace Mdl

//-------------------------------------------------------------------
// External definitions
//-------------------------------------------------------------------
extern llvm::mdl::CpuTableDef CpuTable;

}  // namespace test
}  // namespace llvm

#endif  // test_cond_refs_MACHINE_DESCRIPTION_DATABASE

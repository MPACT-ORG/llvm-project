#ifndef test_fus_1_MACHINE_DESCRIPTION_DATABASE
#define test_fus_1_MACHINE_DESCRIPTION_DATABASE
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
const int kMaxResourceId = 10;
const int kMaxUsedResourceId = 6;
const int kMaxPipePhase = 12;
const int kMaxIssue = 3;
const int kMaxPools = 2;
const int kMaxPoolCount = 1;

namespace Mdl {

//-------------------------------------------------------------------
// Resource Definitions for cpu
//-------------------------------------------------------------------
  namespace cpu {
    const int fu_1 = 1;      // test::Mdl::cpu::fu_1 (func unit)
    const int fu_2 = 2;      // test::Mdl::cpu::fu_2 (func unit)
    const int fu_3 = 3;      // test::Mdl::cpu::fu_3 (func unit)
    const int fu_3a = 4;      // test::Mdl::cpu::fu_3a (func unit)
    const int fu_4 = 5;      // test::Mdl::cpu::fu_4 (func unit)
    const int fu_5 = 6;      // test::Mdl::cpu::fu_5 (func unit)
    const int s1 = 7;      // test::Mdl::cpu::s1 (issue)
    const int s2 = 8;      // test::Mdl::cpu::s2 (issue)
    const int s3 = 9;      // test::Mdl::cpu::s3 (issue)
  }  // namespace cpu
}  // namespace Mdl

//-------------------------------------------------------------------
// External definitions
//-------------------------------------------------------------------
extern llvm::mdl::CpuTableDef CpuTable;

}  // namespace test
}  // namespace llvm

#endif  // test_fus_1_MACHINE_DESCRIPTION_DATABASE

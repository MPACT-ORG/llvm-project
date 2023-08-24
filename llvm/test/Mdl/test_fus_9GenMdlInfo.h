#ifndef test_fus_9_MACHINE_DESCRIPTION_DATABASE
#define test_fus_9_MACHINE_DESCRIPTION_DATABASE
//-------------------------------------------------------------------
// Machine Description Database.
// This file is auto-generated, do not edit.
//-------------------------------------------------------------------
#include <map>
#include <string>

namespace llvm {
namespace x {

//-------------------------------------------------------------------
// Global constant definitions
//-------------------------------------------------------------------
const int kMaxResourceId = 9;
const int kMaxUsedResourceId = 8;
const int kMaxPipePhase = 0;
const int kMaxIssue = 9;
const int kMaxPools = 4;
const int kMaxPoolCount = 1;

namespace Mdl {

//-------------------------------------------------------------------
// Resource Definitions for x1
//-------------------------------------------------------------------
  namespace x1 {
    const int U1 = 1;      // x::Mdl::x1::U1 (func unit)
    const int U2 = 2;      // x::Mdl::x1::U2 (func unit)
    const int U3 = 3;      // x::Mdl::x1::U3 (func unit)
    const int U4 = 4;      // x::Mdl::x1::U4 (func unit)
    const int U5 = 5;      // x::Mdl::x1::U5 (func unit)
    const int U6 = 6;      // x::Mdl::x1::U6 (func unit)
    const int U7 = 7;      // x::Mdl::x1::U7 (func unit)
    const int U8 = 8;      // x::Mdl::x1::U8 (func unit)
  }  // namespace x1
}  // namespace Mdl

//-------------------------------------------------------------------
// External definitions
//-------------------------------------------------------------------
extern llvm::mdl::CpuTableDef CpuTable;

}  // namespace x
}  // namespace llvm

#endif  // test_fus_9_MACHINE_DESCRIPTION_DATABASE

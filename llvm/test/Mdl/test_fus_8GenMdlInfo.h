#ifndef test_fus_8_MACHINE_DESCRIPTION_DATABASE
#define test_fus_8_MACHINE_DESCRIPTION_DATABASE
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
const int kMaxResourceId = 2;
const int kMaxUsedResourceId = 1;
const int kMaxPipePhase = 0;
const int kMaxIssue = 2;
const int kMaxPools = 0;
const int kMaxPoolCount = 0;

namespace Mdl {

//-------------------------------------------------------------------
// Resource Definitions for x1
//-------------------------------------------------------------------
  namespace x1 {
    const int f = 1;      // x::Mdl::x1::f (func unit)
  }  // namespace x1

//-------------------------------------------------------------------
// Resource Definitions for x2
//-------------------------------------------------------------------
  namespace x2 {
    const int f = 1;      // x::Mdl::x2::f (func unit)
  }  // namespace x2
}  // namespace Mdl

//-------------------------------------------------------------------
// External definitions
//-------------------------------------------------------------------
extern llvm::mdl::CpuTableDef CpuTable;

}  // namespace x
}  // namespace llvm

#endif  // test_fus_8_MACHINE_DESCRIPTION_DATABASE

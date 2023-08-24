//===- LanaiSubtarget.cpp - Lanai Subtarget Information -----------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Lanai specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "LanaiSubtarget.h"

#include "Lanai.h"
#include "llvm/Config/config.h"

#define DEBUG_TYPE "lanai-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "LanaiGenSubtargetInfo.inc"

// Include definitions associated with the MDL description.
#if ENABLE_MDL_USE
#include "LanaiGenMdlInfo.h"
// Include virtual predicate function definitions from the MDL description.
#include "LanaiGenMdlTarget.inc"
#define LanaiCpuTable &Lanai::CpuTable
#else
#define LanaiCpuTable nullptr
#endif

using namespace llvm;

void LanaiSubtarget::initSubtargetFeatures(StringRef CPU, StringRef FS) {
  std::string CPUName = std::string(CPU);
  if (CPUName.empty())
    CPUName = "generic";

  ParseSubtargetFeatures(CPUName, /*TuneCPU*/ CPUName, FS);
}

LanaiSubtarget &LanaiSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS) {
  initSubtargetFeatures(CPU, FS);
  return *this;
}

LanaiSubtarget::LanaiSubtarget(const Triple &TargetTriple, StringRef Cpu,
                               StringRef FeatureString, const TargetMachine &TM,
                               const TargetOptions & /*Options*/,
                               CodeModel::Model /*CodeModel*/,
                               CodeGenOptLevel /*OptLevel*/)
    : LanaiGenSubtargetInfo(TargetTriple, Cpu, /*TuneCPU*/ Cpu, FeatureString,
                            LanaiCpuTable),
      FrameLowering(initializeSubtargetDependencies(Cpu, FeatureString)),
      TLInfo(TM, *this) {

  // Register the Target-library-specific predicate table in the cpu table.
#if ENABLE_MDL_USE
  Lanai::CpuTable.SetInstrPredicates(&Lanai::InstrPredicates);
#endif
}

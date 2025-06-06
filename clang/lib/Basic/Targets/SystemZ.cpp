//===--- SystemZ.cpp - Implement SystemZ target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements SystemZ TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "SystemZ.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

static constexpr int NumBuiltins =
    clang::SystemZ::LastTSBuiltin - Builtin::FirstTSBuiltin;

static constexpr llvm::StringTable BuiltinStrings =
    CLANG_BUILTIN_STR_TABLE_START
#define BUILTIN CLANG_BUILTIN_STR_TABLE
#define TARGET_BUILTIN CLANG_TARGET_BUILTIN_STR_TABLE
#include "clang/Basic/BuiltinsSystemZ.def"
    ;

static constexpr auto BuiltinInfos = Builtin::MakeInfos<NumBuiltins>({
#define BUILTIN CLANG_BUILTIN_ENTRY
#define TARGET_BUILTIN CLANG_TARGET_BUILTIN_ENTRY
#include "clang/Basic/BuiltinsSystemZ.def"
});

const char *const SystemZTargetInfo::GCCRegNames[] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "f0",  "f2",  "f4",  "f6",  "f1",  "f3",  "f5",  "f7",
    "f8",  "f10", "f12", "f14", "f9",  "f11", "f13", "f15",
    /*ap*/"", "cc", /*fp*/"", /*rp*/"", "a0",  "a1",
    "v16", "v18", "v20", "v22", "v17", "v19", "v21", "v23",
    "v24", "v26", "v28", "v30", "v25", "v27", "v29", "v31"
};

const TargetInfo::AddlRegName GCCAddlRegNames[] = {
    {{"v0"}, 16}, {{"v2"},  17}, {{"v4"},  18}, {{"v6"},  19},
    {{"v1"}, 20}, {{"v3"},  21}, {{"v5"},  22}, {{"v7"},  23},
    {{"v8"}, 24}, {{"v10"}, 25}, {{"v12"}, 26}, {{"v14"}, 27},
    {{"v9"}, 28}, {{"v11"}, 29}, {{"v13"}, 30}, {{"v15"}, 31}
};

ArrayRef<const char *> SystemZTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::AddlRegName> SystemZTargetInfo::getGCCAddlRegNames() const {
  return llvm::ArrayRef(GCCAddlRegNames);
}

bool SystemZTargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;

  case 'Z':
    switch (Name[1]) {
    default:
      return false;
    case 'Q': // Address with base and unsigned 12-bit displacement
    case 'R': // Likewise, plus an index
    case 'S': // Address with base and signed 20-bit displacement
    case 'T': // Likewise, plus an index
      break;
    }
    [[fallthrough]];
  case 'a': // Address register
  case 'd': // Data register (equivalent to 'r')
  case 'f': // Floating-point register
  case 'v': // Vector register
    Info.setAllowsRegister();
    return true;

  case 'I': // Unsigned 8-bit constant
  case 'J': // Unsigned 12-bit constant
  case 'K': // Signed 16-bit constant
  case 'L': // Signed 20-bit displacement (on all targets we support)
  case 'M': // 0x7fffffff
    return true;

  case 'Q': // Memory with base and unsigned 12-bit displacement
  case 'R': // Likewise, plus an index
  case 'S': // Memory with base and signed 20-bit displacement
  case 'T': // Likewise, plus an index
    Info.setAllowsMemory();
    return true;
  }
}

struct ISANameRevision {
  llvm::StringLiteral Name;
  int ISARevisionID;
};
static constexpr ISANameRevision ISARevisions[] = {
  {{"arch8"}, 8}, {{"z10"}, 8},
  {{"arch9"}, 9}, {{"z196"}, 9},
  {{"arch10"}, 10}, {{"zEC12"}, 10},
  {{"arch11"}, 11}, {{"z13"}, 11},
  {{"arch12"}, 12}, {{"z14"}, 12},
  {{"arch13"}, 13}, {{"z15"}, 13},
  {{"arch14"}, 14}, {{"z16"}, 14},
  {{"arch15"}, 15}, {{"z17"}, 15},
};

int SystemZTargetInfo::getISARevision(StringRef Name) const {
  const auto Rev =
      llvm::find_if(ISARevisions, [Name](const ISANameRevision &CR) {
        return CR.Name == Name;
      });
  if (Rev == std::end(ISARevisions))
    return -1;
  return Rev->ISARevisionID;
}

void SystemZTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  for (const ISANameRevision &Rev : ISARevisions)
    Values.push_back(Rev.Name);
}

bool SystemZTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("systemz", true)
      .Case("arch8", ISARevision >= 8)
      .Case("arch9", ISARevision >= 9)
      .Case("arch10", ISARevision >= 10)
      .Case("arch11", ISARevision >= 11)
      .Case("arch12", ISARevision >= 12)
      .Case("arch13", ISARevision >= 13)
      .Case("arch14", ISARevision >= 14)
      .Case("arch15", ISARevision >= 15)
      .Case("htm", HasTransactionalExecution)
      .Case("vx", HasVector)
      .Default(false);
}

unsigned SystemZTargetInfo::getMinGlobalAlign(uint64_t Size,
                                              bool HasNonWeakDef) const {
  // Don't enforce the minimum alignment on an external or weak symbol if
  // -munaligned-symbols is passed.
  if (UnalignedSymbols && !HasNonWeakDef)
    return 0;

  return MinGlobalAlign;
}

void SystemZTargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__s390__");
  Builder.defineMacro("__s390x__");
  Builder.defineMacro("__zarch__");
  Builder.defineMacro("__LONG_DOUBLE_128__");

  Builder.defineMacro("__ARCH__", Twine(ISARevision));

  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");

  if (HasTransactionalExecution)
    Builder.defineMacro("__HTM__");
  if (HasVector)
    Builder.defineMacro("__VX__");
  if (Opts.ZVector)
    Builder.defineMacro("__VEC__", "10305");

  /* Set __TARGET_LIB__ only if a value was given.  If no value was given  */
  /* we rely on the LE headers to define __TARGET_LIB__.                   */
  if (!getTriple().getOSVersion().empty()) {
    llvm::VersionTuple V = getTriple().getOSVersion();
    // Create string with form: 0xPVRRMMMM, where P=4
    std::string Str("0x");
    unsigned int Librel = 0x40000000;
    Librel |= V.getMajor() << 24;
    Librel |= V.getMinor().value_or(1) << 16;
    Librel |= V.getSubminor().value_or(0);
    Str += llvm::utohexstr(Librel);

    Builder.defineMacro("__TARGET_LIB__", Str.c_str());
  }
}

llvm::SmallVector<Builtin::InfosShard>
SystemZTargetInfo::getTargetBuiltins() const {
  return {{&BuiltinStrings, BuiltinInfos}};
}

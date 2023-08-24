//===- MDLInstrInfo.h - MDL-based instruction modeling --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a set of APIs between the MDL database and the CodeGen
// and MC libraries.  The MDL database uses the Instr class to access
// information about MachineInstr and MCInst objects, and the CodeGen/MC
// libraries use these interfaces to calculate instruction latencies.
//
//===----------------------------------------------------------------------===//

#ifndef MDL_INSTR_INFO_H
#define MDL_INSTR_INFO_H

#include "llvm/MC/MDLInfo.h"
#include <vector>

namespace llvm {
namespace mdl {

/// Calculate the latency between two instructions' operands.
int calculateOperandLatency(const Instr *Def, unsigned DefOpId,
                            const Instr *Use, unsigned UseOpId);

/// Wrapper for MachineInstr Objects.
int calculateOperandLatency(const MachineInstr *Def, unsigned DefOpId,
                            const MachineInstr *Use, unsigned UseOpId,
                            const TargetSubtargetInfo *STI);

/// Find latest resource allocation for an instruction.
int calculateResourceLatency(const MachineInstr *def,
                             const TargetSubtargetInfo *STI);

/// Find the maximum latency of an instruction based on operand references.
int calculateInstructionLatency(Instr *Inst);

/// Wrapper for MCInst objects.
int calculateInstructionLatency(const MCInst *Inst, const MCSubtargetInfo *STI,
                                const MCInstrInfo *MCII);
/// Wrapper for MachineInstr objects.
int calculateInstructionLatency(const MachineInstr *Inst,
                                const TargetSubtargetInfo *STI);

/// Calculate the latency between two instructions that hold or reserve the
/// same resource.
int calculateHazardLatency(const Instr *Reserve, const Instr *Hold);

/// Wrapper for MCInst objects.
int calculateHazardLatency(const MCInst *Reserve, const MCInst *Hold,
                           const MCSubtargetInfo *STI, const MCInstrInfo *MCII);

/// Wrapper for MachineInstr objects.
int calculateHazardLatency(const MachineInstr *Reserve,
                           const MachineInstr *Hold,
                           const TargetSubtargetInfo *STI);

} // namespace mdl
} // namespace llvm

#endif // MDL_INSTR_INFO_H

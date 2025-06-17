//===- TargetMdl.td - MDL instruction information ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines instruction attributes which can be used to tie
// instruction definitions to subunits defined in MDL target descriptions.
//
//===----------------------------------------------------------------------===//

// A SubUnitEncoding describes a single subunit defined in an MDL file.
// These names are expected to consist of an "SU_" prefix with the actual
// name of the defined subunit.   (For example "SU_MyAlu"). 
class SubUnitEncoding {}

// A SubUnits list is used in instruction definitions to tie an instruction
// definition to a set of SubUnits defined in an MDL description.
class SubUnits<list<SubUnitEncoding> subunits> {
  list<SubUnitEncoding> SubUnits = subunits;
}


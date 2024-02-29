//===- mdl_generate.h - Objects for generate the MDL database -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file contains class definitions used to build the machine description
//  database.
//
//===----------------------------------------------------------------------===//

#ifndef MDL_COMPILER_MDL_GENERATE_H_
#define MDL_COMPILER_MDL_GENERATE_H_

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mdl.h"

namespace mpact {
namespace mdl {

class InstrInfo;
class ResourceEvent;
struct TargetDataBase;

using ResourceList = std::vector<ResourceEvent>;
using ResourceSets = std::vector<std::vector<ResourceEvent>>;
using InstrInfoList = std::vector<InstrInfo *>;

constexpr int kOneCycle = 1;

//-----------------------------------------------------------------------------
// A description of a single resource reference.
//-----------------------------------------------------------------------------
class ResourceEvent {
  RefType Type;                     // type of reference
  int PhaseValue = -1;              // value of phase if constant expression
  PhaseExpr *Expr = nullptr;        // when reference happens
  int UseCycles = 1;                // # cycles resource is used
  ResourceRef *Resource;            // referenced resource
  int MicroOps = 0;                 // micro_ops (for fus)
  RefFlags::Item FuFlags;           // various flags for explicit fu refs
  Reference *Ref = nullptr;         // pointer to original reference
  SubUnitInstantiation *Subunit;    // pointer to subunit instantiation context

public:
  ResourceEvent(RefType Type, PhaseExpr *Expr, int Cycles, ResourceRef *Res,
                Reference *Ref = nullptr,
                SubUnitInstantiation *Subunit = nullptr)
      : Type(Type), Expr(Expr), UseCycles(Cycles),
        Resource(Res), Ref(Ref), Subunit(Subunit) {
    Res->getDefinition()->recordReference(Type, Expr, Res, Ref, Subunit);
    setConstantPhase();
  }
  ResourceEvent(RefType Type, PhaseExpr *Expr, ResourceRef *Res,
                Reference *Ref = nullptr,
                SubUnitInstantiation *Subunit = nullptr)
      : Type(Type), Expr(Expr), UseCycles(kOneCycle),
        Resource(Res), Ref(Ref), Subunit(Subunit) {
    Res->getDefinition()->recordReference(Type, Expr, Res, Ref, Subunit);
    setConstantPhase();
  }
  ResourceEvent(RefType Type, PhaseExpr *Expr, ResourceDef *port)
      : Type(Type), Expr(Expr), Resource(new ResourceRef(port)) {
    port->recordReference(Type, Expr, nullptr, nullptr, nullptr);
    setConstantPhase();
  }
  // Constructor for an fus reference, including micro_ops.
  ResourceEvent(RefType Type, Reference *Ref, ResourceRef *Res)
      : Type(Type), Expr(Ref->getPhaseExpr()),
        UseCycles(Ref->getUseCycles()), Resource(Res),
        MicroOps(Ref->getMicroOps()), FuFlags(Ref->getFuFlags()),
        Ref(Ref) {
    Res->getDefinition()->recordReference(
                          Type, Expr, Res, nullptr, nullptr);
  }

  RefType getRefType() const { return Type; }
  bool isFuncUnitRef() const { return Type == RefTypes::kFus; }
  PhaseExpr *getPhaseExpr() const { return Expr; }
  int getUseCycles() const { return UseCycles; }
  int getMicroOps() const { return MicroOps; }

  RefFlags::Item getFuFlags() const { return FuFlags; }
  ResourceRef *getResource() const { return Resource; }
  Reference *getReference() const { return Ref; }
  SubUnitInstantiation *getSubunit() const { return Subunit; }
  void setConstantPhase() { PhaseValue = Expr->getConstantPhase(); }

  // Resource references are sorted by pipeline phase, then by resource id.
  // If the pipeline phase is non-constant, its ordered last. If both are
  // non-constant, use the formatting string to decide (so that the sort
  // is stable).
  bool operator<(const ResourceEvent &Rhs) const {
    if (PhaseValue != Rhs.PhaseValue) {
      if (PhaseValue == -1)
        return false;
      if (Rhs.PhaseValue == -1)
        return true;
      return PhaseValue < Rhs.PhaseValue;
    }
    if (PhaseValue == -1 && Rhs.PhaseValue == -1)
      return Expr->ToString() < Rhs.Expr->ToString();

    return Resource->getFinalResourceId() <
           Rhs.getResource()->getFinalResourceId();
  }
  bool operator>(const ResourceEvent &Rhs) const { return Rhs < *this; }

  std::string ToString() const {
    return formatv("{0}{1}({2},{3})", convertRefTypeToString(getRefType()),
                   Expr->formatProtection(), Expr->ToString(),
                   Resource->ToString());
  }
};

//-----------------------------------------------------------------------------
// A description of operand and resource references for a single instruction
// and subunit instantiation pair.
//-----------------------------------------------------------------------------
class InstrInfo {
  InstructionDef *Instruct;        // pointer to the instruction description
  SubUnitInstantiation *Subunit;   // which subunit instance
  ReferenceList *References;       // valid references for this instruction
  ResourceList Resources;          // sets of resource references
  ReferenceList ResourceRefs;      // conditional resources and FUs

public:
  InstrInfo(InstructionDef *Instruct, SubUnitInstantiation *Subunit,
            ResourceList &Resources, ReferenceList *Refs,
            ReferenceList &ResourceRefs)
      : Instruct(Instruct), Subunit(Subunit), References(Refs),
        Resources(Resources), ResourceRefs(ResourceRefs) {}

  void checkUnreferencedOperands(bool CheckAllOperands);
  ReferenceList *getReferences() const { return References; }
  ResourceList &getResources() { return Resources; }
  ReferenceList &getResourceRefs() { return ResourceRefs; }
  SubUnitInstantiation *getSubunit() const { return Subunit; }
  InstructionDef *getInstruct() const { return Instruct; }
  std::string ToString() const;
  void Dump() const { std::cout << ToString() << "\n"; }
};

//-----------------------------------------------------------------------------
// Everything we know about the target's instructions.
// Constructing this object creates the entire database, which is stored in
// the contained map.
//-----------------------------------------------------------------------------
class InstructionDatabase {
  std::string DirectoryName; // output directory name
  std::string FileName;      // original mdl filename
  bool GenMissingInfo;       // reflects command line option of same name
  MdlSpec &Spec;             // machine description specification
  std::map<std::string, InstrInfoList> InstructionInfo;

public:
  InstructionDatabase(std::string DirectoryName, std::string FileName,
                      bool GenMissingInfo, MdlSpec &Spec);

  void generateInstructionInfo(InstructionDef *Instruct);
  ResourceSets buildResourceSets(ResourceList &Resources,
                                 SubUnitInstantiation *Subunit);
  void recordConditionallyUsedFus(
                  ConditionalRef *Cond, SubUnitInstantiation *Subunit);
  void recordUsedFus(
                  ReferenceList &Resource_refs, SubUnitInstantiation *Subunit);

  void findReferencedOperands(const InstructionDef *Instr, ReferenceList *Refs,
                              CpuInstance *Cpu, std::set<int> &Found);
  void findCondReferencedOperands(const InstructionDef *Instr,
                                  ConditionalRef *Cond, CpuInstance *Cpu,
                                  std::set<int> &Found);
  void addUnreferencedOperandDefs(const InstructionDef *Instr,
                                  ReferenceList *Refs, CpuInstance *Cpu);

  // Check all instruction records for operands that don't have explicit
  // references referring to them - these are likely errors.
  void checkUnreferencedOperands(bool check_all_operands);
  // Given a Reference operand, determine if it is valid for this instruction.
  // If the reference operand is null, its always valid.
  // Return true if its valid.
  bool isOperandValid(const InstructionDef *Instr, const OperandRef *Opnd,
                      RefType Type) const;
  // Look for operand references in phase expressions, and make sure the
  // operand exists in the current instruction.
  bool isPhaseExprValid(const InstructionDef *Instr,
                        const PhaseExpr *Expr) const;
  // Return true if this reference is valid for this instruction.
  bool isReferenceValid(const InstructionDef *Instr,
                        const Reference *Ref) const;
  // Top level function for checking a set of reference predicates against
  // a particular instruction definition.
  ReferenceList *filterReferences(const InstructionDef *Instr,
                                  ReferenceList &Candidates, CpuInstance *Cpu);
  // Filter a single conditional reference.  Simplify if the predicate
  // evaluates to true or false.
  ConditionalRef *filterConditionalRef(const InstructionDef *Instr,
                                       ConditionalRef *Cond, CpuInstance *Cpu);

  MdlSpec &getSpec() { return Spec; }
  auto &getInstructionInfo() { return InstructionInfo; }
  bool genMissingInfo() const { return GenMissingInfo; }

  // Write everything out to the C++ output file.
  void write(bool GenerateLLVMDefs);

  // Dump everything we know about all the target instructions.
  void dumpInstructions();

  std::string getFileName() const { return FileName; }
  std::string getDirectoryName() const { return DirectoryName; }
};

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_GENERATE_H_

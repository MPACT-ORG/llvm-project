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
public:
  ResourceEvent(RefType type, PhaseExpr *expr, int use_cycles, ResourceRef *res,
                Reference *ref = nullptr,
                SubUnitInstantiation *subunit = nullptr)
      : ref_type_(type), phase_expr_(expr), use_cycles_(use_cycles),
        resource_(res), reference_(ref), subunit_(subunit) {
    res->definition()->RecordReference(type, expr, res, ref, subunit);
    SetConstantPhase();
  }
  ResourceEvent(RefType type, PhaseExpr *expr, ResourceRef *res,
                Reference *ref = nullptr,
                SubUnitInstantiation *subunit = nullptr)
      : ref_type_(type), phase_expr_(expr), use_cycles_(kOneCycle),
        resource_(res), reference_(ref), subunit_(subunit) {
    res->definition()->RecordReference(type, expr, res, ref, subunit);
    SetConstantPhase();
  }
  ResourceEvent(RefType type, PhaseExpr *expr, ResourceDef *port)
      : ref_type_(type), phase_expr_(expr), resource_(new ResourceRef(port)) {
    port->RecordReference(type, expr, nullptr, nullptr, nullptr);
    SetConstantPhase();
  }
  // Constructor for an fus reference, including micro_ops.
  ResourceEvent(RefType type, Reference *ref, ResourceRef *res)
      : ref_type_(type), phase_expr_(ref->phase_expr()),
        use_cycles_(ref->use_cycles()), resource_(res),
        micro_ops_(ref->micro_ops()), fu_flags_(ref->fu_flags()),
        reference_(ref) {
    res->definition()->RecordReference(
                          type, phase_expr_, res, nullptr, nullptr);
  }

  RefType ref_type() const { return ref_type_; }
  bool IsFuncUnitRef() const { return ref_type_ == RefTypes::kFus; }
  PhaseExpr *phase_expr() const { return phase_expr_; }
  int use_cycles() const { return use_cycles_; }
  int micro_ops() const { return micro_ops_; }

  RefFlags::Item fu_flags() const { return fu_flags_; }
  ResourceRef *resource() const { return resource_; }
  Reference *reference() const { return reference_; }
  SubUnitInstantiation *subunit() const { return subunit_; }
  void SetConstantPhase() { phase_value_ = phase_expr_->ConstantPhase(); }

  // Resource references are sorted by pipeline phase, then by resource id.
  // If the pipeline phase is non-constant, its ordered last. If both are
  // non-constant, use the formatting string to decide (so that the sort
  // is stable).
  bool operator<(const ResourceEvent &rhs) const {
    if (phase_value_ != rhs.phase_value_) {
      if (phase_value_ == -1)
        return false;
      if (rhs.phase_value_ == -1)
        return true;
      return phase_value_ < rhs.phase_value_;
    }
    if (phase_value_ == -1 && rhs.phase_value_ == -1)
      return phase_expr_->ToString() < rhs.phase_expr_->ToString();

    return resource_->get_final_resource_id() <
           rhs.resource()->get_final_resource_id();
  }
  bool operator>(const ResourceEvent &rhs) const { return rhs < *this; }

  std::string ToString() const {
    return formatv("{0}{1}({2},{3})", RefTypeToString(ref_type()),
                   phase_expr_->FormatProtection(), phase_expr_->ToString(),
                   resource_->ToString());
  }

private:
  RefType ref_type_;                // type of reference
  int phase_value_ = -1;            // value of phase if constant expression
  PhaseExpr *phase_expr_ = nullptr; // when reference happens
  int use_cycles_ = 1;              // # cycles resource is used
  ResourceRef *resource_;           // referenced resource
  int micro_ops_ = 0;               // micro_ops (for fus)
  RefFlags::Item fu_flags_;         // various flags for explicit fu refs
  Reference *reference_ = nullptr;  // pointer to original reference
  SubUnitInstantiation *subunit_;   // pointer to subunit instantiation context
};

//-----------------------------------------------------------------------------
// A description of operand and resource references for a single instruction
// and subunit instantiation pair.
//-----------------------------------------------------------------------------
class InstrInfo {
public:
  InstrInfo(InstructionDef *instruct, SubUnitInstantiation *subunit,
            ResourceList &resources, ReferenceList *refs,
            ReferenceList &resource_refs)
      : instruct_(instruct), subunit_(subunit), references_(refs),
        resources_(resources), resource_refs_(resource_refs) {}

  void CheckUnreferencedOperands(bool check_all_operands);
  ReferenceList *references() const { return references_; }
  ResourceList &resources() { return resources_; }
  ReferenceList &resource_refs() { return resource_refs_; }
  SubUnitInstantiation *subunit() const { return subunit_; }
  InstructionDef *instruct() const { return instruct_; }
  std::string ToString() const;
  void dump() const { std::cout << ToString() << "\n"; }

private:
  InstructionDef *instruct_;      // pointer to the instruction description
  SubUnitInstantiation *subunit_; // which subunit instance

  ReferenceList *references_;   // valid references for this instruction
  ResourceList resources_;      // sets of resource references
  ReferenceList resource_refs_; // conditional resources and FUs
};

//-----------------------------------------------------------------------------
// Everything we know about the target's instructions.
// Constructing this object creates the entire database, which is stored in
// the contained map.
//-----------------------------------------------------------------------------
class InstructionDatabase {
public:
  InstructionDatabase(std::string directory_name, std::string file_name,
                      bool gen_missing_info, MdlSpec &spec);
  void GenerateInstructionInfo(InstructionDef *instruct);
  ResourceSets BuildResourceSets(ResourceList &resources,
                                 SubUnitInstantiation *subunit);
  void RecordConditionallyUsedFus(
                  ConditionalRef *cond, SubUnitInstantiation *subunit);
  void RecordUsedFus(
                  ReferenceList &resource_refs, SubUnitInstantiation *subunit);

  void FindReferencedOperands(const InstructionDef *instr, ReferenceList *refs,
                              CpuInstance *cpu, std::set<int> &found);
  void FindCondReferencedOperands(const InstructionDef *instr,
                                  ConditionalRef *cond, CpuInstance *cpu,
                                  std::set<int> &found);
  void AddUnreferencedOperandDefs(const InstructionDef *instr,
                                  ReferenceList *refs, CpuInstance *cpu);

  // Check all instruction records for operands that don't have explicit
  // references referring to them - these are likely errors.
  void CheckUnreferencedOperands(bool check_all_operands) {
    for (auto &[name, info_set] : instruction_info_)
      for (auto *info : info_set)
        info->CheckUnreferencedOperands(check_all_operands);
  }

  // Given a Reference operand, determine if it is valid for this instruction.
  // If the reference operand is null, its always valid.
  // Return true if its valid.
  bool IsOperandValid(const InstructionDef *instr, const OperandRef *opnd,
                      RefType ref_type) const {
    if (opnd == nullptr)
      return true;
    int op_index = spec_.GetOperandIndex(instr, opnd, ref_type);
    if (op_index == -1)
      return false;

    // For holds and reserves, we don't have to check the reference type.
    int iref_type = static_cast<int>(ref_type);
    if ((iref_type & RefTypes::kAnyUseDef) == 0)
      return true;

    // If the reference is any use or def, make sure it matches the type of the
    // operand declaration in the instruction.  Input operands must be "used",
    // and output operands must be "defed".
    // Occasionally td files give input and output operands the same name/type
    // (in different instructions), and latency rules must provide "defs" and
    // "uses" for those operands, but we don't have an obvious way to decide
    // whether a particular def or use matches an operand reference. So we use
    // an operand's I/O designator to differentiate. (These are -always- there
    // for definitions scraped from llvm).  If an operand doesn't have an I/O
    // designator, we can skip this check.
    auto *op = instr->GetOperandDecl(op_index);
    if (op == nullptr)
      return true;
    if (op->is_input() && (iref_type & RefTypes::kAnyUse) == 0)
      return false;
    if (op->is_output() && (iref_type & RefTypes::kAnyDef) == 0)
      return false;
    return true;
  }

  // Look for operand references in phase expressions, and make sure the
  // operand exists in the current instruction.
  // Return true if the expression is valid.
  bool IsPhaseExprValid(const InstructionDef *instr,
                        const PhaseExpr *expr) const {
    if (!expr)
      return true;
    if (expr->operation() == kOpnd)
      return IsOperandValid(instr, expr->operand(), RefTypes::kNull);
    return IsPhaseExprValid(instr, expr->left()) &&
           IsPhaseExprValid(instr, expr->right());
  }

  // Return true if this reference is valid for this instruction.
  // - If it has an operand reference, then check that the instuction
  //   definition has that operand.
  // - If the phase expression contains operand references, check them too.
  bool IsReferenceValid(const InstructionDef *instr,
                        const Reference *reference) const {
    return IsOperandValid(instr, reference->operand(), reference->ref_type()) &&
           IsPhaseExprValid(instr, reference->phase_expr());
  }

  // Top level function for checking a set of reference predicates against
  // a particular instruction definition.
  ReferenceList *FilterReferences(const InstructionDef *instr,
                                  ReferenceList &candidates, CpuInstance *cpu);
  // Filter a single conditional reference.  Simplify if the predicate
  // evaluates to true or false.
  ConditionalRef *FilterConditionalRef(const InstructionDef *instr,
                                       ConditionalRef *cond, CpuInstance *cpu);

  MdlSpec &spec() { return spec_; }
  auto &instruction_info() { return instruction_info_; }
  bool gen_missing_info() const { return gen_missing_info_; }

  // Write everything out to the C++ output file.
  void Write(bool generate_llvm_defs);

  // Dump everything we know about all the target instructions.
  void DumpInstructions();

  std::string file_name() const { return file_name_; }
  std::string directory_name() const { return directory_name_; }

private:
  std::string directory_name_; // output directory name
  std::string file_name_;      // original mdl filename
  bool gen_missing_info_;      // reflects command line option of same name
  MdlSpec &spec_;              // machine description specification
  std::map<std::string, InstrInfoList> instruction_info_;
};

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_GENERATE_H_

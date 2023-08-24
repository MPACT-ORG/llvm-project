//===- mdl_dump.cpp - Dump out internal MDL objects -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MDL method implementations that dump the internal representation of the
// input machine descriptions.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <map>
#include <string>

#include "mdl.h"

namespace mpact {
namespace mdl {

//----------------------------------------------------------------------------
// Definitions of functions that format objects for printing that represent
// the input machine description.
//----------------------------------------------------------------------------
// Stringify a reference to an Identifier.
std::string Identifier::ToString() const { return name(); }

// Stringify a phase name definition.
std::string PhaseName::ToString() const { return name(); }

// Stringify a register definition.
std::string RegisterDef::ToString() const { return name(); }

// Stringify a register class definition.
std::string RegisterClass::ToString() const {
  std::string hdr = formatv("\nRegister class: {0}", name());
  std::string members = StringVec<RegisterDef *>(members_, " { ", ", ", " }");
  return hdr + members;
}

// Stringify a reference to a register class.
std::string RegisterClassRef::ToString() const { return id_->ToString(); }

// Stringify a pipe phase set definition.
std::string PipePhases::ToString() const {
  std::string type;
  if (is_hard_)
    type = "(hard):";
  else if (is_protected_)
    type = "(protected):";
  else
    type = "(unprotected):";
  return formatv("Pipe Phases {0}{1}", id_->ToString(),
                 StringVec<PhaseName *>(phase_names_, type, ", "));
}

// Stringify a resource definition.
std::string ResourceDef::ToString() const {
  std::string out = formatv("Resource {0}", id_->ToString());
  if (get_resource_id() > 0)
    out += formatv("{{{0}}", get_resource_id());
  if (start_phase_)
    out += formatv("({0}", start_phase_->ToString());
  if (end_phase_)
    out += formatv("..{0}", end_phase_->ToString());
  if (start_phase_)
    out += ")";
  if (bit_size_ > 0)
    out += formatv(":{0}", bit_size_);
  if (pool_size_ > 0)
    out += formatv("[{0}]", pool_size_);
  if (!members_.empty()) {
    auto sep = group_type() == GroupType::kUseAll ? " & " : " , ";
    out += StringVec<Identifier *>(&members_, " {", sep, "}");
  }

  if (reg_class_)
    out += formatv(" <{0}>", reg_class()->id()->ToString());
  if (port_resource() != nullptr)
    out += formatv("<{0}>", port_resource()->ToString());
  return out;
}

// Format a string that summarizes the refs we've seen to this resource.
std::string ResourceDef::ref_summary() const {
  if (earliest_ref_ == -1 && latest_ref_ == -1 && !phase_expr_seen_)
    return "";
  return formatv(",\tcycles: [{0}..{1}]{2}{3}", earliest_ref_, latest_ref_,
                 phase_expr_seen_ ? " (expr) " : " ",
                 FormatReferenceTypes(ref_types_));
}

// Stringify a reference to a resource.
std::string ResourceRef::ToString() const {
  std::string out = id_->ToString();

  if (IsGroupRef()) {
    std::string ids;
    auto sep = definition_->group_type() == GroupType::kUseAll ? "&" : ",";
    for (auto *member : definition_->member_defs()) {
      if (!ids.empty())
        ids += sep;
      ids += std::to_string(member->get_resource_id());
    }
    out += "{" + ids + "}";
  }

  if (get_resource_id() > 0)
    out += formatv("{{{0}}", get_resource_id());

  if (member_)
    out += formatv(".{0}{{{1}}", member_->ToString(), member_id());
  if (first_ != -1 && last_ != -1) {
    if (last_ != first_)
      out += formatv("[{0}..{1}]", first_, last_);
    else
      out += formatv("[{0}]", first_);
  }
  if (pool_count_ != -1)
    out += formatv(":{0}", pool_count_);
  if (pool_count_name_)
    out += formatv(":{0}", pool_count_name_->ToString());
  if (value_name_)
    out += formatv(":{0}", value_name_->ToString());
  if (operand_index_ != -1)
    out += formatv("-->{0}", operand_index_);
  return out;
}

static const char *divider =
    "===========================================================\n";

// Stringify a CPU instance.
std::string CpuInstance::ToString() const {
  std::string out =
      formatv("{0}Cpu Definition:{1}\n\n", divider, id_->ToString());

  return out + formatv("{0}{1}{2}{3}{4}", StringVec<PipePhases *>(pipe_phases_),
                       StringVec<ResourceDef *>(issues_, "Issue Slots:", ", "),
                       StringVec<ResourceDef *>(resources_),
                       StringVec<ClusterInstance *>(clusters_),
                       StringVec<ForwardStmt *>(forward_stmts_));
}

// Stringify a cluster instance.
std::string ClusterInstance::ToString() const {
  std::string out = formatv("{0}Cluster: {1}\n\n", divider, id_->ToString());

  return out + formatv("{0}{1}{2}{3}",
                       StringVec<ResourceDef *>(issues_, "Issue Slots:", ", "),
                       StringVec<ResourceDef *>(resources_),
                       StringVec<FuncUnitInstance *>(func_units_),
                       StringVec<ForwardStmt *>(forward_stmts_));
}

// Stringify a single forwarding statement.
std::string ForwardStmt::ToString() const {
  std::string out = formatv("Forward: {0} -> ", from_unit_->name());
  for (auto [unit, cycles] : to_units_)
    out += formatv("{0}({1})", unit->name(), cycles);
  return out + "\n";
}

// Stringify a functional unit instance.
std::string FuncUnitInstance::ToString() const {
  std::string out = formatv("Func Unit: {0}", type_->ToString());
  if (id_)
    out += formatv(" {0}", id_->ToString());
  out += StringVec<ResourceRef *>(args_, "(", ", ", ")");
  if (pin_any_)
    out += StringVec<Identifier *>(pin_any_, " -> ", " | ", "");
  if (pin_all_)
    out += StringVec<Identifier *>(pin_all_, " -> ", " & ", "");
  return out;
}

// Stringify a subunit instance.
std::string SubUnitInstance::ToString() const {
  std::string out = StringVec<Identifier *>(predicates_, "[", ",", "] : ");
  out += formatv("Subunit: {0}", id_->ToString());
  if (args_ == nullptr)
    return out + "()\n";
  return out + StringVec<ResourceRef *>(args_, "(", ", ", ")\n");
}

// Stringify a latency instance.
std::string LatencyInstance::ToString() const {
  std::string out = StringVec<Identifier *>(predicates_, "[", ",", "] : ");
  out += formatv("Latency {0}", id_->ToString());
  if (args_ == nullptr)
    return out + "()\n";
  return out + StringVec<ResourceRef *>(args_, "(", ", ", ")\n");
}

// Stringify a parameter of a functional unit, subunit, or latency template.
std::string Params::ToString() const {
  const char *kinds[] = {"p", "c", "r"};
  return formatv("{0}:{1}", kinds[static_cast<int>(type_)], id_->ToString());
}

// Stringify a functional unit template definition.
std::string FuncUnitTemplate::ToString() const {
  std::string out =
      formatv("{0}Func Unit Template: {1}\n\n", divider, id_->ToString());

  if (bases_ && !bases_->empty())
    out +=
        StringVec<Identifier *>(bases_, "Base Functional Unit: ", ", ", "\n");

  out += StringVec<Params *>(params_, "Template Parameters(", ", ", ")\n\n");
  if (ports_ && !ports_->empty())
    out += StringVec<Identifier *>(ports_, "Ports: ", ", ", "\n");
  out += StringVec<ResourceDef *>(resources_);
  out += StringVec<Connect *>(connections_, "", "", "");
  out += StringVec<SubUnitInstance *>(subunits_, "", "", "");
  return out;
}

// Stringify a connect statement in a functional unit template.
std::string Connect::ToString() const {
  std::string out = formatv("Connect {0}", id_->ToString());
  if (reg_class_)
    out += formatv(" to {0}", reg_class_->ToString());
  if (resource_)
    out += formatv(" via {0}", resource_->ToString());
  return out + "\n";
}

// Stringify a subunit template definition.
std::string SubUnitTemplate::ToString() const {
  std::string out =
      formatv("{0}Sub Unit Template: {1}\n\n", divider, type_->ToString());

  if (bases_ && !bases_->empty())
    out += StringVec<Identifier *>(bases_, "Base Subunits: ", ", ", "\n");

  out += StringVec<Params *>(params_, "Template Parameters(", ", ", ")\n") +
         StringVec<LatencyInstance *>(latencies_);
  out += "\n";
  return out;
}

// Stringify a latency template definition.
std::string LatencyTemplate::ToString() const {
  auto out = formatv(
      "{0}Latency Template: {1}\n\n{2}{3}{4}", divider, id_->ToString(),
      StringVec<Identifier *>(base_ids_, "Bases: ", ", ", "\n"),
      StringVec<Params *>(params_, "Template Parameters(", ", ", ")\n"),
      StringVec<Reference *>(references_, "   ", "\n   "));

  for (auto &[predicate, funits] : referenced_fus_->func_units())
    out += formatv("FUs: {0} -> {1}\n", (predicate == "" ? "<all>" : predicate),
            StringSet<std::string>(&funits, "", ", ", ""));
  return out;
}

// Find an appropriate name for a operand reference type.
std::string OperandRef::type_name() const {
  if (operand_decl_)
    return operand_decl_->type_name();
  if (operand_)
    return operand_->name();
  if (reg_class_)
    return reg_class_->name();
  if (op_type_)
    return op_type_->ToString();
  return "";
}

// Stringify a single operand descriptor in a latency reference object.
std::string OperandRef::ToString() const {
  std::string out = type_name();
  if (!out.empty())
    out += ":";
  out += "$" + StringVec<Identifier *>(op_names_, "", ".", "");
  if (operand_index_ != -1)
    out += formatv("[{0}]", operand_index_);
  return out;
}

// Stringify a latency expression for debug output.
std::string PhaseExpr::ToString() const {
  std::string left = left_ ? left_->ToString() : "";
  std::string right = right_ ? right_->ToString() : "";

  switch (operation_) {
  case kPlus:
    return formatv("({0}+{1})", left, right);
  case kMinus:
    return formatv("({0}-{1})", left, right);
  case kMult:
    return formatv("({0}*{1})", left, right);
  case kDiv:
    return formatv("({0}/{1})", left, right);
  case kNeg:
    return formatv("(-{0})", left);
  case kPositive:
    return formatv("{{{0}}", left);
  case kOpnd:
    return operand_->ToString();
  case kInt:
    return formatv("{0}", number_);
  case kPhase:
    if (phase_name_)
      return phase_name_->ToString();
    if (phase_)
      return phase_->ToString();
  }
  return "Unknown";
}

// Create a string that briefly represents the protection type of a phase.
std::string PhaseName::FormatProtection() const {
  if (is_hard_)
    return ".h";
  if (is_protected_)
    return ".p";
  return ".u";
}

// Stringify a reference argument in a latency template.
std::string Reference::ToString() const {
  std::string out;
  if (IsConditionalRef()) {
    out = conditional_ref()->ToString(false);
  } else {
    out = RefTypeToString(ref_type());

    if (ref_type() == RefTypes::kFus) {
      out += "(";
      ResourceRef *res = nullptr;
      if (!resources_->empty()) {
        res = (*resources_)[0];
        out += res->name();
      }
      if (phase_expr_ != nullptr)
        out += formatv("<{0}:{1}> ", phase_expr_->ToString(), use_cycles());
      else
        out += formatv("<{0}> ", use_cycles());

      if (micro_ops() != 0)
        out += formatv("Mops={0} ", micro_ops());
      if (get_buffer_size() != -1)
        out += formatv("Buffersize={0} ", get_buffer_size());
      if (RefFlags::is_begin_group(fu_flags()))
        out += "BeginGroup ";
      if (RefFlags::is_end_group(fu_flags()))
        out += "EndGroup ";
      if (RefFlags::is_single_issue(fu_flags()))
        out += "SingleIssue ";
      if (RefFlags::is_retire_ooo(fu_flags()))
        out += "RetireOOO ";
      out += ")";
      if (predicates_)
         out += StringVec<Identifier *>(predicates_, "  {", ",", "}");
      return out;
    }
    out += formatv("{0}({1}", phase_expr_->FormatProtection(),
                   phase_expr_->ToString());
    if (use_cycles() != 1)
      out += formatv(":{0}", use_cycles());
    if (repeat() > 1)
      out += formatv("[{0},{1}]", repeat(), delay());
    if (operand_)
      out += formatv(", {0}", operand_->ToString());
    if (ref_type() != RefTypes::kFus && !resources_->empty())
      out += StringVec<ResourceRef *>(resources_, ", <", ", ", ">");
    if (port_ && port_->reg_class())
      out +=
          formatv(", port {0}<{1}>", port_->name(), port_->reg_class()->name());
    out += ")";
  }
  if (predicates_)
    out += StringVec<Identifier *>(predicates_, "  {", ",", "}");
  return out;
}

// Stringify an if/then/else reference.
std::string ConditionalRef::ToString(bool is_else) {
  std::string pred = predicate_ ? " if " + predicate_->name() : " ";
  std::string out = (is_else ? " else " : "") + pred + "\n";
  for (auto *ref : refs())
    out += "           " + ref->ToString() + "\n";
  if (else_clause())
    out += else_clause()->ToString(true);
  if (out.back() == '\n')
    out.pop_back();
  return out + "\n";
}

// Stringify a single operand declaration for an instruction or operand.
std::string OperandDecl::ToString() const {
  if (print_fully_qualified_declaration_ && types_->size() > 1)
    return StringVec<Identifier *>(types_, "(", ".", ") ") +
           StringVec<Identifier *>(names_, "(", ".", ")");
  return formatv("{0} {1}", types_->back()->name(), names_->back()->name());
}

// Stringify a single operand definition.
std::string OperandDef::ToString() const {
  std::string out = formatv("Operand: {0}", name());
  std::string type = type_ ? formatv("type({0});", type_->ToString()) : "";

  // For LLVM operands, write out a short form of the operand.
  if (operands_ && type_ && !bases_)
    return out + StringVec<OperandDecl *>(operands_, "(", ", ", ")") +
           formatv(" {{ {0} }\n", type);
  if (bases_)
    out += StringVec<Identifier *>(bases_, ": ", ": ", "");
  out += " {\n";
  if (type_)
    out += formatv("    {0}\n", type);
  return out + StringVec<OperandAttribute *>(attributes_) + "}\n";
}

// Stringify an operand attribute.
std::string OperandAttribute::ToString() const {
  std::string out = formatv("    attribute {0} = ", name_->ToString());
  if (values()->size() == 1) {
    out += formatv("{0}", values(0));
  } else {
    out += "[";
    for (auto value : *values())
      out += formatv("{0},", value);
    out += "]";
  }
  if (!predicate_values_->empty())
    out += "\n      ";
  if (!type().empty())
    out += formatv(" if {0}", type());
  if (!predicate_values_->empty())
    out += StringVec<PredValue *>(predicate_values_, " [", ", ", "]");
  if (predicate_)
    out += StringVec<Identifier *>(predicate_, "  {", ",", "}");
  return out;
}

// Format an operand predicate value. Mostly we want to avoid printing
// out long decimal numbers.
std::string PredValue::FormatValue(int64_t value) const {
  constexpr int kMinValue = 0;
  constexpr int kMaxValue = 9999;
  if (value >= kMinValue && value <= kMaxValue)
    return std::to_string(value);
  else
    return formatv("{0:X8}UL", static_cast<uint64_t>(value));
}

// Stringify an operand attribute predicate value.
std::string PredValue::ToString() const {
  if (IsValue())
    return FormatValue(value());
  if (IsRange())
    return formatv("{0}..{1}", FormatValue(low()), FormatValue(high()));
  if (IsMask())
    return formatv("{{ {0:X8}UL }", mask());
  return "empty";
}

// Stringify a single instruction definition.
std::string InstructionDef::ToString() const {
  return formatv(
      "Instruction: {0}{1}{2}{3}{4}", name(),
      StringVec<OperandDecl *>(operands_, "(", ", ", ")\n"),
      StringVec<OperandDecl *>(flat_operands_, "\t\tflat(", ", ", ")\n"),
      StringVec<Identifier *>(subunits_, "\t\t{ subunit(", ",", "); }\n"),
      StringVec<Identifier *>(derived_, "\t\t{ derived(", ",", "); }\n"));
}

// Stringify all the instruction definitions.
// We organize the list by subunit, so that instructions sharing a subunit are
// dumped next to each other.  The purpose of this is to help the user write
// and debug the machine description for similar instructions.
std::string DumpInstructionDefs(const InstructionList &instructions) {
  // build a map of instruction lists indexed by the first subunit name.
  std::string out;
  std::map<std::string, InstructionList> instruction_map;
  for (auto *instruct : instructions)
    if (!instruct->subunits()->empty())
      instruction_map[(*instruct->subunits())[0]->name()].push_back(instruct);

  for (auto &entries : instruction_map) {
    auto [subunit_name, instructions] = entries;
    for (auto *instruction : instructions)
      out += instruction->ToString();
  }

  return out;
}

// Stringify the entire machine description.
std::string MdlSpec::ToString() const {
  return formatv("{0}Machine Description\n\n", divider) +
         StringVec<PipePhases *>(&pipe_phases_) +
         StringVec<ResourceDef *>(&resources_) +
         StringVec<RegisterDef *>(&registers_, "", ", ", "\n") +
         StringVec<RegisterClass *>(&reg_classes_) +
         StringVec<CpuInstance *>(&cpus_) +
         StringVec<FuncUnitTemplate *>(&func_units_) +
         StringVec<SubUnitTemplate *>(&subunits_) +
         StringVec<LatencyTemplate *>(&latencies_) +
         StringVec<OperandDef *>(&operands_) +
         DumpInstructionDefs(instructions_);
}

// Print details of a single functional unit instantiation.
void FuncUnitInstantiation::DumpFuncUnitInstantiation() {
  auto out = formatv("{0}: {{{1}} {2} {3}(", cpu()->name(), cluster()->name(),
                     func_type()->name(), instance()->name());
  if (ResourceRefList *args = instance()->args()) {
    int params = std::min(func_type()->params()->size(), args->size());
    for (int argid = 0; argid < params; argid++) {
      if ((*func_type()->params())[argid]->IsResource())
        out += GetResourceArg(argid)->ToString();
      else
        out += GetClassArg(argid)->ToString();
      if (argid < params - 1)
        out += ", ";
    }
  }
  std::cout << out << ")\n";
}

void ClusterInstance::DumpFuncUnitInstantiations() {
  for (auto *fu : fu_instantiations_) {
    std::cout << "\nFunc_unit: " << fu->func_type()->name()
              << "---------------------------------------\n";
    fu->DumpFuncUnitInstantiation();
  }
}

// Print details of all functional unit instantiations.
void MdlSpec::DumpFuncUnitInstantiations() {
  for (const auto *cpu : cpus())
    for (auto *cluster : *cpu->clusters())
      cluster->DumpFuncUnitInstantiations();
}

// Print details of a single subunit instantiation.
void SubUnitInstantiation::DumpSubUnitInstantiation() {
  auto out =
      formatv("{0}: {{{1}} {2} {3} <{4}>(", func_unit()->cpu()->name(),
              func_unit()->cluster()->name(), func_unit()->func_type()->name(),
              func_unit()->instance()->name(), subunit()->name());
  if (ResourceRefList *args = subunit()->args()) {
    int params = std::min(su_template()->params()->size(), args->size());
    for (int argid = 0; argid < params; argid++) {
      if ((*su_template()->params())[argid]->IsResource())
        out += GetResourceArg(argid)->ToString();
      else
        out += GetPortArg(argid)->ToString();
      if (argid < params - 1)
        out += ", ";
    }
  }
  out += ")\n";
  for (auto *ref : references())
    out += formatv("    {0}\n", ref->ToString());
  std::cout << out;
}

// Print details of all subunit instantiations.
void MdlSpec::DumpSubUnitInstantiations() {
  // Dump out all instantiations for each subunit.
  for (const auto &subunit : su_instantiations()) {
    auto [name, unit] = subunit;
    std::cout << formatv(
        "\nSubunit: {0} ---------------------------------------\n", name);
    for (auto *su : *unit)
      su->DumpSubUnitInstantiation();
  }
}

// Print details of a single latency instantiation.
void LatencyInstantiation::DumpLatencyInstantiation() {
  auto out = formatv("{0}: {{{1}} {2} {3} <{4}>[{5}](",
                     subunit()->func_unit()->cpu()->name(),
                     subunit()->func_unit()->cluster()->name(),
                     subunit()->func_unit()->func_type()->name(),
                     subunit()->func_unit()->instance()->name(),
                     subunit()->subunit()->name(), latency()->name());
  if (ResourceRefList *args = latency()->args()) {
    int params = std::min(lat_template()->params()->size(), args->size());
    for (int argid = 0; argid < params; argid++) {
      if ((*lat_template()->params())[argid]->IsResource())
        out += GetResourceArg(argid)->ToString();
      else
        out += GetPortArg(argid)->ToString();
      if (argid < params - 1)
        out += ", ";
    }
  }
  std::cout << out << ")\n";
}

void MdlSpec::DumpPredicates() {
  for (const auto &[name, expr] : predicate_table_)
    std::cout << formatv("Predicate {0} : {1}\n\n", name, expr->ToString(0));
}

// Format a string that represents the ids associated with a resource.
std::string ResourceDef::resource_format() {
  int id = get_resource_id();
  std::string out = formatv("{0} : ", debug_name());
  if (!IsGroupDef() && pool_size() <= 1)
    out += std::to_string(id);
  if (IsGroupDef()) {
    out += formatv("(gid={0}) ", pool_id());
    out += "[";
    for (auto *mem : member_defs())
      out += std::to_string(mem->get_resource_id()) + ",";
    out.pop_back();
    out += "]";
  }
  if (pool_size() > 1) {
    out += formatv("(pid={0}) ", pool_id());
    out += formatv("[{0}..{1}]", id, pool_size() + id - 1);
  }

  return out;
}

std::string SubPool::ToString() const {
  if (first() == -1 && last() == -1)
    return "[group]";
  return formatv("subrange: [{0}..{1}]", first(), last());
}

// Write out all allocation pools associate with a subpool.
std::string SubPoolInfo::ToString(std::string subpool) const {
  std::string out;
  int pool_id = subpool_id();
  for (auto rit = counts().rbegin(); rit != counts().rend(); rit++)
    out += formatv("    Subpool:{0} size:{1} {2}\n", pool_id++, *rit, subpool);
  return out;
}

// Dump resource ids for each resource.
void MdlSpec::DumpResourceIds() {
  std::string out;
  for (auto *cpu : cpus()) {
    out += formatv("\nResources defined for '{0}' "
                   "---------------------------------------\n", cpu->name());
    for (auto res : cpu->all_resources())
      out += formatv("{0}{1}\n", res->resource_format(), res->ref_summary());
    out += formatv("\nPooled resources defined for '{0}' "
                   "--------------------------------\n", cpu->name());
    for (auto *res : cpu->pool_resources())
      if (!res->alloc_sizes().empty()) {
        out += formatv("{0}{1}\n", res->resource_format(), res->ref_summary());
        for (auto &[subpool, info] : res->sub_pools())
          out += info.ToString(subpool.ToString());
      }
  }
  std::cout << out; // Write out the string!
}

} // namespace mdl
} // namespace mpact

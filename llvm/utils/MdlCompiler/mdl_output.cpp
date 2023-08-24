//===- mdl_output.cpp - Write out the MDL database ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Produce the instruction information database for LLVM as a C++ file.
//
// The overall schema for the database looks like this:
//
//     <A table of CPU definitions>
//        <for each CPU, a table of instructions>
//           <for each instruction, a set of subunit entries>
//              <for each subunit, sets of...
//                 <operand latency records>
//                 <resource reference records>
//                 <pooled resource references>
//                 <register constraints>
//     > > > >
//
// A key aspect of the design is that there is -enormous- duplication of
// information across CPUs, Functional Units, and Instructions, and we
// want to share this information across the database as much as possible.
// The formatting functions automatically share output objects.
//
// The organization of the generated C++ output looks like this:
//
// <generated functions that implement non-trivial instruction predicates>
// <generated functions that implement non-trivial pipeline phase expressions>
// <a table of operand reference lists (operand latencies)>
// <a table of conditional operand reference lists>
// <tables of resource reference lists>
// <a pooled resource allocation table>
// <a pooled resource reference table>
// <operand constraint tables>
// <instruction tables for each CPU definition>
// <an instruction name table (not really necessary...)>
// <a table of CPU definitions>
//
// For each table of shared objects, we create a dictionary of the output
// representation of each entry in the table. Once an entry is in the table
// it is referred to by its unique identifier in the table.
//
// The majority of the code in this file simply handles the formatting of
// the object to generate C++ code.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Error.h"

#include "mdl.h"
#include "mdl_output.h"

namespace mpact {
namespace mdl {

// We generate a LOT of nullptrs in the output, and would prefer something
// that adds less clutter.  So we use __ rather than "nullptr".
constexpr auto kNull = "__";

static const char *divider =
    "\n//-------------------------------------------------------------------\n";

// Functions for creating shared object names.
static std::string TableName(int index, std::string prefix) {
  if (index == -1)
    return kNull;
  return prefix + std::to_string(index);
}
static std::string PredicateName(int index) {
  return TableName(index, "PRED_");
}
static std::string VirtualPredicateName(int index) {
  return TableName(index, "MI_PRED_");
}
static std::string OperandListName(int index) {
  return TableName(index, "OPND_");
}
static std::string CondReferenceName(int index) {
  return TableName(index, "COND_");
}
static std::string CondResourceReferenceName(int index) {
  return TableName(index, "CRES_");
}
static std::string CondPooledResourceReferenceName(int index) {
  return TableName(index, "CPOOL_");
}
static std::string CondConstraintName(int index) {
  return TableName(index, "CREG_");
}
static std::string UsedResourceListName(int index) {
  return TableName(index, "URES_");
}
static std::string HeldResourceListName(int index) {
  return TableName(index, "HRES_");
}
static std::string ReservedResourceListName(int index) {
  return TableName(index, "RRES_");
}
static std::string ResourceGroupName(int index) {
  return TableName(index, "GROUP_");
}
static std::string PoolDescriptorName(int index) {
  return TableName(index, "POOL_");
}
static std::string PooledResourceListName(int index) {
  return TableName(index, "PRES_");
}
static std::string PooledCountFuncName(int index) {
  return TableName(index, "COUNT_");
}
static std::string PoolValueFuncName(int index) {
  return TableName(index, "VALUE_");
}
static std::string ConstraintListName(int index) {
  return TableName(index, "REG_");
}
static std::string PhaseName(int index) { return TableName(index, "PIPE_"); }
static std::string SubunitListName(int index) {
  return TableName(index, "SU_");
}
static std::string SubunitsName(const std::string &cpu,
                                const std::string &name) {
  return formatv("SU__{0}__{1}", cpu, name);
}
static std::string ForwardSetName(int index) {
  return TableName(index, "FWD_");
}

// For non-trivial phase expressions, create a C++ expression to evaluate
// the arithmetic and fetch operands from the instruction if needed.
std::string OutputState::FormatPhaseExpr(const PhaseExpr *expr) const {
  std::string left = expr->left() ? FormatPhaseExpr(expr->left()) : "";
  std::string right = expr->right() ? FormatPhaseExpr(expr->right()) : "";

  switch (expr->operation()) {
  case kPlus:
    return formatv("({0} + {1})", left, right);
  case kMinus:
    return formatv("({0} - {1})", left, right);
  case kMult:
    return formatv("({0} * {1})", left, right);
  case kDiv:
    if (expr->right()->IsExpressionConstant())
      return formatv("({0} / {1})", left, right);
    else
      return formatv("({0} / ({1} ?: 1))", left, right);
  case kNeg:
    return formatv("-({0})", left);
  case kInt:
    return std::to_string(expr->number());
  case kPhase:
    return std::to_string(expr->phase_id());
  case kOpnd:
    return formatv("static_cast<int32_t>(ins->GetOperand({0}))",
                   expr->operand()->operand_index());
  case kPositive:
    return formatv("std::max(0, {0})", left);
  }
  return "Error";
}

// Format a pipeline phase reference. A phase can either evaluate to an
// integer, or the address of a function that calculates the phase.
// If a function is required, generate the body of that function (it's just a
// return statement), and enter it into a table, and return the function name.
std::string OutputState::FormatPhase(const PhaseExpr *expr) {
  if (expr->IsExpressionConstant()) {
    auto result = expr->EvaluateConstantExpression();
    if (result)
      return std::to_string(*result) + "," + kNull;
    spec().ErrorLog(expr, "Invalid phase: divide by zero");
    return "0";
  }

  std::string out = formatv("  return {0};", FormatPhaseExpr(expr));
  auto index = AddEntry(phases_, out);
  return formatv("-1,&{0}", PhaseName(index));
}

// Format reference flags field.
std::string FormatReferenceFlags(const Reference *ref) {
  std::string out;
  if (ref == nullptr || ref->phase_expr()->IsDefaultLatency())
    return "0";
  if (ref->IsProtected())
    out = std::to_string(RefFlags::kProtected);

  if (ref->IsDuplicate())
    out += formatv(out.empty() ? "{0}" : "|{0}",
                   std::to_string(RefFlags::kDuplicate));

  if (out.empty())
    return "0";
  return out;
}

// Format resource reference flags field.
std::string FormatResourceReferenceFlags(const ResourceEvent &ref) {
  std::string out;
  // Handle operand and resource references.
  if (ref.ref_type() != RefTypes::kFus) {
    if (ref.phase_expr()->IsProtected())
      out = std::to_string(RefFlags::kProtected);
    if (ref.reference() && ref.reference()->IsDuplicate())
      out += formatv(out.empty() ? "{0}" : "|{0}", RefFlags::kDuplicate);
    return out;
  }

  // Handle explicit functional unit reference flags.
  if (ref.reference()->is_unbuffered())
    out += formatv("{0}|", RefFlags::kUnbuffered);
  if (ref.reference()->is_out_of_order())
    out += formatv("{0}|", RefFlags::kOutOfOrder);
  if (ref.reference()->is_in_order())
    out += formatv("{0}|", RefFlags::kInOrder);

  if (RefFlags::is_begin_group(ref.reference()->fu_flags()))
    out += formatv("{0}|", RefFlags::kBeginGroup);
  if (RefFlags::is_end_group(ref.reference()->fu_flags()))
    out += formatv("{0}|", RefFlags::kEndGroup);
  if (RefFlags::is_single_issue(ref.reference()->fu_flags()))
    out += formatv("{0}|", RefFlags::kSingleIssue);
  if (RefFlags::is_retire_ooo(ref.reference()->fu_flags()))
    out += formatv("{0}|", RefFlags::kRetireOOO);

  if (!out.empty()) out.pop_back();   // remove last "|"
  return out.empty() ? "0" : out;
}

// Format a predicate function, add it to the table, and return its index.
int OutputState::FormatPredicate(PredExpr *pred) {
  if (pred == nullptr)
    return -1;
  auto func = formatv("return {0};", FormatPredicateFunc(pred));

  // If we're generating a standalone database, check to see if the function
  // includes LLVM definitions for the target. This is conservative, but safe.
  if (generate_llvm_defs_) {
    if (func.find(database_->spec().family_name()) != std::string::npos ||
        func.find("evaluatePredicate") != std::string::npos)
      func = "return true;";
  }

  return AddEntry(reference_predicates_, func);
}

// This function is used to generate a phase function for an if/then/else
// tree where its been determined (in IsSingleOperand) that all clauses of
// the conditional reference access the same operand with various latencies.
// We handle 2 special cases:
//   - All the latencies are exactly the same integer value, so just return it.
//   - There was only one phase function, so just return that function.
// Its possible to see duplicate predicates in different clauses. and there's
// no reason to write out a predicate more than once.
std::string OutputState::FormatSingleConditionalOperand(ConditionalRef *cond) {
  std::string out;
  std::set<int> constant_exprs;
  std::set<int> nontrivial_exprs;
  std::set<int> seen_predicates;

  for (; cond; cond = cond->else_clause()) {
    auto *pred = cond->instr_predicate();
    if (pred != nullptr && !pred->IsTrue()) {
      auto index = FormatPredicate(pred);
      if (seen_predicates.count(index))
        continue; // Skip duplicate predicates
      seen_predicates.insert(index);
      out += formatv("  if ({0}(ins))", PredicateName(index));
    }

    auto *ref = cond->FindSingleRef();
    PhaseExpr *expr = ref->phase_expr();
    if (expr->IsExpressionConstant()) {
      auto value = expr->EvaluateConstantExpression();
      if (value) {
        out += formatv("  return {0};\n", *value);
        constant_exprs.insert(*value);
      }
    } else {
      std::string func = formatv("  return {0};", FormatPhaseExpr(expr));
      int index = AddEntry(phases_, func);
      out += PhaseName(index) + ";\n";
      nontrivial_exprs.insert(index);
      forward_phases_.insert(index);
    }

    if (pred == nullptr || pred->IsTrue())
      break;
  }

  // If we only saw one unique value generated, just return that value.
  if (constant_exprs.size() + nontrivial_exprs.size() == 1) {
    if (!constant_exprs.empty())
      return std::to_string(*constant_exprs.begin()) + "," + kNull;
    return formatv("-1,&{0}", PhaseName(*nontrivial_exprs.begin()));
  }

  // Otherwise insert the new function into the table and return it.
  auto index = AddEntry(phases_, out);
  return formatv("-1,&{0}", PhaseName(index));
}

// Generate a predicated operand reference. This is the top level function for
// handling if/then/else references. This returns a constructor for an
// operand reference, which is either a "normal" constructor if the predicates
// can be folded into phase functions, or a conditional operand reference for
// non-trivial if/then/else references.
std::string OutputState::FormatConditionalOperandRef(ConditionalRef *cond) {
  // If this set of conditionals only references a single operand, we can
  // fold all the predicates into a phase function.
  if (cond->IsSingleOperand()) {
    auto *ref = cond->FindSingleRef();
    int index = ref->operand() ? ref->operand()->operand_index() : -1;
    return formatv("{{{0},{1},{2},{3}}", FormatReferenceType(ref->ref_type()),
                   FormatReferenceFlags(ref),
                   FormatSingleConditionalOperand(cond), index);
  }

  // If it is a set of if/then/else clauses that we can't simplify, generate
  // a conditional operand reference record.
  return formatv("{{{0}}", FormatIfElseOperandRef(cond));
}

// Else clauses are handled by creating a single OperandRef which is initialized
// with an optional predicate, a pointer to an operand reference list, and an
// optional else clause.  Empty else clauses simply return "nullptr".
std::string OutputState::FormatIfElseOperandRef(ConditionalRef *cond) {
  std::string out;
  if (cond == nullptr)
    return kNull;

  // Add the operand list id to the list of OperandRef forward references.
  auto opnds = FormatOperandReferenceList(&cond->refs());
  if (opnds != kNull)
    forward_opnd_refs_.insert(opnds.substr(1, opnds.size() - 1));

  // If the predicate is null and the operand list is empty, just return null.
  if (opnds == kNull && cond->instr_predicate() == nullptr)
    return kNull;

  // If the predicate is null it's an unconditional set of references.
  if (cond->instr_predicate() == nullptr) {
    out = formatv("{1},{0},{1}", opnds, kNull);
  } else {
    auto index = FormatPredicate(cond->instr_predicate());
    auto else_refs = FormatIfElseOperandRef(cond->else_clause());
    out = formatv("&{0},{1},{2}", PredicateName(index), opnds, else_refs);
    // Add else clauses to ConditionalRefs forward references.
    if (else_refs != kNull)
      forward_cond_opnd_refs_.insert(else_refs.substr(1, else_refs.size() - 1));
  }

  auto index = AddEntry(cond_operand_refs_, out);
  return formatv("&{0}", CondReferenceName(index));
}

// Format an operand reference. We generate an autoinitialization for the type:
//     struct OperandRef {
//       ReferenceType type;        // type of the reference (use, def, ...)
//       ReferenceFlags flags;      // reference flags
//       PipePhase phase;           // pipeline phase of the reference
//       PipeFunc phase_func;       // optional pointer to phase function
//       OperandId operand_index;   // operand index
//     };
std::string OutputState::FormatOperandReference(const Reference *ref) {
  int index = ref->operand() ? ref->operand()->operand_index() : -1;
  return formatv("{{{0},{1},{2},{3}}", FormatReferenceType(ref->ref_type()),
                 FormatReferenceFlags(ref), FormatPhase(ref->phase_expr()),
                 index);
}

// Format an operand reference list. Create a vector of operand references,
// and enter into a table so it can be shared between subunits.
std::string OutputState::FormatOperandReferenceList(const ReferenceList *refs) {
  std::string out;
  std::string previous;
  for (const auto *ref : *refs) {
    if (ref->IsConditionalRef()) {
      if (ref->conditional_ref()->HasOperandRefs()) {
        out += FormatConditionalOperandRef(ref->conditional_ref()) + ",";
      }
    } else if (ref->operand() && ref->IsOperandRefType()) {
      auto current = FormatOperandReference(ref);
      if (current != previous) {
        out += current + ",";
        previous = current;
      }
    }
  }
  if (out.empty())
    return kNull;
  out.pop_back(); // throw away trailing comma.

  auto index = AddEntry(operand_refs_, out);
  return formatv("&{0}", OperandListName(index));
}

// Format a single resource reference. We generate an autoinitialization of
// the type:
//    struct ResourceRef {
//      ReferenceType type;         // type of the reference (use, def, ...)
//      ReferenceFlags flags;       // reference flags
//      PipePhase phase;            // pipeline phase of the reference
//      PipeFunc phase_func;        // optional pointer to phase function
//      unsigned int use_cycles;    // # cycles a resource is used
//      ResourceId resource id;     // the resource we're referencing
//      OperandId operand_index;    // operand index for shared resources
//      PoolBits width;             // number of bits in shared value (or -1)
//    };

std::string OutputState::FormatResourceReference(const ResourceEvent &ref) {
  auto *res = ref.resource();
  auto type = FormatReferenceType(ref.ref_type());
  auto flags = FormatResourceReferenceFlags(ref);
  auto phase = FormatPhase(ref.phase_expr());
  int opnd = res->operand_index();
  int cycles = ref.use_cycles();
  int size = res->definition()->bit_size();
  std::string out;

  // If this reference is a duplicate and it doesn't have a valid operand id,
  // there's no reason to write it out.
  if (ref.reference() && ref.reference()->IsDuplicate() &&
      !res->has_operand_index())
    return "";

  // If this was a functional unit reference, write out a FU constructor.
  if (ref.IsFuncUnitRef())
    return formatv("{{{0},{1},{2},{3},{4}}", type, flags, cycles,
                   res->get_final_resource_id(), ref.micro_ops());

  // If this was a reference to an entire group, write out a reference for
  // each group member.
  if (res->IsGroupRef()) {
    for (auto *member : res->definition()->member_defs()) {
      out += formatv("{{{0},{1},{2},{3},{4},{5},{6}},", type, flags, phase,
                     cycles, member->get_resource_id(), opnd, size);
    }
    out.pop_back(); // throw away trailing comma.
    return out;
  }

  // If this is a reference to a single resource, write out a single reference.
  if (!res->IsGroupRef() || res->first() == res->last())
    return formatv("{{{0},{1},{2},{3},{4},{5},{6}}", type, flags, phase, cycles,
                   res->get_final_resource_id(), opnd, size);

  // If the reference was for a range of pool entries, write out a reference
  // for each resource in the range. (This is rare.)
  if (res->IsArrayDef()) {
    for (int id = res->first(); id <= res->last(); id++) {
      out += formatv("{{{0},{1},{2},{3},{4},{5},{6}},", type, flags, phase,
                     cycles, res->get_resource_id() + id, opnd, size);
    }
  }
  out.pop_back(); // throw away trailing comma.
  return out;
}

// Given a list of possibly predicated references, generate the objects for
// each reference.  This can handle pooled or not-pooled reference lists.
std::string OutputState::FormatResourceReferenceList(
    SubUnitInstantiation *subunit, ReferenceList &refs, RefType type,
    OutputSet &output_list, FormatName name, bool format_pooled_refs) {
  std::string out;
  auto *cpu = subunit->cpu();

  for (auto *ref : refs) {
    if (ref->IsFuncUnitRef()) {
      if (type == RefTypes::kUse) // if we're filtering for kUses.
        out += FormatFuncUnitReference(subunit, ref, format_pooled_refs);
      continue;
    }

    if (ref->IsConditionalRef()) {
      auto res = FormatIfElseResourceRef(subunit, ref->conditional_ref(), type,
                                         output_list, name, format_pooled_refs);
      if (res != kNull)
        out += "{&" + res + "},";
      continue;
    }

    // Handle normal case of a reference that may contain resource refs.
    auto ref_type = ref->AdjustResourceReferenceType();
    if (ref_type != type)
      continue;
    for (auto *res : *ref->resources()) {
      if (!res->IsNull() && (res->HasCount() == format_pooled_refs)) {
        PhaseExpr *phase = ref->phase_expr();
        if (auto *start = res->definition()->start_phase())
          phase = new PhaseExpr(subunit->spec()->FindPipeReference(start, cpu));
        if (ref->operand())
          res->set_operand_index(ref->operand()->operand_index());
        auto event = ResourceEvent(ref_type, phase, ref->use_cycles(), res, ref,
                                   subunit);
        if (!res->HasCount())
          out += FormatResourceReference(event) + ",";
        else
          out += FormatPooledResourceReference(event) + ",";
      }
    }
  }

  if (out.empty())
    return kNull;
  out.pop_back(); // throw away trailing comma.

  // Enter it into a table, and return the name of the table entry.
  return name(AddEntry(output_list, out));
}

// Format an explicit functional unit reference. The resources in Fus
// records refer to the name of either a functional unit template or a
// functional unit group.  If its a functional unit template, we find each
// occurance of that template in the current CPU - if there is more than
// one we generate a pooled reference, otherwise we generate a single
// reference of the instantiated FU.  If its a functional unit group, we
// find all occurances of all members of the group, and generate references
// for each individual instantiation.
std::string OutputState::FormatFuncUnitReference(SubUnitInstantiation *subunit,
                                                 Reference *ref,
                                                 bool format_pooled_refs) {
  std::string out;
  auto *cpu = subunit->cpu();

  // If the resource list is empty, this is a plain micro-ops statement.
  if (ref->resources()->empty() && ref->micro_ops() > 0 && !format_pooled_refs)
    return formatv("{{RefFus,{0},{1}},", ref->fu_flags(), ref->micro_ops());

  // For each named FU, create resource events and write them out. If an
  // instance is a sub-functional-unit, return its root parent resource.
  for (auto *res : *ref->resources()) {
    // Generate an entry for a functional unit template reference.
    if (spec().IsFuncUnitTemplate(res->name()) &&
        !format_pooled_refs && !res->is_fu_pool()) {
      auto &funits = cpu->func_unit_instances()[res->name()];
      if (funits.size() == 0) continue;

      auto *fu = new ResourceRef(funits[0]->get_root_resource());
      ResourceEvent fu_res(RefTypes::kFus, ref, fu);
      out += FormatResourceReference(fu_res) + ",";
      continue;
    }

    if (format_pooled_refs && res->fu_pool()) {
      ResourceRef pool(res->fu_pool());
      pool.set_pool_count(1);
      ResourceEvent fu_res(RefTypes::kFus, ref, &pool);
      res->fu_pool()->AddReferenceSizeToPool(&pool, ref, subunit);
      out += FormatPooledResourceReference(fu_res) + ",";
    }
  }

  return out;
}

// Conditionally format a predicated set of resource references.  The input
// reference list may or may not contain resource references, so we need to
// handle the (common) case that none are found.
std::string OutputState::FormatIfElseResourceRef(
    SubUnitInstantiation *subunit, ConditionalRef *cond, RefType type,
    OutputSet &output_list, FormatName name, bool format_pooled_refs) {
  if (cond == nullptr)
    return kNull;

  // Find resource references and generate entries for each. If any are found,
  // add the resource list id to the list of ResourceRef forward references.
  auto then_refs = FormatResourceReferenceList(
      subunit, cond->refs(), type, output_list, name, format_pooled_refs);
  if (then_refs != kNull) {
    if (format_pooled_refs)
      forward_pooled_refs_.insert(then_refs);
    else
      forward_resource_refs_.insert(then_refs);
  }

  // If no resource references were found, and the predicate is null, abort.
  if (then_refs == kNull && cond->instr_predicate() == nullptr)
    return kNull;

  // If the predicate is null it's an unconditional set of references.
  std::string out;
  if (cond->instr_predicate() == nullptr) {
    out = formatv("{1},&{0},{1}", then_refs, kNull);
  } else {
    auto else_refs =
        FormatIfElseResourceRef(subunit, cond->else_clause(), type, output_list,
                                name, format_pooled_refs);
    // Add else clauses to ConditionalRefs forward references.
    if (else_refs != kNull) {
      if (format_pooled_refs)
        forward_cond_pool_refs_.insert(else_refs);
      else
        forward_cond_res_refs_.insert(else_refs);
    }

    if (else_refs == kNull && then_refs == kNull)
      return kNull;
    if (else_refs != kNull)
      else_refs = "&" + else_refs;
    if (then_refs != kNull)
      then_refs = "&" + then_refs;

    auto index = FormatPredicate(cond->instr_predicate());
    out = formatv("&{0},{1},{2}", PredicateName(index), then_refs, else_refs);
  }

  if (format_pooled_refs) {
    auto index = AddEntry(cond_pooled_resource_refs_, out);
    return CondPooledResourceReferenceName(index);
  } else {
    auto index = AddEntry(cond_resource_refs_, out);
    return CondResourceReferenceName(index);
  }
}

// Format a resource reference list. Create a vector of resource references,
// and enter into a table so it can be shared between subunits.
std::string OutputState::FormatResourceReferences(InstrInfo *info, RefType type,
                                                  OutputSet &output_list,
                                                  FormatName name) {
  // First write out entries for all the unconditional resource references.
  std::string out, previous;
  auto *subunit = info->subunit();
  for (auto &ref : info->resources())
    if (!ref.resource()->HasCount() && ref.ref_type() == type) {
      auto resource = FormatResourceReference(ref);
      if (!resource.empty() && resource != previous) {
        out += resource + ",";
        previous = resource;
      }
    }

  // Format conditional resource references and FU references.
  for (auto *ref : info->resource_refs()) {
    if (ref->IsConditionalRef()) {
      auto res = FormatIfElseResourceRef(subunit, ref->conditional_ref(), type,
                                         output_list, name, false);
      if (res != kNull)
        out += "{&" + res + "},";
    } else if (ref->IsFuncUnitRef() && type == RefTypes::kUse) {
      out += FormatFuncUnitReference(subunit, ref, false);
    }
  }

  if (out.empty())
    return kNull;
  out.pop_back(); // throw away trailing comma.

  // Enter it into a table, and return a reference to the table entry.
  auto index = AddEntry(output_list, out);
  return formatv("&{0}", name(index));
}

// Search for a named attribute in an operand definition. If it has an
// attribute predicate, make sure it's valid for this operand.
// Note: Don't check bases here - that is done at a higher level.
OperandAttribute *FindAttribute(const std::string &name, const OperandDef *opnd,
                                const SubUnitInstantiation *subunit) {
  if (opnd == nullptr || opnd->attributes() == nullptr)
    return nullptr;
  for (auto *attr : *opnd->attributes())
    if (name == attr->name())
      if (subunit->ValidPredicate(attr->predicate()))
        return attr;
  return nullptr;
}

// Find the derivation between an operand and a base operand.
// Since we checked derivations earlier, this should always succeed.
bool FindDerivation(OperandDef *ref, const OperandDef *decl,
                    OperandDefList &opnds) {
  opnds.push_back(ref);
  if (ref == decl)
    return true;
  if (ref->base_operands())
    for (auto *base : *ref->base_operands())
      if (FindDerivation(base, decl, opnds))
        return true;

  opnds.pop_back();
  return false;
}

// Generate the conditional code that implements an attribute predicate.
std::string FormatAttributePredicate(const OperandAttribute *attr) {
  std::string out;
  for (auto *pred : *attr->predicate_values()) {
    if (!out.empty())
      out += " ||\n        ";
    if (pred->IsValue())
      out += formatv("(value == {0})", pred->FormatValue(pred->value()));
    else if (pred->IsRange())
      out += formatv("(value >= {0} && value <= {1})",
                     pred->FormatValue(pred->low()),
                     pred->FormatValue(pred->high()));
    else if (pred->IsMask())
      out += formatv("((value & ~{0:X8}UL) == 0)", pred->mask());
  }

  if (attr->predicate_values()->size() > 1)
    return formatv("    if ({0})", out);
  return formatv("    if {0}", out);
}

// Generate a function that returns the appropriate attribute value for the
// given operand derivation.
std::string FormatPooledCountFunction(std::string const &attr,
                                      OperandDefList const &opnds,
                                      const SubUnitInstantiation *subunit) {
  // The generated function has the following declaration:
  //    void COUNT_#(Instr *ins, int operand_id, int size, int values[]) {}
  std::string out;
  std::vector<std::string> lits;
  std::string addr, label, none;
  bool lit_end = false;

  int64_t max_value = 0;
  for (auto *opnd : opnds) {
    for (auto op_attr : *opnd->attributes()) {
      if (op_attr->name() == attr &&
          subunit->ValidPredicate(op_attr->predicate())) {
        int64_t value = op_attr->values(0);
        std::string result = formatv("  return {0};", value);

        std::string attr = op_attr->type();
        if (attr.empty())
          none = result + "  // none\n";
        if (attr == "address" && addr.empty())
          addr = formatv("  {0}  // addr\n", result);
        if (attr == "label" && label.empty())
          label = formatv("  {0}  // label\n", result);

        if (attr == "lit" && !lit_end) {
          std::string out;
          lit_end = op_attr->predicate_values()->empty();
          if (!lit_end) {
            out =
                formatv("{0}{1}\n", FormatAttributePredicate(op_attr), result);
            lits.push_back(out);
          } else {
            lits.push_back(formatv("  {0}\n", result));
          }
        }
        max_value = std::max(max_value, value);
      }
    }
  }

  if (!lits.empty()) {
    out += "  if (ins->isOpndLiteral(operand_index)) {\n"
           "    int64_t value = ins->GetOperand(operand_index);\n";
    for (const auto &lit : lits)
      out += lit;
    out += "  }\n";
  }
  if (!label.empty())
    out += formatv("  if (ins->isOpndLabel(operand_index))\n{0}", label);
  if (!addr.empty())
    out += formatv("  if (ins->isOpndAddress(operand_index))\n{0}", addr);
  if (!none.empty())
    out += none;

  if (none.empty())
    out += formatv("  return {0};  // default\n", max_value);
  return out;
}

// Return a string that encodes the pool count and the address of a function
// to call if a symbolic size was specified: <count, &function>.
// If the provided name matches an operand attribute, generate a function to
// calculate the right attribute value.  If it does't match an attribute,
// generate the address of a user-define function to call.
std::string OutputState::FormatPooledCount(const ResourceEvent &ref) {
  auto *res = ref.resource();
  int pool_count = res->pool_count();

  // If there's no symbolic pooled count specified, just return the
  // specified pool count.
  if (res->pool_count_name() == nullptr)
    return formatv("{0},{1}", pool_count, kNull);

  auto count_name = res->pool_count_name()->name();
  auto out = formatv("{0},&PoolCount_{1}", pool_count, count_name);

  // If there is a symbolic pooled count, but no reference or no operand,
  // then just generate a reference to the user-defined function.
  if (ref.reference() == nullptr || ref.reference()->operand() == nullptr)
    return out;

  // If there is an operand reference, check to see if the pool count name
  // matches an operand attribute with a valid predicate.
  auto *opnd_ref = ref.reference()->operand();
  auto *opnd_base = opnd_ref->operand_decl()->operand();
  auto *opnd_def = opnd_ref->operand();

  // If we can't find a derivation, there's a problem, just return.
  OperandDefList opnds;
  if (!FindDerivation(opnd_def, opnd_base, opnds)) {
    spec().ErrorLog(ref.reference(), "Panic: operand derivation failed\n\n");
    return out;
  }

  // If we don't find the attribute name, generate the reference to the
  // user-defined function.
  OperandAttribute *attr = nullptr;
  for (auto *opnd : opnds)
    if ((attr = FindAttribute(count_name, opnd, ref.subunit())) != nullptr)
      break;
  if (attr == nullptr)
    return out;

  // If the attribute doesn't have predicate values associated with it, just
  // return the attribute value.
  if (attr->predicate_values()->empty())
    return formatv("{0},{1}", attr->values(0), kNull);

  // Finally, we can generate code for the attribute lookup, and return
  // the name of the function.
  auto func = FormatPooledCountFunction(count_name, opnds, ref.subunit());
  auto index = AddEntry(pooled_count_functions_, func);
  return formatv("{0},&{1}", pool_count, PooledCountFuncName(index));
}

// Generate a function that returns the appropriate attribute value(s) for the
// given operand derivation.
const int kMaxPoolCount = 20;

static int FindLSB(int64_t val) {
  int lsb = 0;
  if (val == 0)
    return 0;
  for (; (val & 1) == 0; val >>= 1)
    lsb++;
  return lsb;
}

std::string FormatPoolValuesFunction(std::string const &attr,
                                     ResourceEvent const &ref,
                                     OperandDefList const &opnds,
                                     const SubUnitInstantiation *subunit) {
  // The generated function has the following declaration:
  //    void VALUE_#(Instr *ins, int operand_id, int size, int values[]) {}
  std::string sizes[kMaxPoolCount];
  bool unconditional[kMaxPoolCount] = {false};

  // Collect the attributes for each size found in the operands.
  for (auto *opnd : opnds)
    if (opnd->attributes()) {
      for (auto op_attr : *opnd->attributes()) {
        if (op_attr->name() == attr &&
            subunit->ValidPredicate(op_attr->predicate())) {
          int tuple_size = op_attr->values()->size();
          if (unconditional[tuple_size])
            continue;

          std::string pred;
          if (op_attr->predicate_values()->empty())
            unconditional[tuple_size] = true;
          else
            pred = FormatAttributePredicate(op_attr);

          std::string item;
          for (int i = 0; i < tuple_size; i++) {
            uint32_t val = op_attr->values(i);
            if (int lsb = FindLSB(val))
              item += formatv("      values[{0}] = (value & {1:X8}) >> {2};\n",
                              i, val, lsb);
            else
              item += formatv("      values[{0}] = value & {1:X8};\n", i, val);
          }

          if (!op_attr->predicate_values()->empty())
            item = formatv("  {{\n{0}      return true;\n    }\n", item);
          else
            item += "      return true;\n";
          sizes[tuple_size] += formatv("{0}{1}", pred, item);
        }
      }
    }

  // If the resource reference indicated a specific size (res:1), make sure
  // we found at least one attribute that satisfied that size.  If not, it's
  // an error.  This is a panic error - it should never happen.
  auto *res = ref.resource();
  if (res->pool_count() > 0)
    if (sizes[res->pool_count()].empty())
      subunit->ErrorLog(ref.reference(),
                        "Panic: Incompatible pool size specifier: {0}:{1}",
                        res->id()->ToString(), res->pool_count());

  std::string out = "  uint64_t value = ins->GetOperand(operand_index);\n";

  for (int size = 1; size < kMaxPoolCount; size++) {
    if (!sizes[size].empty()) {
      out += formatv("  if (size == {0}) {{\n{1}", size, sizes[size]);
      if (!unconditional[size]) {
        for (int i = 0; i < size; i++)
          out += formatv("    values[{0}] = value;\n", i);
        out += "    return true;\n";
      }
      out += "  }\n";
    }
  }

  return out + "  return false;\n";
}

// Return a string that encodes the name of a function to call if a reference
// specifies an optional mask operation on an operand allocated to a pool.
// Generate the function to fetch, shift and mask the parts of the operand
// that are shared.
std::string OutputState::FormatPoolValues(const ResourceEvent &ref) {
  auto *res = ref.resource();
  auto *def = res->definition();
  // Some sanity checking.
  if (res->value_name() == nullptr) {
    if (def->has_shared_bits() && res->HasCount()) {
      if (res->pool_count() > 1 || res->pool_count_name() != nullptr)
        spec().ErrorLog(ref.reference(),
                        "Missing value mask attribute on shared resource");
    }
    return kNull;
  }

  auto value_name = res->value_name()->name();

  // If there is an operand reference, check to see if the mask name
  // matches an operand attribute with a valid predicate.
  auto *opnd_ref = ref.reference()->operand();
  auto *opnd_base = opnd_ref->operand_decl()->operand();
  auto *opnd_def = opnd_ref->operand();

  // If we can't find a derivation, there's a problem, just return.
  OperandDefList opnds;
  if (!FindDerivation(opnd_def, opnd_base, opnds)) {
    spec().ErrorLog(ref.reference(), "Panic: operand derivation failed\n\n");
    return kNull;
  }

  // If we don't find the attribute name, it's an error.
  OperandAttribute *attr = nullptr;
  for (auto *opnd : opnds)
    if ((attr = FindAttribute(value_name, opnd, ref.subunit())) != nullptr)
      break;

  if (attr == nullptr) {
    spec().ErrorLog(ref.reference(), "Invalid value mask name: {0}",
                    value_name);
    return kNull;
  }

  // Generate code for the attribute lookup, and return the function name.
  auto func = FormatPoolValuesFunction(value_name, ref, opnds, ref.subunit());
  auto index = AddEntry(pool_mask_functions_, func);
  return formatv("&{0}", PoolValueFuncName(index));
}

// Format a single pool descriptor.
// Generate an autoinitialization for the type:
//    struct PoolDescriptor {
//      uint8_t pool_id;              // which pool to allocate from
//      uint8_t pool_size;            // how many different allocations sizes
//      uint8_t count;                // how many pool elements we need
//      PoolFunc pool_func;           // optional pointer to pool size func
//      OpndValueFunc value_func;     // optional pointer to pool values func
//      uint8_t first;                // index of first legal element id
//      uint8_t last;                 // index of last legal element id
//      uint8_t width;                // width in bits
//    };
std::string OutputState::FormatPoolDescriptor(const ResourceEvent &ref) {
  auto *res = ref.resource();
  SubPool pool(res);
  auto &subpool_info = res->definition()->sub_pool(pool);
  int subpool_size = *subpool_info.counts().rbegin();

  std::string out =
      formatv("{0},{1},{2},{3},{4},{5},{6}", subpool_info.subpool_id(),
              subpool_size, FormatPooledCount(ref), FormatPoolValues(ref),
              pool.first(), pool.last(), res->definition()->bit_size());

  // Enter it into a table of pool descriptors, and return a reference to it.
  auto index = AddEntry(pool_descriptors_, out);
  return formatv("&{0}", PoolDescriptorName(index));
}

// Format a group of resources used in a pool request.  This is simply a list
// of resource ids for groups or arrays. Enter them into a table so they can
// be shared across pool requests.
std::string OutputState::FormatResourceGroup(const ResourceEvent &ref) {
  std::string out;
  auto *res = ref.resource();

  if (res->IsGroupRef()) {
    for (auto *member : res->definition()->member_defs()) {
      out += std::to_string(member->get_resource_id()) + ",";
    }
  } else if (res->IsArrayDef()) {
    for (int id = res->first(); id <= res->last(); id++) {
      out += std::to_string(res->get_resource_id() + id) + ",";
    }
  }

  out.pop_back(); // throw away trailing comma.

  // Enter it into a table of resource groups, and return a reference to it.
  auto index = AddEntry(resource_groups_, out);
  return formatv("{0}", ResourceGroupName(index));
}

// Format a single pooled resource reference.
// Generate an autoinitialization for the type:
//    struct PooledResourceRef {
//      struct ResourceRef {
//        ReferenceType type;         // type of the reference (use, def, ...)
//        ReferenceFlags flags;       // reference flags
//        PipePhase phase;            // pipeline phase of the reference
//        unsigned int use_cycles;    // # cycles a resource is used
//        PipeFunc phase_func;        // optional pointer to phase function
//        ResourceId &resource id[];  // the resources we're referencing
//        OperandId operand_index;    // operand index for shared resources
//        int MicroOps;               // MicroOps for an FU entry
//      };
//      PoolDescriptor *pool;         // pointer to pool descriptor
//    };
std::string
OutputState::FormatPooledResourceReference(const ResourceEvent &ref) {
  auto *res = ref.resource();

  auto pool = FormatPoolDescriptor(ref);
  auto group = FormatResourceGroup(ref);

  if (ref.IsFuncUnitRef())
    return formatv("{{{0},{1},{2},{3},{4},{5}}",
                   FormatReferenceType(ref.ref_type()),
                   FormatResourceReferenceFlags(ref), ref.use_cycles(), group,
                   pool, ref.micro_ops());

  return formatv(
      "{{{0},{1},{2},{3},{4},{5},{6}}", FormatReferenceType(ref.ref_type()),
      FormatResourceReferenceFlags(ref), FormatPhase(ref.phase_expr()),
      ref.use_cycles(), group, res->operand_index(), pool);
}

// Format a pooled operand reference list. Enter it into a table so it can
// be shared with other subunits.
std::string OutputState::FormatPooledResourceReferences(InstrInfo *info,
                                                        OutputSet &output_list,
                                                        FormatName name) {
  // First write out entries for all unconditional pooled references.
  std::string out, previous;
  auto *subunit = info->subunit();
  for (auto &ref : info->resources())
    if (ref.resource()->HasCount()) { // Only pooled references
      auto resource = FormatPooledResourceReference(ref);
      if (resource != previous) {
        out += resource + ",";
        previous = resource;
      }
    }

  // Format conditional pooled resource references and FU references.
  for (auto *ref : info->resource_refs()) {
    if (ref->IsConditionalRef()) {
      auto res =
          FormatIfElseResourceRef(subunit, ref->conditional_ref(),
                                  RefTypes::kUse, output_list, name, true);
      if (res != kNull)
        out += "{&" + res + "},";
    } else if (ref->IsFuncUnitRef()) {
      out += FormatFuncUnitReference(subunit, ref, true);
    }
  }

  if (out.empty())
    return kNull;
  out.pop_back(); // throw away trailing comma.

  auto index = AddEntry(pooled_resource_refs_, out);
  return formatv("&{0}", name(index));
}

// Format a single constraint. Return an empty string if no constraint found,
// or if the MDL constraint doesn't further constrain the operand (ie, it is a
// superset of the operand constraint).
std::string OutputState::FormatConstraint(const Reference *ref) {
  std::string family = database_->spec().family_name();
  if (auto *opnd = ref->operand())
    if (auto *port = ref->port())
      if (auto *reg_class = port->reg_class())
        if (auto *operand_class = opnd->operand_decl()->reg_class())
          if (!reg_class->IsSupersetOf(operand_class))
            return formatv("{{{0},{1}::{2}RegClassId}", opnd->operand_index(),
                           family, reg_class->name());
  return "";
}

// Find and format a list of constraints. Not all operands have constraints,
// so the resulting string could be empty.
std::string OutputState::FormatConstraintList(ReferenceList *refs) {
  std::string out;
  for (auto *ref : *refs) {
    auto constraint = FormatConstraint(ref);
    if (!constraint.empty())
      out += constraint + ",";
  }
  if (out.empty())
    return kNull;
  out.pop_back(); // throw away trailing comma.

  auto index = AddEntry(constraints_, out);
  return ConstraintListName(index);
}

std::string OutputState::FormatIfElseConstraint(ConditionalRef *cond) {
  if (cond == nullptr)
    return kNull;

  auto then_refs = FormatConstraintList(&cond->refs());
  if (then_refs != kNull)
    forward_constraint_refs_.insert(then_refs);

  // If no constraints were found, and the predicate is null, abort.
  if (then_refs == kNull && cond->instr_predicate() == nullptr)
    return kNull;

  std::string out;
  if (cond->instr_predicate() == nullptr) {
    out = formatv("{1},&{0},{1}", then_refs, kNull);
  } else {
    auto else_refs = FormatIfElseConstraint(cond->else_clause());
    if (else_refs != kNull)
      forward_cond_constraint_refs_.insert(else_refs);

    if (else_refs == kNull && then_refs == kNull)
      return kNull;
    if (else_refs != kNull)
      else_refs = "&" + else_refs;
    if (then_refs != kNull)
      then_refs = "&" + then_refs;

    auto index = FormatPredicate(cond->instr_predicate());
    out = formatv("&{0},{1},{2}", PredicateName(index), then_refs, else_refs);
  }
  auto index = AddEntry(cond_constraints_, out);
  return CondConstraintName(index);
}

std::string OutputState::FormatPortReferences(InstrInfo *info) {
  std::string out;
  for (auto *ref : *info->references()) {
    if (ref->IsConditionalRef()) {
      auto constraint = FormatIfElseConstraint(ref->conditional_ref());
      if (constraint != kNull)
        out += "{&" + constraint + "},";
    } else {
      auto constraint = FormatConstraint(ref);
      if (!constraint.empty())
        out += constraint + ",";
    }
  }

  if (out.empty())
    return kNull;
  out.pop_back(); // throw away trailing comma.

  auto index = AddEntry(constraints_, out);
  return formatv("&{0}", ConstraintListName(index));
}

// Scan a reference list, marking resource uses which are identical to earlier
// references. This enables a single instruction to reference resources
// several times - in different operands - without impacting hazards and
// bundle packing.
void MarkDuplicateReferences(ResourceList &refs) {
  for (auto &ref : refs)
    for (auto &old_ref : refs) {
      if (&ref == &old_ref)
        break;
      auto *res = ref.resource();
      auto *old_res = old_ref.resource();
      if (ref.phase_expr()->ToString() != old_ref.phase_expr()->ToString())
        continue;
      if (res->get_final_resource_id() != old_res->get_final_resource_id())
        continue;
      if (ref.ref_type() != old_ref.ref_type())
        continue;
      if (res->HasCount() || old_res->HasCount())
        continue;
      int ref_operand_index = res->operand_index();
      int old_operand_index = old_res->operand_index();
      if (ref_operand_index != old_operand_index) {
        ref.reference()->SetDuplicate();
        break;
      }
    }
}

// Format a single subunit.
std::string OutputState::FormatSubunit(InstrInfo *info) {
  // First format all the operand references.
  auto operands = FormatOperandReferenceList(info->references());

  // Sort the references so that they are ordered by phase, then resource id.
  // This will speed up bundle packing, since functional units and issue slots
  // are the lowest-numbered resources.
  std::stable_sort(info->resources().begin(), info->resources().end());

  MarkDuplicateReferences(info->resources());

  auto used = FormatResourceReferences(
      info, RefTypes::kUse, used_resource_refs_, &UsedResourceListName);
  auto held = FormatResourceReferences(
      info, RefTypes::kHold, held_resource_refs_, &HeldResourceListName);
  auto rsvd = FormatResourceReferences(info, RefTypes::kReserve,
                                       reserved_resource_refs_,
                                       &ReservedResourceListName);
  auto pooled = FormatPooledResourceReferences(info, pooled_resource_refs_,
                                               &PooledResourceListName);
  auto constraints = FormatPortReferences(info);

  // If everything is null (common case with CPU predicates), return null.
  if (operands == kNull && used == kNull && rsvd == kNull && held == kNull &&
      pooled == kNull && constraints == kNull)
    return kNull;

  // Its pretty common that most of these fields are null, so write out shorter
  // initializations if that's the case (this just saves disk space).
  if (held == kNull && rsvd == kNull && pooled == kNull && constraints == kNull)
    return formatv("{{{0},{1}}", operands, used);
  return formatv("{{{0},{1},{2},{3},{4},{5}}", operands, used, held, rsvd,
                 pooled, constraints);
}

// Format a subunit set for an instruction on a single CPU.
std::string OutputState::FormatSubunits(const std::string &instr,
                                        const InstrInfoList &info_list,
                                        const std::string &cpuname) {
  // Generate each subunit and eliminate duplicates.
  std::vector<std::string> units;
  for (auto *info : info_list)
    if (info->subunit()->func_unit()->cpu()->name() == cpuname) {
      auto unit = FormatSubunit(info);
      if (unit != kNull &&
          std::find(units.begin(), units.end(), unit) == units.end())
        units.push_back(unit);
    }

  if (units.empty())
    return kNull; // We didn't find subunits for this CPU.

  // Format the list of strings.
  std::string out;
  for (auto &unit : units) out += unit + ",";
  out.pop_back(); // throw away trailing comma.

  // Not sure if its worthwhile actually sharing these, but its pretty easy.
  auto index = AddEntry(subunits_, out);
  cpu_instr_subunits_[SubunitsName(cpuname, instr)] = index;
  return out;
}

void OutputState::WriteTable(const OutputSet &objects, const std::string &type,
                             const std::string &suffix, FormatName name,
                             const std::string &title,
                             const std::string &info) {
  if (!title.empty())
    output_c() << formatv("{0}// {1} ({2} entries){3}{0}", divider, title,
                          objects.size(), info);
  for (auto &[out, index] : objects)
    output_c() << formatv("{0} {1}{2} {{{3}};\n", type, name(index), suffix,
                          out);
}

// Helper to figure out how many constructors are included on a a vector
// initialization line. We're just counting leading '{'.  Note that some
// vectors don't have any braces.
static int count_vector_init_items(std::string input) {
  int count = 0;
  for (auto c : input)
    if (c == '{')
      count++;
  return std::max(count, 1); // In case there's just one initializer
}

void OutputState::WriteVectorTable(const OutputSet &objects,
                                   const std::string &type, FormatName name,
                                   const std::string &title,
                                   const std::string &info) {
  if (!title.empty())
    output_c() << formatv("{0}// {1} ({2} entries){3}{0}", divider, title,
                          objects.size(), info);
  for (auto &[out, index] : objects) {
    output_c() << formatv("{0} {1}_data[] = {{{2}};\n", type, name(index), out);
    output_c() << formatv("{0}Vec {1} = {{ {2}, {1}_data };\n", type,
                          name(index), count_vector_init_items(out));
  }
}

void OutputState::WritePhases(const OutputSet &phases, FormatName name) const {
  if (phases.empty())
    return;
  output_c() << formatv(
      "{0}// Functions to compute non-trivial pipeline phase expressions{0}",
      divider);

  // Write out any forward declarations that might be needed.
  for (auto index : forward_phases_)
    output_c() << formatv("unsigned {0}(Instr *ins);\n", name(index));
  output_c() << "\n";

  for (auto &[phase, index] : phases)
    output_c() << formatv("unsigned {0}(Instr *ins) {{\n{1} }\n", name(index),
                          phase);
}

void OutputState::WritePoolCountFunctions(const OutputSet &funcs,
                                          FormatName name) const {
  if (funcs.empty())
    return;
  output_c() << formatv(
      "{0}{1}{0}", divider,
      "// Functions to compute attribute-based pool size counts");

  for (auto &[func, index] : funcs)
    output_c() << formatv("int {0}(Instr *ins, int operand_index) {{\n{1}}\n",
                          name(index), func);
}

void OutputState::WritePoolValueFunctions(const OutputSet &funcs,
                                          FormatName name) const {
  if (funcs.empty())
    return;
  output_c() << formatv(
      "{0}{1}{0}", divider,
      "// Functions to fetch and normalize operand values for sharing");

  for (auto &[func, index] : funcs)
    output_c() << formatv("bool {0}(Instr *ins, int operand_index, "
                          "int size, int values[]) {{\n{1}}\n",
                          name(index), func);
}

void OutputState::WritePredicateFunctions(const OutputSet &funcs,
                                          FormatName name,
                                          const std::string &type,
                                          std::fstream &output) const {
  if (funcs.empty())
    return;
  output << formatv("{0}// {1}{0}", divider, type);
  for (auto &[func, index] : funcs) {
    output << formatv("bool {0}(Instr *MI) {{\n{1}\n}\n", name(index), func);
  }
}

void OutputState::WriteVirtualPredicateTable(const OutputSet &funcs) const {
  output_t() << formatv("{0}// Virtual predicate function table{0}", divider);
  output_t() << "std::vector<PredFunc> InstrPredicates { ";
  for (unsigned i = 0; i < funcs.size(); i++) {
    if (i > 0)
      output_t() << ", ";
    output_t() << "&" << VirtualPredicateName(i);
  }
  output_t() << "};\n\n";
}

// TODO(tbd): Write out a representation of register classes.
// No current architecture currently requires this...
void OutputState::WriteClasses(const OutputSet &reg_classes, FormatName name) {}

// Write out some information about each llvm instruction definition.
// This is only necessary when generating a stand-alone database.
void OutputState::WriteInstructionInfo() const {
  if (!generate_llvm_defs_)
    return;
  if (database_->spec().cpus().empty())
    return;

  std::string out;
  std::string family = database_->spec().family_name();
  for (auto *ins : database_->spec().instructions()) {
    out += formatv("{{::llvm::{0}::{1},\"{1}\"},\n", family, ins->name());
    if (ins->derived())
      for (auto *derived : *ins->derived())
        out +=
            formatv("{{::llvm::{0}::{1},\"{1}\"},\n", family, derived->name());
  }

  output_c() << formatv("{0}// Instruction name table ({1} entries){0}",
                        divider, database_->spec().instructions().size());
  output_c() << formatv("InstructionNameMap InstructionNames = {{\n{0}};\n",
                        out);
}

// Write out instruction tables for each defined CPU.
void OutputState::WriteInstructionTables() const {
  std::string family = database_->spec().family_name();
  for (auto *cpu : database_->spec().cpus()) {
    std::string out;
    int instr_count = 0; //  Number of instructions for this CPU.
    out = formatv("  static SubunitTable table;\n"
                  "  static sys::SmartMutex<true> Mutex;\n"
                  "  sys::SmartScopedLock<true> Lock(Mutex);\n"
                  "  if (table.size() != 0) return &table;\n"
                  "  table.resize(::llvm::{0}::INSTRUCTION_LIST_END, {1});\n",
                  family, kNull);

    for (auto &[iname, info] : database_->instruction_info()) {
      std::string su_name = SubunitsName(cpu->name(), iname);
      if (cpu_instr_subunits_.count(su_name)) {
        int id = cpu_instr_subunits_.at(su_name);
        out += formatv("  table[::llvm::{0}::{1}] = &{2};\n", family, iname,
                       SubunitListName(id));
        if (info[0]->instruct()->derived())
          for (auto *derived : *info[0]->instruct()->derived())
            out += formatv("  table[::llvm::{0}::{1}] = &{2};\n", family,
                           derived->name(), SubunitListName(id));

        instr_count++;
      }
    }
    // If there's no instruction info for a CPU, don't write out object.
    cpu->set_instr_count(instr_count);
    if (instr_count == 0) continue;

    output_c() << formatv(
        "{0}// Instruction table initialization for {1} ({2} valid entries){0}",
        divider, cpu->name(), instr_count);
    output_c() << formatv(
        "__attribute__((optnone))\n"
        "SubunitTable *SUNITS_{0}() {{\n{1}  return &table;\n}\n", cpu->name(),
        out);
  }
}

// Generate the forwarding table for a single CPU.
std::string OutputState::FormatForwardingInfo(const CpuInstance *cpu,
                                              FwdNetwork &network) {
  std::string out;
  for (int from = 0; from < network.units(); from++) {
    std::string fwd_set_out;
    for (int to = 0; to < network.units(); to++)
      fwd_set_out += formatv("{0},", network.get(from, to));

    auto index = AddEntry(forward_sets_, fwd_set_out);
    out += formatv("{0},", ForwardSetName(index));
  }

  return formatv("int8_t *FWD_{0}[{1}] = {{ {2} };\n", cpu->name(),
                 network.units(), out);
}

// Given a forwarding statement functional unit specifier, look for a match
// for the name in functional unit templates, functional unit groups, and
// functional unit instances.
// Forwarding statements within a cluster definition apply only to that
// cluster. Cpu-level statements apply across all clusters.
std::vector<int> OutputState::FindUnitIds(const CpuInstance *cpu,
                                          const ClusterInstance *fwd_cluster,
                                          const Identifier *name) const {
  std::vector<int> units;
  auto &spec = database_->spec();
  // If it's a functional unit template, find all instances of that template.
  if (spec.fu_map().count(name->name())) {
    for (auto *cluster : *cpu->clusters())
      if (cluster == fwd_cluster || fwd_cluster == nullptr) {
        for (auto *fu : cluster->fu_instantiations())
          if (fu->func_type()->name() == name->name())
            units.push_back(fu->get_resource()->get_resource_id());
      }
    return units;
  }

  // If it's a group, find all uses of every member functional unit template.
  if (spec.fu_group_map().count(name->name())) {
    for (auto *member : *spec.fu_group_map()[name->name()]->members()) {
      auto gunits = FindUnitIds(cpu, fwd_cluster, member);
      units.insert(units.end(), gunits.begin(), gunits.end());
    }
    return units;
  }

  // If it's not a template or a group, find a functional unit instance of
  // the name.  There potentially could be one in each cluster.
  for (auto *cluster : *cpu->clusters())
    if (cluster == fwd_cluster || fwd_cluster == nullptr) {
      for (auto *fu : cluster->fu_instantiations())
        if (fu->instance()->name() == name->name())
          units.push_back(fu->get_resource()->get_resource_id());
    }
  return units;
}

void OutputState::ExpandForwardStmt(FwdNetwork &network, const CpuInstance *cpu,
                                    const ClusterInstance *cluster,
                                    const ForwardStmt *fwd) const {
  auto &spec = database_->spec();
  auto from = fwd->from_unit();
  auto defs = FindUnitIds(cpu, cluster, from);
  if (defs.empty()) {
    spec.ErrorLog(from, "Invalid functional unit: {0}", from->name());
    return;
  }
  for (const auto &[to, cycles] : fwd->to_units()) {
    auto uses = FindUnitIds(cpu, cluster, to);
    if (!uses.empty()) {
      for (auto def : defs)
        for (auto use : uses)
          network.set(def, use, cycles);
    } else {
      spec.ErrorLog(to, "Invalid functional unit: {0}", to->name());
    }
  }
}

// Generate forwarding information tables. For each CPU, use all the
// forward clauses to build a representation of the forwarding network, then
// write out a dense representation of each network.
void OutputState::GenerateForwardingInfo() {
  std::vector<std::string> networks;
  for (auto *cpu : database_->spec().cpus())
    if (!cpu->forward_stmts()->empty()) {
      FwdNetwork network(cpu->max_fu_id() + 1);
      for (auto *fwd : *cpu->forward_stmts())
        ExpandForwardStmt(network, cpu, nullptr, fwd);
      for (auto *cluster : *cpu->clusters())
        for (auto *fwd : *cluster->forward_stmts())
          ExpandForwardStmt(network, cpu, cluster, fwd);
      networks.push_back(FormatForwardingInfo(cpu, network));
    }

  if (networks.empty())
    return;
  output_c() << formatv("{0}// Functional unit forwarding tables.{0}", divider);

  // Write out the networks for each processor.
  WriteTable(forward_sets_, "int8_t", "[]", &ForwardSetName, "");
  for (auto &item : networks)
    output_c() << item;
}

// Calculate a "resource factor".  This is used by LLVM generic scheduler
// to relate functional unit usage to cycles (unique for each CPU). We
// calculate it here so we don't have to in LLVM.
int CalcResourceFactor(CpuInstance *cpu) {
  int factor = std::max(1, cpu->max_issue());
  for (auto size : cpu->fu_pool_sizes())
    factor = std::lcm(factor, size);
  return factor;
}

// Write out the top-level CPU table, which contains pointers to instruction
// tables for each CPU.
void OutputState::WriteCpuList() const {
  std::string out, cpu_defs;
  for (auto *cpu : database_->spec().cpus()) {
    int execute_stage = database_->spec().FindFirstExecutePhase(cpu)->index();
    std::string fwd = cpu->forward_stmts()->empty()
                          ? kNull
                          : formatv("&FWD_{0}[0]", cpu->name());
    int resource_factor = CalcResourceFactor(cpu);
    auto sunits_name = cpu->get_instr_count() ?
                       formatv("&SUNITS_{0}", cpu->name()) : kNull;
    cpu_defs +=
        formatv("CpuConfig<CpuParams<{0},{1},{2}, {3},{4}, {5},{6}, "
                "{7},{8},{9},{10}>> CPU_{11}({12},{13},{14},NAMES_{15});\n",
                cpu->all_resources().back()->get_resource_id(),
                cpu->max_used_resource_id(), cpu->max_fu_id(),
                cpu->pool_count(), cpu->max_pool_allocation(),
                std::max(1, cpu->max_issue()), cpu->reorder_buffer_size(),
                execute_stage, cpu->load_phase(), cpu->high_latency_def_phase(),
                cpu->max_resource_phase(), cpu->name(), sunits_name, fwd,
                resource_factor, cpu->name());

    for (const auto &llvm_name : cpu->llvm_names())
      out += formatv("  {{\"{0}\", &CPU_{1} },\n", llvm_name, cpu->name());
  }

  // Write out CPU configurations for each subtarget in the family.
  output_c() << formatv("{0}// Family CPU Descriptions.\n"
                        "//  CpuParams:\n"
                        "//    - Total number of defined resources\n"
                        "//    - Maximum \"used\" resource id\n"
                        "//    - Maximum functional unit id\n"
                        "//\n"
                        "//    - Number of distinct allocation pools\n"
                        "//    - Largest resource pool allocation size\n"
                        "//\n"
                        "//    - Instruction issue width\n"
                        "//    - Instruction reorder buffer size\n"
                        "//\n"
                        "//    - First execution pipeline phase\n"
                        "//    - Default load phase\n"
                        "//    - \"High-latency instruction\" write phase\n"
                        "//    - Latest resource use pipeline phase"
                        "{0}{1}",
                        divider, cpu_defs);

  // Write out the top-level cpu table for this family.
  output_c() << formatv("{0}// Top-level {1} Subtarget Description Table.{0}",
                        divider, database_->spec().family_name());
  output_c() << formatv("CpuTableDict CpuDict = {{\n{0}};\n\n", out);
  output_c() << formatv("CpuTableDef CpuTable = CpuTableDef(CpuDict);\n");
}

// Open the output files, abort if unable to do that.
void OutputState::OpenOutputFiles() {
  // Split out the input filename and directory.
  auto infile = std::filesystem::path(database_->file_name());
  std::string dir_name = infile.parent_path();
  std::string base_name = infile.stem();
  if (!database_->directory_name().empty())
    dir_name = database_->directory_name();

  auto AddSlash = [](std::string path_name) {
    if (!path_name.empty() && path_name.back() != '/')
      path_name += "/";
    return path_name;
  };

  // Open the main database output file.
  file_name_c_ = formatv("{0}{1}GenMdlInfo.inc", AddSlash(dir_name), base_name);
  output_c_ = new std::fstream(file_name_c_, std::fstream::out);
  if (!output_c_->is_open()) {
    llvm::errs() << formatv("Cannot open output file \"{0}\", aborting\n",
                            file_name_c_);
    exit(EXIT_FAILURE);
  }

  // Open the Target library component of the database.
  file_name_t_ =
      formatv("{0}{1}GenMdlTarget.inc", AddSlash(dir_name), base_name);
  output_t_ = new std::fstream(file_name_t_, std::fstream::out);
  if (!output_t_->is_open()) {
    llvm::errs() << formatv("Cannot open output file \"{0}\", aborting\n",
                            file_name_t_);
    exit(EXIT_FAILURE);
  }

  // Open output header file filename.
  file_name_h_ = formatv("{0}{1}GenMdlInfo.h", AddSlash(dir_name), base_name);
  output_h_ = new std::fstream(file_name_h_, std::fstream::out);
  if (!output_h_->is_open()) {
    llvm::errs() << formatv("Cannot open output file \"{0}\", aborting\n",
                            file_name_h_);
    exit(EXIT_FAILURE);
  }
}

// Write out headers to the C and H output files.
void OutputState::WriteHeader() {
  auto infile = std::filesystem::path(database_->file_name());
  std::string cpu_name = infile.stem();
  output_c() << formatv("{0}// Machine Description Database.\n"
                        "// This file is auto-generated, do not edit.{1}\n",
                        divider + 1, divider);

  if (!generate_llvm_defs_) {
    output_c() << "#include \"llvm/Support/Mutex.h\"\n";
    output_c() << "#include \"llvm/MC/MDLInfo.h\"\n";
    output_c() << "#include \"llvm/MC/MDLInstrInfo.h\"\n";
    output_c() << "#include \"" + cpu_name + "InstrInfo.h\"\n";
  }
  output_c() << formatv("#include \"{0}\"\n\n", file_name_h_);

  output_t() << formatv(
      "{0}// Machine Description Database: Target library components\n"
      "// This file is auto-generated, do not edit.{1}",
      divider + 1, divider);
  output_t()
      << "// This file contains MDL predicate functions that call Target\n"
         "// library functions. Since MDL lives in MC, and MC is included\n"
         "// in programs that may NOT include the Target library, we need\n"
         "// to virtualize these."
      << divider;
  output_t() << "#include \"llvm/MC/MDLInfo.h\"\n";
  output_t() << "#include \"llvm/MC/MDLInstrInfo.h\"\n\n";
  output_t() << "#include \"" + cpu_name + "InstrInfo.h\"\n";

  output_h() << formatv("#ifndef {0}_MACHINE_DESCRIPTION_DATABASE\n", cpu_name);
  output_h() << formatv("#define {0}_MACHINE_DESCRIPTION_DATABASE\n", cpu_name);
  output_h() << formatv("{0}// Machine Description Database.\n"
                        "// This file is auto-generated, do not edit.{1}",
                        divider + 1, divider);
  output_h() << "#include <map>\n";
  output_h() << "#include <string>\n";
}

void OutputState::WriteTrailer() {
  auto infile = std::filesystem::path(database_->file_name());
  std::string cpu_name = infile.stem();
  output_h() << formatv("\n#endif  // {0}_MACHINE_DESCRIPTION_DATABASE\n",
                        cpu_name);
}

// Write out some global statistics about the spec:
//   - Maximum number of resources (across CPUs, for each CPU).
//   - Maximum resource id used in RefUse operations.
//   - Maximum pipeline phase used by RefUse operations.
//   - Maximum number of instructions that can be issued in parallel.
//   - Maximum number of pools (across CPUs, for each CPU).
//   - Maximum pool allocation size across CPUs.
// NOTE: These are worst-case numbers across all family members, we may
// want to write out CPU-specific versions for compiler performance reasons.
void OutputState::WriteSpecDefinitions() {
  MdlSpec &spec = database_->spec();
  int max_res = 0;
  int max_use_res = 0;
  int max_phase = 0;
  int max_issue = 0;
  int max_pools = 0;
  int max_pool_alloc = 0;

  // Find that absolute worst-case pipeline phase.
  int max_phase_worst_case = 0;
  for (auto *pipe : spec.pipe_phases())
    max_phase_worst_case =
        std::max(max_phase_worst_case, pipe->phase_names()->back()->index());

  // Find maximum number of resources across CPUs.
  for (auto *cpu : spec.cpus())
    max_res = std::max(max_res, cpu->all_resources().back()->get_resource_id());

  // Find maximum number of pools across CPUs.
  for (auto *cpu : spec.cpus())
    max_pools = std::max(max_pools, cpu->pool_count());

  // Find the maximum pool allocation size across CPUs.
  for (auto *cpu : spec.cpus()) {
    int cpu_max_alloc = 0;
    for (auto *def : cpu->pool_resources())
      if (!def->alloc_sizes().empty())
        cpu_max_alloc = std::max(cpu_max_alloc, *def->alloc_sizes().rbegin());
    cpu->set_max_pool_allocation(cpu_max_alloc);
    max_pool_alloc = std::max(max_pool_alloc, cpu_max_alloc);
  }

  // Find maximum resource use phase for each CPU.
  // Also find the maximum resource id used in RefUse rules (for all CPUs).
  for (auto *cpu : spec.cpus()) {
    int max_cpu_phase = 0;
    int max_cpu_use_res = 0;
    for (auto *res : cpu->all_resources())
      if (res->ref_types() & (RefTypes::kUse | RefTypes::kFus)) {
        if (res->phase_expr_seen())
          max_cpu_phase = max_phase_worst_case;
        else
          max_cpu_phase = std::max(max_cpu_phase, res->latest_ref());

        // If referencing a group or pool, we need to note all the pool or
        // group members as explicitly used.
        if (res->IsGroupDef()) {
          for (auto *member : res->member_defs())
            max_cpu_use_res =
                std::max(max_cpu_use_res, member->get_resource_id());
        } else {
          int res_id = res->get_resource_id();
          if (res->pool_size() > 0)
            res_id += res->pool_size() - 1;
          max_cpu_use_res = std::max(max_cpu_use_res, res_id);
        }
      }

    cpu->set_max_resource_phase(max_cpu_phase);
    max_phase = std::max(max_phase, max_cpu_phase);

    cpu->set_max_used_resource_id(max_cpu_use_res);
    max_use_res = std::max(max_use_res, max_cpu_use_res);
  }

  // Find maximum issue size for each CPU.
  // Count issue slots for each cluster. If a cluster doesn't have issue
  // slots, count functional unit instantiations.
  for (auto *cpu : spec.cpus()) {
    int issue = 0;
    for (auto *clst : *cpu->clusters())
      issue += !clst->issues()->empty() ? clst->issues()->size()
                                        : clst->func_units()->size();

    if (issue == 0) issue = MCSchedModel::DefaultIssueWidth;
    cpu->set_max_issue(issue);
    max_issue = std::max(max_issue, issue);
  }

  // Find the earliest pipeline phase referenced by name in a "use" clause
  // for each CPU.
  for (auto &[instr_name, info_list] : database_->instruction_info()) {
    for (auto *info : info_list) {
      auto *cpu = info->subunit()->func_unit()->cpu();
      int min_use = cpu->early_use_phase();
      for (auto *ref : *info->references()) {
        if (ref->operand() && ref->ref_type() == RefTypes::kUse) {
          if (auto *phase = ref->phase_expr()->GetPhaseName()) {
            if (min_use == -1)
              min_use = phase->index();
            else
              min_use = std::min(min_use, phase->index());
          }
        }
      }
      cpu->set_early_use_phase(min_use);
    }
  }
  output_h() << formatv("\nnamespace llvm {{\nnamespace {0} {{\n",
                        spec.family_name());

  // Use globally default values for undefined attributes.
  output_h() << formatv("{0}// Global constant definitions{0}", divider);
  output_h() << formatv("const int kMaxResourceId = {0};\n", max_res);
  output_h() << formatv("const int kMaxUsedResourceId = {0};\n", max_use_res);
  output_h() << formatv("const int kMaxPipePhase = {0};\n", max_phase);
  output_h() << formatv("const int kMaxIssue = {0};\n", max_issue);
  output_h() << formatv("const int kMaxPools = {0};\n", max_pools);
  output_h() << formatv("const int kMaxPoolCount = {0};\n", max_pool_alloc);
}

// Write out definitions we expect LLVM tablegen to create:
//    - instruction ids.
//    - register ids.
//    - register class ids.
// This function will be unnecessary after integration with LLVM.
void OutputState::WriteLLVMDefinitions() {
  if (!generate_llvm_defs_)
    return;

  MdlSpec &spec = database_->spec();
  int id = 0;
  output_h() << formatv("{0}// LLVM Instruction defs{0}", divider);
  output_h() << "  enum {\n";

  for (auto *instr : spec.instructions()) {
    output_h() << formatv("    {0},  // {1}\n", instr->name(), id++);
    if (instr->derived())
      for (auto *dinstr : *instr->derived())
        output_h() << formatv("    {0},  // {1}\n", dinstr->name(), id++);
  }
  output_h() << formatv("    INSTRUCTION_LIST_END,  // {0}\n", id++);

  output_h() << formatv("{0}// Register defs{0}", divider);
  for (auto *reg : spec.registers())
    output_h() << formatv("  {0},  // {1}\n", reg->name(), id++);

  output_h() << formatv("{0}// Register class def{0}", divider);
  for (auto *reg_class : spec.reg_classes())
    output_h() << formatv("  {0}RegClassId,  // {1}\n", reg_class->name(),
                          id++);

  output_h() << formatv("\n  }; // enum\n");
}

// Format the fully-qualified C++ name of a resource.
std::string ResourceCppName(MdlSpec &spec, CpuInstance *cpu,
                         ClusterInstance *cluster, ResourceDef *res) {
  std::string name = formatv("{0}::Mdl::{1}::", spec.family_name(),
                             cpu->name());
  if (cluster && !cluster->IsNull())
    name += formatv("{0}::", cluster->name());
  name += res->name();
  return name;
}

// Format a debug name for a used resource.
std::string ResourceName(MdlSpec &spec, ClusterInstance *cluster,
                         ResourceDef *res) {
  std::string name;
  if (cluster && !cluster->IsNull())
    name += formatv("{0}:", cluster->name());

  if (auto *instance = res->get_fu())
    name += formatv("{0}:", instance->func_type()->name());

  return name + res->name();
}

void SaveResourceName(ResourceDef *res, std::string name,
                      std::vector<std::string> &res_names) {
  int id = res->get_resource_id();
  if (res->pool_size() <= 1)
    res_names[id] = name;
  else
    for (int i = 0; i < res->pool_size(); i++)
      res_names[id + i] = formatv("{0}[{1}]", name, i);
}

// Write out a single resource definition.
void AddResourceDef(std::string *out, MdlSpec &spec, CpuInstance *cpu,
                    ClusterInstance *cluster, ResourceDef *res,
                    std::string note, std::vector<std::string> &res_names) {
  std::string prefix = "  ";
  if (cluster && !cluster->IsNull())
    prefix += "  ";

  // Note: we don't need to print out groups.
  if (!res->IsGroupDef()) {
    auto name = ResourceCppName(spec, cpu, cluster, res);
    *out += formatv("{0}  const int {1} = {2};      // {3} ({4})\n", prefix,
                    res->name(), res->get_resource_id(), name, note);
    SaveResourceName(res, ResourceName(spec, cluster, res), res_names);
  }
}

// Write out definitions for functional unit and issue slot resources.
// We also capture the names of each resource and create a table of them
// indexed by id.
void OutputState::WriteResourceDefinitions() {
  MdlSpec &spec = database_->spec();
  std::string out;
  output_c() << formatv("{0}// Resource name tables{0}", divider);

  for (auto *cpu : spec.cpus()) {
    std::vector<std::string> res_names;
    res_names.resize(cpu->max_resource_id() + 1);

    out +=
        formatv("{0}// Resource Definitions for {1}{0}", divider, cpu->name());
    out += formatv("  namespace {0} {{\n", cpu->name());
    for (auto *res : *cpu->resources())
      AddResourceDef(&out, spec, cpu, nullptr, res, "resource", res_names);

    // Write out C++ definitions for resources associated with a cluster.
    // Note that we DON'T write definitions for resources associated with
    // functional unit instances. (Its messy dealing with resource scoping).
    for (auto *cluster : *cpu->clusters()) {
      if (!cluster->IsNull())
        out += formatv("\n    namespace {0} {{\n", cluster->name());

      // Write out functional unit resource definitions. We only write names
      // for top-level functional unit resources, and don't write out
      // catchall units. We also add functional unit instance resources to the
      // name table (even though we don't write out definitions).
      for (auto *fu : cluster->fu_instantiations()) {
        if (fu->parent() == nullptr && fu->get_resource()->is_used() &&
            !fu->instance()->is_catchall_unit()) {
          auto *res = fu->get_resource();
          AddResourceDef(&out, spec, cpu, cluster, res, "func unit", res_names);
        }
        for (auto *res : fu->resources())
          if (!res->IsGroupDef())
            SaveResourceName(res, ResourceName(spec, cluster, res), res_names);
      }

      for (auto *issue : *cluster->issues())
        AddResourceDef(&out, spec, cpu, cluster, issue, "issue", res_names);
      for (auto *res : *cluster->resources())
        AddResourceDef(&out, spec, cpu, cluster, res, "resource", res_names);
      if (!cluster->IsNull())
        out += formatv("    }  // namespace {0}\n", cluster->name());
    }
    out += formatv("  }  // namespace {0}\n", cpu->name());

    // Write out resource names for this CPU.
    std::string names;
    for (unsigned idx = 1; idx < res_names.size(); idx++)
      names += "\"" + res_names[idx] + "\",";
    if (!names.empty()) names.pop_back();
    output_c() << formatv("std::string NAMES_{0}[] = {{\"\",{1}};\n",
                          cpu->name(), names);
  }

  output_h() << "\nnamespace Mdl {\n";
  output_h() << out;
  output_h() << "}  // namespace Mdl\n";
}

// Write external definitions to the output header file.
void OutputState::WriteExterns() {
  std::string family = database_->spec().family_name();
  output_h() << formatv("{0}// External definitions{0}", divider);
  if (generate_llvm_defs_) {
    output_h() << "using InstructionNameMap = std::map<int, std::string>;\n";
    output_h() << formatv("extern InstructionNameMap InstructionNames;\n",
                          family);
  }
  output_h() << formatv("extern llvm::mdl::CpuTableDef CpuTable;\n\n", family);
  output_h() << formatv("}  // namespace {0}\n}  // namespace llvm\n", family);
}

// Top level function for writing out the machine description.
void OutputState::WriteCpuTable() {
  for (auto [instr_name, info] : database_->instruction_info())
    for (auto *cpu : database_->spec().cpus())
      FormatSubunits(instr_name, info, cpu->name());

  // Collect and write out overall spec parameters after processing all the
  // CPUs' subunits.
  WriteSpecDefinitions();

  output_c() << formatv("\nnamespace llvm {{\nnamespace {0} {{\n",
                        database_->spec().family_name());
  output_t() << formatv("\nnamespace llvm {{\nnamespace {0} {{\n",
                        database_->spec().family_name());
  output_c() << formatv("using namespace ::llvm::mdl;\n");
  output_t() << formatv("using namespace ::llvm::mdl;\n");
  output_c() << formatv("constexpr auto {0} = nullptr;\n", kNull);

  // Generate some constants for reference types.
  output_c() << "constexpr auto RefPred    = ReferenceTypes::RefPred;\n";
  output_c() << "constexpr auto RefUse     = ReferenceTypes::RefUse;\n";
  output_c() << "constexpr auto RefDef     = ReferenceTypes::RefDef;\n";
  output_c() << "constexpr auto RefKill    = ReferenceTypes::RefKill;\n";
  output_c() << "constexpr auto RefUseDef  = ReferenceTypes::RefUseDef;\n";
  output_c() << "constexpr auto RefHold    = ReferenceTypes::RefHold;\n";
  output_c() << "constexpr auto RefReserve = ReferenceTypes::RefReserve;\n";
  output_c() << "constexpr auto RefFus     = ReferenceTypes::RefFus;\n";

  WritePredicateFunctions(reference_predicates_, &PredicateName,
                          "Predicate functions", output_c());
  WritePredicateFunctions(virtual_ref_predicates_, &VirtualPredicateName,
                          "Virtual predicate functions", output_t());
  WriteVirtualPredicateTable(virtual_ref_predicates_);

  WritePhases(phases_, &PhaseName);
  WritePoolCountFunctions(pooled_count_functions_, &PooledCountFuncName);
  WritePoolValueFunctions(pool_mask_functions_, &PoolValueFuncName);

  // Write out forward references for conditional references.
  output_c() << formatv(
      "{0}// Forward references for conditional references{0}", divider);
  for (auto name : forward_opnd_refs_)
    output_c() << formatv("extern OperandRefVec {0};\n", name);
  if (!forward_opnd_refs_.empty())
    output_c() << "\n";
  for (auto name : forward_cond_opnd_refs_)
    output_c() << formatv("extern ConditionalRef<OperandRef> {0};\n", name);
  if (!forward_cond_opnd_refs_.empty())
    output_c() << "\n";

  for (auto name : forward_resource_refs_)
    output_c() << formatv("extern ResourceRefVec {0};\n", name);
  if (!forward_resource_refs_.empty())
    output_c() << "\n";
  for (auto name : forward_cond_res_refs_)
    output_c() << formatv("extern ConditionalRef<ResourceRef> {0};\n", name);
  if (!forward_cond_res_refs_.empty())
    output_c() << "\n";

  for (auto name : forward_pooled_refs_)
    output_c() << formatv("extern PooledResourceRefVec {0};\n", name);
  if (!forward_pooled_refs_.empty())
    output_c() << "\n";
  for (auto name : forward_cond_pool_refs_)
    output_c() << formatv("extern ConditionalRef<PooledResourceRef> {0};\n",
                          name);
  if (!forward_cond_pool_refs_.empty())
    output_c() << "\n";

  for (auto name : forward_constraint_refs_)
    output_c() << formatv("extern OperandConstraintVec {0};\n", name);
  if (!forward_constraint_refs_.empty())
    output_c() << "\n";
  for (auto name : forward_cond_constraint_refs_)
    output_c() << formatv("extern ConditionalRef<OperandConstraint> {0};\n",
                          name);
  if (!forward_cond_constraint_refs_.empty())
    output_c() << "\n";

  output_c()
      << divider
      << "// Conditional Reference Tables:\n"
         "//     - Predicate function (optional)\n"
         "//     - References (operands, resource or pooled resource refs\n"
         "//     - \"Else\" clause conditional reference (optional)"
      << divider;

  WriteTable(cond_operand_refs_, "ConditionalRef<OperandRef>", "",
             &CondReferenceName, "Conditional Operand Reference Table");
  WriteTable(cond_resource_refs_, "ConditionalRef<ResourceRef>", "",
             &CondResourceReferenceName,
             "Conditional Resource Reference Table");
  WriteTable(cond_pooled_resource_refs_, "ConditionalRef<PooledResourceRef>",
             "", &CondPooledResourceReferenceName,
             "Conditional Pooled Resource Reference Table");
  WriteTable(cond_constraints_, "ConditionalRef<OperandConstraint>", "",
             &CondConstraintName, "Conditional Constraints Table");

  WriteVectorTable(operand_refs_, "OperandRef", &OperandListName,
                   "Operand Reference Table",
                   "\n//     - Resource type(use, def, cond)\n"
                   "//     - Reference flags (protected, unprotected)\n"
                   "//     - Pipeline phase\n"
                   "//     - Pipeline phase function (optional)\n"
                   "//     - Operand index\n"
                   "//   or (for conditional references)\n"
                   "//     - Conditional reference");

  output_c()
      << divider
      << "// Resource Reference Tables:\n"
         "//     - Reference type (use, hold, reserve)\n"
         "//     - Reference flags (protected, unprotected, reserved)\n"
         "//     - Pipeline phase\n"
         "//     - Pipeline phase function (optional)\n"
         "//     - Used cycles\n"
         "//     - Resource id\n"
         "//     - Operand index (for shared resources)\n"
         "//     - Width in bits (for shared resources)\n"
         "//   or (for functional unit descriptors)\n"
         "//     - Reference type (fus)\n"
         "//     - Reference flags (reserved, buffered, begin_group, ...)\n"
         "//     - Used cycles\n"
         "//     - Resource id\n"
         "//     - Number of MicroOps\n"
         "//   or (for \"unitless\" micro-ops)\n"
         "//     - Reference type (fus)\n"
         "//     - Number of MicroOps\n"
         "//   or (for conditional references)\n"
         "//     - Conditional reference"
      << divider;

  WriteVectorTable(used_resource_refs_, "ResourceRef", &UsedResourceListName,
                   "Used Resource Reference Table");
  WriteVectorTable(held_resource_refs_, "ResourceRef", &HeldResourceListName,
                   "Held Resource Reference Table");
  WriteVectorTable(reserved_resource_refs_, "ResourceRef",
                   &ReservedResourceListName,
                   "Reserved Resource Reference Table");

  WriteTable(resource_groups_, "ResourceIdType", "[]", &ResourceGroupName,
             "Resource Group Table");

  auto pool_descriptor_table =
      "\n"
      "//      pool_id -    which pool to allocate from\n"
      "//      pool_size -  how many different allocations sizes\n"
      "//      count -      how many pool elements we need\n"
      "//      pool_func -  optional pointer to pool size func\n"
      "//      value_func - optional pointer to pool values func\n"
      "//      first -      index of first legal element id\n"
      "//      last -       index of last legal element id\n"
      "//      width -      width in bits";

  WriteTable(pool_descriptors_, "PoolDescriptor", "", &PoolDescriptorName,
             "Pool Descriptor Table", pool_descriptor_table);

  auto pooled_refs_header =
      "\n"
      "//     - Resource type (use, hold, reserve)\n"
      "//     - Reference flags (protected, unprotected, reserved\n"
      "//     - Pipeline phase\n"
      "//     - Pipeline phase function (optional)\n"
      "//     - Used cycles\n"
      "//     - Resource group\n"
      "//     - Operand index (for shared resources)\n"
      "//     - Pool descriptor\n"
      "//   or (for functional unit descriptors)\n"
      "//     - Reference type (fus)\n"
      "//     - Reference flags (reserved, buffered, begin_group, ...)\n"
      "//     - Used cycles\n"
      "//     - Group id\n"
      "//     - Pool id\n"
      "//     - Number of MicroOps";

  WriteVectorTable(pooled_resource_refs_, "PooledResourceRef",
                   &PooledResourceListName, "Pooled Resource Reference Table",
                   pooled_refs_header);

  auto constraint_table_header = "\n"
                                 "//     - Operand Index\n"
                                 "//     - Constraint id\n"
                                 "//   or (for conditional reference)\n"
                                 "//     - Conditional constraint name\n";

  WriteVectorTable(constraints_, "OperandConstraint", &ConstraintListName,
                   "Operand Constraint Table", constraint_table_header);

  WriteVectorTable(subunits_, "Subunit", &SubunitListName, "Subunit Table");

  WriteInstructionTables();
  WriteInstructionInfo();
  GenerateForwardingInfo();

  WriteResourceDefinitions();      // Write out resource names
  WriteCpuList();

  output_c() << formatv("}  // namespace {0}\n}  // namespace llvm\n\n",
                        database_->spec().family_name());
  output_t() << formatv("}  // namespace {0}\n}  // namespace llvm\n\n",
                        database_->spec().family_name());
}

} // namespace mdl
} // namespace mpact

//===- mdl_generate.cpp - Generate the mdl database -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These functions perform the second half of the architecture expansion,
// where we specialize each generated subunit instance for each instruction.
// From this, we can generate the internal representation of the complete
// database for the spec.
//
// The general algorithm for this is:
//
//    for each llvm instruction description:
//       for each subunit it qualifies for:
//          for each specialized instance of that subunit:
//             further specialize the subunit for the current instruction
//             add the final specialized subunit to the instruction definition
//
// After this pass, each instruction will have a set of subunit instances,
// each of which has the following information:
//   - a cpu and functional unit combination the instruction can run on,
//   - the resources it uses (on that cpu/functional unit), and when,
//   - any resource pool requirements for the instruction,
//   - the latencies of all operand reads and writes,
//   - any CPU and/or Functional unit register constraints imposed on the ins.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "mdl_output.h"

namespace mpact {
namespace mdl {

// Check that each operand in the instruction is mentioned in at least
// one reference record. Unmentioned operands are a likely error.
void InstrInfo::CheckUnreferencedOperands(bool check_all_operands) {
  std::set<int> referenced_operands;
  for (auto *ref : *references_)
    if (ref->operand())
      referenced_operands.insert(ref->operand()->operand_index());

  for (unsigned op_id = 0; op_id < instruct_->flat_operands()->size(); op_id++)
    if ((check_all_operands || instruct_->GetOperandDecl(op_id)->reg_class()) &&
        !referenced_operands.count(op_id))
      subunit_->WarningLog(
          instruct_, "Operand{0}in instruction \"{1}\" is unreferenced",
          StringVec(instruct_->GetOperandDecl(op_id)->op_names(), " \"", ".",
                    "\" "),
          instruct_->name());
}

// Stringify a InstrInfo object, suitable for writing debug information.
std::string InstrInfo::ToString() const {
  std::string out = formatv("{0}    Subunit: {1}.{2}\n", instruct_->ToString(),
                            subunit_->func_unit()->cpu()->name(),
                            subunit_->func_unit()->instance()->name());

  out += "      Operand references:\n";
  for (auto *ref : *references_)
    if ((ref->operand() && ref->IsOperandRefType()) || ref->IsConditionalRef())
      out += formatv("      ===>  {0}\n", ref->ToString());

  if (!resource_refs_.empty())
    out += "      FU references:\n";
  for (auto *ref : resource_refs_)
    if (ref->IsFuncUnitRef())
      out += formatv("      --->  {0}\n", ref->ToString());

  if (resources_.empty())
    return out;
  out += "      Resources:\n";
  for (auto &res : resources_)
    if (!res.resource()->HasCount())
      out += formatv("            {0}\n", res.ToString());

  out += "      Pool Resources:\n";
  for (auto &res : resources_)
    if (res.resource()->HasCount()) {
      out += formatv("            {0} ", res.ToString());
      SubPool subpool(res.resource());
      auto &subpool_info = res.resource()->definition()->sub_pool(subpool);
      out += formatv(" subpool id: {0}", subpool_info.subpool_id());
      out += " size requests: ";
      auto comma = "";
      for (auto request : subpool_info.counts()) {
        out += formatv("{0}{1}", comma, request);
        comma = ",";
      }
      out += "\n";
    }

  out += "      Architectural Register Constraints:\n";
  for (auto *ref : *references_)
    if (auto *opnd = ref->operand())
      if (auto *port = ref->port())
        if (auto *reg_class = port->reg_class())
          out += formatv("            operand {0}: {1}\n",
                         opnd->operand_index(), reg_class->name());
  return out;
}

//----------------------------------------------------------------------------
// Find all functional-unit-related resources associated with this subunit
// instance, including the implied functional unit resource (and its bases).
// Note that we don't include any explicit functional units specified by an
// fus() clause, these are handled separately.
//----------------------------------------------------------------------------
void GetFuncUnitResources(SubUnitInstantiation *subunit, ResourceSets &res_set,
                          PhaseName *phase) {
  // Add all the implicit functional unit resources, including parent fu ids.
  // Note: Each fu is added as an independent resource in the res set.
  auto fu_resources = subunit->GetFuncUnitResources();
  for (auto *def : fu_resources) {
    // Don't write out catchall units or unreserved functional units.
    if (is_catchall_name(def->name()) ||
        def->get_fu()->instance()->is_unreserved())
      continue;
    auto *fu = new ResourceRef(def);
    ResourceEvent fu_res(RefTypes::kUse, new PhaseExpr(phase), 1, fu);
    std::vector<ResourceEvent> items{fu_res};
    res_set.push_back(items);
  }
}

//----------------------------------------------------------------------------
// Build a resource set for this instruction instance.
//----------------------------------------------------------------------------
ResourceSets
InstructionDatabase::BuildResourceSets(ResourceList &resources,
                                       SubUnitInstantiation *subunit) {
  ResourceSets res_set;
  for (auto &item : resources) {
    std::vector<ResourceEvent> items;

    // Resource pools can be managed along with "Normal" resources, or
    // separately. Its more computationally expensive at compile time to manage
    // them with other resources ((N*P)^2 vs (N^2 + P^2)), but vastly more
    // convenient to consider them together.  So if N+P is "small enough", we
    // should just add pooled resources in with the normal resources (which the
    // code below will accomplish).  But currently I don't think thats typically
    // the case, so for now I've just commented this (working) code out.  We'll
    // get much fewer resource sets, and have to model all pools in the compiler
    // at compile time (which will typically be much faster).
    // TODO(tbd) - Determine what kinds of pools this makes sense for.
#if 0
    // Handle resource pools.
    ResourceRef *ref = item.resource();
    if (ref->IsPool() && !has_shared_bits() &&
        (ref->last() - ref->first()) < 4  &&
        (ref->HasCount() == 1 || ref->operand_index() == -1)) {
      for (int id = ref->first(); id <= ref->last(); id += ref->pool_count()) {
        auto *newref = new ResourceRef(*ref);
        newref->set_subrange(id, id + ref->pool_count() - 1);
        newref->set_pool_count(-1);
        ResourceEvent newevent(item.ref_type(), item.phase_expr(), newref);
        items.push_back(newevent);
      }
      res_set.push_back(items);
      continue;
    }
#endif
#if 0
    // Handle resource groups.
    if (item.resource()->IsGroupRef() && !has_shared_bits() &&
        item.resource()->definition()->members().size() < 4) {
      for (auto mem : *item.resource()->definition()->member_defs()) {
        auto *newref = new ResourceRef(*mem);
        ResourceEvent newevent(item.ref_type(), item.phase_expr(), newref);
        items.push_back(newevent);
      }
      res_set.push_back(items);
      continue;
    }
#endif

    // Handle single resource items.
    items.push_back(item);
    res_set.push_back(items);
  }

  // We need to identify a "default" phase to use for implicit resources.
  auto *phase0 = spec_.FindFirstPhase();

  // Add all the implicit functional unit resources, including parent fu ids.
  // Note: Each fu is added as an independent resource in the res set.
  GetFuncUnitResources(subunit, res_set, phase0);

  // If we determined we don't need to model resource slots, we're done.
  auto *cpu = subunit->cpu();
  if (!cpu->needs_slot_resources())
    return res_set;

  // Add "any" slot resources associated with this functional unit instance.
  // Note: we add all of them as a single pooled entry in the res_set.
  auto *slots_any = subunit->GetSlotResourcesAny();
  if (slots_any && !slots_any->empty()) {
    std::vector<ResourceEvent> items;
    for (auto *pin_any : *slots_any) {
      auto *slot = new ResourceRef(*pin_any);
      auto *phase = phase0;
      if (slot->definition()->start_phase())
        phase = spec_.FindPipeReference(slot->definition()->start_phase(), cpu);
      items.emplace_back(RefTypes::kUse, new PhaseExpr(phase), slot);
    }
    res_set.push_back(items);
  }

  // Add "all" slot resources associated with this functional unit instance.
  // Note: Each slot is added as an independent resource in the res_set.
  // TODO: we might want to try just doing pooled allocations for these - all
  // the mechanism exists, we just need to create a pooled allocation
  // ResourceEvent, and the backend does the rest.
  auto *slots_all = subunit->GetSlotResourcesAll();
  if (slots_all && !slots_all->empty()) {
    for (auto *pin_all : *slots_all) {
      auto *slot = new ResourceRef(*pin_all);
      auto *phase = phase0;
      if (slot->definition()->start_phase())
        phase = spec_.FindPipeReference(slot->definition()->start_phase(), cpu);
      ResourceEvent pin_res(RefTypes::kUse, new PhaseExpr(phase), slot);
      std::vector<ResourceEvent> items{pin_res};
      res_set.push_back(items);
    }
  }

  return res_set;
}

//----------------------------------------------------------------------------
// Build a set of all possible resource combinations found in the input
// resource set.
//----------------------------------------------------------------------------
void BuildResourceCombos(ResourceSets &res_set, unsigned index,
                         std::vector<ResourceEvent> &current,
                         ResourceSets &result) {
  if (index == res_set.size()) {
    result.push_back(current);
    return;
  }

  for (auto &resource : res_set[index]) {
    current.push_back(resource);
    BuildResourceCombos(res_set, index + 1, current, result);
    current.pop_back();
  }
}

//----------------------------------------------------------------------------
// Annotate phase expressions with instruction-specific operand information.
//----------------------------------------------------------------------------
void AnnotatedPhaseExpr(const InstructionDef *instr, PhaseExpr *expr,
                        MdlSpec &spec, CpuInstance *cpu) {
  if (expr->operation() == kOpnd) {
    int index = spec.GetOperandIndex(instr, expr->operand(), RefTypes::kUse);
    expr->operand()->set_operand_index(index);
    expr->operand()->set_operand_decl(instr->GetOperandDecl(index));
    return;
  }
  if (expr->operation() == kPhase) {
    spec.SpecializePhaseExpr(expr, cpu);
    return;
  }
  if (expr->left())
    AnnotatedPhaseExpr(instr, expr->left(), spec, cpu);
  if (expr->right())
    AnnotatedPhaseExpr(instr, expr->right(), spec, cpu);
}

//----------------------------------------------------------------------------
// Annotate a reference with instruction-specific operand information.
//----------------------------------------------------------------------------
Reference *AnnotatedReference(const InstructionDef *instr, Reference *ref,
                              int delay, MdlSpec &spec, CpuInstance *cpu) {
  auto *newref = new Reference(*ref, delay); // Make a private copy.
  ref->set_used(); // Note that we used this reference.
  if (newref->operand()) {
    int index = spec.GetOperandIndex(instr, newref->operand(), ref->ref_type());
    newref->operand()->set_operand_index(index);
    newref->operand()->set_operand_decl(instr->GetOperandDecl(index));
  }

  AnnotatedPhaseExpr(instr, newref->phase_expr(), spec, cpu);
  newref->SetConstantPhase(); // Evaluate constant phase expressions.
  return newref;
}

// Return true if two register class have any common registers.
bool classes_overlap(const RegisterDefList *a, const RegisterDefList *b) {
  for (auto *item_a : *a)
    for (auto *item_b : *b)
      if (item_a->name() == item_b->name())
        return true;
  return false;
}

// Return true if any of any instruction's port constraints are incompatible
// with operand constraints.  This is a nice optimization to prune subunits
// whose port constraints are incompatible with an instruction's operand
// constraints.  It is ok to be conservative. We skip conditional references
// since the predicates could impact whether a reference is used or not.
bool HasIncompatibleConstraints(const ReferenceList *references) {
  for (const auto *ref : *references)
    if (!ref->IsConditionalRef())
      if (const auto *opnd = ref->operand())
        if (const auto op_decl = opnd->operand_decl())
          if (const auto *op_class = op_decl->reg_class())
            if (const auto *port = ref->port())
              if (const auto *pclass = port->reg_class())
                if (!classes_overlap(pclass->members(), op_class->members()))
                  return true;
  return false;
}

// Filter conditional references, and return a filtered copy.
// If there's no predicate, just filter the refs (this is an "else" clause).
// if there's a predicate, evaluate it:
//   - if its true, filter and return its associated reference list.
//   - if its false, filter and return its "else" clause (if there is one)
//   - if it cannot be fully evaluated, create a copy with filtered references
//     and else clause.
ConditionalRef *InstructionDatabase::FilterConditionalRef(
    const InstructionDef *instr, ConditionalRef *cond, CpuInstance *cpu) {
  // If the entire clause is missing, just return nullptr.
  if (cond == nullptr)
    return nullptr;
  auto *pred = cond->predicate();
  // If the predicate is missing, it evaluates to True, so return filtered refs.
  if (pred == nullptr)
    return new ConditionalRef(
        *cond, nullptr, FilterReferences(instr, cond->refs(), cpu), nullptr);

  // Evaluate the predicate, and if it is True or False, we can simplify.
  auto *value = spec_.EvaluatePredicate(pred->name(), instr);
  if (value->IsTrue())
    return new ConditionalRef(
        *cond, nullptr, FilterReferences(instr, cond->refs(), cpu), nullptr);
  if (value->IsFalse())
    return FilterConditionalRef(instr, cond->else_clause(), cpu);

  // If we can't completely evaluate the predicate, create a copy, filter its
  // references, and recur on its else clause.
  auto *then_clause = FilterReferences(instr, cond->refs(), cpu);
  auto *else_clause = FilterConditionalRef(instr, cond->else_clause(), cpu);
  auto *newcond = new ConditionalRef(*cond, pred, then_clause, else_clause);
  newcond->SetInstrPredicate(value);
  return newcond;
}

// Given a list of references, determine if each reference is valid for the
// specified instruction.  Return a filtered list of references.
ReferenceList *InstructionDatabase::FilterReferences(
    const InstructionDef *instr, ReferenceList &candidates, CpuInstance *cpu) {
  auto *refs = new ReferenceList;

  for (auto *ref : candidates) {
    ref->set_seen(); // Note that we've seen a possible reference to this.

    // If it's not a conditional reference, check to see if its valid for
    // this instruction, and if it is valid add to the reference list.
    // Expand WriteSequences to discrete references.
    if (!ref->IsConditionalRef()) {
      if (IsReferenceValid(instr, ref)) {
        refs->push_back(AnnotatedReference(instr, ref, 0, spec_, cpu));
        int delay = ref->delay();
        for (int repeat = 1; repeat < ref->repeat(); repeat++) {
          refs->push_back(AnnotatedReference(instr, ref, delay, spec_, cpu));
          delay += ref->delay();
        }
      }
      continue;
    }

    // Recursively filter each conditional reference.  If the result is a
    // single unconditional ConditionalRef object, just add all of its
    // references to the list. Otherwise, add the conditional reference to
    // the list.
    auto *cond = FilterConditionalRef(instr, ref->conditional_ref(), cpu);
    if (cond == nullptr)
      continue;
    if (cond->predicate() != nullptr)
      refs->push_back(new Reference(*cond, nullptr, cond));
    else
      refs->insert(refs->end(), cond->refs().begin(), cond->refs().end());
  }

  return refs;
}

//----------------------------------------------------------------------------
// If there are unreferenced output operands in an instruction, we need at
// least one Def in the database to use for the default latency.  This is
// a hack to handle cases where LLVM has unmentioned defs or dynamically adds
// defs to an instruction instance. Scan a reference list looking for the
// kFu with the largest latency, and create a default def for that list.
// These will provide the compiler with "default" def phases.
// Return the number of default defs inserted.
//----------------------------------------------------------------------------
int AddDefaultDefs(ReferenceList &refs, CpuInstance *cpu, MdlSpec &spec) {
  ReferenceList defs;
  int count = 0;

  // Scan conditional reference list for defs.
  for (auto *ref : refs) {
    for (auto *cond = ref->conditional_ref(); cond; cond = cond->else_clause())
      count += AddDefaultDefs(cond->refs(), cpu, spec);
  }

  // Scan the references looking for the latest Def or Use. If no defs are
  // found, add a default def.
  Reference *latest = nullptr;
  Reference *latest_def = nullptr;
  int latest_latency = -1;

  for (auto *ref : refs) {
    if (ref->IsDef() || ref->IsUse()) {
      if (ref->phase_expr()->IsExpressionConstant()) {
        auto latency = ref->phase_expr()->EvaluateConstantExpression();
        if (!latency) {
          spec.ErrorLog(ref, "Invalid latency: divide by zero");
          continue;
        }
        if (latest == nullptr || *latency > latest_latency ||
            (*latency == latest_latency && latest->IsDef())) {
          latest = ref;
          latest_latency = *latency;
        }
      }
    }
  }

  // If we haven't seen a def, create a default reference, either the latest
  // Use, or the first execute phase for the CPU.
  if (latest_def == nullptr) {
    auto *opnd = new OperandRef("<default_operand>");
    if (latest)
      refs.push_back(new Reference(RefTypes::kDef, latest->phase_expr(), opnd));
    else
      refs.push_back(
          new Reference(RefTypes::kDef, spec.FindFirstExecutePhase(cpu), opnd));
    count++;
  }
  return count;
}

//----------------------------------------------------------------------------
// Given an instruction and a reference list, create a set of referenced
// operand indexes.
//----------------------------------------------------------------------------
void InstructionDatabase::FindCondReferencedOperands(
    const InstructionDef *instr, ConditionalRef *cond, CpuInstance *cpu,
    std::set<int> &found) {
  if (cond == nullptr)
    return;
  FindReferencedOperands(instr, &cond->refs(), cpu, found);
  FindCondReferencedOperands(instr, cond->else_clause(), cpu, found);
}

void InstructionDatabase::FindReferencedOperands(const InstructionDef *instr,
                                                 ReferenceList *refs,
                                                 CpuInstance *cpu,
                                                 std::set<int> &found) {
  if (refs == nullptr)
    return;
  for (const auto *ref : *refs) {
    if (ref->IsConditionalRef())
      FindCondReferencedOperands(instr, ref->conditional_ref(), cpu, found);
    else if (ref->operand() != nullptr)
      found.insert(ref->operand()->operand_index());
  }
}

//----------------------------------------------------------------------------
// Create pool names for functional unit pools and groups.  This allows us to
// manage and reuse these pools. We sort and concatenate the names to create
// a canonical name.
//----------------------------------------------------------------------------
std::string FuPoolName(ResourceRef *res, CpuInstance *cpu, MdlSpec &spec) {
  std::set<std::string> names;
  if (spec.IsFuncUnitTemplate(res->name())) {
    auto &funits = cpu->func_unit_instances()[res->name()];
    if (funits.size() == 1) return res->name();

    for (auto *funit : funits)
      names.insert(funit->get_root_resource()->name());
    return StringSet(&names, "", ":", "");
  }
  if (spec.IsFuncUnitGroup(res->name())) {
    auto *group = spec.fu_group_map()[res->name()];
    for (auto *fu_template : group->fu_members()) {
      auto &funits = cpu->func_unit_instances()[fu_template->name()];
      for (auto *funit : funits)
        names.insert(funit->get_root_resource()->name());
    }
    return StringSet(&names, "", ":", "");
  }
  return "";     // Can't get here...
}

//----------------------------------------------------------------------------
// Helper for recording fu references.
//----------------------------------------------------------------------------
void InstructionDatabase::RecordConditionallyUsedFus(
                ConditionalRef *cond, SubUnitInstantiation *subunit) {
  RecordUsedFus(cond->refs(), subunit);
  if (cond->else_clause())
    RecordConditionallyUsedFus(cond->else_clause(), subunit);
}

//----------------------------------------------------------------------------
// Look for FU references in resource lists, and mark those FUs as used, so
// that they will be assigned resource ids.
//----------------------------------------------------------------------------
void InstructionDatabase::RecordUsedFus(ReferenceList &resource_refs,
                                        SubUnitInstantiation *subunit) {
  auto *cpu = subunit->cpu();
  for (auto *ref : resource_refs) {
    if (ref->IsConditionalRef()) {
      RecordConditionallyUsedFus(ref->conditional_ref(), subunit);
      continue;
    }
    if (!ref->IsFuncUnitRef()) continue;

    // Functional unit references that reference more than one FU are split up
    // into multiple refs, so this loop will only iterate once. We need to
    // handle three cases:
    // - a reference to a single functional unit template,
    // - a reference to an FU template that has multiple instances,
    // - a reference to a functional unit group.
    for (auto *res : *ref->resources()) {
      ResourceDef *pool = nullptr;
      // Handle the case of a functional unit template.
      if (spec_.IsFuncUnitTemplate(res->name())) {
        auto &funits = cpu->func_unit_instances()[res->name()];

        // If there's just a single unit, record it as used.
        if (funits.size() == 1) {
          auto *def = funits[0]->get_root_resource();
          def->RecordFuReference();
          ref->set_buffer_size(funits[0]->instance()->buffer_size(res->name()));
          continue;
        }

        // If there are multiple instances of the functional unit, create a
        // create a pool of all instances of that template. If the pool
        // already exists, use the existing one.
        auto name = FuPoolName(res, cpu, spec_);
        if (auto *pool = FindItem(cpu->all_resources(), name)) {
          res->set_fu_pool(pool);
          ref->set_buffer_size(funits[0]->instance()->buffer_size(res->name()));
          continue;
        }
        // Create a new ResourceDef Pool, and add it to the CPUs list of
        // FU pools (so it can be reused).
        std::set<int> buffer_sizes;
        int size = -1;
        pool = new ResourceDef(name);
        for (auto *funit : funits) {
          auto *def = funit->get_root_resource();
          pool->add_member_def(def);
          pool->members().push_back(def->id());
          size = funit->instance()->buffer_size(res->name());
          buffer_sizes.insert(size);    // count different sizes
          def->RecordFuReference();
        }
        if (buffer_sizes.size() > 1)
          spec_.WarningLog(ref, "Inconsistent functional unit buffer sizes");
        ref->set_buffer_size(size);
      }

      // If the reference is a functional unit group, create a pool of all
      // instances of all group members.
      else if (spec_.IsFuncUnitGroup(res->name())) {
        auto name = FuPoolName(res, cpu, spec_);
        auto *group = spec_.fu_group_map()[res->name()];
        if (auto *pool = FindItem(cpu->all_resources(), name)) {
          res->set_fu_pool(pool);
          ref->set_buffer_size(group->buffer_size());
          continue;
        }
        // Create a new ResourceDef Pool, and add it to the CPUs list of
        // FU pools (so it can be reused), and annotate the resource reference.
        pool = new ResourceDef(name);
        ref->set_buffer_size(group->buffer_size());
        for (auto *fu_template : group->fu_members()) {
          auto &funits = cpu->func_unit_instances()[fu_template->name()];
          for (auto *funit : funits) {
            auto *def = funit->get_root_resource();
            pool->add_member_def(def);
            pool->members().push_back(def->id());
            def->RecordFuReference();
          }
        }
      }
      else continue;     // Skip CPU entries

      // Add the new pool to the CPU's pool table, and annotate the reference.
      pool->add_alloc_size(1);
      pool->RecordReference(RefTypes::kFus, ref->phase_expr(), nullptr,
                                ref, subunit);
      cpu->add_cpu_resource(pool, "FuncPool", nullptr, nullptr, nullptr);
      cpu->add_fu_pool_size(pool->members().size());
      res->set_fu_pool(pool);         // Annotate the fu reference.

      auto *pool_ref = new ResourceRef(pool);
      pool_ref->set_pool_count(1);
      pool->AddReferenceSizeToPool(pool_ref, ref, subunit);
    }
  }
}

//----------------------------------------------------------------------------
// Find unreferenced output operands, and create default references for them.
//----------------------------------------------------------------------------
void InstructionDatabase::AddUnreferencedOperandDefs(
    const InstructionDef *instr, ReferenceList *refs, CpuInstance *cpu) {
  // First find all the referenced operands.
  std::set<int> referenced_opnds;
  FindReferencedOperands(instr, refs, cpu, referenced_opnds);

  // Find register operands that have no references, create a vector of them.
  std::vector<OperandRef *> opnds;
  for (unsigned op_id = 0; op_id < instr->flat_operands()->size(); op_id++)
    if (referenced_opnds.count(op_id) == 0) {
      auto *opnd = instr->GetOperandDecl(op_id);
      if (opnd->is_input())
        continue;
      auto *back = opnd->types()->back();
      auto *front = opnd->types()->front();

      // See if the operand declaration is a register or a register class.
      if (auto *rclass = FindItem(spec_.reg_classes(), back->name())) {
        opnds.push_back(new OperandRef(front, opnd->op_names(), rclass, op_id));
      } else if (FindItem(spec_.registers(), back->name()) != nullptr) {
        opnds.push_back(new OperandRef(nullptr, new IdList(1, back), op_id));
      }
    }

  // If we found unreferenced output operands, add "default" defs to
  // represent the worst-case pipeline phase for unspecified defs.
  // If only one default operand was added, and it's the last item in the
  // reference list, just use its latency and remove it from the list.
  PhaseExpr *def_latency = nullptr;
  if (!opnds.empty() && AddDefaultDefs(*refs, cpu, spec_) == 1 &&
      refs->back()->IsDefaultOperandRef()) {
    def_latency = refs->back()->phase_expr();
    refs->pop_back();
  } else {
    def_latency = PhaseExpr::DefaultLatency();
  }

  // We found unreferenced register-based output operands, so create a
  // references for them.
  for (auto *opnd : opnds)
    refs->push_back(new Reference(RefTypes::kDef, def_latency, opnd));
}

//----------------------------------------------------------------------------
// Generate all instruction information records for a target instruction.
// Instructions can have more than one subunit.  If so, instantiate them all.
//----------------------------------------------------------------------------
void InstructionDatabase::GenerateInstructionInfo(InstructionDef *instr) {
  // For each subunit, create reference records for this instruction.
  for (auto *subunit : *instr->subunits())
    for (auto *unit : *spec_.su_instantiations()[subunit->name()]) {
      // Mark this subunit as used.
      spec().su_map()[subunit->name()]->inc_use();

      // Create a list of valid references for this subunit.
      // Check each reference to see if its valid for this instruction.
      auto *cpu = unit->cpu();
      auto *refs = FilterReferences(instr, unit->references(), cpu);

      // Check each reference for incompatible constraints imposed
      // by ports. These are not valid subunits, and we don't want to add
      // this subunit instance to the database.
      if (HasIncompatibleConstraints(refs))
        continue;

      // Sort the references by pipeline phase. This is primarily to order
      // operand references by type and phase for cosmetic reasons.
      std::stable_sort(
          refs->begin(), refs->end(),
          [](const Reference *a, const Reference *b) { return *a < *b; });

      // Add defs for unreferenced register operand defs.  This isn't
      // necessary, so its currently disabled.
      // AddUnreferencedOperandDefs(instr, refs, cpu);

      // Given a list of validated references, create a list of events for
      // unconditional resource references. At this point, we don't add
      // FUs and conditional refs - these are added later for each combination
      // of unconditional resource refs.
      // For each port reference, add it and its associated resources.
      // For each pooled reference, annotate it with its operand index.
      // For resources associated with operands:
      //      - they are always "used".
      //      - tag the resource reference with its associate operand index.
      //      - If the resource has a defined cycle id, use it.
      ReferenceList resource_refs;
      ResourceList resources;
      for (auto *ref : *refs) {
        // Don't add functional unit and conditional references, just add them
        // to the resource reference list for this instruction/subunit.
        if (ref->IsFuncUnitRef() ||
            (ref->IsConditionalRef() &&
             ref->conditional_ref()->HasResourceRefs())) {
          resource_refs.push_back(ref);
          continue;
        }
        // Add all other resource references.
        auto ref_type = ref->AdjustResourceReferenceType();
        for (auto *res : *ref->resources()) {
          if (!res->IsNull()) {
            PhaseExpr *phase = ref->phase_expr();

            // If the resource definition has a specified phase, use it instead.
            if (auto *start = res->definition()->start_phase())
              phase = new PhaseExpr(spec_.FindPipeReference(start, cpu));
            if (ref->operand())
              res->set_operand_index(ref->operand()->operand_index());
            resources.emplace_back(ref_type, phase, ref->use_cycles(), res, ref,
                                   unit);
          }
        }
      }

      // Create sets of reference resource combinations.
      ResourceSets res_set = BuildResourceSets(resources, unit);
      ResourceList current;
      ResourceSets resource_combos;
      BuildResourceCombos(res_set, 0, current, resource_combos);

      // Find FU references, mark them as used.  If pooled, add them to
      // the CPU's resource pool list.
      RecordUsedFus(resource_refs, unit);

      //----------------------------------------------------------------------
      // AND FINALLY: For the current instruction, for each subunit, for each
      // resource combination, create an instruction record that captures all
      // of this information and add it to the instruction database.
      //----------------------------------------------------------------------
      for (auto &res : resource_combos) {
        auto *new_inst = new InstrInfo(instr, unit, res, refs, resource_refs);
        instruction_info_[instr->name()].push_back(new_inst);
      }
    }
}

//----------------------------------------------------------------------------
// Dump everything we know about all the instructions.
//----------------------------------------------------------------------------
void InstructionDatabase::DumpInstructions() {
  std::cout << "\n---------------------------------------------------------\n";
  std::cout << " Instruction info for \"" << file_name_ << "\"";
  std::cout << "\n---------------------------------------------------------\n";

  // Debug: dump out all the instruction information we've generated.
  for (auto &instruct_list : instruction_info_)
    for (auto &instruct : instruct_list.second)
      instruct->dump();
}

//----------------------------------------------------------------------------
// Start the process of generating the final instruction information.
//----------------------------------------------------------------------------
InstructionDatabase::InstructionDatabase(std::string directory_name,
                                         std::string file_name,
                                         bool gen_missing_info, MdlSpec &spec)
    : directory_name_(directory_name), file_name_(file_name),
      gen_missing_info_(gen_missing_info), spec_(spec) {
  // Add all the target instructions to the instruction database.
  for (auto *instr : spec.instructions())
    if (!instr->subunits()->empty())
      GenerateInstructionInfo(instr);
}

//----------------------------------------------------------------------------
// Write out the entire database to the output C++ file.
//----------------------------------------------------------------------------
void InstructionDatabase::Write(bool generate_llvm_defs) {
  OutputState output(this, generate_llvm_defs);
  output.WriteHeader();
  output.WriteLLVMDefinitions();
  // output.WriteResourceDefinitions();
  output.WriteCpuTable();
  output.WriteExterns();
  output.WriteTrailer();
}

} // namespace mdl
} // namespace mpact

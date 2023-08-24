//===- mdl_util.cpp - Miscellaneous utilities for MDL error checking ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definition of methods that do basic semantic checking on the input mdl.
//    - Check for duplicate definitions (templates, resources, etc)
//    - For each template, make sure its bases exist and have compatible
//      parameters.
//    - For each instantiation, make sure its template exists and has
//      compatible parameters.
//
// As part of error checking, we link various components together so that
// later passes don't have to repeatedly perform name lookups:
//    - link (fu/su/lat) instances to their associated templates.
//    - link instance arguments to their associated template parameters.
//    - link templates to their base templates.
//
// Note that we don't do ALL name lookups here, since this is done in
// template instantiation.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "mdl.h"
#include "llvm/Support/Error.h"

namespace mpact {
namespace mdl {

// Create some "Null" resource definitions.
ResourceRef *NullResourceRef = nullptr;
RegisterClass *NullRegisterClass = nullptr;
ResourceDef *NullPortDef = nullptr;

//----------------------------------------------------------------------------
// Basic error logging.
//----------------------------------------------------------------------------
void Abort() {
  llvm::errs() << "Errors found, aborting\n";
  exit(EXIT_FAILURE);
}

void MdlSpec::WriteMessage(const MdlItem *item, const std::string &msg) {
  if (item == nullptr) {
    llvm::errs() << formatv("Error: {0}\n", msg);
    return;
  }
  std::string message = formatv("{0} {1}", item->Location(), msg);
  // If we've already see this exact message, don't print it out again!
  // This is fairly common, since latencies and subunits are potentially
  // instantiated many times.
  if (!error_messages_.count(message)) {
    error_messages_.insert(message);
    llvm::errs() << message << "\n";
  }
}

int SubUnitInstantiation::ErrorsSeen() const { return spec()->ErrorsSeen(); }
int FuncUnitInstantiation::ErrorsSeen() const { return spec()->ErrorsSeen(); }

//----------------------------------------------------------------------------
// Pipeline Phase expression methods.
//----------------------------------------------------------------------------
// Find the phase reference in an expression and return it.
PhaseName *PhaseExpr::GetPhaseName() const {
  PhaseName *name;
  if (operation() == kPhase)
    return phase_name_;
  if (left() && (name = left()->GetPhaseName()))
    return name;
  if (right() && (name = right()->GetPhaseName()))
    return name;
  return nullptr;
}

// Early check to see if there's at least one symbolic name in a phase expr.
bool PhaseExpr::HasPhaseName() const {
  if (operation() == kPhase)
    return true;
  if (left() && left()->HasPhaseName())
    return true;
  if (right() && right()->HasPhaseName())
    return true;
  return false;
}

// Return true if an expression doesn't contain any operand references.
bool PhaseExpr::IsExpressionConstant() const {
  if (left() && !left()->IsExpressionConstant())
    return false;
  if (right() && !right()->IsExpressionConstant())
    return false;
  return operation() != kOpnd;
}

// Evaluate a constant expression.
llvm::ErrorOr<int> PhaseExpr::EvaluateConstantExpression() const {
  // Recur on children, return error if either has errors.
  llvm::ErrorOr<int> left_value = 0, right_value = 0;
  if (left()) {
    left_value = left()->EvaluateConstantExpression();
    if (!left_value)
      return left_value;
  }
  if (right()) {
    right_value = right()->EvaluateConstantExpression();
    if (!right_value)
      return right_value;
  }

  if (operation() == kPlus)
    return *left_value + *right_value;
  if (operation() == kMinus)
    return *left_value - *right_value;
  if (operation() == kMult)
    return *left_value * *right_value;
  // Return an error for division by zero.
  if (operation() == kDiv) {
    if (*right_value == 0)
      return llvm::errc::invalid_argument;
    return *left_value / *right_value;
  }
  if (operation() == kNeg)
    return -*left_value;
  if (operation() == kPositive)
    return std::max(0, *left_value);
  if (operation() == kInt)
    return number();
  if (operation() == kPhase)
    return phase_id();
  return llvm::errc::invalid_argument;  // Cannot reach here
}

//----------------------------------------------------------------------------
// Make sure the overall specification has some basic components defined.
// We do this rather late in the semantic checking phase, because some of
// the components are auto-generated during semantic checking.
//----------------------------------------------------------------------------
void MdlSpec::CheckInputStructure() {
  if (family_name_.empty())
    ErrorLog(nullptr, "Specify a processor family name");
  if (cpus_.empty())
    ErrorLog(nullptr, "Specify at least one cpu definition");
  if (pipe_phases_.empty())
    ErrorLog(nullptr, "Specify at least one pipeline definition.");
  if (func_units_.empty())
    ErrorLog(nullptr, "Specify at least one functional unit definition.");
  if (subunits_.empty())
    ErrorLog(nullptr, "Specify at least one subunit definition.");
  if (latencies_.empty())
    ErrorLog(nullptr, "Specify at least one latency definition.");
  if (instructions_.empty())
    ErrorLog(nullptr, "Specify at least one instruction definition.");

  if (ErrorsSeen())
    Abort();
}

//----------------------------------------------------------------------------
// Initialize some global subobjects of the MdlSpec object:
// - Create dictionaries for functional units, subunits, latencies, operands,
//   instructions, groups, and register classes.  Check for duplicates.
// - Build instantiation objects for subunits.
// - Initialize some global null object descriptors.
//----------------------------------------------------------------------------
void MdlSpec::BuildDictionaries() {
  for (auto *fu : func_units())
    if (!fu_map().emplace(fu->name(), fu).second)
      ErrorLog(fu, "Duplicate functional unit definition: {0}", fu->name());

  // Group names cannot conflict with themselves or functional unit names.
  for (auto *group : func_unit_groups()) {
    if (!fu_group_map().emplace(group->name(), group).second)
      ErrorLog(group, "Duplicate functional unit group definition: {0}",
               group->name());
    if (FindItem(fu_map(), group->name()) != nullptr)
      ErrorLog(group, "Group name conflicts with functional unit name: {0}",
               group->name());
  }

  for (auto *su : subunits())
    if (!su_map().emplace(su->name(), su).second)
      ErrorLog(su, "Duplicate subunit definition: {0}", su->name());

  for (auto *lat : latencies())
    if (!lat_map().emplace(lat->name(), lat).second)
      ErrorLog(lat, "Duplicate latency definition: {0}", lat->name());

  for (auto *opnd : operands())
    if (!operand_map().emplace(opnd->name(), opnd).second)
      ErrorLog(opnd, "Duplicate operand definition: {0}", opnd->name());

  for (auto *instr : instructions())
    if (!instruction_map().emplace(instr->name(), instr).second)
      ErrorLog(instr, "Duplicate instruction definition: {0}", instr->name());

  for (auto *rclass : reg_classes())
    if (!reg_class_map().emplace(rclass->name(), rclass).second)
      ErrorLog(rclass, "Duplicate register class definition: {0}",
               rclass->name());

  // Initialize a vector of instantiations for every subunit template.
  for (auto *su : subunits())
    su_instantiations()[su->name()] = new std::vector<SubUnitInstantiation *>;

  // Create some "Null" resource definitions.
  NullResourceRef = new ResourceRef("__");
  NullRegisterClass = new RegisterClass("__");
  NullPortDef = new ResourceDef("__");

  // Scan over all CPU specs, collect feasible predicate names.
  FindValidPredicateNames();
}

//---------------------------------------------------------------------------
// Look for implicitly defined functional unit templates, and create a
// definition for them. Implicitly defined functional units can occur in
// any CPU definition as a functional unit instance, or as a base of a
// functional unit template, or as a member of a group.
// Note that we DON'T allow implicit template definitions to have parameters.
// Also note that we will check if these are ever associated with any
// subunits - if they are not, its probably an error.
//---------------------------------------------------------------------------
void MdlSpec::FindImplicitFuncUnitTemplates() {
  // Check each functional unit instance in each CPU definition.
  for (auto *cpu : cpus())
    for (auto *cluster : *cpu->clusters())
      for (auto *unit : *cluster->func_units()) {
        auto *type = unit->type();
        if (FindItem(fu_map_, type->name()) == nullptr) {
          auto *fu = new FuncUnitTemplate(type);
          fu_map_.emplace(type->name(), fu);
          func_units_.push_back(fu);
        }
        // If there's a group defined with this name, generate an error.
        if (auto *group = FindItem(fu_group_map_, type->name()))
           ErrorLog(group,
                    "Group name conflicts with functional unit name: {0}",
                    group->name());
      }

  // Check that each base of an implicitly defined functional unit template
  // is defined. If its not, then define it as an implicitly defined
  // functional unit.
  for (auto [name, unit] : fu_map_)
    if (unit->is_implicitly_defined())
      for (auto *base : *unit->bases())
        if (FindItem(fu_map_, base->name()) == nullptr) {
          auto *fu = new FuncUnitTemplate(base);
          func_units_.push_back(fu);
          fu_map_.emplace(base->name(), fu);
        }

  // Check all the names in an FU group.  They should either refer to a group,
  // or a functional unit.  If they are undefined, report an error.
  for (auto [name, group] : fu_group_map())
    for (auto *id : *group->members())
      if (FindItem(fu_map_, id->name()) == nullptr  &&
          FindItem(fu_group_map_, id->name()) == nullptr)
        ErrorLog(id, "Undefined functional unit reference: {0}", id->name());
}

//---------------------------------------------------------------------------
// Create a set of all feasible names for predicates.  If not found in
// this list, a predicate use will elicit a warning.  We do this because
// a misspelled predicate will *always* fail silently, so we want to find
// predicates that can -never- be true.  This isn't strictly an error, but
// it is most likely a typo.
// We allow three kinds of predicates:
//   - defined CPU names
//   - Instantiated functional unit names
//   - Functional unit template names.
//---------------------------------------------------------------------------
void MdlSpec::FindValidPredicateNames() {
  for (auto *cpu : cpus()) {
    valid_predicate_names_.emplace(cpu->name());
    for (auto *cluster : *cpu->clusters())
      for (auto *fu_inst : *cluster->func_units())
        if (fu_inst->id())
          valid_predicate_names_.emplace(fu_inst->name());
  }

  for (auto *func_template : func_units())
    valid_predicate_names_.emplace(func_template->name());
}

//---------------------------------------------------------------------------
// Check that a predicate name is at least feasible.  If it is not, generate
// a warning.
//---------------------------------------------------------------------------
void MdlSpec::IsValidPredicateName(const Identifier *name) {
  if (!valid_predicate_names_.count(name->name()))
    WarningLog(name, "Predicate is invalid: {0}", name->name());
}

//---------------------------------------------------------------------------
// Sanity check the input for duplicate definitions.  We do this before
// any resource promotions so that we don't get lots of duplicate error
// messages.  Any errors found here are considered fatal, so just abort.
//---------------------------------------------------------------------------
// NOTES:
//  - Things we have dictionaries for (functional units, subunits, latencies,
//    operands, instructions, groups, and register classes) are checked against
//    themselves when they are created. They may still need to be checked
//    against other types of objects.
//  - Not all of the namespaces interfere. CPUs, clusters, and templates
//    (functional units, subunits, and latencies) have their own scope.
//  - Resource member lists have their own namespaces, but names must be
//    unique within that space.
//---------------------------------------------------------------------------
void MdlSpec::CheckForDuplicateDefs() {
  // Check that phase names groups and phase names are unique.
  CheckPhaseDefinitions(&pipe_phases());

  // Check that globally defined resources, registers and classes are unique.
  FindDuplicates(resources());
  FindDuplicates(registers());
  FindDuplicates(registers(), resources());
  FindDuplicates(resources(), reg_classes());
  FindDuplicates(registers(), reg_classes());
  FindDuplicateMembers(resources());

  // Operand names cannot conflict with registers or register class names.
  FindDuplicates(reg_classes(), operands());
  FindDuplicates(registers(), operands());

  // For functional unit templates, look for redefinitions of parameters,
  // registers, resources, or ports. We do allow locally defined resources and
  // ports to hide globally defined objects.
  for (auto *funit : func_units()) {
    FindDuplicates(*funit->params());
    FindDuplicates(*funit->resources());
    FindDuplicates(*funit->ports());
    FindDuplicateMembers(*funit->resources());

    // Don't allow redefinitions across object types (ports, resources).
    FindDuplicates(*funit->ports(), *funit->resources());

    // Don't allow shadowing of template parameters.
    FindDuplicates(*funit->resources(), *funit->params());
    FindDuplicates(*funit->ports(), *funit->params());
  }

  // For subunit and latency templates, check for unique parameters.
  for (auto *sunit : subunits())
    FindDuplicates(*sunit->params());
  for (auto *lat : latencies())
    FindDuplicates(*lat->params());

  // CPU's have a separate namespace, but redefinitions aren't allowed.
  // For each cpu definition, check for redefinitions of registers,
  // resources, and functional unit instance names.
  // For each cluster, check cluster names, register/resource names, and
  // functional unit instances.
  FindDuplicates(cpus());
  for (auto *cpu : cpus()) {
    // Check that phase names groups and phase names are unique.
    // Note that these can mask globally defined phase definitions.
    CheckPhaseDefinitions(cpu->pipe_phases());

    // Make sure CPU definitions aren't masking globally defined things.
    FindDuplicates(*cpu->resources());
    FindDuplicates(*cpu->resources(), resources());
    FindDuplicates(*cpu->resources(), registers());
    FindDuplicates(*cpu->resources(), reg_classes());
    FindDuplicates(*cpu->clusters());
    FindDuplicateMembers(*cpu->resources());

    for (auto *cluster : *cpu->clusters()) {
      FindDuplicates(*cluster->resources());
      FindDuplicates(*cluster->func_units());
      FindDuplicates(*cluster->issues());
      FindDuplicates(*cluster->issues(), *cluster->func_units());
      FindDuplicates(*cluster->issues(), *cluster->resources());
      FindDuplicates(*cluster->resources(), *cpu->resources());
      FindDuplicates(*cluster->resources(), resources());
      FindDuplicates(*cluster->resources(), registers());
      FindDuplicates(*cluster->resources(), reg_classes());
      FindDuplicateMembers(*cluster->resources());

      // If this is a generated (promoted) cluster, we also check this
      // cluster's definitions against CPU-level definitions.
      if (cluster->IsNull()) {
        FindDuplicates(*cluster->issues(), *cpu->resources());
        FindDuplicates(*cluster->issues(), *cpu->clusters());
        FindDuplicates(*cluster->func_units(), *cpu->resources());
        FindDuplicates(*cluster->func_units(), *cpu->clusters());
      }
    }
  }

  // For each instruction definition, check for duplicate operand names.
  for (auto *instruct : instructions())
    FindDuplicates(*instruct->operands());

  // For each operand definition, check for duplicate (sub)operand names and
  // duplicate attribute definitions.
  for (auto *operand : operands())
    FindDuplicates(*operand->operands());
}

//---------------------------------------------------------------------------
// Look up a phase name in the pipeline definition set.  First look for
// CPU-specific phases, and if not found look for a global definition.
// Return null if it's not found anywhere.
//---------------------------------------------------------------------------
PhaseName *MdlSpec::SearchPipeReference(Identifier *phase, CpuInstance *cpu) {
  if (cpu)
    for (auto *p1 : *cpu->pipe_phases())
      if (auto *item = FindItem(*p1->phase_names(), phase->name()))
        return item;

  for (auto *p1 : pipe_phases())
    if (auto *item = FindItem(*p1->phase_names(), phase->name()))
      return item;

  return nullptr;
}

//---------------------------------------------------------------------------
// Look up a phase name in the pipeline definition set.
// Print an error message if not found.
// Return a pointer to the object if found, or a fake object if not found.
//---------------------------------------------------------------------------
PhaseName *MdlSpec::FindPipeReference(Identifier *phase, CpuInstance *cpu) {
  if (auto *item = SearchPipeReference(phase, cpu))
    return item;

  // This is ultimately a fatal error.  Return a fake object.
  ErrorLog(phase, "Pipeline phase \"{0}\" not found for cpu: {1}",
           phase->name(), cpu->name());
  return new PhaseName(*phase, "", false, false);
}

//---------------------------------------------------------------------------
// Check that phase names groups and phase names are unique.
//---------------------------------------------------------------------------
void MdlSpec::CheckPhaseDefinitions(PipeDefList *pipes) {
  FindDuplicates(*pipes);

  for (auto *p1 : *pipes) {
    FindDuplicates(*p1->phase_names());
    for (auto *p2 : *pipes)
      if (p1 != p2)
        FindDuplicates(*p1->phase_names(), *p2->phase_names());
  }
}

//---------------------------------------------------------------------------
// Specialize a phase expression for the context its instantiated in.
// Return true if at least one valid pipeline phase is found.
//---------------------------------------------------------------------------
void MdlSpec::SpecializePhaseExpr(PhaseExpr *expr, CpuInstance *cpu) {
  if (expr->operation() == kPhase) {
    auto *phase = FindPipeReference(expr->phase(), cpu);
    expr->set_phase_name(phase);
  }
}

//---------------------------------------------------------------------------
// If a resource definition has a start_phase or end_phase specified,
// look them up in the pipe phase definitions.
//---------------------------------------------------------------------------
void MdlSpec::CheckPipeReference(ResourceDef *def, CpuInstance *cpu) {
  if (def->start_phase() != nullptr)
    FindPipeReference(def->start_phase(), cpu);
  if (def->end_phase() != nullptr)
    FindPipeReference(def->end_phase(), cpu);
}

//---------------------------------------------------------------------------
// Quick check that reference phases contain at least one phase name.
//---------------------------------------------------------------------------
void MdlSpec::CheckReferencePhases(ReferenceList *refs) {
  if (refs == nullptr)
    return;
  for (auto *ref : *refs) {
    // Functional unit refs don't always have an explicit phase expression.
    // We need to add one, but must do it later when we instantiate it.
    if (ref->IsFuncUnitRef() && ref->phase_expr() == nullptr)
      continue;
    // For conditional refs, recur on the then/else clauses.
    if (ref->IsConditionalRef()) {
      for (auto *cc = ref->conditional_ref(); cc; cc = cc->else_clause())
        CheckReferencePhases(&cc->refs());
      continue;
    }
    // Normal case - make sure there's a phase name mentioned somewhere.
    if (!ref->phase_expr()->HasPhaseName())
      ErrorLog(ref->phase_expr(), "Invalid phase: missing phase name");
  }
}

//---------------------------------------------------------------------------
// Look up references to pipeline phases.
// These occur in resource definitions, issue definitions, and references.
// Any errors found here aren't immediately fatal, so we always return.
//---------------------------------------------------------------------------
void MdlSpec::CheckPipeReferences() {
  // Check resources defined globally.
  for (auto *res : resources())
    CheckPipeReference(res, nullptr);

  // Check resources defined in CPUs and CPU clusters.
  for (auto *cpu : cpus()) {
    for (auto *issue : *cpu->issues())
      CheckPipeReference(issue, cpu);
    for (auto *res : *cpu->resources())
      CheckPipeReference(res, cpu);
    for (auto *cluster : *cpu->clusters()) {
      for (auto *issue : *cluster->issues())
        CheckPipeReference(issue, cpu);
      for (auto *res : *cluster->resources())
        CheckPipeReference(res, cpu);
    }
  }

  // We can't statically check phase references in latency template definitions.
  // Phase names are defined on a per-cpu basis, so we we need to check
  // templates' phase references when we instantiate the template. This happens
  // later in the compilation process.

  // That said, we -can- check that phase expressions contain at least ONE
  // phase name.  We do this to disallow references to phase indexes.
  for (const auto *latency : latencies())
    CheckReferencePhases(latency->references());
}

//---------------------------------------------------------------------------
// Make sure functional unit, subunit, and latency templates are declared
// with compatible parameters with their bases (types and numbers).
// Return true if any errors are found.
//---------------------------------------------------------------------------
// NOTE: The strictest possible policy would be that the number, order,
// name, and type of parameters must be identical.  This could be
// relaxed in the future, with several possible policies:
//  - extra parameters in parent template are ok (supported).
//  - missing base parameters are passed null (not supported).
//  - order of parameters doesn't matter, just match on name (not supported).
//---------------------------------------------------------------------------
void MdlSpec::SameParams(const ParamsList *params,
                         const ParamsList *base_params, MdlItem *item) {
  const bool base_can_have_extra_params = false; // not supported downstream.
  const bool base_can_have_fewer_params = true;

  // Check that the number of parameters is compatible.
  if ((!base_can_have_extra_params && (params->size() < base_params->size())) ||
      (!base_can_have_fewer_params && (params->size() > base_params->size()))) {
    ErrorLog(item, "Incompatible parameters for template and base");
    return;
  }

  int min_params = std::min(params->size(), base_params->size());

  for (int idx = 0; idx < min_params; idx++) {
    if ((*params)[idx]->type() != (*base_params)[idx]->type())
      ErrorLog(item, "Unmatched parameter types for template and base");
    if ((*params)[idx]->name() != (*base_params)[idx]->name())
      ErrorLog(item, "Unmatched parameter names for template and base");
  }
}

//---------------------------------------------------------------------------
// Some helper template functions for finding cycles in MDL template
// definitions.  Recursive template definitions are not allowed.
//---------------------------------------------------------------------------
template <class T>
bool FindCycle(T *root, T *unit, std::set<std::string> &visited) {
  visited.insert(unit->name());

  for (auto *child : unit->unit_bases()) {
    if (child == root)
      return true;
    if (visited.count(child->name()) == 0 &&
        FindCycle(root, child, visited))
      return true;
  }
  return false;
}

template <class T> void FindCycles(T &item, MdlSpec *md, std::string type) {
  for (auto *unit : item) {
    std::set<std::string> visited;
    if (FindCycle(unit, unit, visited))
      md->ErrorLog(unit, "Recursively defined {0} template: {1}", type,
                   unit->name());
  }
}

//---------------------------------------------------------------------------
// Some helper template functions for duplicate bases in MDL template
// definitions.  Duplicate bases in general are not allowed.  This is a
// pretty naive implementation - derived units aren't so common that this
// needs to be particularly efficient.
//---------------------------------------------------------------------------
template <class T>
void FindAllBases(T *top_unit, T *unit, MdlSpec *md, std::string &type,
                  std::set<std::string> &seen) {
  for (auto *base : unit->unit_bases()) {
    if (seen.count(base->name()))
      md->ErrorLog(top_unit, "{0} template {1} has duplicate bases: {2}", type,
                   top_unit->name(), base->name());
    seen.insert(base->name());
    FindAllBases(top_unit, base, md, type, seen);
  }
}

// Ensure that a template doesn't have duplicate bases. Note that this
// assumes we've already checked for recursively defined templates.
template <class T>
void FindDuplicateBases(T &item, MdlSpec *md, std::string type) {
  for (auto *unit : item) {
    std::set<std::string> seen;
    FindAllBases(unit, unit, md, type, seen);
  }
}

//---------------------------------------------------------------------------
// Check validity of template bases for functional units, subunits, latencies
// and functional unit groups, and link the template to its bases.
// Functional unit and latency template bases must have parameters which are
// compatible with the base. We also explicitly check for recursively
// defined templates. Any errors found here are considered fatal, so just abort.
//---------------------------------------------------------------------------
void MdlSpec::CheckTemplateBases() {
  for (auto fu : func_units())
    if (auto *bases = fu->bases()) {
      for (auto *base : *bases) {
        if (auto *fu_base = FindItem(fu_map(), base->name())) {
          fu->add_base(fu_base);
          SameParams(fu->params(), fu_base->params(), fu);
        } else {
          ErrorLog(fu, "Undefined functional unit base: {0}", base->name());
        }
      }
    }

  // Check that subunit bases exist, and link the template to its bases. If
  // there are any string bases, use these to tie the subunit to a matched set
  // of instructions.
  // Unlike other template bases, subunit templates can have different
  // parameters than their bases (by design), so we don't check for parameter
  // compatibility.
  for (auto *su : subunits()) {
    if (auto *bases = su->bases()) {
      for (auto *base : *bases) {
        if (auto *su_base = FindItem(su_map(), base->name())) {
          su->add_base(su_base);
          su_base->add_derived_subunit(su);
        } else {
          ErrorLog(su, "Undefined subunit base: {0}", base->name());
        }
      }
    }
    TieSubUnitToInstructions(su, su->regex_bases());
  }

  for (auto *latency : latencies())
    if (auto *bases = latency->base_ids()) {
      for (auto *base : *bases)
        if (auto *lat_base = FindItem(lat_map(), base->name())) {
          latency->add_base(lat_base);
          SameParams(latency->params(), lat_base->params(), latency);
        } else {
          ErrorLog(latency, "Undefined latency base: {0}", base->name());
        }
    }

  // If a functional unit group includes an FU group, expand that group into
  // the parent group.
  for (auto *group : func_unit_groups())
    if (!ExpandGroup(group, group->members(), 0))
      break;

  // Check that we don't have recursive derivations for templates.
  FindCycles(func_units(), this, "functional unit");
  FindCycles(subunits(), this, "subunit");
  FindCycles(latencies(), this, "latency");

  if (ErrorsSeen())
    Abort();

  // Check for duplicate bases.  Functional units *can* have duplicate bases,
  // and duplicate bases for latencies are relatively common and harmless.
  // But subunits cannot have duplicate bases.
  FindDuplicateBases(subunits(), this, "Subunit");

  if (ErrorsSeen())
    Abort(); // If any errors found, abort.
}

//---------------------------------------------------------------------------
// Link all the members of functional unit group to the group.  Subgroups
// are allowed, but we need to check for recursion.
// This function returns true if no errors were found, else returns false.
//---------------------------------------------------------------------------
bool MdlSpec::ExpandGroup(FuncUnitGroup *group, IdList *members,
                          unsigned depth) {
  if (depth >= fu_group_map().size()) {
    ErrorLog(group, "Group is recursively defined: {0}", group->name());
    return false;
  }

  for (auto *member : *group->members()) {
    if (auto *func_unit = FindItem(fu_map(), member->name())) {
      group->add_unit(func_unit);
      continue;
    }
    if (auto *subgroup = FindItem(fu_group_map(), member->name()))
      if (!ExpandGroup(group, subgroup->members(), depth + 1))
        return false;
  }
  return true;
}

//---------------------------------------------------------------------------
// Check for lexical argument compatibility between an instantiation and
// a template definition (for functional units, subunits, and latencies).
// Explicitly link argument objects to their associated template parameters.
// Return true if any errors are found.
//---------------------------------------------------------------------------
void MdlSpec::ValidateArgs(const ParamsList *params,
                           const ResourceRefList *instance, MdlItem *item) {
  if (params->size() != instance->size()) {
    ErrorLog(item, "Instance has wrong number of parameters");
    return;
  }
  for (unsigned idx = 0; idx < params->size(); idx++)
    (*instance)[idx]->set_parameter((*params)[idx]);
}

//---------------------------------------------------------------------------
// For each functional unit, subunit, and latency instantiation, find the
// referenced template definition (if it exists) and make sure the parameters
// match. Link instances to their templates, and link instant arguments to
// the associated template parameter.
// Any errors found here are considered fatal, so just abort.
//---------------------------------------------------------------------------
void MdlSpec::CheckInstantiations() {
  // For every CPU and cluster, find each functional unit instantiation and
  // check its parameters against its functional unit template definition.
  for (auto *cpu : cpus()) {
    for (auto *cluster : *cpu->clusters()) {
      for (auto *fu_inst : *cluster->func_units()) {
        if (auto *fu_temp = FindItem(fu_map(), fu_inst->type()->name())) {
          ValidateArgs(fu_temp->params(), fu_inst->args(), fu_inst);
          fu_inst->set_template(fu_temp);
        } else {
          ErrorLog(fu_inst, "Undefined functional unit reference: {0}",
                   fu_inst->type()->name());
        }
      }
    }
  }

  // For every functional unit template definition, find each subunit
  // instantiation and check its parameters against its subunit template
  // definition.
  for (auto *fu_template : func_units()) {
    for (auto *instance : *fu_template->subunits()) {
      if (auto *su_temp = FindItem(su_map(), instance->name())) {
        ValidateArgs(su_temp->params(), instance->args(), instance);
        instance->set_template(su_temp);
      } else {
        ErrorLog(instance, "Undefined subunit reference: {0}",
                 instance->name());
      }
    }
  }

  // For every subunit template base definition, find each latency instantiation
  // and check its parameters against its latency template definition.
  for (auto *su_template : subunits()) {
    for (auto *lat_inst : *su_template->latencies()) {
      if (auto *lat_temp = FindItem(lat_map(), lat_inst->name())) {
        ValidateArgs(lat_temp->params(), lat_inst->args(), lat_inst);
        lat_inst->set_template(lat_temp);
      } else {
        ErrorLog(lat_inst, "Undefined latency reference: {0}",
                 lat_inst->name());
      }
    }
  }

  if (ErrorsSeen())
    Abort(); // If any errors found, abort.
}

// For each CPU, determine if we need to explicitly manage issue slots.
// - If there's more than one cluster, we conservatively decide to manage them.
// - If any functional unit instance pins issue slots, we must manage them.
void MdlSpec::CheckIssueSlots() {
  for (auto *cpu : cpus()) {
    if (cpu->clusters()->size() > 1) {
      cpu->set_needs_slot_resources(true);
      continue;
    }
    for (auto *cluster : *cpu->clusters())
      for (auto *fu_inst : *cluster->func_units())
        if (fu_inst->pin_any() || fu_inst->pin_all()) {
          cpu->set_needs_slot_resources(true);
          break;
        }
  }
}

//---------------------------------------------------------------------------
// Check instruction definitions for valid subunits.
// Any errors found here are considered fatal, so just abort.
//---------------------------------------------------------------------------
void MdlSpec::CheckInstructions() {
  for (auto *instruct : instructions())
    for (auto *subunit : *instruct->subunits())
      if (!su_map().count(subunit->name()))
        ErrorLog(subunit, "Undefined subunit reference: {0}", subunit->name());
}

//---------------------------------------------------------------------------
// Flatten a single operand - push an operand for each component onto the
// operand list.
//---------------------------------------------------------------------------
void MdlSpec::FlattenOperand(OperandDecl *opnd, OperandDeclList *flat_ops) {
  // If this is not a reference to another operand, just add it to the list.
  if (opnd->is_implied_register() || opnd->reg_class() ||
      opnd->operand()->operands()->empty()) {
    flat_ops->push_back(opnd);
    return;
  }

  // Recursively handle operands that reference other operands.
  for (auto *sub_opnd : *opnd->operand()->operands()) {
    auto *new_sub_opnd = new OperandDecl(sub_opnd, opnd);
    new_sub_opnd->add_type(sub_opnd->type());
    new_sub_opnd->add_name(sub_opnd->op_name());
    FlattenOperand(new_sub_opnd, flat_ops);
  }
}

//---------------------------------------------------------------------------
// Create an operand list that flattens the operand declarations:
//       operand opnd(GPR reg, imm value);
//       instruction inst(opnd X)
//   becomes:
//       instruction inst(opnd.GPR X.reg, opnd.imm X.value);
// Note: Since we've already checked the validity of the operands, there
// will not be any errors encountered here.
//---------------------------------------------------------------------------
void MdlSpec::FlattenInstructionOperands() {
  for (auto *instruct : instructions())
    if (instruct->operands()) {
      auto *flat_ops = new OperandDeclList();
      for (auto *opnd : *instruct->operands())
        FlattenOperand(new OperandDecl(opnd, opnd), flat_ops);

      instruct->set_flat_operands(flat_ops);
    }
}

// Determine if a derived operand definition is based on the specified operand.
// Return true if it does.
bool MdlSpec::FindOperandDerivation(const OperandDef *derived,
                                    const OperandDef *operand) const {
  if (derived == operand)
    return true;

  for (auto *base : *derived->base_operands())
    if (FindOperandDerivation(base, operand))
      return true;
  return false;
}

// Check a qualified operand name list with an instruction's flattened
// operand list.
bool MdlSpec::CompareOpndNames(const OperandDecl *opnd, const IdList &names) {
  int opnd_size = opnd->op_names()->size();
  int names_size = names.size();

  // If the operand reference isn't fully qualified, we allow you to skip
  // the last name if the underlying operand type is a register or register
  // class.  If you don't like this behavior, provide all the names!
  if (opnd_size != names_size) {
    if (opnd_size != names_size + 1)
      return false;

    // Make sure the missing operand type is a register or register class.
    std::string opnd_type = opnd->types()->back()->name();
    if (!FindItem(registers(), opnd_type) && !reg_class_map().count(opnd_type))
      return false;
  }

  // All the leading names need to match.
  for (int index = 0; index < names_size; index++)
    if ((*opnd->op_names())[index]->name() != names[index]->name())
      return false;
  return true;
}

// Look up qualified operand name in the instruction's flattened operand
// list, and return its index in the list, or -1 if not found.
// Implied operands may show up several times in the operand list, and
// we need to differentiate defs from uses.
int MdlSpec::FindOperandName(const InstructionDef *instruct,
                             const IdList &names, RefType type) {
  int index = 0;
  int itype = static_cast<int>(type);
  for (auto *op_decl : *instruct->flat_operands()) {
    // Check for references to implied register operands.
    if (op_decl->is_implied_register() && (itype & RefTypes::kAnyUseDef)) {
      if (names[0]->name() == op_decl->type()->name()) {
        if (op_decl->is_input() && (itype & RefTypes::kAnyUse))
          return index;
        if (op_decl->is_output() && (itype & RefTypes::kAnyDef))
          return index;
      }
      continue;
    }
    // Handle "normal" operands.
    if (CompareOpndNames(op_decl, names))
      return index;
    index++;
  }
  return -1;
}

// Look up an operand by name and optional type, and return the index or
// -1 if not found.  If a non-empty type is provided, it must match the
// operand definition type.
int MdlSpec::FindOperand(const InstructionDef *instr, const IdList &name,
                         const std::string &type, RefType ref_type) {
  // First check to see if it's a variant operand ($$<number>). Note that these
  // never have a declared operand type.
  if (name[0]->is_vararg())
    return name[0]->vararg_index() + instr->num_flat_operands() - 1;

  // If the operand is simply an operand index ($<number>) use that as the
  // operand id, otherwise look up the operand name(s).
  int opnd_id;
  if (name[0]->is_number()) {
    opnd_id = name[0]->get_number();
    if (opnd_id >= instr->num_flat_operands())
      return -1;
  } else {
    opnd_id = FindOperandName(instr, name, ref_type);
  }
  if (opnd_id == -1)
    return -1;

  // See if the operand types match.  If either is empty, we match.
  auto opnd_decl = instr->GetOperandDecl(opnd_id);
  if (opnd_decl == nullptr)
    return opnd_id;

  std::string decl_type = opnd_decl->type_name();
  if (type.empty() || decl_type.empty() || decl_type == type)
    return opnd_id;

  // If the operand match type is a derived operand, check if its derived
  // from the declared operand type.
  if (!type.empty() && !decl_type.empty()) {
    auto *ins_opnd_type = FindItem(operands(), decl_type);
    auto *ref_opnd_type = FindItem(operands(), type);
    if (ins_opnd_type && ref_opnd_type && ref_opnd_type->IsDerivedOperand())
      if (FindOperandDerivation(ref_opnd_type, ins_opnd_type))
        return opnd_id;
  }

  // If the operand names don't match, and they are both register classes,
  // and if the reference class is a strict superset of the declared class,
  // it's a match.
  auto *decl_class = FindItem(reg_classes(), decl_type);
  auto *ref_class = FindItem(reg_classes(), type);
  if (decl_class && ref_class && ref_class->IsSupersetOf(decl_class))
    return opnd_id;
  return -1; // Not found.
}

// Given an operand reference, determine its index in this instruction.
// Return the index, or -1 if not found.
int MdlSpec::GetOperandIndex(const InstructionDef *instr,
                             const OperandRef *operand, RefType ref_type) {
  if (operand == nullptr)
    return -1;
  std::string type = operand->op_type() ? operand->op_type()->name() : "";
  return FindOperand(instr, *operand->op_names(), type, ref_type);
}

//---------------------------------------------------------------------------
// Make sure we don't have recursively defined operands.
// This can happen with suboperands or operand bases.
// Return true if recursion found.
//---------------------------------------------------------------------------
bool MdlSpec::CheckRecursiveOperands(OperandDef *opnd, OperandDefList &seen) {
  seen.push_back(opnd);

  // Check suboperands.
  if (auto *sub_operands = opnd->operands()) {
    for (auto *opnd_decl : *sub_operands) {
      if (opnd_decl->operand()) {
        if (FindItem(seen, opnd_decl->operand()->name())) {
          ErrorLog(seen[0], "Recursively defined operand: {0}",
                   seen[0]->name());
          return true;
        }
        if (CheckRecursiveOperands(opnd_decl->operand(), seen))
          return true;
      }
    }
  }

  // Check base operands.
  for (auto *base : *opnd->base_operands()) {
    if (FindItem(seen, base->name())) {
      ErrorLog(seen[0], "Recursively defined operand: {0}", seen[0]->name());
      return true;
    }
    if (CheckRecursiveOperands(base, seen))
      return true;
  }

  seen.pop_back();
  return false;
}

// Define a set to keep track of operand definitions we've already seen.
using OperandDefSet = std::unordered_set<OperandDef *>;

//---------------------------------------------------------------------------
// Check that derived operands have only one derivation to any base operand.
// If there is more than one derivation (ie a diamond pattern) the derivation
// is ambiguous, and we can't always generate meaningful code for it.
// Return nullptr if the derivation is ambiguous.
//---------------------------------------------------------------------------
static OperandDef *CheckOperandDerivation(OperandDef *opnd,
                                          OperandDefSet &seen) {
  // If we've already seen this operand, its either recursive (already
  // checked) or ambiguously defined.  We've already checked for recursion,
  // so have to abort if we see it, but we don't want to report it as
  // an ambiguous derivation.
  if (seen.count(opnd))
    return (opnd->IsDerivedOperand()) ? nullptr : opnd;

  seen.insert(opnd);
  for (auto *base : *opnd->base_operands())
    if (auto *item = CheckOperandDerivation(base, seen))
      return item;

  if (opnd->IsDerivedOperand())
    seen.erase(opnd);
  return nullptr;
}

//---------------------------------------------------------------------------
// Check that derived operands have only one derivation to any base operand.
//---------------------------------------------------------------------------
void MdlSpec::CheckOperandDerivations(OperandDef *opnd) {
  OperandDefSet seen;
  if (auto *base = CheckOperandDerivation(opnd, seen))
    ErrorLog(opnd, "Ambiguous operand derivation: {0}->{1}", opnd->name(),
             base->name());
}

//---------------------------------------------------------------------------
// Check that a single operand reference is either a reference to a defined
// operand, a register class, or a register name (an implied operand).
// Link valid declarations with their definitions.
// NOTE: We currently don't allow instruction definitions or operands to
// directly reference derived operands, so we explicitly check for this here.
// Derived operands exist to qualify regular operand types in reference rules.
//---------------------------------------------------------------------------
void MdlSpec::CheckOperand(OperandDecl *operand_decl) {
  const std::string &name = operand_decl->type()->name();
  if (operand_map().count(name)) {
    operand_decl->set_operand(operand_map()[name]);
    if (operand_decl->operand()->bases())
      ErrorLog(operand_decl, "Invalid use of a derived operand: {0}", name);
  } else if (reg_class_map().count(name)) {
    operand_decl->set_regclass(reg_class_map()[name]);
  } else if (FindItem(registers(), name) != nullptr) {
    operand_decl->set_is_implied_register();
  } else {
    ErrorLog(operand_decl, "Undefined operand type: {0}", name);
  }

  if (!operand_decl->is_implied_register() && !operand_decl->is_ellipsis())
    if (operand_decl->name().empty())
      ErrorLog(operand_decl, "Instruction operands must have names");
}

//---------------------------------------------------------------------------
// Check that operand references (in instruction definitions, operand
// definitions, and latency references) refer to valid operands.
// Link operand declarations to their definitions. Also link derived
// operands to their base operands.
// Check for recursively defined operands, or ambiguously derived operands.
//---------------------------------------------------------------------------
void MdlSpec::CheckOperands() {
  // Check instruction definitions for valid operand types.  They can
  // be either operand definitions or register class definitions.
  for (auto *instruct : instructions())
    for (auto *operand : *instruct->operands())
      CheckOperand(operand);

  // Check operand definitions for valid operand types, and link declarations
  // to their definitions.  If an operand is derived, link it to its base.
  for (auto *operand_def : operands()) {
    for (auto *operand : *operand_def->operands())
      CheckOperand(operand);
    if (auto *base_list = operand_def->bases()) {
      for (auto *base : *base_list)
        if (operand_map().count(base->name()))
          operand_def->add_base_operand(operand_map()[base->name()]);
        else
          ErrorLog(base, "Undefined base operand: {0}", base->name());
    }
  }
  if (ErrorsSeen())
    Abort(); // If any errors found, abort.

  // Check for recursively defined operands.
  for (auto *opnd_def : operands()) {
    OperandDefList seen;
    CheckRecursiveOperands(opnd_def, seen);
  }

  // Check for valid derivations for derived operands.
  for (auto *opnd_def : operands())
    if (!opnd_def->base_operands()->empty()) {
      CheckOperandDerivations(opnd_def);
    }
  if (ErrorsSeen())
    Abort(); // If any errors found, abort.

  // Once we've checked all the operands, flatten the operand hierarchy to a
  // single level.
  FlattenInstructionOperands();
}

//---------------------------------------------------------------------------
// Scan references in each latency rule and report references which were
// encountered, but never valid (in any instruction, in any subunit).
//---------------------------------------------------------------------------
void MdlSpec::CheckReferenceUse() {
  for (auto *latency : latencies())
    for (auto *ref : *latency->references())
      if (ref->seen() && !ref->used())
        WarningLog(ref, "Reference never used: {0}", ref->ToString());
}

//---------------------------------------------------------------------------
// Print a warning for any subunit template that isn't used.
//---------------------------------------------------------------------------
void MdlSpec::CheckSubunitUse() {
  for (auto *subunit : subunits())
    if (subunit->use_count() == 0)
      WarningLog(subunit, "Subunit never used: {0}", subunit->name());
}

//---------------------------------------------------------------------------
// Check a single resource definition for a pooled resource.
// Pooled resources with shared bits must specify a phase - there's no
// reasonable way to manage these across arbitrary pipeline phases.
//---------------------------------------------------------------------------
void MdlSpec::CheckResourceDef(const ResourceDef *def) {
  if (def->IsPoolDef() && def->start_phase() == nullptr &&
      def->has_shared_bits())
    ErrorLog(def, "Shared resource pools must have a pipeline phase: {0}",
             def->ToString());
}

//---------------------------------------------------------------------------
// Make sure shared resource pools have been declared with a pipe phase.
// Do this before functional unit instantiation and global/group resource
// promotion, so that we don't get duplicate error messages.
//---------------------------------------------------------------------------
void MdlSpec::CheckResourceDefs() {
  // Check resources defined globally.
  for (auto def : resources())
    CheckResourceDef(def);

  // Check resources defined in functional units.
  for (auto *funit : func_units())
    for (auto def : *funit->resources())
      CheckResourceDef(def);

  // Check resources defined in cpus (and clusters).
  FindDuplicates(cpus());
  for (auto *cpu : cpus()) {
    for (auto def : *cpu->resources())
      CheckResourceDef(def);
    for (auto *cluster : *cpu->clusters())
      for (auto def : *cluster->resources())
        CheckResourceDef(def);
  }
}

//---------------------------------------------------------------------------
// Print a warning for any inconsistent resource use.
//---------------------------------------------------------------------------
void MdlSpec::CheckResourceUse() {
  for (auto *cpu : cpus())
    for (auto *res : cpu->all_resources())
      if (res != cpu->all_resources().back()) {   // skip last dummy resource
        if (!res->is_used())
          WarningLog(res, "Resource never referenced: {0}", res->debug_name());
        else if (res->only_held())
          WarningLog(res, "Resource Held but never Reserved: {0}", res->name());
        else if (res->only_reserved())
          WarningLog(res, "Resource Reserved but never Held: {0}", res->name());
      }
}

//---------------------------------------------------------------------------
// Check that conditional references have a valid predicate, and also check
// the predicated references for validity.
//---------------------------------------------------------------------------
void MdlSpec::CheckConditionalReferences(ConditionalRef *cond_ref) {
  if (cond_ref == nullptr)
    return;
  if (cond_ref->predicate() != nullptr)
    if (predicate_table_.count(cond_ref->predicate()->name()) == 0)
      ErrorLog(cond_ref->predicate(), "Undefined predicate name: {0}",
               cond_ref->predicate()->name());

  for (auto *ref : cond_ref->refs()) {
    if (ref->operand() && ref->operand()->op_type() != nullptr)
      CheckSubOperands(ref->operand(), ref->operand()->op_type(), 1);
    if (ref->IsConditionalRef())
      CheckConditionalReferences(ref->conditional_ref());
  }
  CheckConditionalReferences(cond_ref->else_clause());
}

//---------------------------------------------------------------------------
// Check operand references in each rule. Note that, at this point in the
// compilation, we can only check references which explicitly specify an
// operand type. We look for references that will -always- fail for any
// instruction.
//---------------------------------------------------------------------------
void MdlSpec::CheckReferences() {
  for (auto *latency : latencies())
    for (auto *ref : *latency->references()) {
      if (ref->operand() && ref->operand()->op_type() != nullptr)
        CheckSubOperands(ref->operand(), ref->operand()->op_type(), 1);
      if (ref->IsConditionalRef())
        CheckConditionalReferences(ref->conditional_ref());
    }
}

//---------------------------------------------------------------------------
// Promote globally defined resources to be CPU-defined resources.  This
// gives each CPU a unique set of resources, so we can name them, renumber
// them, and track use of them separately for each CPU.
//---------------------------------------------------------------------------
void MdlSpec::PromoteGlobalResources() {
  for (auto *cpu : cpus())
    for (auto *resource : resources())
      cpu->resources()->push_back(new ResourceDef(*resource));
}

// If we promoted a member and the promoted resource already exists, check that
// they have compatible definitions.
void MdlSpec::CheckPromotedMember(ResourceDef *group, Identifier *member,
                                  ResourceDef *promoted) {
  // The promoted resource cannot be part of a group.
  if (promoted->IsPoolDef())
    ErrorLog(member, "Invalid group member: {0}", member->name());
  // The group and the promoted resource must have the same attributes.
  if (group->bit_size() != promoted->bit_size() ||
      group->start_phase() != promoted->start_phase() ||
      group->end_phase() != promoted->end_phase())
    ErrorLog(member, "Inconsistent group definition: {0}", member->name());
}

//---------------------------------------------------------------------------
// Given a list of resource definitions for a scope, find group definitions
// and promote each member to a regular resource definition, and annotate
// the resource group with the new resource definition. If the resource is
// already defined (either by the user or a previous promotion), make sure
// the definitions match.
//---------------------------------------------------------------------------
void MdlSpec::PromoteResourceGroupMembers(ResourceDefList *resources,
                                          ResourceDefList *outer_scope,
                                          ResourceRefDict *args) {
  ResourceDefList promos;
  for (auto *resource : *resources) {
    if (resource->IsGroupDef()) {
      for (unsigned idx = 0; idx < resource->members().size(); idx++) {
        auto *mem = resource->members()[idx];
        // See if this member is defined or has been previously promoted.
        ResourceDef *def = FindItem(*resources, mem->name());
        if (def == nullptr)
          def = FindItem(promos, mem->name());
        if (def == nullptr && outer_scope != nullptr)
          def = FindItem(*outer_scope, mem->name());
        if (def == nullptr && args != nullptr) {
          if (auto *ref = FindItem(*args, mem->name())) {
            def = ref->definition();
            resource->members()[idx] = new Identifier(def->id(), mem->index());
          }
        }

        // If we didn't find the resource, create a new resource and add it to
        // the list of things to promote.
        if (def == nullptr) {
          def = new ResourceDef(*mem, mem, resource->bit_size(), 0,
                                resource->start_phase(), resource->end_phase());
          promos.push_back(def);
        }
        // Add the promoted resource to the def list for the group.
        CheckPromotedMember(resource, mem, def);
        resource->add_member_def(def);
      }
      // After promoting all the members of a group, check that we didn't end
      // up with duplicate members in the group.
      FindDuplicates(resource->members());
    }
  }

  // Add all the new resources to the defined resources.
  resources->insert(resources->end(), promos.begin(), promos.end());
}

//---------------------------------------------------------------------------
// Scan arguments to functional unit instances, and promote implicit group
// definitions to cluster resources.
//---------------------------------------------------------------------------
void PromoteFuncUnitGroupArgs(ClusterInstance *cluster) {
  for (auto *instance : *cluster->func_units()) {
    for (auto *arg : *instance->args()) {
      if (arg->IsGroupRef() && arg->implicit_group())
        cluster->resources()->push_back(arg->definition());
    }
  }
}

//---------------------------------------------------------------------------
// Promote group member definitions to regular resources for CPUs, Clusters.
// We promote functional unit templates' resources separately for each
// instance.
//---------------------------------------------------------------------------
void MdlSpec::PromoteResourceGroups() {
  for (auto *cpu : cpus()) {
    PromoteResourceGroupMembers(cpu->resources(), nullptr, nullptr);
    for (auto *clus : *cpu->clusters()) {
      PromoteFuncUnitGroupArgs(clus);
      PromoteResourceGroupMembers(clus->resources(), cpu->resources(), nullptr);
    }
  }
}

//---------------------------------------------------------------------------
// Return true if this is a valid operand reference.
//---------------------------------------------------------------------------
bool MdlSpec::CheckSubOperands(OperandRef *ref, const Identifier *opnd,
                               int idx) {
  int size = ref->op_names()->size();
  bool is_reg_class = reg_class_map().count(opnd->name());
  bool is_operand = operand_map().count(opnd->name());

  if (is_reg_class)
    ref->set_regclass(reg_class_map()[opnd->name()]);
  if (is_operand)
    ref->set_operand(operand_map()[opnd->name()]);

  if (is_reg_class && idx == size)
    return true;

  if (is_operand && idx < size) {
    OperandDef *op_type = operand_map()[opnd->name()];
    if (!op_type->operands()->empty()) {
      auto *item =
          FindItem(*op_type->operands(), (*ref->op_names())[idx]->name());
      if (item != nullptr)
        return CheckSubOperands(ref, item->type(), idx + 1);
      opnd = nullptr; // Force an error message.
    }
  }

  if (opnd == nullptr || (!is_reg_class && !is_operand)) {
    ErrorLog(ref, "Undefined operand type: {0}", ref->ToString());
    return false;
  }

  if (idx < size) {
    ErrorLog(ref, "Over-qualified operand reference: {0}", ref->ToString());
    return false;
  }

  if (is_operand && !operand_map()[opnd->name()]->operands()->empty()) {
    ErrorLog(ref, "Under-qualified operand reference: {0}", ref->ToString());
    return false;
  }

  return true;
}

} // namespace mdl
} // namespace mpact

//===- mdl.cpp - Instantiate mdl template objects -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains methods that implement instantiations of functional units,
// subunits, and latency templates. The functions in this file implement
// the first pass of architecture definition: generating a dictionary of
// specialized subunit instances.
//
// General theory of architecture definition and expansion:
//
// A top-level architecture description consists of CPU definitions, functional
// unit template definitions, subunit template definitions, and latency
// template definitions.
//
// CPU definitions are composed of resource definitions, cluster definitions,
// and specialized functional unit instances. Clusters are also collections of
// resources and functional units. Each functional unit instance in a CPU
// (or cluster) definition can be specialized with resource expressions and
// register class parameters.
//
// Functional unit templates, like C++ templates, have parameters which can
// be specified on each instance of the functional unit, creating specialized
// instances of the functional unit.  They are composed of locally defined
// resources and specialized subunit instances. Subunits are specialized with
// resource expressions and defined ports (a type of FU-defined resource).
//
// Subunit templates similarly have parameters which can be specified on each
// instance of the subunit in a functional unit template, creating specialized
// instances of the subunit in each functional unit instance. A subunit template
// instantiates one or more latency templates, which are specialized with
// resources and ports.  Subunit templates are associated with each instruction
// in the machine description, and is how we tie instruction behaviors to
// CPUs and functional units.
//
// Latency templates similarly have parameters which can be specified on each
// instance of the latency in subunit templates.  Latencies are specialized by
// both the subunits they are instantiated be, and by the instructions they
// are applied to.
//
// This first phase of architecture expansion proceeds as follows:
//
//   For each CPU definition:
//      For each functional unit instance (or each FU in a Cluster definition):
//         Create a specialized functional unit instance for the <CPU/FU> tuple
//         For each subunit instance:
//            create the specialized instance,
//            for each latency instance:
//              specialize the latency for the subunit, add it to the subunit
//            add the subunit instance to a dictionary of subunits<CPU/FU/SU>
//         Instantiate any base functional units
//
// This phase creates a global dictionary of specialized subunit instances,
// and is used in the "generate" phase to generate latency instances that are
// specialized per CPU, Cluster, Functional unit, Subunit, and instructions.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mdl.h"
#include "llvm/Support/Regex.h"

namespace mpact {
namespace mdl {

//---------------------------------------------------------------------------
// Find the first pipeline phase from the first pipeline definition.  This
// is used as a "default" pipe phase identifier for implicit resources.
// If there are no phase names, make one up to avoid errors downstream.
//---------------------------------------------------------------------------
PhaseName *MdlSpec::FindFirstPhase() {
  if (first_phase_name_ != nullptr)
    return first_phase_name_;
  if (pipe_phases().empty() || pipe_phases()[0]->phase_names()->empty())
    return first_phase_name_ = new PhaseName("E1");

  return first_phase_name_ = (*pipe_phases()[0]->phase_names())[0];
}

//---------------------------------------------------------------------------
// Find the first pipeline phase from the first pipeline definition.  This
// is used as a "default" pipe phase identifier for implicit resources.
// First look in the specified CPU, then the top-level spec.  If you don't
// find it there, try "E1".  If you don't find that, use first phase.
//---------------------------------------------------------------------------
PhaseName *MdlSpec::FindFirstExecutePhase(CpuInstance *cpu) {
  if (cpu != nullptr && !cpu->pipe_phases()->empty())
    for (auto *pipe_def : *cpu->pipe_phases())
      if (pipe_def->first_execute_phase_name() != nullptr)
        return pipe_def->first_execute_phase_name();

  for (auto *pipe_def : pipe_phases())
    if (pipe_def->first_execute_phase_name() != nullptr)
      return pipe_def->first_execute_phase_name();

  auto first = Identifier("E1");
  if (auto *phase = SearchPipeReference(&first, cpu))
    return phase;
  return FindFirstPhase();
}

//---------------------------------------------------------------------------
// Conversions between strings and reference types.
//---------------------------------------------------------------------------
// For parsing the mdl input file.
RefType StringToRefType(const std::string &ref_type) {
  static std::map<std::string, RefType> *mapping =
      new std::map<std::string, RefType>({{"predicate", RefTypes::kPred},
                                          {"use", RefTypes::kUse},
                                          {"def", RefTypes::kDef},
                                          {"kill", RefTypes::kKill},
                                          {"usedef", RefTypes::kUseDef},
                                          {"hold", RefTypes::kHold},
                                          {"res", RefTypes::kReserve},
                                          {"fus", RefTypes::kFus}});
  if (mapping->count(ref_type))
    return mapping->at(ref_type);
  return RefTypes::kNull;
}

// Table to convert RefType values to dense set of indexes.
constexpr int RefMap[] = {
    0, 1, 2, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0,     //  0-15
    5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 16-31
    6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 32-47
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 48-63
    7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 64-79
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 80-95
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 96-111
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8}; // 112-128

// For writing out debug information.
std::string RefTypeToString(RefType ref_type) {
  static const char *refs[] = {"null",   "predicate", "use",     "def", "kill",
                               "usedef", "hold",      "reserve", "fus"};
  if (ref_type < RefTypes::kNull || ref_type > RefTypes::kFus)
    return "RefNull";
  return refs[RefMap[static_cast<int>(ref_type)]];
}

// For writing out the database.
std::string FormatReferenceType(RefType ref_type) {
  static const char *refs[] = {"RefNull", "RefPred",    "RefUse",
                               "RefDef",  "RefKill",    "RefUseDef",
                               "RefHold", "RefReserve", "RefFus"};
  if (ref_type < RefTypes::kNull || ref_type > RefTypes::kFus)
    return "RefNull";
  return refs[RefMap[static_cast<int>(ref_type)]];
}

// For writing out aggregate references (where they are ORed together).
std::string FormatReferenceTypes(int ref_type) {
  std::string out;
  if (ref_type & static_cast<int>(RefTypes::kPred))
    out += " Predicate";
  if (ref_type & static_cast<int>(RefTypes::kUse))
    out += " Use";
  if (ref_type & static_cast<int>(RefTypes::kDef))
    out += " Def";
  if (ref_type & static_cast<int>(RefTypes::kKill))
    out += " Kill";
  if (ref_type & static_cast<int>(RefTypes::kUseDef))
    out += " UseDef";
  if (ref_type & static_cast<int>(RefTypes::kHold))
    out += " Hold";
  if (ref_type & static_cast<int>(RefTypes::kReserve))
    out += " Reserve";
  if (ref_type & static_cast<int>(RefTypes::kFus))
    out += " Fus";
  return formatv("<{0}>", out.substr(1));
}

//---------------------------------------------------------------------------
// Create a subpool descriptor for a resource reference.
//---------------------------------------------------------------------------
SubPool::SubPool(const ResourceRef *res) {
  if (res->IsGroupRef()) {
    first_ = 0;
    last_ = res->definition()->members().size() - 1;
  } else {
    first_ = res->first();
    last_ = res->last();
  }
}

//---------------------------------------------------------------------------
// For each instruction that has no subunits specified, generate a default
// subunit that references each of its register operands in the same pipeline
// phase. Tie the subunit to a "fake" functional unit, and add that
// functional unit instance to each CPU instance.
//---------------------------------------------------------------------------
// NOTE: If an instruction doesn't have any subunits, we won't have any
// detailed functional unit or latency information for it.  The back-end
// latency management will report "default" latencies, and will not have
// any scheduling constraints for those instructions. If it's a "real"
// instruction (vs a pseudo-instruction), thats probably a bad idea.  The
// generation of default latency information is enabled by the
// "gen_missing_info" command-line flag.  Recommended you not use it.
//---------------------------------------------------------------------------
// NOTE: In theory, different CPUs could have different "first execute"
// stages, so we really ought to iterate over instructions separately for
// each CPU. But thats -really- expensive and its generally very reasonable
// to depend on the function finding "E1" in the spec-level phase table.
//---------------------------------------------------------------------------
void MdlSpec::CheckInstructionSubunits() {
  MdlItem item;
  std::string unit = "$pseudo_unit";
  int pseudo_subunits = 0;

  for (auto *instr : instructions())
    if (instr->subunits()->empty()) {
      auto *refs = new ReferenceList;
      // Create reference objects for each register-based operand. There's a
      // few complexities to this. Register class operands can be embedded
      // in other operands, and we need to properly represent the operand
      // hierarchy in the reference. Register-specific references can just
      // reference the register directly.
      for (const auto *opnd : *instr->flat_operands()) {
        auto *back = opnd->types()->back();
        auto *front = opnd->types()->front();
        OperandRef *ref_opnd = nullptr;
        if (auto *rclass = FindItem(reg_classes(), back->name())) {
          ref_opnd = new OperandRef(front, opnd->op_names(), rclass);
        } else if (FindItem(registers(), back->name()) != nullptr) {
          ref_opnd = new OperandRef(item, nullptr, new IdList(1, back));
        }
        RefType ref_type = opnd->is_input() ? RefTypes::kUse : RefTypes::kDef;
        auto *phase = FindFirstExecutePhase(nullptr);
        refs->push_back(new Reference(ref_type, phase, ref_opnd));
      }
      // Create an explicit reference to the functional unit.
      auto *phase = FindFirstPhase();
      refs->push_back(new Reference(RefTypes::kFus, phase, unit));

      // We create new templates for the latency and subunit, and new
      // instances for both, then add them to the appropriate spec tables.
      auto lname = formatv("$latency{0}", pseudo_subunits);
      auto sname = formatv("$subunit{0}", pseudo_subunits++);
      auto *latency = new LatencyTemplate(lname, refs);
      auto *instance = new LatencyInstance(lname);
      auto *subunit = new SubUnitTemplate(sname, instance, latency);

      // Add the latency and subunit templates to the global sets of units.
      latencies().push_back(latency);
      subunits().push_back(subunit);
      su_instantiations()[sname] = new std::vector<SubUnitInstantiation *>;

      // Add the latency and subunit templates to the dictionaries.
      lat_map().emplace(latency->name(), latency);
      su_map().emplace(subunit->name(), subunit);

      // Add a subunit instance to the instruction.
      instr->subunits()->push_back(new Identifier(sname));
    }

  if (pseudo_subunits == 0)
    return;

  // Add an implicitly defined functional unit template to the dictionary.
  fu_map_.emplace(unit, new FuncUnitTemplate(new Identifier(unit)));

  // Add the pseudo unit to the first cluster of each cpu instance.
  for (auto *cpu : cpus())
    if (!cpu->clusters()->empty())
      cpu->clusters()->front()->func_units()->push_back(
          new FuncUnitInstance(unit));
}

//---------------------------------------------------------------------------
// If a subunit referenced a CPU in an fus clause, create a "fake" functional
// unit and add the subunit to that.
//---------------------------------------------------------------------------
void MdlSpec::AddSubunitToCpu(CpuInstance *cpu, SubUnitTemplate *subunit) {
  auto fu_temp_name = catchall_name(cpu->name());

  // If the CPU doesn't have a catchall functional unit, create a CPU-specific
  // functional unit template, and add an instance of it to the CPU.
  auto *cluster = (*cpu->clusters())[0];
  auto *fu = FindItem(*cluster->func_units(), fu_temp_name);
  if (fu == nullptr) {
    auto *fu_template = new FuncUnitTemplate(new Identifier(fu_temp_name));
    fu_map_[fu_temp_name] = fu_template;
    cluster->func_units()->push_back(fu = new FuncUnitInstance(fu_temp_name));
    fu->set_template(fu_template);
  }

  // If the subunit hasn't been added previously, add it now.
  if (!FindItem(*fu->get_template()->subunits(), subunit->name())) {
    auto *instance = new SubUnitInstance(*subunit, subunit->type());
    instance->set_template(subunit);
    fu->get_template()->add_subunit_instance(instance);
  }
}

//---------------------------------------------------------------------------
//  Scan a reference list looking for functional unit references.
//  Create a set of them and return the set.  Note: we will accept CPU names
//  as well, and handle them properly.
//  We expand functional unit groups to the set of their members.  For groups
//  with only one member, we rewrite the resource reference to that func
//  unit template.
//---------------------------------------------------------------------------
void MdlSpec::FindLatencyFuncUnits(ReferenceList *references,
                                   LatencyFuncUnits &func_units) {
  for (auto *ref : *references) {
    if (ref->IsConditionalRef()) {
      for (auto *cnd = ref->conditional_ref(); cnd; cnd = cnd->else_clause())
        FindLatencyFuncUnits(&cnd->refs(), func_units);
    } else if (ref->ref_type() == RefTypes::kFus) {
      for (auto *res : *ref->resources()) {
        if (FindItem(fu_map_, res->name()) || FindItem(cpus_, res->name())) {
          func_units.AddUnit(res->name(), ref->predicates());
        } else if (auto *group = FindItem(fu_group_map(), res->name())) {
          if (group->members()->size() > 1) {
            for (auto *fu : *group->members())
              func_units.AddUnit(fu->name(), ref->predicates());
          } else {
            res->rename((*group->members())[0]);
            func_units.AddUnit(res->name(), ref->predicates());
          }
        } else {
          ErrorLog(res, "Invalid functional unit specifier: {0}", res->name());
        }
      }
    }
  }
}

//---------------------------------------------------------------------------
// Find the set of explicitly referenced FUS for each latency template.
//---------------------------------------------------------------------------
void MdlSpec::FindLatencyFuncUnits(LatencyTemplate *lat) {
  if (lat->referenced_fus()) return;

  // Find all the functional units mentioned by this latency template.
  auto *fus = new LatencyFuncUnits;
  lat->set_referenced_fus(fus);
  FindLatencyFuncUnits(lat->references(), *fus);

  // Find referenced fu sets for base units.
  if (lat->base_ids())
    for (auto *base : *lat->base_ids()) {
      FindLatencyFuncUnits(lat_map_[base->name()]);
      fus->AddBaseUnits(lat_map_[base->name()]->referenced_fus());
    }
}

//---------------------------------------------------------------------------
// For each functional unit template, enumerate all CPUs that instantiate it,
// including all uses of the unit as a subunit of another template.
//---------------------------------------------------------------------------
void MdlSpec::FindFunctionalUnitClientCpus(FuncUnitTemplate *funit,
                                           CpuInstance *cpu) {
  funit->add_client_cpu(cpu->name());
  for (auto *base : funit->unit_bases())
    FindFunctionalUnitClientCpus(base, cpu);
}

void MdlSpec::FindFunctionalUnitClientCpus() {
  for (auto *cpu : cpus_)
    for (auto *cluster : *cpu->clusters())
      for (auto *funit : *cluster->func_units())
        FindFunctionalUnitClientCpus(funit->get_template(), cpu);
}

//---------------------------------------------------------------------------
// For each CPU, build a vector of functional unit instances for each used
// functional unit template.  This is used when we're writing out fus()
// records.
//---------------------------------------------------------------------------
void MdlSpec::BuildFuncUnitInstancesMap() {
  for (auto *cpu : cpus_)
    for (auto *cluster : *cpu->clusters())
      for (auto *funit : cluster->fu_instantiations()) {
        auto &name = funit->func_type()->name();
        if (!is_catchall_name(name) && !is_based_fu_instance(name))
          cpu->func_unit_instances()[name].push_back(funit);
      }
}

bool HasValidPredicate(IdList *predicates, CpuInstance *cpu,
                       MdlSpec *spec) {
  if (predicates == nullptr) return true;

  for (auto *pred : *predicates)
    if (pred->name() == cpu->name()) return true;
  return false;
}


//---------------------------------------------------------------------------
// In a "bottom-up" architecture definition, we don't have explicit template
// definitions for functional units, and we need to tie latency "fus()"
// references to the CPU's that contain instances of the referenced functional
// units. We do that by creating (for each CPU) a CPU-specific "catchall"
// functional unit template (and an instance) that instantiates all of the
// subunits/latencies that reference that CPU's implicitly defined functional
// units.
//---------------------------------------------------------------------------
void MdlSpec::TieSubUnitsToFunctionalUnits() {
  // If there were no Fus records seen, we can skip this (expensive) work.
  if (!has_explicit_fu_refs()) return;

  // For each latency template, create a set of functional units it references.
  for (auto *lat : latencies_)
    FindLatencyFuncUnits(lat);

  // For each functional unit template, find the CPU's that instantiate it.
  FindFunctionalUnitClientCpus();

  // For each CPU, for each subunit template, for each latency, find any
  // explicit references to functional units.  We reiterate the search for
  // subunit (for each CPU) so that we can look for valid CPU-based predicates.
  for (auto *cpu : cpus_) {
    for (auto *subunit : subunits_) {
      std::set<std::string> func_units;
      if (subunit->latencies())
        for (auto *latency : *subunit->latencies())
          if (HasValidPredicate(latency->predicates(), cpu, this))
            if (auto *lat_fus = latency->get_template()->referenced_fus())
              for (auto &[pred, funits] : lat_fus->func_units())
                if (pred.empty() || pred == cpu->name())
                  for (auto &funit : funits)
                    func_units.insert(funit);

      if (func_units.empty()) continue;
      // If we found functional units for this subunit (for this CPU), add the
      // subunit to that CPU.
      // If the subunit template has parameters, print an error, since we
      // can't support that (implicitly defined functional units can't pass
      // parameters to the subunit instance.
      if (!subunit->params()->empty()) {
        ErrorLog(subunit, "Subunits/Latencies with parameters cannot reference"
                          " implicitly defined functional units: {0}",
                          subunit->name());
        break;
      }
      bool found = true;
      for (auto &unit : func_units) {
        // If the latency references a CPU name, add the subunit to that CPU.
        if (auto *cpu = FindItem(cpus_, unit)) {
          AddSubunitToCpu(cpu, subunit);
          continue;
        }
        // Annotate this template as explicitly referenced.
        fu_map_[unit]->set_referenced();

        // We only allow explicit functional unit references (fus) to
        // reference implicitly defined functional units. Referencing an
        // explicitly defined functional unit template is not supported,
        // so we issue a warning and ignore the reference.
        if (!fu_map_[unit]->is_implicitly_defined()) {
          WarningLog(subunit,
                     "Invalid reference to an explicitly defined "
                     "functional unit \"{0}\"",
                     unit);
          found = false;
          continue;
        }
        // If the current CPU is not a client of any mentioned unit, we will
        // not add this subunit to this CPU.
        if (!fu_map()[unit]->client_cpus().count(cpu->name()))
          found = false;
      }
      if (found) AddSubunitToCpu(cpu, subunit);
    }
  }

  // Since we've defined all implicitly defined functional units, and now
  // checked for references to them, check that implicitly defined templates
  // have references. Unreferenced implicit functional unit templates are
  // reported as a warning.  Implicitly defined based functional units must
  // have at least one base referenced.
  for (auto [name, fu] : fu_map_)
    if (fu->is_implicitly_defined() && !fu->is_referenced() &&
        !is_catchall_name(name)) {
      bool found_ref = false;
      for (auto *base : *fu->bases())
        if (fu_map_[base->name()]->is_referenced()) {
          found_ref = true;
          break;
        }
      if (!found_ref)
        WarningLog(fu, "Undefined functional unit \"{0}\"", name);
    }
}

// Helper function for recursively adding derived subunits to instructions.
static void AddDerivedSubUnits(InstructionDef *instruction,
                               SubUnitTemplate *subunit) {
  for (auto *derived_unit : subunit->derived_subunits()) {
    instruction->add_subunit(derived_unit);
    AddDerivedSubUnits(instruction, derived_unit);
  }
}

// Helper function to determine if a regular expression has a prefix that we
// can search for. Generally, this is anything up to the first metacharacter.
// However, if the expression has a top level | or ? operator, we can't
// define a prefix.
static std::string GetPrefix(std::string &regex) {
  static const char meta[] = "()^$*+?.[]\\{}";
  auto first_meta = regex.find_first_of(meta);
  if (first_meta == std::string::npos)
    return regex;

  int param = 0;
  for (char ch : regex) {
    if (ch == '(')
      param++;
    else if (ch == ')')
      param--;
    else if ((ch == '|' || ch == '?') && param == 0)
      return "";
  }

  return regex.substr(0, first_meta);
}

//---------------------------------------------------------------------------
// Given a list of regular expressions, add the subunit to each matched
// instruction.  Following tablegen's format, these aren't *quite* regular
// expressions in that they are always prefix searches - we must match
// the whole instruction name.
//---------------------------------------------------------------------------
void MdlSpec::TieSubUnitToInstructions(SubUnitTemplate *subunit,
                                       StringList *regex_bases) {
  if (regex_bases == nullptr)
    return;

  // We can speed the searches where the expression has an alphanumeric prefix,
  // by only searching names that begin with that prefix.
  for (auto &regex : *regex_bases) {
    auto prefix = GetPrefix(regex);
    auto pattern = regex.substr(prefix.size());

    std::optional<llvm::Regex> rex;
    if (!pattern.empty()) {
      if (pattern[0] != '^')
        pattern = formatv("^({0})", pattern);
      rex = llvm::Regex(pattern);
    }

    // If we see a prefix, we can narrow the range of instructions searched.
    bool match = false;
    auto end = instruction_map_.end();
    auto begin = instruction_map_.begin();
    if (!prefix.empty())
      begin = instruction_map_.lower_bound(prefix);

    // If we don't have a prefix, we need to search every single instruction.
    if (prefix.empty()) {
      for (auto itr = begin; itr != end; ++itr)
        if (rex->match(itr->first)) {
          itr->second->add_subunit(subunit);
          match = true;
        }
    } else {
      // If we have a prefix, only search instructions with that prefix.
      for (auto itr = begin; itr != end; ++itr) {
        if (itr->first.compare(0, prefix.size(), prefix) != 0)
          break;
        if (!rex || rex->match(itr->first.substr(prefix.size()))) {
          itr->second->add_subunit(subunit);
          match = true;
        }
      }
    }
    if (!match)
      ErrorLog(subunit, "Unmatched base instruction expression \"{0}\"", regex);
  }
}

//---------------------------------------------------------------------------
// Tie each derived subunit to each instruction that uses any of its base
// subunits (recursively).
//---------------------------------------------------------------------------
void MdlSpec::TieDerivedSubUnitsToInstructions() {
  for (auto *instruction : instructions_)
    if (auto *subunits = instruction->subunits()) {
      IdList base_subunits = *subunits; // We're going to add to vector...
      for (auto *subunit : base_subunits)
        AddDerivedSubUnits(instruction, su_map()[subunit->name()]);
    }
}

//---------------------------------------------------------------------------
// If a merged definition has allocation information, make sure it's correct.
//---------------------------------------------------------------------------
ResourceRef *FuncUnitInstantiation::CheckAllocation(ResourceRef *def,
                                                    ResourceRef *ref) {
  int count = ref->pool_count();
  Identifier *count_name = ref->pool_count_name();
  Identifier *value_name = ref->value_name();

  // Copy any allocation information from the reference to the definition.
  def->set_value_name(value_name);
  def->set_pool_count_name(count_name);
  def->set_pool_count(count);

  // Return if there is no allocation request, or the request is symbolic.
  // (We will check symbolic sizes later).
  if (!ref->HasCount() || count_name)
    return def;

  // Check array references.
  // Array allocations must be non-zero and between 1 and the pool size.
  if (def->IsArrayDef()) {
    if (count == 0 || count > def->pool_size()) {
      ErrorLog(ref, "Invalid resource allocation size: {0}", count);
      return nullptr;
    }
    // The pool size must evenly divide the number of entries in the pool.
    if (def->pool_size() % count != 0) {
      ErrorLog(ref, "Pool count must evenly divide the resource pool size");
      return nullptr;
    }
    return def;
  }
  // Allocation for everything else must be 1.
  if (count != 1) {
    ErrorLog(ref, "Invalid resource allocation size: {0}", count);
    return nullptr;
  }
  return def;
}

//---------------------------------------------------------------------------
// Given an incoming resource reference and a possibly-qualified use of
// that resource, check that the qualification makes sense, and produce a
// resultant resource reference in terms of the original resource.
// Some important assumptions about incoming references:
//     - Input pool references are represented with an explicit range.
//     - Input references should have an associated definition.
//     - Input references have already been error checked.
// The following definition/reference combinations are supported:
//   1.  name --> name                   // no change, always legal
//   2.  name --> name.member            // name is a group & member exists
//   3.  name[range] --> name[subrange]  // ok if subrange is legal
//   4.  name[range] --> name[#]         // ok if index is legal
//
// "def" represents a resource definition, which is either an explicit
// definition, or a template parameter bound to a definition.
// "ref" is a possibly-qualified use of that resource in an instantiation
// of a template.  This function returns a reference that represents the
// fully qualified reference.
//---------------------------------------------------------------------------
ResourceRef *FuncUnitInstantiation::MergeRefs(ResourceRef *def,
                                              ResourceRef *ref) {
  if (def->IsNull())
    return new ResourceRef(*def);

  // Case 1: if the reference is unqualified, just return the def.
  if (ref->IsUnqualifiedRef())
    return CheckAllocation(new ResourceRef(*def), ref);

  // Case 2: look up the member reference, and return a reference to the
  // group's promoted resource.
  if (def->IsGroupRef() && ref->member()) {
    auto *mem = FindItem(def->definition()->members(), ref->member()->name());
    if (mem == nullptr) {
      ErrorLog(ref, "Resource member not found: {0}", ref->member()->name());
      return nullptr;
    }
    auto *member = def->definition()->get_member_def(mem->index());
    return new ResourceRef(member);
  }

  // Case 3 and 4: Ensure the subrange is a subset of the def's range.
  // Note: All subranges are zero based relative to the original pool def.
  // But in general we don't want successive qualifications to make the
  // subrange larger.
  if (def->IsArrayDef() && (ref->IsSubrange() || ref->IsIndexed())) {
    if (ref->first() < def->first() || ref->last() > def->last()) {
      if (ref->IsIndexed())
        ErrorLog(ref, "Invalid resource pool index: {0}; expected [{1}..{2}]",
                 ref->first(), def->first(), def->last());
      else
        ErrorLog(ref, "Invalid resource pool subrange");
      return nullptr;
    }
    auto *qualified_ref = new ResourceRef(*def);
    qualified_ref->set_subrange(ref->first(), ref->last());
    return CheckAllocation(qualified_ref, ref);
  }

  // Member references cannot be further qualified.
  if (def->member()) {
    ErrorLog(ref, "Invalid member reference qualification");
    return nullptr;
  }
  // Member references can only be used with grouped resources.
  if (ref->member()) {
    ErrorLog(ref, "Invalid member reference: {0}", ref->member()->name());
    return nullptr;
  }

  // For everything else, check any pool allocations.
  if (def->IsGroupDef() || def->IsArrayDef())
    return CheckAllocation(new ResourceRef(*def), ref);

  ErrorLog(ref, "Invalid resource qualifiers");
  return nullptr;
}

//----------------------------------------------------------------------------
// Implementations of FuncUnitInstantiation methods.
//----------------------------------------------------------------------------

// Create definition objects for each locally defined reference and port.
void FuncUnitInstantiation::InstantiateLocalDefs() {
  for (auto *res : *get_template()->resources())
    resources().push_back(new ResourceDef(*res));
  for (auto *port : *get_template()->ports())
    ports().push_back(new ResourceDef(port));
}

// Look up a register class in the template's parameter list.
RegisterClass *FuncUnitInstantiation::FindRegClass(Identifier *item) {
  if (auto *arg = FindItem(class_args(), item->name()))
    return arg->regs();
  return nullptr;
}

// Bind a functional unit instantiation parameter to a register class.
void FuncUnitInstantiation::BindClassArg(ResourceRef *arg) {
  class_args()[arg->get_parameter()->name()] = BindFuncUnitClass(arg);
}

// Bind a functional unit instantiation parameter to a resource reference.
void FuncUnitInstantiation::BindResourceArg(ResourceRef *arg) {
  resource_args()[arg->get_parameter()->name()] = BindFuncUnitResource(arg);
}

// Map a functional unit instantiation parameter id to its bound class.
RegisterClassRef *FuncUnitInstantiation::GetClassArg(int param_id) {
  return class_args()[(*get_template()->params())[param_id]->name()];
}

// Map a functional unit instantiation parameter id to its bound resource.
ResourceRef *FuncUnitInstantiation::GetResourceArg(int param_id) {
  return resource_args()[(*get_template()->params())[param_id]->name()];
}

// Given a predicate for a subunit or latency instance, determine if it
// matches the instantiation context's cpu name, functional unit name, or
// functional unit template type.
bool FuncUnitInstantiation::ValidPredicate(IdList *predicates) const {
  if (predicates == nullptr)
    return true;
  for (auto *id : *predicates) {
    if (id->name() == cpu()->name() || id->name() == instance()->name() ||
        id->name() == func_type()->name() ||
        spec()->IsValidInstructionPredicate(id->name()))
      return true;
    spec()->IsValidPredicateName(id);
  }
  return false;
}

//---------------------------------------------------------------------------
// For each subunit instance in a functional unit instantiation, create a
// subunit instantiation, bind its instance parameters, and instantiate
// all of its latency instances.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::InstantiateSubunits() {
  for (auto *instance : *get_template()->subunits())
    if (ValidPredicate(instance->predicates())) {
      auto *subunit = new SubUnitInstantiation(this, instance);
      BindSubUnitParameters(subunit);
      subunit->InstantiateLatencies();
      spec()->AddSubUnitInstantiation(subunit);
    }
}

//---------------------------------------------------------------------------
// Process connect statements. Find the connected resources and register
// classes, do some error checking.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::ProcessConnects() {
  for (auto *connect : *get_template()->connections()) {
    // First make sure the referenced port definition exists.
    auto *port = FindItem(ports(), connect->name());
    if (port == nullptr) {
      ErrorLog(connect, "Port not found: {0}", connect->name());
      return;
    }

    // If a register class is specified, find it, either as an incoming
    // argument or globally defined.
    if (connect->reg_class()) {
      auto *name = connect->reg_class();
      auto *reg_class = FindRegClass(name);
      if (reg_class == nullptr)
        reg_class = FindItem(spec()->reg_classes(), name->name());
      if (reg_class == nullptr) {
        ErrorLog(connect, "Register class not found: {0}", name->name());
        return;
      }
      port->set_reg_class(reg_class);
    }

    // If a resource reference was provided, verify it.
    if (auto *resource = connect->resource()) {
      ResourceRef *ref = nullptr;
      if (auto *res = FindItem(resource_args(), resource->name())) {
        ResourceRef arg(*res);
        ref = MergeRefs(&arg, resource);
      } else if (auto *def = FindItem(resources(), resource->name())) {
        ResourceRef arg(def);
        ref = MergeRefs(&arg, resource);
      }

      if (ref == nullptr)
        ErrorLog(connect, "Resource not found: {0}", resource->name());
      else
        port->set_port_resource(ref);
    }
    if (ErrorsSeen())
      return;
  }
}

//---------------------------------------------------------------------------
// Bind a subunit instance port argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceDef *FuncUnitInstantiation::BindSubUnitPort(ResourceRef *arg) {
  if (arg->IsNull())
    return NullPortDef;
  if (auto *port_arg = FindItem(ports(), arg->name()))
    return port_arg;

  ErrorLog(arg, "Port argument not found: {0}", arg->name());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a subunit resource argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceRef *FuncUnitInstantiation::BindSubUnitResource(ResourceRef *arg) {
  // If this is a "null" binding, just return null.
  if (arg->IsNull())
    return NullResourceRef;

  // Search for the resource definition in arguments and FU-level definitions.
  if (auto *resource = FindItem(resource_args(), arg->name())) {
    ResourceRef ref(*resource);
    return MergeRefs(&ref, arg);
  }
  if (auto *resource = FindItem(resources(), arg->name())) {
    ResourceRef def(resource);
    return MergeRefs(&def, arg);
  }

  ErrorLog(arg, "Resource argument not found: {0}", arg->name());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a functional unit instance resource argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceRef *FuncUnitInstantiation::BindFuncUnitResource(ResourceRef *arg) {
  // If this is a "null" binding, just return null.
  if (arg->IsNull())
    return NullResourceRef;

  // Search for resource definition in the cluster and CPU level.
  ResourceDef *def;
  if ((def = FindItem(*cluster()->resources(), arg->name())) ||
      (def = FindItem(*cpu()->resources(), arg->name()))) {
    ResourceRef ref(def);
    return MergeRefs(&ref, arg);
  }

  ErrorLog(arg, "Resource argument not found: {0}", arg->name());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a functional unit instance register class argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
RegisterClassRef *FuncUnitInstantiation::BindFuncUnitClass(ResourceRef *arg) {
  // If this is a "null" binding, just return null.
  if (arg->IsNull())
    return new RegisterClassRef(NullRegisterClass);

  // Look up the register class in the global class table.
  if (auto *item = FindItem(spec()->reg_classes(), arg->name()))
    return new RegisterClassRef(item);

  // If we don't find the class, but find a register definition, create a
  // custom class that contains just that single register (a common case).
  if (RegisterDef *reg = FindItem(spec()->registers(), arg->name())) {
    auto *members = new RegisterDefList;
    members->push_back(reg);
    std::string newname = "[" + arg->name() + "]";
    return new RegisterClassRef(
        new RegisterClass(*arg, new Identifier(newname), members));
  }

  ErrorLog(arg, "Register class argument not found: {0}", arg->name());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind functional unit instantiation parameters to resources and classes.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::BindFuncUnitParameters() {
  auto &instance_args = *instance()->args();
  int num_params = instance_args.size();

  // Iterate over the parameters, bind the parameters of the instance
  // to the objects (register classes or resources) they refer to.
  for (int argid = 0; argid < num_params; argid++) {
    if (instance_args[argid]->get_parameter()->IsResource()) {
      BindResourceArg(instance_args[argid]);
    } else {
      BindClassArg(instance_args[argid]);
    }
  }
}

//---------------------------------------------------------------------------
// Look up functional unit pinning resources.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::BindFuncUnitSlotResources() {
  IdList *slots_any = instance()->pin_any();
  IdList *slots_all = instance()->pin_all();
  IdList *slots = slots_any ? slots_any : slots_all;
  IdList *implicit_slots = nullptr;

  // If the instance wasn't pinned to any slots, and slots have been
  // declared for this cpu/cluster, create an "any" set of resources.
  if (slots == nullptr && (cluster()->issues() || cpu()->issues())) {
    auto *issues = cluster()->issues() ? cluster()->issues() : cpu()->issues();
    slots = slots_any = implicit_slots = new IdList;
    for (auto *res : *issues)
      slots->push_back(res->id());
  }

  // Find the definition of any pin reference.
  auto resource_list = new ResourceRefList;
  ResourceDef *res;
  for (auto *slot : *slots) {
    if ((res = FindItem(*cluster()->issues(), slot->name())) ||
        (res = FindItem(*cpu()->issues(), slot->name())))
      resource_list->push_back(new ResourceRef(res));
    else
      ErrorLog(res, "Issue slot resource not found: {0}", slot->name());
  }

  // Add the slot references to the functional unit instance.
  if (slots_any)
    instance()->set_resource_slots_any(resource_list);
  else
    instance()->set_resource_slots_all(resource_list);

  if (implicit_slots != nullptr)
    delete implicit_slots;
}

//---------------------------------------------------------------------------
// Bind subunit instantiation parameters to ports and resources.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::BindSubUnitParameters(SubUnitInstantiation *su) {
  auto &instance_args = *su->subunit()->args();
  int num_params = instance_args.size();

  for (int argid = 0; argid < num_params; argid++)
    if (instance_args[argid]->get_parameter()->IsResource())
      su->BindResourceArg(instance_args[argid]);
    else
      su->BindPortArg(instance_args[argid]);
}

//---------------------------------------------------------------------------
// Implementation of SubUnitInstantiation methods.
//---------------------------------------------------------------------------

// Bind a port definition to the specified subunit instantiation parameter.
void SubUnitInstantiation::BindPortArg(ResourceRef *arg) {
  port_args()[arg->get_parameter()->name()] = func_unit()->BindSubUnitPort(arg);
}

// Bind a resource definition to the specified subunit instantiation parameter.
void SubUnitInstantiation::BindResourceArg(ResourceRef *arg) {
  resource_args()[arg->get_parameter()->name()] =
      func_unit()->BindSubUnitResource(arg);
}

// Map a subunit instantiation parameter id to its bound resource.
ResourceRef *SubUnitInstantiation::GetResourceArg(int param_id) {
  return resource_args()[(*su_template()->params())[param_id]->name()];
}

// Map a subunit instantiation parameter id to its bound port.
ResourceDef *SubUnitInstantiation::GetPortArg(int param_id) {
  return port_args()[(*su_template()->params())[param_id]->name()];
}

// Given a predicate for a latency instance, determine if it matches the
// instantiation context's cpu name or functional unit name.
bool SubUnitInstantiation::ValidPredicate(IdList *predicates) const {
  return func_unit()->ValidPredicate(predicates);
}

//---------------------------------------------------------------------------
// Bind a latency instance port argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceDef *SubUnitInstantiation::BindLatPort(ResourceRef *arg) {
  if (arg->IsNull())
    return NullPortDef;
  if (auto *port_arg = FindItem(port_args(), arg->name()))
    return port_arg;

  ErrorLog(arg, "Port argument not found: {0}", arg->name());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a latency resource argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceRef *SubUnitInstantiation::BindLatResource(ResourceRef *arg) {
  // If this is a "null" binding, just return null.
  if (arg->IsNull())
    return NullResourceRef;

  // Search for the resource definition in arguments an SU-level definitions.
  if (auto *resource = FindItem(resource_args(), arg->name())) {
    ResourceRef ref(*resource);
    return func_unit()->MergeRefs(&ref, arg);
  }

  ErrorLog(arg, "Resource argument not found: {0}", arg->name());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind latency instantiation parameters to ports and resources.
//---------------------------------------------------------------------------
void SubUnitInstantiation::BindLatencyParams(LatencyInstantiation *lat) {
  auto &instance_args = *lat->latency()->args();
  int num_params = instance_args.size();

  for (int argid = 0; argid < num_params; argid++) {
    if (instance_args[argid]->get_parameter()->IsResource()) {
      lat->BindResourceArg(instance_args[argid]);
    } else {
      lat->BindPortArg(instance_args[argid]);
    }
  }
}

//---------------------------------------------------------------------------
// Bind latency reference resources to template parameters.
//---------------------------------------------------------------------------
void SubUnitInstantiation::BindLatencyResources(LatencyInstantiation &lat,
                                                Reference *reference,
                                                ResourceRefList *resources) {
  for (auto *res : *resources) {
    ResourceRef *ref = nullptr;
    if (auto *resource = FindItem(lat.resource_args(), res->name())) {
      ref = func_unit()->MergeRefs(resource, res);
    } else if (auto *port = FindItem(lat.port_args(), res->name())) {
      reference->add_port(port);
      if (auto *port_res = port->port_resource())
        ref = func_unit()->MergeRefs(port_res, res);
    } else if (!res->IsNull()) {
      ErrorLog(res, "Resource undefined: {0}", res->name());
    }
    // If we have a valid resource reference, add it to the reference.
    if (ref == nullptr)
      continue;
    reference->add_resource(ref);

    // Check for unqualified pool/group references.  If it's a group
    // reference, either use all the members or just one, depending on
    // how the group was defined. If it's an array reference, print an
    // error message.
    if (ref->IsPooledResourceRef() && !res->use_all_members()) {
      if (ref->IsGroupRef()) {
        if (ref->definition()->group_type() == GroupType::kUseAll)
          ref->set_use_all_members();
        else
          ref->set_pool_count(1); // Set pool allocation for group to 1.
      } else {                    // It's an array reference.
        ErrorLog(reference,
                 "Unqualified pool - use :* to reference whole pool: {0}",
                 ref->ToString());
      }
    }
  }
}

//---------------------------------------------------------------------------
// Recursively copy conditional references and all their references and
// else clauses.
//---------------------------------------------------------------------------
ConditionalRef *
SubUnitInstantiation::CopyLatencyCondReference(LatencyInstantiation &lat,
                                               ConditionalRef *cond) {
  // Copy the else clause, if there is one.
  if (cond == nullptr)
    return nullptr;
  auto *else_clause = CopyLatencyCondReference(lat, cond->else_clause());

  // Make a copy of the conditional reference (and the copied else clause),
  // and copy the references associated with this condition.
  auto *copy = new ConditionalRef(cond, else_clause);
  for (auto *ref : cond->refs())
    CopyLatencyReference(lat, copy->refs(), ref);
  return copy;
}

//---------------------------------------------------------------------------
// When instantiating a latency, copy each reference, bind resources to
// instance parameters, and do some error checking.
//---------------------------------------------------------------------------
void SubUnitInstantiation::CopyLatencyReference(LatencyInstantiation &lat,
                                                ReferenceList &references,
                                                Reference *ref) {
  if (!ValidPredicate(ref->predicates()))
    return;

  // Recursively copy conditional references.
  if (ref->IsConditionalRef()) {
    auto *cond = CopyLatencyCondReference(lat, ref->conditional_ref());
    references.push_back(new Reference(ref, cond));
    return;
  }

  // If the reference doesn't have a phase expression, give it one.
  auto *phase = ref->phase_expr();
  if (phase == nullptr)
    phase = new PhaseExpr(spec_->FindFirstExecutePhase(cpu()));
  else
    phase = phase->clone();

  // Copy the reference, and for each resource reference, bind the named
  // resource to the value passed into the subunit instance resource or
  // port parameter.
  auto *new_reference = new Reference(ref, phase);
  if (!ref->IsFuncUnitRef())
    BindLatencyResources(lat, new_reference, ref->resources());

  // Currently we don't allow holds/reserves on pooled resources.
  if (new_reference->ref_type() & (RefTypes::kHold | RefTypes::kReserve))
    for (auto *res : *new_reference->resources())
      if (res->HasCount())
        ErrorLog(ref, "Hold/reserve not supported on pool references: {0}",
                 res->ToString());

  references.push_back(new_reference);
}

//---------------------------------------------------------------------------
// Add references from a latency template to a subunit. lat_template is
// passed in explicitly so that we can instantiate parents and bases.
//---------------------------------------------------------------------------
void SubUnitInstantiation::InstantiateLatency(LatencyInstantiation &lat,
                                              LatencyTemplate *lat_template) {
  for (auto *reference : *lat_template->references())
    CopyLatencyReference(lat, references(), reference);
}

//---------------------------------------------------------------------------
// Instantiate a latency template and all of its bases, recursively.
//---------------------------------------------------------------------------
void SubUnitInstantiation::InstantiateLatencyBases(LatencyInstantiation &lat,
                                                   LatencyTemplate *parent,
                                                   LatencyList &bases) {
  // There's no need to instantiate a latency template in a particular
  // subunit more than once (which is possible if you have multiple bases,
  // or recursive bases).
  if (std::find(bases.begin(), bases.end(), parent) != bases.end())
    return;
  bases.push_back(parent);

  InstantiateLatency(lat, parent);
  if (ErrorsSeen())
    return;

  for (auto *base : parent->unit_bases()) {
    InstantiateLatencyBases(lat, base, bases);
    if (ErrorsSeen())
      return;
  }
}

//---------------------------------------------------------------------------
// Instantiate all the latencies (and latency bases) associated with a
// subunit instantiation.
//---------------------------------------------------------------------------
void SubUnitInstantiation::InstantiateLatencies() {
  if (su_template()->latencies() == nullptr)
    return;

  for (auto *instance : *su_template()->latencies())
    if (ValidPredicate(instance->predicates())) {
      LatencyInstantiation latency(this, instance);
      BindLatencyParams(&latency);
      if (ErrorsSeen())
        return;
      LatencyList bases; // used to avoid duplicates and recursion.
      InstantiateLatencyBases(latency, instance->get_template(), bases);
      if (ErrorsSeen())
        return;
    }
}

//---------------------------------------------------------------------------
// Implementation of LatencyInstantiation methods.
//---------------------------------------------------------------------------

// Bind a port definition to the specified latency instantiation parameter.
void LatencyInstantiation::BindPortArg(ResourceRef *arg) {
  port_args()[arg->get_parameter()->name()] = subunit()->BindLatPort(arg);
}

// Bind a resource definition to the specified latency instantiation parameter.
void LatencyInstantiation::BindResourceArg(ResourceRef *arg) {
  resource_args()[arg->get_parameter()->name()] =
      subunit()->BindLatResource(arg);
}

// Map a latency instantiation parameter id to its bound resource.
ResourceRef *LatencyInstantiation::GetResourceArg(int param_id) {
  return resource_args()[(*lat_template()->params())[param_id]->name()];
}

// Map a latency instantiation parameter id to its bound port.
ResourceDef *LatencyInstantiation::GetPortArg(int param_id) {
  return port_args()[(*lat_template()->params())[param_id]->name()];
}

//----------------------------------------------------------------------------
// Implementations of MdlSpec methods.
//----------------------------------------------------------------------------

// Create and add a Functional Unit instantiation to the mdl spec table.
FuncUnitInstantiation *
MdlSpec::AddFuncUnitInstantiation(CpuInstance *cpu, ClusterInstance *cluster,
                                  FuncUnitInstance *fu_inst) {
  auto *fu = new FuncUnitInstantiation(this, cpu, cluster, fu_inst);
  cluster->AddFuncUnitInstantiation(fu);
  return fu;
}

// Create a base function unit instance object and add to mdl spec table.
FuncUnitInstantiation *
MdlSpec::AddFuncUnitBaseInstantiation(FuncUnitInstantiation *parent,
                                      FuncUnitTemplate *base) {
  auto *fu = new FuncUnitInstantiation(parent, base);
  parent->cluster()->AddFuncUnitInstantiation(fu);
  return fu;
}

// Recursively add base functional units to instantiated parents.
void MdlSpec::AddFunctionalUnitBases(FuncUnitInstantiation *parent) {
  auto *root = parent->get_template();
  for (auto *base : root->unit_bases()) {
    if (base == root) {
      WarningLog(base, "Recursive functional unit derivation, ignored");
      continue;
    }
    if (!base->unit_bases().empty())
      WarningLog(base, "Nested functional unit derivation, ignored");

    auto *fu = AddFuncUnitBaseInstantiation(parent, base);
    fu->set_resource();
    fu->ProcessConnects();
    fu->InstantiateSubunits();
  }
}

// Instantiate a single functional unit, and all of its base units.
void MdlSpec::InstantiateFunctionalUnit(CpuInstance *cpu,
                                        ClusterInstance *cluster,
                                        FuncUnitInstance *fu) {
  auto *fu_top = AddFuncUnitInstantiation(cpu, cluster, fu);
  fu_top->set_resource();

  // Bind parameters to their associated definitions, check for errors.
  // If errors found, don't try to instantiate any subunits.
  fu_top->BindFuncUnitParameters();
  if (ErrorsSeen())
    return;

  // After processing parameters, promote groups.
  PromoteResourceGroupMembers(&fu_top->resources(), nullptr,
                              &fu_top->resource_args());

  // Bind pinning resources.
  fu_top->BindFuncUnitSlotResources();

  // Process connect statements and instantiate subunits.
  fu_top->ProcessConnects();
  fu_top->InstantiateSubunits();

  // For each base unit, create a separate instantiation with the same
  // parameters as the parent, and instantiate its subunits.
  AddFunctionalUnitBases(fu_top);
}

// Instantiate every functional unit instance (in every CPU and cluster).
// Simply abort if any errors are found.
void MdlSpec::InstantiateFunctionalUnits() {
  for (auto *cpu : cpus())
    for (auto *cluster : *cpu->clusters())
      for (auto *fu_inst : *cluster->func_units())
        InstantiateFunctionalUnit(cpu, cluster, fu_inst);

  if (ErrorsSeen())
    Abort();
}

bool IsValidPoolCount(const ResourceRef *resource, const Reference *ref,
                      int count, const SubUnitInstantiation *subunit) {
  if (count == 0)
    return false;
  if (count < 0) {
    subunit->ErrorLog(ref, "Negative allocation size");
    return false;
  }
  if (count > resource->pool_size()) {
    subunit->ErrorLog(ref, "Allocation size exceeds resource pool size: {0}",
                      resource->ToString());
    return false;
  }
  return true;
}

// Update the subpool reference table for this reference.
void ResourceDef::AddReferenceSizeToPool(const ResourceRef *resource,
                                         const Reference *ref,
                                         const SubUnitInstantiation *subunit) {
  SubPool pool(resource);
  auto &pool_info = sub_pool(pool);

  // If the pool has a defined pool count, just use it.
  if (resource->pool_count() != -1) {
    if (IsValidPoolCount(resource, ref, resource->pool_count(), subunit)) {
      pool_info.add_count(resource->pool_count());
      return;
    }
  }

  // If we have no idea what a symbolic size attribute is, just record the
  // worst case number (the whole pool).
  if (ref == nullptr || ref->operand() == nullptr) {
    pool_info.add_count(resource->pool_size());
    return;
  }

  // Find the whole derivation of the operand (if any).
  auto *opnd_ref = ref->operand();
  auto *opnd_base = opnd_ref->operand_decl()->operand();
  auto *opnd_def = opnd_ref->operand();
  OperandDefList opnds;
  if (!FindDerivation(opnd_def, opnd_base, opnds))
    return; // This is a panic, which will have already been seen and reported.

  // Make sure we find at least one occurrence of the named attribute.
  // If it's not found, set a worst-case pool count.
  auto &count_name = resource->pool_count_name()->name();
  OperandAttribute *attr = nullptr;
  for (auto *opnd : opnds)
    if ((attr = FindAttribute(count_name, opnd, subunit)) != nullptr)
      break;
  if (attr == nullptr) {
    pool_info.add_count(resource->pool_size());
    return;
  }

  // Walk through all the operand derivations, and find values associated with
  // the attribute, and add them to the resource pools definition.
  for (auto *opnd : opnds)
    for (auto *op_attr : *opnd->attributes())
      if (op_attr->name() == count_name)
        if (subunit->ValidPredicate(op_attr->predicate()))
          if (IsValidPoolCount(resource, ref, op_attr->values(0), subunit))
            pool_info.add_count(op_attr->values(0));
}

// Annotate a resource with the attributes of a reference to it.
void ResourceDef::RecordReference(RefType type, const PhaseExpr *expr,
                                  const ResourceRef *resource,
                                  const Reference *ref,
                                  const SubUnitInstantiation *subunit) {
  // For pools, record each pool size and the number of resources requested.

  if (resource != nullptr && resource->HasCount()) {
    AddReferenceSizeToPool(resource, ref, subunit);
    add_alloc_size(resource->pool_count());
  }

  ref_types_ |= type;
  if (!expr->IsExpressionConstant()) {
    phase_expr_seen_ = true;
    return;
  }
  auto phase = expr->EvaluateConstantExpression();
  if (!phase)
    return;
  int cycles = ref ? (ref->use_cycles() - 1) : 0;
  if (earliest_ref_ == -1 || *phase < earliest_ref_)
    earliest_ref_ = *phase;
  if (latest_ref_ == -1 || *phase + cycles > latest_ref_)
    latest_ref_ = *phase + cycles;
}

// Add a nice debug name to a resource definition.
void ResourceDef::set_debug_name(std::string type, const CpuInstance *cpu,
                                 const ClusterInstance *cluster,
                                 const FuncUnitInstantiation *fu) {
  std::string cpus = cpu ? formatv("{0}.", cpu->name()) : "";
  std::string cls = cluster ? formatv("{0}.", cluster->name()) : "";
  std::string fus = fu ? formatv("{0}.", fu->name()) : "";
  debug_name_ = formatv("{0}.{1}{2}{3}{4}", type, cpus, cls, fus, name());
}

// Assign a resource id to a resource definition.
// Note that we don't assign ids to groups or their members, they are subsumed
// by their promoted members.
static int AssignId(ResourceDef *def, int resource_id) {
  def->set_resource_id(resource_id);
  if (def->pool_size() > 1)
    return resource_id + def->pool_size();
  return resource_id + 1;
}

// Assign resource ids to each functional unit, issue slot, and resource
// defined for each CPU. Note: We don't assign ids to global resources, since
// they are all copied into each CPU's resource set.
// Notes:
//    - We assign functional units, then issue slots, then CPU resources, then
//      cluster resources, then functional unit instance resources, because
//      we want FU and Issue slots to have the lowest ids (helps speed up
//      scheduling).
//    - We note the largest functional unit id for each CPU.
//    - We note the largest CPU-defined resource for each CPU.
void MdlSpec::AssignResourceIds() {
  for (auto *cpu : cpus()) {
    ResourceDef *last_fu = nullptr;

    // First add resources defined for each functional unit in each cluster.
    for (auto *cluster : *cpu->clusters())
      for (auto *fu : cluster->fu_instantiations())
        if (!fu->instance()->is_catchall_unit() &&
            fu->get_resource()->is_used() && fu->parent() == nullptr)
          cpu->add_cpu_resource(fu->get_resource(), "FuncUnit", cpu, cluster,
                                nullptr);

    // Note the resource id of the last functional unit.
    if (!cpu->all_resources().empty())
      last_fu = cpu->all_resources().back();

    // Add resources defined for CPU-level issue slots.
    for (auto *res : *cpu->issues())
      cpu->add_cpu_resource(res, "Issue", cpu, nullptr, nullptr);

    // Add resources defined for cluster-level issue slots.
    for (auto *cluster : *cpu->clusters())
      for (auto *res : *cluster->issues())
        cpu->add_cpu_resource(res, "Issue", cpu, cluster, nullptr);

    // Add all resources defined at the CPU level.
    for (auto *res : *cpu->resources())
      cpu->add_cpu_resource(res, "Resource", cpu, nullptr, nullptr);

    // Add all other resources defined in clusters.
    for (auto *cluster : *cpu->clusters())
      for (auto *res : *cluster->resources())
        cpu->add_cpu_resource(res, "Resource", cpu, cluster, nullptr);

    // Add resources defined in functional unit instantiations.
    for (auto *cluster : *cpu->clusters()) {
      for (auto *fu : cluster->fu_instantiations())
        for (auto *resource : fu->resources())
          cpu->add_cpu_resource(resource, "FU Resource", cpu, cluster, fu);
    }

    // Add one fake resource to mark the end of the list.
    cpu->add_cpu_resource(new ResourceDef("end"), "fake", cpu, nullptr,
                          nullptr);

    // We've collected all the resources together, assign ids.
    // We skip resource groups, since their members were promoted to
    // regular resources.
    int resource_id = 1;
    for (auto *res : cpu->all_resources())
      if (!res->IsGroupDef())
        resource_id = AssignId(res, resource_id);

    if (last_fu)
      cpu->set_max_fu_id(last_fu->get_resource_id());
    cpu->set_max_resource_id(resource_id);
  }
}

// Assign pool ids (per CPU) to each resource that defines a pool.
void MdlSpec::AssignPoolIds() {
  for (auto *cpu : cpus()) {
    // First find and sort the pools by their size.  We prefer to allocate
    // the most constrained pools first.
    for (auto *res : cpu->all_resources())
      if (res->IsPoolDef())
        cpu->add_pool_resource(res);

    std::sort(cpu->pool_resources().begin(), cpu->pool_resources().end(),
              [](const ResourceDef *a, const ResourceDef *b) {
                return a->size() < b->size();
              });

    int pool_id = 0;
    for (auto *res : cpu->pool_resources()) {
      res->set_pool_id(pool_id);
      for (auto &[pool, pool_info] : res->sub_pools()) {
        pool_info.set_subpool_id(pool_id);
        pool_id += *pool_info.counts().rbegin();
      }
    }
    cpu->set_pool_count(pool_id);
  }
}

} // namespace mdl
} // namespace mpact

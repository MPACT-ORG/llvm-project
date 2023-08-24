//===- arch.cpp - Extract architecture model from tablegen records  -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Extract the MDL model from the information scraped from TableGen.
//
// We use ProcessorModel, SchedModel, ProcResource, SchedReadWrite, and
// InstRW records to build an internal representation of an MDL machine model.
// If Itineraries are present, we also process FuncUnit, ProcessorItinerary,
// InstrItinData, InstrStage, and Bypass objects to build the internal model
// for instruction bundling.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "scan.h"
#include "llvm/Support/Regex.h"

namespace mpact {
namespace mdl {
namespace scan {

//-------------------------------------------------------------------------
// Annotate functional units which have resource groups with a full set of
// the members of the resource group. We assume that any resource can be
// a resource group, so we need to recur on each member of a group.
//-------------------------------------------------------------------------
void MachineDescription::PopulateResourceGroup(ProcResource *func) {
  // If a functional unit doesn't have a resource group, just add its name
  // to its (otherwise empty) group.
  if (!func->is_resource_group()) {
    func->resource_group().insert(func->name());
    return;
  }
  // If we've already processed this object, return.
  if (!func->resource_group().empty())
    return;

  for (const auto &item : func->group_names()) {
    PopulateResourceGroup(proc_resources_[item]);
    auto &members = proc_resources_[item]->resource_group();
    func->resource_group().insert(members.begin(), members.end());
  }
}

//-------------------------------------------------------------------------
// Populate CPUs with Functional units and issue width.
//-------------------------------------------------------------------------
void MachineDescription::PopulateCPUsWithFUs() {
  // Annotate SchedModels with cpu definitions that use them.
  for (const auto &[name, cpu] : cpus_)
    sched_models_[cpu->sched_model()]->add_cpu(name);

  // Expand ProcResourceGroups for each functional unit.
  for (const auto &[name, unit] : proc_resources_)
    PopulateResourceGroup(unit);

  // If a func_unit has a super unit, add that functional unit to the super
  // unit as a subunit. If a functional unit has a SchedModel and not a
  // Super unit, add it to that SchedModel's list of functional units.
  for (auto &[name, unit] : proc_resources_) {
    if (!unit->fu_super().empty()) {
      proc_resources_[unit->fu_super()]->add_child_unit(unit->name());
    } else if (!unit->sched_model().empty()) {
      for (const auto &item : unit->resource_group())
        if (proc_resources_[item]->fu_super().empty())
          sched_models_[unit->sched_model()]->add_func_unit(item);
    }
  }

  // For every SchedWrite object, add its ProcResources to its SchedModel.
  for (const auto &[name, rw_unit] : rw_units_)
    if (rw_unit->is_write())
      for (auto &[model, sched_model] : rw_unit->sched_model_info())
        for (auto &resource : sched_model.func_units())
          if (proc_resources_[resource]->fu_super().empty())
            for (const auto &unit : proc_resources_[resource]->resource_group())
              if (proc_resources_[unit]->fu_super().empty())
                sched_models_[model]->add_func_unit(unit);

  // For each functional unit instance, create a set of super-unit names.
  for (auto &[name, res] : proc_resources())
    CreateSuperUnitNames(name);
}

// A "Super" unit is a ProcResource (ie functional unit in MDL) which is
// named by other ProcResources as a "Super" unit, forming a tree-like
// hierarchy of ProcResources through "Super" and "child" links. Each
// ProcResource has one or more "instances", and an arbitrary set of
// children - which also have arbitrary instances and children. A ProcResource
// can have only a single Super unit, so the hierarchy forms a tree (and
// importantly, not a DAG nor a general DFG).
//
// The instances of a top-level Super ProcResource model a set of parallel
// execution streams. Its child units must be (recursively) allocated across
// its instances. Each instance of a ProcResource is a collection of
// functional units that are associated with the ProcResource's execution
// stream. In the MDL, this is modeled as a set of functional units each with
// a set of base units.
//
// So briefly, the algorithm does a depth-first walk of each ProcResource tree,
// and allocates children instances to parent instances. In the end, each
// top-level Super ProcResource has a set of functional unit template names
// (usually derived templates: a:b:c:d) correspoinding to each of its instances.
ProcResource *
MachineDescription::CreateSuperUnitNames(const std::string &unit_name) {
  auto *super = proc_resources_[unit_name];
  auto unit = unit_name;
  if (super->buffer_size() > -1)
    unit += formatv("<{0}>", super->buffer_size());

  // No need to process a functional unit more than once - we don't necessarily
  // process them in top-down order.
  if (!super->super_names().empty())
    return super;

  // First initialize all the instances of the Super FU with its own name.
  for (int i = 0; i < super->num_instances(); i++)
    super->super_names().emplace_back(unit);

#if 0
  // Note: this is clever, but when the bases have buffers, we can't express
  // this in a functional unit /template/, so we can carry more information if
  // we just define the instance has having bases.  It might make sense to
  // create a template if the processor doesn't have a reorder queue, but
  // its purely a cosmetic difference even in that case.

  // If there's only one instance of this unit, just add all of its child
  // units to its base.
  if (super->num_instances() == 1) {
    for (auto &base_name : super->child_func_units()) {
      for (auto &name : CreateSuperUnitNames(base_name)->super_names())
        super->add_base_unit(name.name());
    }
    return super;
  }
#endif

  // Recur over each child unit, then allocate each child's instances to this
  // ProcResource's instances such that the child instances are allocated
  // evenly across the least congested parent instances. After allocating all
  // of a child's instances, sort the parent's instances by the number of
  // children allocated to them.
  for (auto &base_name : super->child_func_units()) {
    auto *base = CreateSuperUnitNames(base_name);
    int super_sz = super->num_instances();
    int base_sz = base->num_instances();
    int count = std::min(super_sz, base_sz);
    for (int first = 0; first < count; first += count)
      for (int i = first; i < count + first; i++)
        super->super_names()[i].add_name(base->super_names()[i].name());

    if (base_sz != super_sz)
      std::sort(super->super_names().begin(), super->super_names().end());
  }
  return super;
}

//-------------------------------------------------------------------------
// Add a new SchedModel to the set of information about the target.
//-------------------------------------------------------------------------
void SchedReadWrite::AddSchedModel(const std::string &sched_model,
                                   const std::vector<std::string> &func_units,
                                   const std::vector<std::string> &res_cycles,
                                   const std::vector<std::string> &start_cycles,
                                   const std::string &latency,
                                   const std::string &micro_ops, bool is_write,
                                   bool is_begin_group, bool is_end_group,
                                   bool is_single_issue, bool retire_ooo) {
  auto model = FixAttribute(sched_model);

  assert(!sched_model_info_.count(model) &&
         formatv("Duplicate SchedModel:{0}", model).c_str());

  sched_model_info_.emplace(model,
          SchedModelRef(latency, micro_ops, is_write, is_begin_group,
                        is_end_group, is_single_issue, retire_ooo,
                        func_units, res_cycles, start_cycles));
}

//-------------------------------------------------------------------------
// Add a new SchedReadWrite to the set of information about the target.
//-------------------------------------------------------------------------
void MachineDescription::AddSchedReadWrite(
    const std::string &name, const std::string &sched_model,
    const std::vector<std::string> &func_units,
    const std::vector<std::string> &res_cycles,
    const std::vector<std::string> &start_cycles, const std::string &latency,
    const std::string &micro_ops, bool is_write, bool is_begin_group,
    bool is_end_group, bool is_single_issue, bool retire_ooo) {
  if (!rw_units_.count(name))
    rw_units_[name] = new SchedReadWrite(
        name, sched_model, func_units, res_cycles, start_cycles, latency,
        micro_ops, is_write, is_begin_group, is_end_group, is_single_issue,
        retire_ooo);
  else
    rw_units_[name]->AddSchedModel(
        sched_model, func_units, res_cycles, start_cycles, latency, micro_ops,
        is_write, is_begin_group, is_end_group, is_single_issue, retire_ooo);
}

//-------------------------------------------------------------------------
// Return the operand type.
//-------------------------------------------------------------------------
std::string MachineDescription::GetOperandType(const std::string &opnd) {
  std::size_t pos = opnd.find(' ');
  return opnd.substr(0, pos);
}

//-------------------------------------------------------------------------
// Update Instructions's implicit input & output operands with that of its
// base instruction.
//-------------------------------------------------------------------------
void MachineDescription::AddImplicitOperands() {
  for (const auto &[name, instruct] : instructions_) {
    auto &uses = instruct->implicit_uses_;
    auto &defs = instruct->implicit_defs_;
    auto base_instr_name = instruct->base_instr_;

    if (!base_instr_name.empty()) {
      auto base_instr = instructions_[base_instr_name];
      auto base_uses = base_instr->implicit_uses_;
      auto base_defs = base_instr->implicit_defs_;

      uses.insert(uses.end(), base_uses.begin(), base_uses.end());
      defs.insert(defs.end(), base_defs.begin(), base_defs.end());

      assert(base_instr->base_instr_.empty() &&
             "Unexpected multiple inheritance of base instr");
    }
  }
}

//-------------------------------------------------------------------------
// Find operands that are referenced by instructions (or other operands).
//-------------------------------------------------------------------------
void MachineDescription::FindReferencedOperands() {
  for (const auto &[name, instruct] : instructions_)
    if (instruct->HasSubunits() || ignore_subunits()) {
      for (auto &out : instruct->outs_) {
        std::string op = GetOperandType(out);
        if (operands_.count(op)) {
          operands_[op]->set_referenced();
        } else if (register_class_list_.count(op)) {
          register_class_list_[op]->set_referenced();
        } else if (op != "...") {
          std::cerr << "Unrecognized operand type:" << op << "\n";
        }
      }
      for (auto &in : instruct->ins_) {
        std::string op = GetOperandType(in);
        if (operands_.count(op)) {
          operands_[op]->set_referenced();
        } else if (register_class_list_.count(op)) {
          register_class_list_[op]->set_referenced();
        } else if (op != "...") {
          std::cerr << "Unrecognized operand type:" << op << "\n";
        }
      }
    }

  // Annotate operands and register classes that are referenced from
  // referenced operands.  If a name is not found in the operand types set,
  // check the register class set.  If it's not there, check the value_type
  // set.  If found there, create an operand for it.
  bool change = true;
  while (change) {
    change = false;
    for (const auto &[name, operand] : operands_)
      for (auto &opnd : operand->ops()) {
        std::string op = GetOperandType(opnd);
        if (operands_.count(op) && !operands_[op]->referenced()) {
          operands_[op]->set_referenced();
          change = true;
        } else if (register_class_list_.count(op)) {
          register_class_list_[op]->set_referenced();
        } else if (value_type_set_.count(op)) {
          std::vector<std::string> ops;
          operands_[op] = new Operand(op, ops, op);
          operands_[op]->set_referenced();
        }
      }
  }
}

bool MachineDescription::IsReadUnit(const std::string &name,
                                    const std::string &model) {
  if (auto *res = GetSchedReadWrite(name, model))
    return res->is_read();
  if (IsSchedVariant(name))
    return sched_variants_[name]->is_read();
  return false;
}

// To find the functional units associated with a read, find the set of
// functional units associated with its "valid writes".
void MachineDescription::FindReadUnits(const std::string &name,
                                       const std::string &model,
                                       ForwardUnits &units,
                                       const std::string &predicate) {
  if (auto *read = GetSchedReadWrite(name, model))
    if (auto *read_ref = read->HasModel(model))
      for (auto &write : read_ref->valid_writes())
        FindWriteUnits(write, model, units, read_ref->latency(), predicate);

  if (IsSchedVariant(name))
    for (auto &variant : sched_variants_[name]->variants())
      for (auto &read_ref : sched_vars_[variant]->selected())
        FindReadUnits(read_ref, model, units,
                      sched_vars_[variant]->predicate());
}

// Find the set of functional units based on the instructions write units.
void MachineDescription::FindWriteUnits(const std::string &name,
                                        const std::string &model,
                                        ForwardUnits &units, int latency,
                                        const std::string &predicate) {
  if (auto *res = GetSchedReadWrite(name, model))
    if (auto *write_ref = res->HasModel(model))
      for (auto &fu : write_ref->func_units())
        units.emplace(fu, latency, predicate);

  if (IsSchedVariant(name))
    for (auto &variant : sched_variants_[name]->variants())
      for (auto &write_ref : sched_vars_[variant]->selected())
        FindWriteUnits(write_ref, model, units, latency, predicate);
}

// Collect forwarding information for each InstRW record.
void MachineDescription::ExtractForwardingInfo(InstRW *instrw) {
  if (!gen_forwarding_info())
    return;

  auto const &model = instrw->sched_model();

  // Accumulate the set of functional units for this InstRW record.
  // Scan the resource writes and note the FUs used by each of them.
  ForwardUnits write_units;
  for (const auto &res_name : instrw->rw_units())
    if (!IsReadUnit(res_name, model))
      FindWriteUnits(res_name, model, write_units, 0, "");

  // Add each write unit to the set of functional units for this instruction.
  for (const auto &[unit, latency, pred] : write_units)
    instrw->func_units().insert(unit);

  // Scan all the read units, and accumulate the set of functional units
  // that forward for each read.
  int res_id = 0;
  for (const auto &res_name : instrw->rw_units()) {
    if (IsReadUnit(res_name, model)) {
      ForwardUnits fwd_units;
      FindReadUnits(res_name, model, fwd_units, "");

      for (auto &[to, ignore_lat, ignore_pred] : write_units)
        for (auto &[from, latency, pred] : fwd_units) {
          instrw->forwarding_info().emplace(res_id, latency, from, to, pred);
          forwarding_network_.AddEdge(model, from, to, latency);
        }
    }
    res_id++;
  }
}

// Expand an instruction's SchedReadWrite list to a set of InstRW records.
// The trick here is to find the model associated with the rw_unit.
// The "model" parameter is optional - if a valid model is passed in, we only
// generate InstRW for cases where the model in the InstRW matches the
// passed-in model.
// If we create a new InstRW, extract any forwarding information from it.
void MachineDescription::GenerateInstRWs(
    Instruction *instr, const std::vector<std::string> &rw_units,
    const std::string &first, const std::string &model) {
  if (rw_units.empty())
    return;

  const auto rw_name = !first.empty() ? first : rw_units[0];

  // Check for aliases first, which typically have specific models they
  // apply to. We only add InstRWs for these if the model matches. Since some
  // rw_names are both aliases and rw_units, we fall through to the
  // SchedReadWrite case after checking aliases.
  if (IsSchedAliasMatch(rw_name)) {
    for (const auto &[alias_model, name] : sched_alias_matches_[rw_name]) {
      if (model.empty() || alias_model == model) {
        auto *instrw = new InstRW("alias", rw_units, alias_model, {}, {});
        ExtractForwardingInfo(instrw);
        instr->add_inst_rw(instrw);
      }
    }
  }

  // If there's no model passed in, we generate InstRWs for every model
  // associated with the rw_name.
  if (IsSchedReadWrite(rw_name)) {
    for (const auto &[rw_model, item] : rw_units_[rw_name]->sched_model_info())
      if (model.empty() || model == rw_model) {
        auto *instrw = new InstRW("rw", rw_units, rw_model, {}, {});
        ExtractForwardingInfo(instrw);
        instr->add_inst_rw(instrw);
      }
    return;
  }

  if (IsSchedVariant(rw_name)) {
    const auto &var_model = sched_variants_[rw_name]->sched_model();
    if (model.empty() || var_model == model) {
      auto *instrw = new InstRW("var", rw_units, var_model, {}, {});
      ExtractForwardingInfo(instrw);
      instr->add_inst_rw(instrw);
    }
    return;
  }

  if (IsWriteSequence(rw_name)) {
    const auto &seq_model = write_sequences_[rw_name]->sched_model();
    const auto &first_unit = write_sequences_[rw_name]->writes()[0];
    if (model.empty())
      return GenerateInstRWs(instr, rw_units, first_unit, seq_model);

    auto *instrw = new InstRW("dup", rw_units, seq_model, {}, {});
    ExtractForwardingInfo(instrw);
    instr->add_inst_rw(instrw);
  }
}

// Recursively flatten a single operand.
void MachineDescription::FlattenOperand(std::string type, std::string subtype,
                                        std::string name,
                                        std::vector<FlatOperand> &operands) {
  auto &ops = operands_[subtype]->ops();
  for (unsigned idx = 0; idx < ops.size(); idx++) {
    auto subtype = Instruction::opnd_type(ops[idx]);
    auto subname = Instruction::opnd_name(ops[idx]);
    if (subname.empty()) subname = formatv("{0}", idx);

    if (IsOperand(subtype) && !operands_[subtype]->ops().empty())
      FlattenOperand(type, subtype, name + "." + subname, operands);
    else {
      auto is_reg = IsRegister(subtype) || IsRegisterClass(subtype);
      if (ops.size() == 1)
        operands.emplace_back(type, name, "", is_reg);
      else if (!subname.empty())
        operands.emplace_back(type, name, subname, is_reg);
      else operands.emplace_back(type, name, idx, is_reg);
    }
  }
}

// Given an instruction and either its "ins" or "outs" operand list, flatten
// the list to include every component of the operand.
std::vector<FlatOperand> MachineDescription::FlattenInstrOperands(
    Instruction *instr, const std::vector<std::string> &operands) {
  std::vector<FlatOperand> flat_opnds;

  // Look for operands which have suboperands, and add all the suboperands
  // to the instructions operand list.  Suboperands may or may not have
  // names, so either write out the suboperand name, or the index.
  for (const auto &opnd : operands) {
    auto type = Instruction::opnd_type(opnd);
    auto name = Instruction::opnd_name(opnd);
    if (IsRegister(type) || IsRegisterClass(type)) {
      flat_opnds.emplace_back(type, name, "", true);
      continue;
    }
    if (IsOperand(type) && !operands_[type]->ops().empty()) {
      FlattenOperand(type, type, name, flat_opnds);
      continue;
    }
    flat_opnds.emplace_back(type, name, "", false);
  }
  return flat_opnds;
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

// Iterate over all the instregex entries in InstrRW records, and expand
// them to lists of matched instruction names. Cache the lists so we only
// match expressions once (there tend to be a lot of identical expressions).
void MachineDescription::ExpandInstRWs() {
  std::map<std::string, std::vector<std::string>> regex_dict;

  for (InstRW *instrw : instrw_info_) {
    for (auto regex : instrw->instregex()) {
      auto prefix = GetPrefix(regex);
      auto pattern = regex.substr(prefix.size());

      std::optional<llvm::Regex> rex;
      if (!pattern.empty()) {
        if (pattern[0] != '^')
          pattern = formatv("^({0})", pattern);
        rex = llvm::Regex(pattern);
      }

      auto end = instructions_.end();
      auto begin = instructions_.begin();
      if (!prefix.empty())
        begin = instructions_.lower_bound(prefix);

      if (!regex_dict.count(regex)) {
        std::vector<std::string> matches;

        if (prefix.empty()) {
          for (auto itr = begin; itr != end; ++itr)
            if (rex->match(itr->first))
              matches.push_back(itr->first);
        } else {
          for (auto itr = begin; itr != end; ++itr) {
            if (itr->first.compare(0, prefix.size(), prefix) != 0)
              break;
            if (!rex || rex->match(itr->first.substr(prefix.size())))
              matches.push_back(itr->first);
          }
        }
        regex_dict[regex] = matches;
      }
      auto &instrs = instrw->instrs();
      auto &dict = regex_dict[regex];
      instrs.insert(instrs.end(), dict.begin(), dict.end());
    }
  }
}

//-----------------------------------------------------------------------------
// An instruction definition might not directly specify the read/write
// units, but they are specified indirectly using InstRW records. InstRW
// records associate instructions, specified using regexes, to rw-units for
// a particular schedule model.
//-----------------------------------------------------------------------------
void MachineDescription::ProcessInstRW() {
  // For each InstRW, expand the entry to the set of instructions it applies to.
  ExpandInstRWs();

  // Extract forwarding information from InstRW records.
  for (InstRW *instrw : instrw_info_)
    ExtractForwardingInfo(instrw);

  // Add each InstRW object to every instruction that uses it.
  for (InstRW *instrw : instrw_info_)
    // if (sched_models()[instrw->sched_model()]->has_itineraries())
      for (const auto &instr : instrw->instrs())
        if (instructions_.count(instr))
          instructions_[instr]->add_inst_rw(instrw);

  // If an instruction has SchedRW objects, create InstRW records for it.
  for (auto &[name, instr] : instructions_)
    GenerateInstRWs(instr, instr->rw_units(), "", "");

  //--------------------------------------------------------------------------
  // Instructions' "ins" and "outs" lists specify the instructions declared
  // operands, and operand types.  The instructions' SchedRW attributes
  // provide a set of Read or Write resources that are associated with each
  // component of an operand.  Thus, we need to flatten the instruction's
  // ins and out lists to describe each operand component, rather than just
  // the declared operand.
  //--------------------------------------------------------------------------
  for (auto &[name, instr] : instructions_) {
    instr->flattened_ins() = FlattenInstrOperands(instr, instr->ins());
    instr->flattened_outs() = FlattenInstrOperands(instr, instr->outs());
  }

  //--------------------------------------------------------------------------
  // We can specify the relationship between functional units and subunits
  // in either a bottom up or tops down approach.  In the tops down approach
  // each functional unit specifies which subunits it implements.  In a
  // bottoms up approach, each subunit specifies which functional units it
  // is associated with. The bottoms up approach is more similar to what
  // tablegen uses, and enables subunits to provide details about each
  // functional unit they use, and when, and for how long.
  //--------------------------------------------------------------------------
  // #define USE_TOPS_DOWN_ARCH_SPEC   // Define this to use tops down approach

  // Create a latency for each InstRW object, and add them to a global table.
  // For each generated latency, create a subunit which is added to the
  // instruction and each functional unit associated with the InstRW.
  for (auto &[name, instr] : instructions_) {
    for (const auto &[sched_model, inst_rw] : instr->inst_rws()) {
      auto lats = FormatReferences(instr, sched_model, inst_rw);

      // If specified on the command line, generate subunit bases to tie
      // subunit templates to sets of instructions.  This is an alternate
      // way to tie subunits to instructions (rather than the typical approach
      // of tying instructions to subunits in the instruction definition).
      std::string subunit_base;
      if (gen_subunit_bases()) {
        // Format a list of instructions (or regular expressions) that describe
        // which instructions use this subunit.
        std::string bases;
        if (inst_rw->instregex().empty())
          for (auto inst : inst_rw->instrs())
            bases += formatv(" : \"{0}\"", inst);
        else
          for (auto regex : inst_rw->instregex())
            bases += formatv(" : \"{0}\"", regex);
        subunit_base = formatv(": base{0}", add_subunit_base(bases));
      }

#ifdef USE_EXPLICIT_LATENCY_TEMPLATES
      // Create the body of the subunit template.
      int lat_id = add_latency(lats);
      std::string subunit =
          formatv("{1}() { latency lat{0}(); }", lat_id, subunit_base);
#else
      auto subunit = formatv("{1}() {{{{{0} }}", lats, subunit_base);
#endif

      // Add the subunit to the global subunit table.
      // Add the subunit (id) to the instruction.
      int subunit_id = add_subunit(subunit);
      instr->add_subunit(subunit_id);
      sched_model_subunits_[subunit_id] = sched_model;
    }
  }
}

#define IGNORE_CORTEXA9_ITINERARIES

// Add scheduling information from ItinRW records to each instruction.
// These are only used for ARM/CortexA9, where they are used to translate
// Itineraries to Schedules.
void MachineDescription::ProcessItinRW() {
#ifdef IGNORE_CORTEXA9_ITINERARIES
  return;
#endif
  // Create a map of ItinRW records indexed by matched itinerary class names.
  std::map<std::string, std::vector<const ItinRW *>> itin_map;
  for (auto *itin_rw : itinrw_info())
    for (auto &itin_class : itin_rw->itin_classes())
      itin_map[itin_class].push_back(itin_rw);

  // Add ItinRWs' rw_unit sets to each instruction that references an itinerary.
  for (const auto &[name, instr] : instructions_)
    if (!instr->itinerary().empty() && itin_map.count(instr->itinerary())) {
      for (auto *itin_rw : itin_map[instr->itinerary()])
        GenerateInstRWs(instr, itin_rw->rw_units(), "", itin_rw->sched_model());
    }
}

// Check each instruction to see if it has any scheduling information
// associated with it.  Print a warning if none is found.
void MachineDescription::CheckSchedulingInfo() {
  // If instructions don't have any scheduling info, issue a warning.
  if (!no_warnings())
    for (const auto &[name, instr] : instructions_)
      if (instr->inst_rws().empty() && instr->rw_units().empty() &&
          instr->itinerary().empty() && !instr->generic_)
        std::cerr << "Warning: No scheduling info for instr : " << name << "\n";
}

//------------------------------------------------------------------------
// If a resource expression is a disjunction of at least one conjunction,
// reassociate the operands to collect the conjunctions and disjunctions.
// Example:    A | (B & C) | D)  -->   (A | D) | (B & C)
//------------------------------------------------------------------------
void ReassociateDisjunctions(std::vector<ResExpr> &exprs) {
  auto &expr = exprs.front();
  auto &opnds = expr.opnds();
  if (!expr.isOr() || opnds.end() == std::find_if(opnds.begin(), opnds.end(),
                                                  [](const ResExpr &item) {
                                                    return item.isAnd();
                                                  }))
    return;

  ResExpr original = exprs.front();
  ResExpr disjunction(ResOp::kOr);
  exprs.clear();
  for (auto &item : original.opnds())
    if (item.isAnd())
      exprs.push_back(item);
    else
      disjunction.opnds().push_back(item);
  if (!disjunction.opnds().empty())
    exprs.push_back(disjunction);
}

//------------------------------------------------------------------------
// We prefer disjunctions over conjunctions, so if we have a conjunction
// of a disjunction, convert it to a disjunction of conjunctions.
//------------------------------------------------------------------------
void DistributeConjunctions(ResExpr &expr) {
  auto &opnds = expr.opnds();
  // Quit if this isn't the kind of expression we're looking for.
  if (!expr.isAnd() ||
      std::find_if(opnds.begin(), opnds.end(), [](const ResExpr &item) {
        return item.isOr();
      }) == opnds.end())
    return;

  // Initialize the "answer" to the first expression, or set of expressions.
  std::vector<ResExpr> answer;
  if (opnds[0].op() == ResOp::kRes)
    answer.push_back(opnds[0]);
  else
    answer = opnds[0].opnds();

  // Perform the distribution over the rest of the operands of the AND.
  for (unsigned idx = 1; idx < opnds.size(); idx++) {
    // If the operand is a single resource, do the distribute in place, since
    // the result is the same size as the original.
    if (opnds[idx].op() == ResOp::kRes) {
      for (auto &item : answer)
        if (expr.opnds()[idx] != item)
          item = ResExpr(ResOp::kAnd, item, expr.opnds()[idx]);
      continue;
    }
    // Handle OR operators. Since the result can grow exponentially, we
    // operate into a clean vector.
    auto temp = answer; // save off old vector.
    answer.clear();
    for (auto &lhs : opnds[idx].opnds())
      for (auto &rhs : temp)
        if (lhs != rhs) {
          ResExpr And(ResOp::kAnd, lhs, rhs);
          // We're creating an OR of AND's, so there's no reason to add
          // duplicate results into the answer.
          if (std::find(answer.begin(), answer.end(), And) == answer.end())
            answer.push_back(And);
        }
  }
  // Finally, create the OR operator over the distributed expressions.
  expr = ResExpr(ResOp::kOr, answer);
}

//------------------------------------------------------------------------
// Determine if a "FuncUnit" name is actually an issue slot.
// Tablegen doesn't differentiate between functional unit names and issue
// slot names, but the MDL has a nice way of supporting issue slots.  So
// this is a target-specific hack that identifies which functional unit
// names are actually issue slot names.
//------------------------------------------------------------------------
bool MachineDescription::IsIssueSlot(const std::string &name) const {
#if 0
  // This is disabled because the tablegen descriptions don't accurately
  // describe the actual number of issue slots for these architectures.
  if (family() == "Hexagon")
    return name.find("SLOT") != std::string::npos;
  if (family() == "R600") {
    if (name == "ALU_W" || name == "ALU_X" || name == "ALU_Y" ||
        name == "ALU_Z" || name == "TRANS")
      return true;
  }
#endif
  return false;
}

//------------------------------------------------------------------------
// Preprocess InstrStage objects to build resource expressions for each
// stage. Rewrite ComboUnits as conjunction expressions. The resulting
// expression is either a resource, a conjunction of resources, or a
// disjunction of resources and/or conjunctions:
//     expr     : <res>  |  <conj>  |  <disj> ;
//     conj     : <res>  ('&'  <res>)* ;
//     disj     : <conj>  ('|' <conj>)+ ;
// This reflects the underlying capability of a single itinerary stage.
//------------------------------------------------------------------------
void MachineDescription::PreprocessItinStages() {
  // For each stage definition, build a resource expression and stash it in
  // the stage.  Expand ComboUnits to conjunctions of their constituant parts.
  // Also note for each stage whether it's an issue stage.
  for (const auto &[name, stage] : instr_stages_) {
    std::vector<ResExpr> operands;
    stage->set_issue_flag();
    for (auto &unit : stage->func_units()) {
      if (!IsComboUnit(unit))
        operands.emplace_back(unit);
      else
        operands.emplace_back(ResOp::kAnd, combo_units_[unit]);
      if (!IsIssueSlot(unit))
        stage->clear_issue_flag();
    }
    // If there's more than one item in the list, create a disjunction.
    // Otherwise, just annotate the stage with the single expression.
    if (operands.size() > 1)
      stage->set_resource_expr(ResExpr(ResOp::kOr, operands));
    else
      stage->set_resource_expr(operands[0]);
  }
}

//------------------------------------------------------------------------
// Scan itinerary data objects and collect issue slots and template
// arguments.
//------------------------------------------------------------------------
void MachineDescription::PreprocessInstrItineraryData() {
  PreprocessItinStages();

  for (auto [name, itin_data] : instr_itinerary_data_) {
    // Scan stages for issue stages, and accumulate the expressions into one.
    ResExpr issue;
    for (const auto &stage_name : itin_data->stages()) {
      auto *stage = instr_stages(stage_name);
      if (stage->is_issue_stage()) {
        if (issue.isNop())
          issue = stage->resource_expr();
        else
          issue = ResExpr(ResOp::kAnd, issue, stage->resource_expr());
      }
    }

    // MDL doesn't support conjunctions of disjunctions for issue stages,
    // so redistribute them, then split the disjunctions into separate
    // expressions.
    if (!issue.isNop()) {
      DistributeConjunctions(issue);
      itin_data->set_issue_stage(issue);
      ReassociateDisjunctions(itin_data->issue_stages());
    }

    // Scan non-issue stages, and parse resource expressions into template
    // arguments.  Stages with the same pipeline stage and cycle are
    // conjunctions.
    std::map<std::pair<int, int>, ResExpr> exprset;
    int phase = 0;
    for (auto &stage_name : itin_data->stages()) {
      auto *stage = instr_stages(stage_name);
      if (!stage->is_issue_stage()) {
        auto index = std::make_pair(phase, stage->cycles());
        if (exprset.count(index) == 0)
          exprset[index] = stage->resource_expr();
        else
          exprset[index] =
              ResExpr(ResOp::kAnd, exprset[index], stage->resource_expr());
      }
      phase += stage->cycle_increment();
    }

    // Create a template argument for each expression. For conjunctions,
    // split out each disjunction subexpression as a separate argument.
    std::vector<ResArg> args;
    for (auto &[index, expr] : exprset) {
      if (!expr.isAnd()) {
        args.emplace_back(expr, index.first, index.second);
        continue;
      }
      std::vector<ResExpr> and_operands;
      std::vector<ResExpr> or_operands;
      for (auto &operand : expr.opnds()) {
        if (operand.isOr())
          or_operands.push_back(operand);
        else
          and_operands.push_back(operand);
      }

      if (and_operands.size() == 1) {
        args.emplace_back(and_operands[0], index.first, index.second);
      } else if (and_operands.size() != 0) {
        args.emplace_back(ResExpr(ResOp::kAnd, and_operands), index.first,
                          index.second);
      }

      for (auto Or : or_operands)
        args.emplace_back(Or, index.first, index.second);
    }

    // Expand disjunctions into a set of expressions.
    for (auto &arg : args)
      ReassociateDisjunctions(arg.exprs());
    itin_data->set_resource_args(args);
  }
}

// Add latencies and subunits for each instruction with an itinerary.
void MachineDescription::ProcessItineraries() {
  PreprocessInstrItineraryData();

  // Annotate InstrItinData objects with their associated models.
  for (const auto &[name, model] : sched_models()) {
    if (model->has_itineraries())
      if (auto *itins = processor_itineraries()[model->itineraries()])
        for (auto &itin_name : itins->iid())
          instr_itinerary_data_[itin_name]->add_model(name);
  }

  // For each instruction with an itinerary, create a combined subunit/latency
  // record, and create a functional unit which uses that subunit, then add
  // the functional unit to any processor that supports the instruction. Note
  // that instructions may have inst_rws AND itineraries.
  for (const auto &[name, inst] : instructions_) {
    if (!inst->itinerary().empty()) {
      for (auto *itin : instr_itinerary_class(inst->itinerary())) {
        // If this itinerary only has a single model, and the instruction has
        // InstRW info for that model, don't bother generating this subunit.
        if (itin->models().size() == 1 &&
            inst->has_inst_rws(*itin->models().begin()))
          continue;

        auto subunit = FormatItinSubunit(inst, itin);
        int subunit_id = add_subunit(subunit);
        auto *fu = add_itinerary_fu(itin);
        inst->add_subunit(subunit_id);
        fu->add_subunit(subunit_id, itin);

        for (auto &name : itin->models()) {
          auto *model = sched_models_[name];
#ifdef IGNORE_CORTEXA9_ITINERARIES
          if (model->output_name() == "CortexA9")
            break;
#endif
          if (!inst->has_inst_rws(model->name()))
            model->add_itinerary_func_unit(fu);
        }
      }
    }
  }
}

// Create a new functional unit template based on an itinerary.
// TODO(tbd): Use IID Class name?
ItineraryFuncUnit *
MachineDescription::add_itinerary_fu(InstrItineraryData *itin) {
  auto &stages = itin->stages();
  if (itinerary_fus_.count(stages))
    return itinerary_fus_[stages];
  auto name = formatv("FU{0}", itinerary_fus_.size());
  auto *new_fu = new ItineraryFuncUnit(name, itin);
  itinerary_fus_.insert({stages, new_fu});
  return new_fu;
}

} // namespace scan
} // namespace mdl
} // namespace mpact

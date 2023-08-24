//===- output.cpp - Write out the generate machine description ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file generates the MDL output file.  The functional are arranged
// in the order we normally expect to see things:
//      Pipeline definitions;
//      CPU definitions;
//      Functional unit template definitions;
//      Subunit/latency template definitions;
//      Latency template definitions;
//      Register definitions;
//      Register class definitions;
//      Operand definitions;
//      Instruction definitions;
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "scan.h"

namespace mpact {
namespace mdl {
namespace scan {

using StringSet = std::set<std::string>;

//----------------------------------------------------------------------------
// Write out the entire MDL file.
//----------------------------------------------------------------------------
void MachineDescription::WriteMDL(std::string &input_file, bool gen_arch_spec,
                                  std::string &output_dir) {
  OpenOutputFiles(input_file, gen_arch_spec, output_dir);
  if (gen_arch_spec) {
    WriteFileHeaders();
    WritePipelinePhase();
    WriteProcessorModel();
    WriteFUInfo();
    WriteSubunits();
    // WriteLatencies();     // We don't (currently) generate these
    WritePredicates();
  }

  WriteRegisterSets();
  WriteRegisterClasses();
  WriteOperandsMdl();
  WriteInstructionsMdl();

  output_inst_->close();
  if (gen_arch_spec)
    output_arch_->close();
}

//----------------------------------------------------------------------------
// Open the output files, check for errors.
//----------------------------------------------------------------------------
void MachineDescription::OpenOutputFiles(std::string &input_file,
                                         bool gen_arch_spec,
                                         std::string &output_dir) {
  if (!output_dir.empty())
    output_dir += "/";

  if (output_name_.empty()) {
    auto infile = std::filesystem::path(input_file);
    output_name_ = infile.stem();
  }

#ifdef BUILDING_FOR_GOOGLE3
  // Converting architecture name 'family_' to lowercase to use it as a prefix
  // for generated file name. This is consistent with the naming convention used
  // in other generated files (using other related tools like mdl compiler).
  // Moreover, mdl specific bzl-build rules uses lowercase architecture name to
  // specify generated file names as well as rule names and by convention, rules
  // names need to be in lowercase.
  std::transform output_name_.begin(), output_name_.end(), output_name_.begin(),
                 [](unsigned char c) {
    return std::tolower(c); });
#endif

  arch_file_name_ = output_dir + output_name_ + ".mdl";
  inst_file_name_ = output_dir + output_name_ + "_instructions.mdl";

  if (gen_arch_spec) {
    output_arch_ = new std::fstream(arch_file_name_, std::fstream::out);
    if (!output_arch_->is_open()) {
      std::cerr << formatv("Cannot open output file \"{0}\", aborting\n",
                           arch_file_name_);
      exit(EXIT_FAILURE);
    }
  }

  output_inst_ = new std::fstream(inst_file_name_, std::fstream::out);
  if (!output_inst_->is_open()) {
    std::cerr << formatv("Cannot open output file \"{0}\", aborting\n",
                         inst_file_name_);
    exit(EXIT_FAILURE);
  }
}

//----------------------------------------------------------------------------
// Given an instruction, return a list of register operand descriptors to be
// used in latency references.  Each item contains the qualified operand
// name, whether it's a def or use, and its original operand id.  Implicit
// operands have negative operand ids to differentiate them from explicit
// operands.
//----------------------------------------------------------------------------
OperandRefs MachineDescription::GetRegisterOperands(Instruction *instr) {
  OperandRefs opnds;
  int opnd_idx = 0;
  for (auto &out : instr->flattened_outs()) {
    if (out.is_register())
      opnds.emplace_back("def", "$" + out.format(), opnd_idx);
    opnd_idx++;
  }
  for (auto &in : instr->flattened_ins()) {
    if (in.is_register())
      opnds.emplace_back("use", "$" + in.format(), opnd_idx);
    opnd_idx++;
  }

  if (opnd_idx == 0) opnd_idx--;     // Make sure these are actually negative.
  for (auto &implicit_def : instr->implicit_defs())
    opnds.emplace_back("def", "$" + implicit_def, -opnd_idx++);
  for (auto &implicit_use : instr->implicit_uses())
    opnds.emplace_back("use", "$" + implicit_use, -opnd_idx++);
  return opnds;
}

//----------------------------------------------------------------------------
// Given an instruction, return a list of non-register operands that could
// be used in latency references.  These are potentially used to calculate
// an instruction's "latency".
//----------------------------------------------------------------------------
OperandRefs MachineDescription::GetNonRegisterOperands(Instruction *instr) {
  OperandRefs opnds;
  int opnd_id = instr->outs().size();

  for (auto &op : instr->ins()) {
    auto name = Instruction::opnd_name(op);
    auto type = Instruction::opnd_type(op);
    if (!IsRegister(name) && !IsRegisterClass(type))
      opnds.emplace_back("use", "$" + name, opnd_id);
    opnd_id++;
  }
  return opnds;
}

// Determine if a single unit is a write (depending on its type).
bool MachineDescription::IsWrite(const std::string &name,
                                 const std::string &model) const {
  auto unit = GetSchedRef(name, model);
  if (IsSchedReadWrite(unit))
    return rw_units_.at(unit)->is_write();
  if (IsSchedVariant(unit))
    return sched_variants_.at(unit)->is_write();
  return true;
}

// Determine if a single unit is a variadic access.
bool MachineDescription::IsVariadic(const std::string &name,
                                    const std::string &model) const {
  auto unit = GetSchedRef(name, model);
  if (IsSchedVariant(unit))
    return sched_variants_.at(unit)->is_variadic();
  return false;
}

//----------------------------------------------------------------------------
// Given a SchedReadWrite name and SchedModel name, lookup the name.  If it
// is an alias, recursively look up the aliased name.
//----------------------------------------------------------------------------
SchedReadWrite *
MachineDescription::GetSchedReadWrite(const std::string &name,
                                      const std::string &model) const {
  if (IsSchedAliasMatch(name) && sched_alias_matches_.at(name).count(model))
    return GetSchedReadWrite(sched_alias_matches_.at(name).at(model), model);
  if (IsSchedReadWrite(name))
    return rw_units_.at(name);
  return nullptr;
}

std::string MachineDescription::GetSchedRef(const std::string &name,
                                            const std::string &model) const {
  if (IsSchedAliasMatch(name) && sched_alias_matches_.at(name).count(model))
    return GetSchedRef(sched_alias_matches_.at(name).at(model), model);
  return name;
}

// Format a single reference.   Return true if it contained a variant record.
bool MachineDescription::FormatRef(std::string &out, const Instruction *instr,
                                   const std::string &sched_model,
                                   const std::string &unit_name,
                                   const std::string &operand, int repeat,
                                   int &latency, bool &fu_seen,
                                   std::string indent /* = "   " */) {
  auto name = GetSchedRef(unit_name, sched_model);
  if (name == "NoReadAdvance" || name == "ReadDefault")
    return false;

  // For SchedReadWrite objects, write out a single def or use.
  if (IsSchedReadWrite(name)) {
    if (auto *rw_unit = GetSchedReadWrite(name, sched_model)) {
      if (auto *ref = rw_unit->HasModel(sched_model)) {
        bool is_write = IsWrite(unit_name, sched_model);

        // Adjust the latency. Note that we accumulate all the latencies
        // from a write sequence, which can initially start at 0. Read
        // latencies start at one, and are adjusted by the Advance amount,
        // which can be negative. Cycle 1 reads and Cycle 0 writes aren't
        // explicitly written out.
        if (is_write) {
          latency += ref->latency();
        } else {
          latency = ref->latency() + 1;
          if (latency == 1 && repeat <= 1)
            return false;
        }

        // Generate the reference. If it's a repeated reference, write out
        // the repeat count.  Note that use latencies can be 0 or negative for
        // ReadAdvance entries, so these need special formatting.  Write
        // latencies of zero can be ignored.
        if (!operand.empty() && (!is_write || ref->is_valid_write_latency())) {
          std::string count = (repeat > 1) ? formatv("[{0}]", repeat) : "";
          std::string op = is_write ? "def" : "use";
          if (latency < 1)
            out += formatv(" {0}(E1-{1}, ${2});", op, -latency + 1, operand);
          else
            out += formatv(" {0}(E{1}{2}, ${3});", op, latency, count, operand);
        }

        // Write out functional unit, micro-ops, and flags information. These
        // are of the following general form:
        //      fus(<func-unit-references>, <micro-ops>, <flags>);
        // A functional unit reference has three different forms:
        //   <name>                      // use fu for 1 cycle
        //   <name> < <n> >              // use fu for <n> cycles
        //   <name> < <phase> : <n> >    // use fu for <n> cycles starting at
        //                                   <phase>
        if (is_write) {
          std::string fus;
          int fu_id = 0;
          auto &res_cycles = ref->res_cycles();
          auto &start_cycles = ref->start_cycles();
          for (const auto &fu_name : ref->func_units()) {
            // Currently tablegen defines "res_cycles" as starting at cycle
            // 0, even if there is an optional "StartAt" attribute.  In MDL,
            // we adjust for this so that the counting begins at the first
            // cycle the resource is used in.
            int start = !start_cycles.empty() ? start_cycles[fu_id] : 0;
            int cycles = !res_cycles.empty() ? res_cycles[fu_id++] : 1;

            std::string fu_cycles;
            if (start != 0)
              fu_cycles = formatv("<E{0}:{1}>", start + 1, cycles - start);
            else if (cycles > 1)
              fu_cycles = formatv("<{0}>", cycles);
            fus += fu_name + fu_cycles + "&";
            fu_seen = true;
          }
          if (!fus.empty())
            fus.pop_back(); // delete trailing "&"

          auto flags = ref->FormatFusFlags();
          if (!fus.empty())
            out += formatv(" fus({0}, {1}{2});", fus, ref->micro_ops(), flags);
          else if (ref->micro_ops() > 0 || !flags.empty())
            out += formatv(" fus({0}{1});", ref->micro_ops(), flags);
        }
      }
    }
    return false;
  }

  // Do something reasonable with WriteSequences. There are only two
  // WriteSequence cases of interest (in all current targets):
  // - The repeat count is 1 for several write resources, or
  // - The repeat cout is > 1 for a single write resource.
  // If the repeat count is 1, we adjust the latency for each resource to
  // reflect earlier writes in the sequence. If the repeat count is greater
  // than 1, we accumulate latencies for the different writes.
  // Rather than write out duplicate references for the single resource case,
  // simply pass the repeat count to the underlying object.
  if (IsWriteSequence(name)) {
    int latency = 0;
    bool ifs = false;
    auto *sequence = write_sequences_.at(name);
    auto &writes = sequence->writes();
    for (auto &write : writes)
      ifs |= FormatRef(out, instr, sched_model, write, operand,
                       sequence->repeat() * repeat, latency, fu_seen, indent);
    return ifs;
  }

  // Generate the if/then/else for variant references for an operand.  We
  // generally want these to look like:
  //       if <predicate> { <refs> }
  //       else if <predicate> { <refs> }
  //       ...
  //       else { <refs> }
  // If the SchedVariant is marked as Variadic, then each selected resource
  // applies to a different variadic operand.
  // Note that "NoSchedPred" is equivalent to "true".
  if (IsSchedVariant(name)) {
    std::vector<std::tuple<std::string,            // Predicate
                           std::string,            // Predicated references
                           std::string>> cc_refs;  // Other refs
    std::string cond;
    bool is_variadic = IsVariadic(name, sched_model);

    for (const auto &var_name : sched_variants_[name]->variants()) {
      auto *var = sched_vars_[var_name];

      // Generate the defs and uses for this variant's resources. This may
      // produce conditional references, unconditional references, or
      // nothing at all.
      std::string refs, ifs;
      int variadic_id = 1;
      for (const auto &select : var->selected()) {
        std::string item;
        auto opnd = is_variadic ? "$" + std::to_string(variadic_id++) : operand;
        int latency = 0;
        if (FormatRef(item, instr, sched_model, select, opnd, repeat, latency,
                      fu_seen, indent + "   "))
          ifs += item;
        else
          refs += item;
      }
      cc_refs.emplace_back(var->predicate(), ifs, refs);
      if (var->predicate() == "NoSchedPred")
        break;
    }

    // Look for trailing entries with empty reference clauses and delete them.
    // Note that we can't delete empty clauses if there are subsequent else
    // clauses.
    for (unsigned idx = cc_refs.size(); idx > 0; --idx) {
      auto &[pred, ifs, refs] = cc_refs[idx - 1];
      if (ifs.empty() && refs.empty())
        cc_refs.pop_back();
      else
        break;
    }

    // Write out the valid if/then/else clauses.
    bool generated_if = false;
    for (auto &[pred, ifs, refs] : cc_refs) {
      cond += "\n" + indent;
      if (generated_if)
        cond += "else ";
      if (pred != "NoSchedPred") {
        cond += "if " + PredName(pred) + " ";
        generated_if = true;
      }
      if (generated_if)
         cond += "{";
      cond += ifs;
      if (!refs.empty() && !ifs.empty())
        cond += indent + "  ";
      cond += refs;
      if (!ifs.empty())
        cond += indent;
      if (generated_if)
         cond += " }";
      if (!generated_if) break;
    }
    out += cond + "\n";
    return generated_if;
  }

  out += "ERROR(" + name + ")";
  return false;
}

//----------------------------------------------------------------------------
// Given an instruction, a SchedModel, and a vector of rw-units, produce
// all the latency references for that instruction.
// Return a tuple:
//    - a string containing the formatted defs/uses.
//    - the number of microops specified, or 0.
//----------------------------------------------------------------------------
constexpr bool kWriteUnmentionedOperands = false;

std::string MachineDescription::FormatReferences(Instruction *instr,
                                                 const std::string &sched_model,
                                                 const InstRW *inst_rw) {
  int reads = 0;
  int writes = 0;
  int variable_operands = 0;
  auto &rw_units = inst_rw->rw_units();

  if (instr->name() == "VSTMDIA")
    std::cout << " ";

  //--------------------------------------------------------------------------
  // For each rw_unit, create a set of operand references.
  //--------------------------------------------------------------------------
  std::string refs, ifs;
  bool fu_seen = false;
  for (const auto &unit : rw_units) {
    bool is_write = IsWrite(unit, sched_model);
    std::string opnd = is_write ? instr->out(writes) : instr->in(reads);
    int opid = is_write ? instr->out_index(writes++) : instr->in_index(reads++);
    if (opid >= 0 && gen_operand_indexes())
      opnd = std::to_string(opid);

    // If we didn't find a matching operand, create a reference to "extra"
    // operands.  These look like "$$<number>" in the reference.
    if (opnd.empty() && instr->has_variable_ops())
      opnd = "$" + std::to_string(++variable_operands);
    std::string out;
    int lat = 0;
    if (FormatRef(out, instr, sched_model, unit, opnd, 1, lat, fu_seen))
      ifs += out;
    else
      refs += out;
  }

  //--------------------------------------------------------------------------
  // Combine all the ifs with all the refs.  Try to combine everything on
  // one line if possible.
  //--------------------------------------------------------------------------
  auto out = ifs;
  if (!refs.empty()) {
    if (!ifs.empty())
      out += "  " + refs + "\n"; // Indent the refs.
    else
      out += refs;
  }
  //--------------------------------------------------------------------------
  // If we didn't see any resource (functional unit) uses, generate an fus
  // statement for the whole CPU (assuming we have one of those, which should
  // be rare).
  //--------------------------------------------------------------------------
  if (!fu_seen && !out.empty()) {
    if (sched_model.empty())
      std::cerr << formatv("Warning: no functional unit assignments for {0}\n",
                           instr->name());
    else
      out +=
          formatv(" fus({0}, 0);",
                  SchedMachineModel::fix_output_name(sched_model, family_));
  }

  //--------------------------------------------------------------------------
  // Write out all the unmentioned operand references.  We don't bother at
  // the moment, but we may want to do this explicitly at some point.
  //--------------------------------------------------------------------------
  if (kWriteUnmentionedOperands) {
    out += "\n";
    for (;;) {
      auto write = instr->out(writes++);
      if (write.empty())
        break;
      if (instr->may_load())
        out += formatv("  def.(LOAD_PHASE, {0});", write);
      else
        out += formatv("  def.(E1, {0});", write);
    }
    for (;;) {
      auto read = instr->in(reads++);
      if (read.empty())
        break;
      out += formatv("  use.(E1, {0});", read);
    }
  }

  //--------------------------------------------------------------------------
  // Return the latency body.
  //--------------------------------------------------------------------------
  if (sched_model.empty()) return out;

  return formatv(" {0} : {{{1}}",
          SchedMachineModel::fix_output_name(sched_model, family_), out);
}

// Helper to create cycle name from cycle id.
std::string cycle_name(int cycle) {
  if (cycle == 0) return "F1";
  return formatv("E{0}", cycle);
}

//----------------------------------------------------------------------------
// Format a subunit associated with an instruction and an itinerary. These are
// of the form:
//    subunit sub_name(resource r0, r1,...) {{      // resources optional
//       use(E1, r0, r1,...);                       // use any resources
//       def(...); use(...); ...
//    }}
//----------------------------------------------------------------------------
std::string MachineDescription::FormatItinSubunit(Instruction *inst,
                                                  InstrItineraryData *itin) {
  int parameters = itin->resource_args().size();

  // Generate the resource defs for the subunit.
  std::string resource_defs = parameters ? "resource " : "";
  for (int idx = 0; idx < parameters; idx++)
    resource_defs += formatv("{0}r{1}", idx ? ", " : "", idx);

  // Generate the resource uses. The common case is that all resources are
  // used in the same cycle, so try to combine uses into a single clause.
  std::string resource_uses;
  int resource_id = 0, phase = -1, time = -1;
  for (auto &arg : itin->resource_args()) {
    if (phase != arg.phase() || time != arg.cycles()) {
      if (resource_id)
        resource_uses += "); ";
      resource_uses += formatv(" use({0}", cycle_name(arg.phase()));
      if (arg.cycles() != 1)
        resource_uses += formatv(":{0}", arg.cycles());
    }
    resource_uses += formatv(", r{0}", resource_id++);
    phase = arg.phase();
    time = arg.cycles();
  }

  std::string out = "() {{";
  if (parameters)
    out = formatv("({0}) {{{{ {1});", resource_defs, resource_uses);

  // Get descriptors of all the instructions register operands, and generate
  // references for each of them. We assume that all registers in a single
  // operand are all referenced in the same cycle.
  auto operands = GetRegisterOperands(inst);
  auto &cycles = itin->operand_cycles();
  int cycles_size = static_cast<int>(cycles.size());

  // Format defs/uses for each register operand.
  for (auto &[type, name, op_id] : operands) {
    // Determine what cycle this event takes place in.
    auto cycle = op_id >= 0 && op_id < cycles_size ? cycles[op_id] : "0";
    set_max_pipeline_phase(std::stoul(cycle.c_str()));
    if (cycle != "0")
      cycle = "E" + cycle;
    else if (type == "def" && cycles.empty() && op_id >= 0 && inst->may_load())
      cycle = "LOAD_PHASE";
    else
      cycle = "E1";
    out += formatv("  {0}({1}, {2});", type, cycle, name);
  }

#if 0
  // We need to capture explicit cycles for non-register operands, since this
  // is used to calculate instruction latencies in some conditions.
  for (auto &[type, name, op_id] : GetNonRegisterOperands(inst)) {
    if (op_id >= cycles_size) break;
    if (cycles[op_id] != "0")
      out += formatv("   use(E{0}, {1});", cycles[op_id], name);
  }
#endif

  // Append the model names onto the end of the subunit record. We can't share
  // subunits between instructions on different models, since a single subunit
  // implicitly describes a model-specific behavior for some set of
  // instructions.
  out += "}}   // ";
  for (auto model : itin->models())
    out += model + ",";
  out.pop_back();
  out += "\t" + itin->instr_itinerary_class() + "\t";    // Add itinerary class
  for (auto &expr : itin->resource_args())
    out += expr.ToString() + "||";
  out.pop_back();
  out.pop_back();
  return out;
}

// Write out issue resources for a cpu.
static std::string FormatIssueResources(int num_issue_slots) {
  if (num_issue_slots <= 0)
    return "";
  std::string out;
  for (int idx = 0; idx < num_issue_slots; idx++)
    out += formatv(" s{0},", idx);
  out.pop_back(); // delete trailing comma

  return formatv("  issue(F1){0};\n", out);
}

// Provide some CPU-specific default latencies.
static std::string FormatDefaultLatencies(int load_latency, int high_latency) {
  std::string out;
  if (load_latency > 0)
    out = formatv("LOAD_PHASE={0}", load_latency);
  if (load_latency > 0 && high_latency > 0)
    out += ", ";
  if (high_latency > 0)
    out += formatv("HIGH_PHASE={0}", high_latency);
  if (out.empty())
    return out;
  return formatv("  protected phases defaults {{ {0} };\n", out);
}

//----------------------------------------------------------------------------
// Generate a string that represents a MDL CPU definition.
//----------------------------------------------------------------------------
std::string SchedMachineModel::Format(MachineDescription *md) {
  if (cpus_.empty())
    return "";

  // Write out the start of the CPU definition - the name and subtargets.
  std::string cpus;
  for (auto &cpu : cpus_)
    cpus += formatv("\"{0}\", ", cpu);
  cpus.pop_back();
  cpus.pop_back(); // delete trailing comma
  auto out = formatv("cpu {0}({1}) {{\n", output_name_, cpus);

  // Optionally write out a pipeline specification for the default load latency.
  out += FormatDefaultLatencies(load_latency_, high_latency_);

  // Write out issue slot and resource definitions.
  if (!has_itineraries()) {
    out += FormatIssueResources(issue_width_);
  } else {
    std::string slots, resources;
    for (const auto &res :
         md->processor_itineraries()[itineraries()]->resources()) {
      if (!md->IsComboUnit(res)) {
        auto *resset = md->IsIssueSlot(res) ? &slots : &resources;
        if (!resset->empty())
          *resset += ", ";
        *resset += res;
      }
    }
    if (!slots.empty())
      out += formatv("  issue(F1) {0};\n", slots);
    else
      out += FormatIssueResources(issue_width_);

    if (!resources.empty())
      out += formatv("  resource {0};\n", resources);
    out += "\n";
  }

  // If the CPU has a reorder buffer, write out a spec for that.
  if (micro_op_buffer_size() > 0)
    out += formatv("  reorder_buffer<{0}>;\n", micro_op_buffer_size());

  // Write out schedule-based functional unit instances.
  int count = 0;
  for (const auto &unit : func_units_) {
    auto *func_unit = md->proc_resources()[unit];
    std::string buffer;
    for (int idx = 0; idx < func_unit->num_instances(); idx++) {
      out += formatv("  func_unit {0} U{1}();\n",
                     func_unit->super_names()[idx].name(), count++);
    }
  }

  // Write out itinerary-based functional unit instances.
  if (has_itineraries()) {
    for (auto *fu : itinerary_fus())
      count = fu->FormatInstance(&out, count, md);
  }

  // Write out schedule-based functional unit forwarding graph.
  if (md->gen_forwarding_info()) {
    std::map<std::string, std::string> forwards;
    for (const auto &[tuple, latency] : md->forwarding_network().graph()) {
      const auto &[model, def, use] = tuple;
      if (model == name_) {
        std::string to = formatv("{0}({1})", use, *latency.begin());
        if (!forwards.count(def))
          forwards[def] = to;
        else
          forwards[def] += "," + to;
      }
    }
    if (!forwards.empty())
      out += "\n";
    for (const auto &[from, to] : forwards)
      out += formatv("  forward {0} -> {1};\n", from, to);
  }

  return out + "}\n\n";
}

//----------------------------------------------------------------------------
// Generate all permutations of candidate resource set indexes for the
// functional unit instance parameters.
//----------------------------------------------------------------------------
static void ExpandCombos(std::vector<std::vector<int>> &result,
                         std::vector<int> &sizes, std::vector<int> &indexes,
                         unsigned level) {
  if (level == sizes.size()) {
    result.push_back(indexes);
    return;
  }
  for (int index = 0; index < sizes[level]; index++) {
    indexes.push_back(index);
    ExpandCombos(result, sizes, indexes, level + 1);
    indexes.pop_back();
  }
}

//----------------------------------------------------------------------------
// Generate a string that represents functional unit instances, and append
// them to the current output string.  Return the number of instances
// generated.
//----------------------------------------------------------------------------
int ItineraryFuncUnit::FormatInstance(std::string *out, int fu_id,
                                      MachineDescription *md) {
  auto *itin = itinerary();
  auto &slots = itin->issue_stages();

  // Given the parameter lists for this itinerary, each which has a set of
  // resource specification options, build a permutation of all arguments'
  // resource sets.
  std::vector<int> parameter_sizes;
  parameter_sizes.reserve(itin->resource_args().size());
  for (auto arg : itin->resource_args())
    parameter_sizes.push_back(arg.exprs().size());

  std::vector<int> items;
  std::vector<std::vector<int>> index_set;
  ExpandCombos(index_set, parameter_sizes, items, 0);

  // Use the generated index sets to create functional unit instances
  // for each combination of resource specifications.
  auto &args = itin->resource_args();
  for (auto &combo : index_set) {
    std::string resources;
    for (unsigned arg = 0; arg < combo.size(); arg++)
      resources += (arg ? ", " : "") + args[arg].exprs()[combo[arg]].ToString();
    if (slots.empty())
      *out +=
          formatv("  func_unit {0}<> U{1}({2});\n", name(), fu_id++, resources);
    for (auto &slot : slots)
      *out += formatv("  func_unit {0}<> U{1}({2}) -> {3};\n", name(), fu_id++,
                      resources, slot.ToString());
  }
  return fu_id;
}

//----------------------------------------------------------------------------
// Helper function for Functional unit template definitions.
// Generate a string containing resource names.
//----------------------------------------------------------------------------
static std::string FormatResSet(int stages) {
  std::string out;
  for (int res_id = 0; res_id < stages; res_id++)
    out += formatv("r{0}, ", res_id);
  if (!out.empty()) {
    out.pop_back();
    out.pop_back();
  } // delete comma
  return out;
}

//----------------------------------------------------------------------------
// Write out subunit instances in a functional unit template definition.
// Write out a few subunits on each line.
//----------------------------------------------------------------------------
static std::string FormatSubunitInstances(const std::set<int> &subunits,
                                          std::string res_set,
                                          std::string indent = "   ") {
  std::string out, sub, separator;

  for (auto subunit : subunits) {
    auto item = formatv("sub{0}({1})", subunit, res_set);
    if (sub.size() + item.size() + indent.size() + separator.size() >= 73) {
      out += formatv("{0}subunit {1};\n", indent, sub);
      sub = item;
    } else {
      sub += separator + item;
    }
    separator = ",";
  }
  if (!sub.empty())
    out += formatv("{0}subunit {1};", indent, sub);
  return out;
}

//----------------------------------------------------------------------------
// Write out subunit instances in an itinerary-based functional unit template
// definition. The input is a set of <subunit/ItineraryData> pairs. Each
// itinerary has a set of functional units its associated with.
// Most of the complexity of this function is making the output look pretty.
//----------------------------------------------------------------------------
std::string
FormatSubunitInstances(MachineDescription *md,
                       const std::set<std::pair<int, std::string>> &subunits,
                       std::string res_set) {
  std::string out;

  // Any subunit may be associated with more than one SchedModel, so we
  // create a map of SchedModel predicates with a set of subunits for
  // each. Avoid long predicate lines by inserting linefeeds.
  std::map<std::string, std::set<int>> models_and_subunits;
  for (const auto &[subunit, itin_name] : subunits) {
    std::string models, line;
    auto *itins = md->instr_itinerary_data(itin_name);
    for (const auto &model : itins->models()) {
      if (line.size() + model.size() + 4 > 79) {
        models += line + "\n   ";
        line = "";
      }
      line += formatv("{0}, ", md->sched_models()[model]->output_name());
    }
    if (!line.empty()) {
      line.erase(line.length() - 2);
      line += ": ";
    }
    models += line;
    models_and_subunits[models].insert(subunit);
  }

  // For each functional unit predicate set, write out its associated subunits.
  // Note that its possible to have unpredicated functional units.  If there's
  // only one predicate we don't need to write it out.
  for (const auto &[models, subunit_ids] : models_and_subunits) {
    auto pred = models.empty() ? "" : formatv("   {0}{{", models);
    if (models_and_subunits.size() == 1)
      pred.clear();
    auto indent = pred.empty() ? "   " : "     ";
    auto subs = FormatSubunitInstances(subunit_ids, res_set, indent);
    if (pred.empty())
      out += subs + "\n";
    else if (pred.size() + subs.size() < 75)
      out += pred + subs + " };\n";
    else
      out += pred + "\n" + subs + "\n   };\n";
  }
  return out;
}

//----------------------------------------------------------------------------
// Generate a string that represents an MDL Function Unit Template definition
// for derived itineraries.
//----------------------------------------------------------------------------
std::string ItineraryFuncUnit::FormatTemplate(MachineDescription *md) {
  std::string out = formatv("func_unit {0} (", name());
  std::string res_set = FormatResSet(itinerary_->resource_args().size());
  if (!res_set.empty())
    out += "resource ";
  out += formatv("{0}) {{\n", res_set);

  // Write out subunit instances.
  out += FormatSubunitInstances(md, subunits_, res_set);
  return out + "}\n\n";
}

//----------------------------------------------------------------------------
// Generate a string that represents an MDL Function Unit Template definition.
//----------------------------------------------------------------------------
std::string ProcResource::Format(MachineDescription *md) const {
  // Write out resource groups to define all the names.
  if (is_resource_group()) {
    std::string names;
    auto size = buffer_size() != -1 ? formatv("<{0}>", buffer_size()) : "";
    for (auto &fu : resource_group_)
      names += fu + ", ";
    names.pop_back(), names.pop_back(); // delete trailing comma
    return formatv("func_group {0}{1}: {2};\n", name_, size, names);
  }

  // Format the bases.  Don't make the line too long.
  std::string out = formatv("func_unit {0}", name_);
  std::string bases;
  int indent = out.size();
  for (const auto &fu : base_func_units_) {
    if (bases.size() + indent + fu.size() + 3 > 80) {
      out += bases + "\n";
      bases = "          ";
      indent = 10;
    }
    bases += formatv(" : {0}", fu);
  }

  // Format the subunits for this functional unit, if any.
  std::string subs = FormatSubunitInstances(subunits_, "");

  // Write out the template if it has bases or subunits.
  if (bases.empty() && subs.empty())
    return "";

  if (!subs.empty())
    return formatv("func_unit {0}{1}() {{\n{2}\n}\n\n", name_, bases, subs);
  return formatv("{0}{1}() {{}\n", out, bases);
}

//---------------------------------------------------------------------------
// Format a register definition.
//---------------------------------------------------------------------------
std::string RegDefinition::Format() const {
  if (index_ == last_index_)
    return name_;
  return formatv("{0}[{1}..{2}]", prefix_, index_, last_index_);
}

//---------------------------------------------------------------------------
// Format a register class definition.
//---------------------------------------------------------------------------
std::string RegisterClass::Format() {
  if (!registers_.empty())
    std::sort(registers_.begin(), registers_.end());
  std::string out;
  for (unsigned i = 0; i < registers_.size(); i++) {
    int first, last;
    if (!out.empty())
      out += ", ";
    std::string prefix = registers_[i].prefix();
    first = registers_[i].index();
    if (first == -1 || i == registers_.size() - 1) {
      out += registers_[i].name();
    } else {
      for (last = first; i + 1 < registers_.size(); i++) {
        if (registers_[i + 1].prefix() != prefix ||
            registers_[i + 1].index() != last + 1)
          break;
        last = registers_[i + 1].index();
      }
      if (first == last)
        out += formatv("{0}{1}", prefix, first);
      else
        out += formatv("{0}[{1}..{2}]", prefix, first, last);
    }
  }
  return formatv("register_class {0} {{ {1} };\n", name_, out);
}

//----------------------------------------------------------------------------
// Generate a string that represents an MDL Operand record.
//----------------------------------------------------------------------------
std::string Operand::Format() const {
  std::string out;
  for (const auto &opnd : ops_) {
    if (!out.empty())
      out += ", ";
    out += opnd;
  }
  return formatv("operand {0}({1}) {{ type({2}); }\n", name_, out, type_);
}

//----------------------------------------------------------------------------
// Generate a string that represents an MDL Instruction record.
//----------------------------------------------------------------------------
std::string Instruction::Format(bool full_definition) const {
  constexpr auto ellipsis = "...";
  std::string out, opnds, variable_ops;

  for (const auto &opnd : outs_) {
    if (opnd == ellipsis) {
      variable_ops = ellipsis;
    } else {
      if (!opnds.empty())
        opnds += ", ";
      opnds += formatv("{0}(O)", opnd);
    }
  }
  for (const auto &opnd : ins_) {
    if (opnd == ellipsis) {
      variable_ops = ellipsis;
    } else {
      if (!opnds.empty())
        opnds += ", ";
      opnds += formatv("{0}(I)", opnd);
    }
  }
  for (const auto &opnd : implicit_defs_) {
    if (!opnds.empty())
      opnds += ", ";
    opnds += formatv("{0}(O)", opnd);
  }
  for (const auto &opnd : implicit_uses_) {
    if (!opnds.empty())
      opnds += ", ";
    opnds += formatv("{0}(I)", opnd);
  }

  if (!opnds.empty() && !variable_ops.empty())
    opnds += ", ";

  out = formatv("instruction {0}({1}{2}) {{", name_, opnds, variable_ops);
  if (pseudo_)
    out += "  // pseudo-instr";
  out += "\n";

  if (!full_definition)
    return out;

  // write out declared subunits
  if (HasSubunits()) {
    out += "     subunit(";
    for (auto &unit : *subunit_) {
      out += unit + ", ";
    }
    out.pop_back();
    out.pop_back(); // delete trailing comma
    out += ");\n";
  }

  // write out derived subunits
  if (!subunits().empty()) {
    std::string units;
    for (auto &unit : subunits())
      units += formatv("sub{0},", unit);
    units.pop_back(); // delete trailing comma
    out += formatv("     subunit({0});\n", units);
  }

  if (HasChildren()) {
    out += "     derived(";
    for (auto *child : children_)
      out += formatv("{0}, ", child->name());
    out.pop_back();
    out.pop_back(); // delete trailing comma
    out += ");\n";
  }

  if (!assembly_.empty())
    out += formatv("     // {0}\n", assembly_);
  return out + "}\n";
}

//----------------------------------------------------------------------------
// Generate a string that is a CSV representation of an instruction and
// all of its operands.
//----------------------------------------------------------------------------
std::string Instruction::ToCsvString() const {
  std::string out = name_;

  // write out operands - pad them out so the operands all line up
  for (auto &opnd : outs_)
    out += formatv("\t({0})", opnd);
  for (int oc = 1 - outs_.size(); oc > 0; oc--)
    out += "\t.";
  for (auto const &in : ins_)
    out += formatv("\t{0}", in);
  for (int ic = 9 - ins_.size(); ic > 0; ic--)
    out += "\t.";

  out += formatv("\t assembly:({0})\t pattern:{1}", assembly_, pattern_);
  if (!parent_.empty())
    out += formatv("\t parent:{0}", parent_);

  if (HasSubunits()) {
    out += "\t subunit:";
    for (auto &unit : *subunit_)
      out += formatv(" {0}", unit);
  }

  return out + "\n";
}

constexpr int kNoEntries = -1;
constexpr char divider[] =
    "//---------------------------------------------------------------------\n";
constexpr char file_banner[] =
    "// This file is autogenerated from an LLVM Target Description File.\n";

//----------------------------------------------------------------------------
// Write out a file header.
//----------------------------------------------------------------------------
void MachineDescription::WriteFileHeaders() {
  output_inst() << formatv("{0}{1}{0}family {2};\n", divider, file_banner,
                           family());
  output_arch() << formatv("{0}{1}{0}import \"{2}\"\n", divider, file_banner,
                           inst_file_name_);
}

//----------------------------------------------------------------------------
// Write a header for each generated architecture specification section.
//----------------------------------------------------------------------------
void MachineDescription::WriteArchHeader(const char *section, int entries) {
  if (entries == 0)
    return;
  auto out = formatv("\n{0}// {1}", divider, section);
  if (entries != kNoEntries)
    out += formatv(" ({0} entries)", entries);
  output_arch() << out + "\n" + divider;
}

//----------------------------------------------------------------------------
// Write a header for each instruction description section.
//----------------------------------------------------------------------------
void MachineDescription::WriteInstHeader(const char *section) {
  output_inst() << formatv("\n{0}// {1}\n{0}", divider, section);
}

//----------------------------------------------------------------------------
// Dump out pipeline phases.
//----------------------------------------------------------------------------
void MachineDescription::WritePipelinePhase() {
  WriteArchHeader("Pipeline phase definitions", kNoEntries);
  int max_phase = 0;

  // Check RWUnit records for latency information.
  for (const auto &[name, rw_unit] : rw_units_)
    for (const auto &[sched_model, sched_model_info] :
         rw_unit->sched_model_info())
      max_phase = std::max(max_phase, sched_model_info.latency());

  // Check Itinerary records for latency information.
  for (auto [name, itinerary_data] : instr_itinerary_data_)
    for (auto &cycle : itinerary_data->operand_cycles())
      max_phase = std::max(max_phase, std::stoi(cycle, nullptr, 0));

  if (max_phase == 0)
    max_phase = max_pipeline_phase();
  std::string phases = max_phase ? formatv("E[1..{0}]", max_phase + 1) : "E1";
  output_arch() << formatv("protected phases {0} {{ F1, {1} };\n", family(),
                           phases);
}

//----------------------------------------------------------------------------
// Write out all processor variants.
//----------------------------------------------------------------------------
void MachineDescription::WriteProcessorModel() {
  WriteArchHeader("CPU Description Classes", sched_models_.size());
  for (const auto &[name, model] : sched_models_)
    output_arch() << model->Format(this);
}

// ----------------------------------------------------------------------------
// Write out all functional template definitions.
//----------------------------------------------------------------------------
void MachineDescription::WriteFUInfo() {
  WriteArchHeader("Functional Unit Groups", kNoEntries);
  for (const auto &[name, fu] : proc_resources_)
    if (fu->is_resource_group())
      output_arch() << fu->Format(this);
  output_arch() << "\n";

  WriteArchHeader("Functional Unit Templates", kNoEntries);
  // Write out Schedule-based functional units.
  for (const auto &[name, fu] : proc_resources_)
    if (!fu->is_resource_group())
      output_arch() << fu->Format(this);

  // Write out itinerary functional unit templates.
  for (const auto &[stages, fu] : itinerary_fus_)
    output_arch() << fu->FormatTemplate(this);
}

//----------------------------------------------------------------------------
// Write out all subunit template definitions.
//----------------------------------------------------------------------------
void MachineDescription::WriteSubunits() {
  WriteArchHeader("Subunit Definitions", subunits_.size());

#ifndef WRITE_SUBUNITS_FOR_EACH_SCHED_MODEL
  for (const auto &[subunit, index] : subunits_)
    output_arch() << formatv("subunit sub{0}{1}\n", index, subunit);
  output_arch() << "\n";
#else
  for (auto &[model_name, model] : sched_models_) {
    output_arch() << formatv("\n// Subunits for {0}\n", model_name);
    for (auto &[subunit, index] : subunits_)
      if (sched_model_subunits_[index] == model_name)
        output_arch() << formatv("subunit sub{0}{1}\n", index, subunit);
  }
#endif

  for (const auto &[base, index] : subunit_bases_)
    output_arch() << formatv("subunit base{0}{1}() {{}\n", index, base);
}

//----------------------------------------------------------------------------
// Write out all latency template definitions.
//----------------------------------------------------------------------------
void MachineDescription::WriteLatencies() {
  WriteArchHeader("Latency Definitions", latencies_.size());
  for (const auto &[latency, idx] : latencies_)
    output_arch() << formatv("latency lat{0}() {{{1}}\n", idx, latency);
}

static std::string PredIndent(int indent) {
  return std::string(indent * 2 + 2, ' ');
}

static bool IsMultiLine(std::string &pred) {
  if (!str_contains(pred, "\n"))
    return false;
  if (pred.size() > 70)
    return true;
  pred.erase(std::remove(pred.begin(), pred.end(), '\n'), pred.end());
  return false;
}

//----------------------------------------------------------------------------
// Functions managing whether predicates should be written out.
//----------------------------------------------------------------------------
bool MachineDescription::IsPredicateReferenced(const std::string &name) {
  if (IsMCSchedPredicate(name))
    return mc_sched_predicates()[name]->is_referenced();
  if (IsMCInstPredicate(name))
    return mc_inst_predicates()[name]->is_referenced();
  if (IsMCStatement(name))
    return statements()[name]->is_referenced();
  if (IsMCOpcodeSwitchCase(name))
    return switch_cases()[name]->is_referenced();
  return true;
}

// Mark the predicate expression as referenced.
void MachineDescription::SetPredicateReferenced(const std::string &name) {
  if (IsMCSchedPredicate(name))
    mc_sched_predicates()[name]->set_is_referenced();
  if (IsMCInstPredicate(name))
    mc_inst_predicates()[name]->set_is_referenced();
  if (IsMCStatement(name))
    statements()[name]->set_is_referenced();
  if (IsMCOpcodeSwitchCase(name))
    switch_cases()[name]->set_is_referenced();
}

bool MachineDescription::OutputPredicate(const std::string &name) {
  if (name == "NoSchedPred")
    return false;
  return name != kTrue && name != kFalse &&
         (!str_contains(name, "anonymous_") || IsPredicateReferenced(name));
}

//----------------------------------------------------------------------------
// When writing out predicate names, avoid writing out anonymous names.
// If it's an MCSchedPredicate, return its base predicate.
//----------------------------------------------------------------------------
std::string MachineDescription::PredName(const std::string &name) {
  if (str_contains(name, "anonymous_") && IsMCSchedPredicate(name))
    return PredName(mc_sched_predicates()[name]->inst_predicate());

  SetPredicateReferenced(name);
  return name;
}

//----------------------------------------------------------------------------
// Functions for writing out predicate expressions. In general we'd like these
// to look nice, and approximately what they looked like in the original
// tablegen source.
//----------------------------------------------------------------------------

// Format a named predicate reference.
std::string MachineDescription::FormatPred(const std::string &name,
                                           int indent) {
  std::string out;
  if (!str_contains(name, "anonymous_"))
    return name;
  if (IsMCInstPredicate(name))
    out = mc_inst_predicates()[name]->Format(this, indent);
  else if (IsMCStatement(name))
    out = statements()[name]->Format(this, indent);
  else if (IsMCOpcodeSwitchCase(name))
    out = switch_cases()[name]->Format(this, indent);
  return out;
}

// Format an Instruction Predicate Object.
std::string MCInstPredicate::Format(MachineDescription *md, int indent) const {
  std::string out;

  // Handle predicates that handle sets of opcodes.
  if (!valid_opcodes_.empty()) {
    auto sep = "";
    for (auto &op : valid_opcodes_) {
      out += formatv("{0}{1}", sep, op);
      sep = ",";
    }
    return formatv("{0}<{1}>", kCheckOpcode, out);
  }

  // Handle compound predicates (CheckAny, CheckAll, CheckNot).
  if (!predicates_.empty()) {
    auto sep = "";
    out += formatv("{0}<", attributes_[0]);
    for (auto &pred : predicates_) {
      out += formatv("{0}\n{1}", sep, PredIndent(indent + 1));
      out += md->FormatPred(pred, indent + 1);
      sep = ",";
    }
    return out + ">";
  }

  // Handle statement predicates.
  if (!statement_.empty())
    return out + md->FormatPred(statement_, indent);

  if (attributes_.empty())
    return "";

  // Handle all the rest (CheckIsRegOperand, etc)
  std::string op = attributes_[0];

  // CheckRegOperand always has 2 required parameters, and an optional
  // function_mapper.
  if (op == kCheckRegOperand) {
    if (!function_mapper_.empty())
      out = formatv(", {0}", function_mapper_);
    return formatv("{0}<{1},{2}{3}>", op, opindex_, register_name_, out);
  }
  // CheckImmOperand has an optional "value" parameter and an optional
  // function_mapper.  If the value parameter is missing, but we have an
  // function_mapper, pass an empty value parameter.
  if (op == kCheckImmOperand) {
    if (!immval_.empty())
      out = formatv(", {0}", immval_);
    else if (!function_mapper_.empty())
      out = ", \"\"";
    if (!function_mapper_.empty())
      out += formatv(", {0}", function_mapper_);
    return formatv("{0}<{1}{2}>", op, opindex_, out);
  }
  if (op == kCheckZeroOperand || op == kCheckIsRegOperand ||
      op == kCheckIsImmOperand || op == kCheckInvalidRegOperand) {
    return formatv("{0}<{1}>", op, opindex_);
  }
  if (op == kCheckSameRegOperand) {
    return formatv("{0}<{1},{2}>", op, opindex_, immval_);
  }
  if (op == kCheckFunctionPredicate) {
    return formatv("{0}<{1},{2}>", op, function_name_, function_mapper_);
  }
  if (op == kCheckFunctionPredicateWithTII) {
    return formatv("{0}<{1},{2},{3}>", op, function_name_, function_mapper_,
                   immval_);
  }
  if (op == kCheckNumOperands) {
    return formatv("{0}<{1}>", op, immval_);
  }
  // By default, we don't pass any parameters.
  return formatv("{0}<>", op);
}

// Format an MCStatement predicate object.
std::string MCStatement::Format(MachineDescription *md, int indent) const {
  std::string out;
  if (!predicate_.empty()) {
    auto pred = md->FormatPred(predicate_, indent + 1);
    if (IsMultiLine(pred))
      pred = formatv("\n{0}{1}", PredIndent(indent + 1), pred);
    return out +
           formatv("\n{0}{1}<{2}>", PredIndent(indent), kReturnStatement, pred);
  }

  out += formatv("{0}<\n", kOpcodeSwitchStmt);
  for (auto &item : cases_)
    out += formatv("{0},", md->FormatPred(item, indent + 1));
  if (!default_.empty())
    out += md->FormatPred(default_, indent + 1);
  return out + ">";
}

// Format an OpcodeSwitchCase predicate object.
std::string MCOpcodeSwitchCase::Format(MachineDescription *md,
                                       int indent) const {
  std::string out = PredIndent(indent);

  out += formatv("{0}<\n{1}[", kOpcodeSwitchCase, PredIndent(indent + 1));
  auto sep = "";
  for (auto &opcode : cases_) {
    out += formatv("{0}{1}", sep, opcode);
    sep = ",";
  }
  out += "],";
  if (!case_stmt_.empty()) {
    out += md->FormatPred(case_stmt_, indent + 2);
  }
  return out + ">";
}

//----------------------------------------------------------------------------
// Write out all predicate definitions.
//----------------------------------------------------------------------------
void MachineDescription::WritePredicates() {
  WriteArchHeader("Predicate Definitions", kNoEntries);

  for (const auto &[name, predicate] : mc_inst_predicates_)
    if (OutputPredicate(name)) {
      auto pred = predicate->Format(this, 0);
      if (IsMultiLine(pred))
        pred = formatv("\n{0}{1}", PredIndent(0), pred);
      output_arch() << formatv("\npredicate {0} : {1};\n", name, pred);
    }

  for (const auto &[name, predicate] : mc_sched_predicates_) {
    if (OutputPredicate(name)) {
      auto pred = FormatPred(predicate->inst_predicate(), 0);
      if (IsMultiLine(pred))
        pred = formatv("\n{0}{1}", PredIndent(0), pred);
      output_arch() << formatv("\npredicate {0} : {1};\n", name, pred);
    }
  }

  for (const auto &[name, predicate] : sched_predicates_) {
    if (OutputPredicate(name)) {
      auto pred = FormatPred(predicate->predicate(), 0);
      if (IsMultiLine(pred))
        pred = formatv("\n{0}{1}", PredIndent(0), pred);
      output_arch() << formatv("\npredicate {0} : [{{{1}}];\n", name, pred);
    }
  }

  for (const auto &[name, predicate] : statements_)
    if (OutputPredicate(name)) {
      auto pred = predicate->Format(this, 0);
      if (IsMultiLine(pred))
        pred = formatv("\n{0}{1}", PredIndent(0), pred);
      output_arch() << formatv("\npredicate {0} : {1};\n", name, pred);
    }

  for (const auto &[name, predicate] : switch_cases_)
    if (OutputPredicate(name)) {
      auto pred = predicate->Format(this, 0);
      if (IsMultiLine(pred))
        pred = formatv("\n{0}{1}", PredIndent(0), pred);
      output_arch() << formatv("\npredicate {0} : {1};\n", name, pred);
    }

  output_arch() << "\n";
}

//----------------------------------------------------------------------------
// Dump out register sets.
//----------------------------------------------------------------------------
void MachineDescription::WriteRegisterSets() {
  FindRegisterSets();

  WriteInstHeader("Register definitions");
  for (const auto &reg : register_sets_)
    output_inst() << formatv("register {0};\n", reg.Format());
}

//----------------------------------------------------------------------------
// Write out all referenced register classes.
//----------------------------------------------------------------------------
void MachineDescription::WriteRegisterClasses() {
  WriteInstHeader("Register Classes");
  for (const auto &[name, regclass] : register_class_list_) {
    GetMemberList(regclass);
    if (regclass->referenced())
      output_inst() << regclass->Format();
  }
}

//----------------------------------------------------------------------------
// Write out all referenced operand templates.
//----------------------------------------------------------------------------
void MachineDescription::WriteOperandsMdl() {
  WriteInstHeader("Operand Definitions");
  for (const auto &[name, opnd] : operands_)
    if (opnd->referenced())
      output_inst() << opnd->Format();
}

//----------------------------------------------------------------------------
// Write out all instruction definitions.
//----------------------------------------------------------------------------
void MachineDescription::WriteInstructionsMdl() {
  WriteInstHeader("Instruction Definitions");

  if (!ignore_subunits()) {
    // Write out instructions sorted by subunit, then by instruction name.
    std::map<std::string, std::vector<Instruction *>> subunit_map;
    for (const auto &[name, ins] : instructions_)
      if (ins->HasSubunits())
        subunit_map[(*ins->subunit_)[0]].push_back(ins);

    for (const auto &[name, instruct_set] : subunit_map)
      for (auto *ins : instruct_set)
        output_inst() << ins->Format(true);
  } else {
    // Write out all instructions whether or not they have subunits.
    for (const auto &[name, ins] : instructions_)
      output_inst() << ins->Format(true);
  }
}

//----------------------------------------------------------------------------
// Write out CSV entries for each instruction.
//----------------------------------------------------------------------------
void MachineDescription::WriteInstructionsCsv() {
  std::cout << "-------------- Instructions --------------" << std::endl;
  for (const auto &[name, ins] : instructions_)
    ins->WriteCsv();
}

} // namespace scan
} // namespace mdl
} // namespace mpact

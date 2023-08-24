//===- debug.cpp - Dump debug information for all tablegen records --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Write out all the internal data objects that contain TableGen information
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>

#include "scan.h"

namespace mpact {
namespace mdl {
namespace scan {

//---------------------------------------------------------------------------
// Write out all the things we've scraped from the TableGen file.
//---------------------------------------------------------------------------
void MachineDescription::DumpTableGenInfo() {
  DumpStats();
  DumpSchedMachineModel();
  DumpProcessorModel();
  DumpFUInfo();
  DumpSchedReadWrite(true);
  DumpInstRW();
  DumpItinRW();
  DumpForwardingInfo();
  DumpSchedVariant();
  DumpSchedVar();
  DumpPredicates();
  DumpSchedAlias();
  DumpWriteSequence();
  DumpProcessorItineraries();
  DumpInstrStage();
  DumpInstrItineraryData();
  DumpBypasses();
  DumpInstrItineraryClasses();
  DumpInstructions();
}

void MachineDescription::DumpForwardingGraph() {
  std::cout << "\nForwarding Network:\n";
  for (const auto &[tuple, latency] : forwarding_network_.graph()) {
    const auto &[model, def, use] = tuple;
    std::cout << formatv("  {0}: {1}-->{2} = <", model, def, use);
    for (auto lat : latency)
      std::cout << lat << ",";
    std::cout << ">\n";
  }
}

void MachineDescription::DumpInstrForwardingInfo() {
  for (const auto &[name, instr] : instructions_) {
    std::string out;
    for (auto &[sched_model, inst_rw] : instr->inst_rws())
      out += inst_rw->FwdString(this);
    if (out.empty())
      continue;
    out += "   Functional units: ";
    for (auto &[sched_model, inst_rw] : instr->inst_rws())
      for (const auto &unit : inst_rw->func_units())
        out += unit + ",";
    out += "\n";

    std::cout << instr->Format(false) + out + "\n";
  }
}

//---------------------------------------------------------------------------
// Write out some statistics about the tablegen input.
//---------------------------------------------------------------------------
void MachineDescription::DumpStats() {
  std::cerr << formatv("\nInstructions: {0}\n"
                       "Schedule Models: {1}\n"
                       "Operands: {2}\n"
                       "CPUs: {3}\n"
                       "Processor Resources: {4}\n"
                       "InstRW definitions: {5}\n"
                       "ReadWrite units: {6}\n"
                       "Subunits: {7}\n"
                       "Latencies: {8}\n",
                       instructions_.size(), sched_models_.size(),
                       operands_.size(), cpus_.size(), proc_resources_.size(),
                       instrw_info_.size(), rw_units_.size(), subunits_.size(),
                       latencies_.size());
}

//---------------------------------------------------------------------------
// Format a name indicating what kind of name it is.
//---------------------------------------------------------------------------
std::string MachineDescription::FormatName(const std::string &name) const {
  std::string out = name;

  if (IsInstruction(name))
    out += ".I";
  if (IsOperand(name))
    out += ".O";
  if (IsProcessorModel(name))
    out += ".C";
  if (IsSchedModel(name))
    out += ".SM";
  if (IsProcResource(name))
    out += ".FU";
  if (IsSchedReadWrite(name))
    out += ".RW";
  if (IsSchedVariant(name))
    out += ".SVS";
  if (IsSchedVar(name))
    out += ".sv";
  if (IsMCSchedPredicate(name))
    out += ".MSP";
  if (IsSchedPredicate(name))
    out += ".SP";
  if (IsMCInstPredicate(name))
    out += ".MIP";
  if (IsSchedAliasMatch(name))
    out += ".AM";
  if (IsWriteSequence(name))
    out += ".WS";
  if (IsProcessorItinerary(name))
    out += ".PI";
  if (IsInstrItineraryClass(name))
    out += ".IC";
  if (IsInstrItineraryData(name))
    out += ".ID";
  if (IsInstrStage(name))
    out += ".IS";
  if (IsBypass(name))
    out += ".BP";
  return out;
}

std::string MachineDescription::DumpSchedRW(const std::string &name,
                                            const std::string &model,
                                            std::string prefix) {
  if (IsSchedReadWrite(name))
    return prefix + rw_units_[name]->ToString(this, model, true);
  if (IsWriteSequence(name))
    return prefix + write_sequences_[name]->ToString(this);
  if (IsSchedVariant(name))
    return prefix + sched_variants_[name]->ToString(this, prefix + "   ");
  if (IsSchedAliasMatch(name) && sched_alias_matches_[name].count(model))
    return prefix +
           DumpSchedRW(sched_alias_matches_[name][model], model, prefix);
  return prefix + "Error(" + name + ")";
}

std::string Instruction::ToString(MachineDescription *md) {
  std::string out = Format(false); // just format the instruction and opnds.
  if (!flattened_outs().empty()) {
    out += "  Flattened outs: ";
    for (const auto &opnd : flattened_outs())
      out += opnd.type() + " " + opnd.format() + ", ";
    out += "\n";
  }
  if (!flattened_ins().empty()) {
    out += "  Flattened ins: ";
    for (const auto &opnd : flattened_ins())
      out += opnd.type() + " " + opnd.format() + ", ";
    out += "\n";
  }

  if (!inst_rws().empty()) {
    out += "   RWUnits=[";
    for (auto &rw_unit : rw_units())
      out += formatv("{0},", md->FormatName(rw_unit));
    out += "]\n";
  }

  for (auto &[sched_model, inst_rw] : inst_rws()) {
    out += formatv("  InstRW {0}: SchedModel={1} RWUnits=[", inst_rw->name(),
                   sched_model);
    for (auto &rw_unit : inst_rw->rw_units())
      out += formatv("{0}, ", md->FormatName(rw_unit));
    out += "]\n";
    for (auto &rw_unit : inst_rw->rw_units())
      out += md->DumpSchedRW(rw_unit, sched_model, "   ");

    out += inst_rw->FwdString(md);

    auto refs = md->FormatReferences(this, sched_model, inst_rw);
    int index = md->add_latency(refs);
    out += formatv("     {0} (index={1})\n", refs, index);
  }

  // Gather the register operands, and find the last explicit operand ref.
  auto opnds = md->GetRegisterOperands(this);
  int last_opnd_idx = 0;
  for (auto &[type, name, op_id] : opnds)
    if (op_id > last_opnd_idx)
      last_opnd_idx = op_id;

  int last_operand_cycles = 0; // Consistency check operand_cycles size.

  if (!itinerary_.empty()) {
    for (auto *itin : md->instr_itinerary_class(itinerary_)) {
      std::string separator_and;
      out +=
          formatv("  Itin name:{0}, Itin class:{1}  :\n    FUS:", itin->name(),
                  itinerary_);
      for (const auto &stage : itin->stages()) {
        auto *instr_stage = md->instr_stages(stage);
        out += formatv("{0}(", separator_and);
        std::string separator_or;
        for (const auto &unit : instr_stage->func_units()) {
          out += formatv("{0}{1}", separator_or, unit);
          separator_or = "||";
        }
        out += ")";
        separator_and = " && ";
      }
      out += "\n    CPUS: ";
      for (const auto &[name, model] : md->sched_models()) {
        if (auto *itins = md->processor_itineraries()[model->itineraries()])
          if (itins->iid().count(itin->name()))
            out += formatv("{0},", model->name());
      }
      out += "\n    Operand cycles = [";
      for (const auto &operand_cycle : itin->operand_cycles())
        out += formatv("{0}, ", operand_cycle);
      out += "]";
      int cycle_size = itin->operand_cycles().size();
      if (cycle_size) {
        if (last_operand_cycles != 0 && last_operand_cycles != cycle_size)
          out += " Inconsistent number of operand cycles.";
        if (last_opnd_idx >= cycle_size)
          out += " Too few operand cycles.";
        last_operand_cycles = itin->operand_cycles().size();
      }
      out += "\n";
    }
  }

  return out + "}\n";
}

//---------------------------------------------------------------------------
// Format SchedMachineModel information
//---------------------------------------------------------------------------
std::string SchedMachineModel::ToString(const MachineDescription *md) const {
  auto out =
      formatv("SchedMachineModel {0}, issue_width={1}, load_latency={2}, "
              "micro_op_buffer_size={3}, misprediction_penalty={4}, "
              "itineraries={5}\n    CPUS: ",
              name_, issue_width_, load_latency_, micro_op_buffer_size_,
              mispredict_penalty_, itineraries_);
  for (auto &cpu : cpus_)
    out += formatv("{0}, ", cpu);

  out += "\n     FUS: ";
  for (const auto &fu : func_units_)
    out += formatv("{0}, ", fu);
  return out + "\n\n";
}

//----------------------------------------------------------------------------
// Format InstrStage records.
//----------------------------------------------------------------------------
std::string InstrStage::ToString(const MachineDescription *md) const {
  std::string out =
      formatv("      InstrStage {0}: cycles={1}, func_units=[", name_, cycles_);
  for (const auto &func_unit : func_units_)
    out += formatv("{0}, ", func_unit);
  out += formatv("], timeinc={0}", timeinc_);

  if (!resource_expr_.isNop())
    out += ",  " + resource_expr_.ToString();
  return out + "\n";
}

//---------------------------------------------------------------------------
// Format Processor model information.
//---------------------------------------------------------------------------
std::string ProcessorModel::ToString(MachineDescription *md) const {
  return formatv("Cpu {0}, SchedModel={1}\n", name_, sched_model_);
}

//----------------------------------------------------------------------------
// Format SchedVar records.
//----------------------------------------------------------------------------
std::string SchedVar::ToString(MachineDescription *md,
                               std::string prefix) const {
  std::string out =
      formatv("{2}SchedVar {0}:\n"
              "{2}   Predicate={1},\n"
              "{2}   Selected=[",
              md->FormatName(name_), md->FormatName(predicate_), prefix);

  for (const auto &rw_unit : selected_)
    out += formatv("{0},", md->FormatName(rw_unit));
  out += "]\n";
  for (const auto &rw_unit : selected_)
    out += md->DumpSchedRW(rw_unit, "?", prefix + "      ");

  return out + "\n";
}

//----------------------------------------------------------------------------
// Format SchedVar records.
//----------------------------------------------------------------------------
std::string SchedVariant::ToString(MachineDescription *md,
                                   std::string prefix) const {
  std::string out = is_write_ ? "--->VariantWrite" : "--->VariantRead ";
  out += formatv("={0}, schedModel={1}, ", md->FormatName(name_), sched_model_);

  if (is_variadic())
    out += "Variadic ";
  out += "Variants[=";
  for (const auto &variant : variants_)
    out += formatv("{0},", md->FormatName(variant));
  out += "]\n";
  for (const auto &variant : variants_)
    out += md->sched_vars()[variant]->ToString(md, prefix + "   ");

  return out;
}

//----------------------------------------------------------------------------
// Format Function Unit information.
//----------------------------------------------------------------------------
std::string ProcResource::ToString(MachineDescription *md) const {
  auto out = formatv("Funcunit {0}<{1}>, {2} super={3} is resource grp={4} "
                     "Buffer Size={5}",
                     name_, num_instances_, sched_model_, fu_super_,
                     is_proc_res_group_, buffer_size_);
  if (!group_names_.empty()) {
    out += ", Resources=[";
    for (const auto &fu : group_names_)
      out += formatv("{0} ", fu);
    out += "]";
  }
  return out + "\n";
}

//----------------------------------------------------------------------------
// Format SchedReadWrite records.
//----------------------------------------------------------------------------
std::string SchedReadWrite::ToString(MachineDescription *md,
                                     const std::string &model,
                                     bool all_info) const {
  if (all_info) {
    std::string out = is_write_ ? "Write-Unit " : "Read-Unit ";
    out += formatv("{0}, \n", name_);

    std::string unit;
    for (auto &[sched_model, sched_model_info] : sched_model_info_) {
      if (!model.empty() && model.find(sched_model) == std::string::npos)
        continue;
      unit += formatv("      (SchedModel={0} Latency={1} MOps={2}", sched_model,
                      sched_model_info.latency(), sched_model_info.micro_ops());

      if (sched_model_info.is_begin_group())
        unit += " BeginGroup";
      if (sched_model_info.is_end_group())
        unit += " EndGroup";
      if (sched_model_info.is_single_issue())
        unit += " SingleIssue";
      if (sched_model_info.retire_ooo())
        unit += " RetireOOO";
      unit += is_write_ ? " FUs=[" : ", ValidWrites=[";
      for (auto &fu : sched_model_info.func_units())
        unit += formatv("{0},", md->FormatName(fu));
      if (is_write_) {
        unit += "] Resource Cycles=[";
        for (auto cycle : sched_model_info.res_cycles())
          unit += formatv("{0},", cycle);
        unit += "] StartAt Cycles=[";
        for (auto cycle : sched_model_info.start_cycles())
          unit += formatv("{0},", cycle);
      }
      unit += "]),\n";
    }
    if (unit.empty())
      return out + "\n";
    return out + unit;
  }

  if (!is_write_)
    return "";

  // Debugging stuff.
  std::string out;
  for (auto &[model, info] : sched_model_info_) {
    auto &func_units = info.func_units();
    auto &res_cycles = info.res_cycles();
    auto &start_cycles = info.start_cycles();
    int micro_ops = info.micro_ops();
    if (func_units.size() == 0 || micro_ops == (int)func_units.size())
      continue;

    out += formatv("{0}: {1,-20} \t[ ", md->family(), name());
    for (auto &unit : func_units) {
      out += formatv("{0}", unit);
      if (md->IsProcResourceGroup(unit))
        out +=
            formatv("({0})", md->proc_resources()[unit]->group_names().size());
      auto *funit = md->proc_resources()[unit];
      if (funit->fu_super() != "")
        out += formatv("<<{0}>>", funit->fu_super());
      if (!funit->child_func_units().empty()) // it IS a superunit
        out += ("<<>>");
      if (funit->num_instances() != 1)
        out += formatv("[[{0}]]", funit->num_instances());
      out += " ";
    }
    out += "] \t[ ";
    for (auto cycles : res_cycles)
      out += formatv("{0} ", cycles);
    if (!start_cycles.empty()) {
      out += "] \t@[ ";
      for (auto cycles : start_cycles)
        out += formatv("{0} ", cycles);
    }
    out += formatv("] \t{0}", micro_ops);
    out += info.FormatFusFlags();

    std::string com;
    if (!res_cycles.empty() && res_cycles.size() != func_units.size())
      com += formatv(" F{0}{2}R{1}", func_units.size(), res_cycles.size(),
                     func_units.size() < res_cycles.size() ? "<" : ">");
    if (res_cycles.empty() && micro_ops != 0 &&
        func_units.size() != (unsigned)micro_ops)
      com += formatv(" F{0}{2}M{1}", func_units.size(), micro_ops,
                     (int)func_units.size() < micro_ops ? "<" : ">");
    if (!res_cycles.empty() && micro_ops != 0) {
      int sum_cycles = 0;
      for (auto &cycles : res_cycles)
        sum_cycles += cycles;
      if (sum_cycles != micro_ops)
        com += formatv(" R{0}{2}M{1}", sum_cycles, micro_ops,
                       sum_cycles < micro_ops ? "<" : ">");
    }
    if (!com.empty())
      out += "\t   ///" + com;
    out += "\n";
  }

  return out;
}

//----------------------------------------------------------------------------
// Format SchedAlias records.
//----------------------------------------------------------------------------
std::string SchedAlias::ToString(MachineDescription *md) const {
  return formatv("SchedAlias {0}: SchedModel={1}, Match RW={2} Alias RW={3}\n",
                 name_, sched_model_, md->FormatName(match_rw_),
                 md->FormatName(alias_rw_));
}

//----------------------------------------------------------------------------
// Format SchedPredicate records.
//----------------------------------------------------------------------------
std::string SchedPredicate::ToString(MachineDescription *md) const {
  return formatv("SchedPredicate {0}: SchedModel={1}, predicate={2}\n", name_,
                 sched_model_, md->FormatName(predicate_));
}

//----------------------------------------------------------------------------
// Format MCSchedPredicate records.
//----------------------------------------------------------------------------
std::string MCSchedPredicate::ToString(MachineDescription *md) const {
  auto out = formatv("MCSchedPredicate {0}", name_);
  if (!sched_model_.empty())
    out += formatv(": SchedModel={0}", sched_model_);
  return out + formatv(", MCInstPred={0}\n", md->FormatName(inst_predicate_));
}

//----------------------------------------------------------------------------
// Indent formatting of predicates an appropriate amount.
//----------------------------------------------------------------------------
void PredIndent(std::string *out, int indent) {
  *out += std::string(indent * 2, ' ');
}

//----------------------------------------------------------------------------
// Format MCInstPredicate components.
//----------------------------------------------------------------------------
std::string MCPredFormat(const std::string &name, MachineDescription *md,
                         int indent) {
  if (name.empty())
    return "";
  std::string out;
  PredIndent(&out, indent);

  if (md->mc_inst_predicates().count(name))
    return out + formatv("MCInstPredicate={0}: {1}", name,
                         md->mc_inst_predicates()[name]->ToString(md, indent));
  else if (md->statements().count(name))
    return out + formatv("MCStatement={0}\n{1}", name,
                         md->statements()[name]->ToString(md, indent));
  else if (md->switch_cases().count(name))
    return out + formatv("MCOpcodeSwitchCases={0}\n{1}", name,
                         md->switch_cases()[name]->ToString(md, indent));
  return out + formatv("Unknown Object: {0}\n", name);
}

//----------------------------------------------------------------------------
// Format MCInstPredicate records.
//----------------------------------------------------------------------------
std::string MCInstPredicate::ToString(MachineDescription *md,
                                      int indent) const {
  std::string out;
  if (indent == 0 && str_contains(name_, "anonymous_"))
    return "";

  if (indent == 0) {
    out += formatv("MCInstPredicate={0}: ", name_);
  } else if (!str_contains(name_, "anonymous_")) {
    out += "{...}\n";
    return out;
  }

  if (name_ == kTrue || name_ == kFalse)
    return out + "\n";

  out += "[";
  for (auto &attr : attributes_)
    out += formatv("{0},", attr);
  out += "]";

  if (!function_name_.empty())
    out += formatv(", FN={0}", function_name_);
  if (!function_mapper_.empty())
    out += formatv(", FuncMapper={0}", function_mapper_);
  if (!opindex_.empty())
    out += formatv(", opindex={0}", opindex_);
  if (!immval_.empty())
    out += formatv(", immval={0}", immval_);
  if (!register_name_.empty())
    out += formatv(", Register={0}", register_name_);
  if (!valid_opcodes_.empty()) {
    out += ", valid_opcodes=[";
    for (auto &opcode : valid_opcodes_)
      out += formatv("{0},", opcode);
    out += "]";
  }

  out += formatv("\n{0}", MCPredFormat(statement_, md, indent + 1));

  if (!predicates_.empty()) {
    for (auto &pred : predicates_)
      out += MCPredFormat(pred, md, indent + 1);
  }
  return out;
}

//----------------------------------------------------------------------------
// Format MCStatement records.
//----------------------------------------------------------------------------
std::string MCStatement::ToString(MachineDescription *md, int indent) const {
  std::string out;
  if (indent == 0 && str_contains(name_, "anonymous_"))
    return "";

  if (!predicate_.empty())
    out = MCPredFormat(predicate_, md, indent + 1);

  if (!cases_.empty()) {
    PredIndent(&out, indent);
    out += "  Cases=\n";
    for (auto &opcode : cases_)
      out += MCPredFormat(opcode, md, indent + 2);
  }

  if (!default_.empty())
    out += MCPredFormat(default_, md, indent + 1);
  return out;
}

//----------------------------------------------------------------------------
// Format MCOpcodeSwitchCase records.
//----------------------------------------------------------------------------
std::string MCOpcodeSwitchCase::ToString(MachineDescription *md,
                                         int indent) const {
  std::string out;
  if (indent == 0)
    return "";

  PredIndent(&out, indent);
  out += "  Opcodes=[";
  for (auto &opcode : cases_)
    out += formatv("{0},", opcode);
  out += "]\n";

  if (!case_stmt_.empty()) {
    PredIndent(&out, indent);
    out += formatv("  Default=({0})\n{1}", case_stmt_,
                   MCPredFormat(case_stmt_, md, indent + 2));
  }
  return out;
}

//----------------------------------------------------------------------------
// Format WriteSequence records.
//----------------------------------------------------------------------------
std::string WriteSequence::ToString(MachineDescription *md) const {
  std::string out =
      formatv("WriteSequence Name={0}, SchedModel={1}, Repeat={2}, Writes=[",
              name_, sched_model_, repeat_);

  for (const auto &write : writes_)
    out += formatv("{0},", md->FormatName(write));

  return out + "]\n";
}

//----------------------------------------------------------------------------
// Format InstRW records.
//----------------------------------------------------------------------------
std::string InstRW::ToString(const MachineDescription *md, bool full) const {
  std::string out = "InstRW: regex=[";
  for (const auto &regex : instregex_)
    out += formatv("\"{0}\",", regex);
  out += "], instrs[";
  if (full)
    for (const auto &instr : instrs_)
      out += formatv("{0},", instr);

  out += formatv("], schedModel={0},\n   RWUnits=[", sched_model_);
  for (const auto &rw_unit : rw_units_)
    out += formatv("{0},", md->FormatName(rw_unit));
  return out + "]\n" + FwdString(md) + "\n";
}

//-------------------------------------------------------------------------
// Format InstrRW forwarding information.
//-------------------------------------------------------------------------
std::string InstRW::FwdString(const MachineDescription *md) const {
  if (forwarding_info_.empty())
    return "";

  std::string out;
  for (auto [res_id, latency, def, use, pred] : forwarding_info_) {
    if (pred == "NoSchedPred")
      pred = "";
    out += formatv("   Forward {2}-->{3}: res {0}, latency {1} :{4} {5}\n",
                   res_id, latency, def, use, pred, sched_model_);
  }
  return out;
}

//----------------------------------------------------------------------------
// Format ItinRW records.
//----------------------------------------------------------------------------
std::string ItinRW::ToString(const MachineDescription *md) const {
  std::string out = "ItinRW " + name_ + " : ";
  out += formatv("schedModel={0}, RWUnits=[", sched_model_);
  for (const auto &rw_unit : rw_units_)
    out += formatv("{0},", md->FormatName(rw_unit));
  out += "]\n    Itin Classes=[";
  for (const auto &unit : itin_classes_)
    out += unit + ",";
  return out + "]\n";
}

//-------------------------------------------------------------------------
// Format Processor Itineraries records.
//-------------------------------------------------------------------------
std::string ProcessorItineraries::ToString(const MachineDescription *md) const {
  auto out = formatv("ProcessorItineraries name {0}, func_units=[", name_);
  for (const auto &resource : resources_)
    out += formatv("{0}, ", resource);
  out += "], Bypass=[";
  for (const auto &bypass : bypass_)
    out += formatv("{0}, ", bypass);

  out += "], IID=[";
  for (const auto &iid : iid_)
    out += formatv("{0}, ", iid);
  return out + "]\n\n";
}

//----------------------------------------------------------------------------
// Format Instruction Itinerary Data records.
//----------------------------------------------------------------------------
std::string InstrItineraryData::ToString(MachineDescription *md) const {
  auto out = formatv("    InstrItineraryData {0}: class={1}, micro_ops={2}",
                     name_, instr_itinerary_class_, num_micro_ops_);
  out += ", operand_cycles=[";
  for (const auto &operand_cycle : operand_cycles_)
    out += formatv("{0}, ", operand_cycle);
  out += "], bypasses=[";
  for (const auto &bypass : bypasses_)
    out += formatv("{0}, ", bypass);

  out += "], stages=[";
  for (const auto &stage : stages_)
    out += formatv("{0}, ", stage);
  out += "]\n";
  for (const auto &stage : stages_)
    out += md->instr_stages(stage)->ToString(md);

  if (!issue_stages_.empty()) {
    out += "    ==> Issue resources: ";
    for (auto &expr : issue_stages_)
      out += expr.ToString() + " || ";
    out = out.substr(0, out.size() - 4);
    out += "\n";
  }
  if (!resource_args_.empty()) {
    for (unsigned arg = 0; arg < resource_args_.size(); arg++)
      out += formatv("    ==> Template argument {0}: {1}\n", arg + 1,
                     resource_args_[arg].ToString());
    out += "\n";
  }
  return out;
}

constexpr char divider[] =
    "\n//-------------------------------------------------------------------\n";
static void DumpHeader(const char *title, int size) {
  std::cout << formatv("{0}// {1} ({2} entries){0}", divider, title, size);
}

//----------------------------------------------------------------------------
// Dump out all processor variants.
//----------------------------------------------------------------------------
void MachineDescription::DumpProcessorModel() {
  DumpHeader("CPU Description Classes", cpus_.size());
  for (const auto &[name, cpu] : cpus_)
    cpu->Dump(this);
}

//----------------------------------------------------------------------------
// Dump out all schedule models.
//----------------------------------------------------------------------------
void MachineDescription::DumpSchedMachineModel() {
  DumpHeader("SchedModel Description Classes", sched_models_.size());
  for (const auto &[name, model] : sched_models_)
    model->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all functional units.
//----------------------------------------------------------------------------
void MachineDescription::DumpFUInfo() {
  DumpHeader("Functional Unit Descriptions", proc_resources_.size());
  for (const auto &[name, fu] : proc_resources_)
    fu->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all rwunits.
//----------------------------------------------------------------------------
void MachineDescription::DumpSchedReadWrite(bool all_info) {
  DumpHeader("SchedReadWrite Descriptions", rw_units_.size());
  for (const auto &[name, rwunit] : rw_units_)
    rwunit->Dump(this, all_info);
}

// ----------------------------------------------------------------------------
// Dump out all InstRW records.
//----------------------------------------------------------------------------
void MachineDescription::DumpInstRW() {
  DumpHeader("InstRW Descriptions", instrw_info_.size());
  for (const auto *instrw : instrw_info_)
    instrw->Dump(this, true);
}

// ----------------------------------------------------------------------------
// Dump InstRW records that have ReadAdvance ReadWrite records.
//----------------------------------------------------------------------------
void MachineDescription::DumpForwardingInfo() {
  int count = 0;
  for (auto *instrw : instrw_info_)
    if (!instrw->forwarding_info().empty())
      count++;

  DumpHeader("Forwarding Descriptions", count);
  for (auto *instrw : instrw_info_)
    if (!instrw->forwarding_info().empty())
      instrw->Dump(this, false);
}

// ----------------------------------------------------------------------------
// Dump out all ItinRW records.
//----------------------------------------------------------------------------
void MachineDescription::DumpItinRW() {
  DumpHeader("ItinRW Descriptions", itinrw_info_.size());
  for (const auto *itinrw : itinrw_info_)
    itinrw->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all SchedVariant records.
//----------------------------------------------------------------------------
void MachineDescription::DumpSchedVariant() {
  DumpHeader("SchedVariant Descriptions", sched_variants_.size());
  for (const auto &[name, variant] : sched_variants_)
    variant->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all SchedVar records.
//----------------------------------------------------------------------------
void MachineDescription::DumpSchedVar() {
  DumpHeader("SchedVar Descriptions", sched_vars_.size());
  for (const auto &[name, var] : sched_vars_)
    var->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all MCSchedPredicate, MCInstPredicates, SchedPredicates, and
// associated statements.
//----------------------------------------------------------------------------
void MachineDescription::DumpPredicates() {
  DumpHeader("MCSchedPredicate Descriptions", mc_sched_predicates_.size());
  for (const auto &[name, sp] : mc_sched_predicates_)
    sp->Dump(this);

  DumpHeader("SchedPredicate Descriptions", sched_predicates_.size());
  for (const auto &[name, sp] : sched_predicates_)
    sp->Dump(this);

  DumpHeader("MCInstPredicate Descriptions", mc_inst_predicates_.size());
  for (const auto &[name, fp] : mc_inst_predicates_)
    fp->Dump(this);

  DumpHeader("MCStatement Descriptions", statements_.size());
  for (const auto &[name, stmt] : statements_)
    stmt->Dump(this);

  DumpHeader("MCOpcodeSwitchCase Descriptions", switch_cases_.size());
  for (const auto &[name, stmt] : switch_cases_)
    stmt->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all SchedAlias records.
//----------------------------------------------------------------------------
void MachineDescription::DumpSchedAlias() {
  DumpHeader("SchedAlias Descriptions", sched_alias_.size());
  for (const auto &[name, alias] : sched_alias_)
    alias->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all WriteSequence records.
//----------------------------------------------------------------------------
void MachineDescription::DumpWriteSequence() {
  DumpHeader("WriteSequence Descriptions", write_sequences_.size());
  for (const auto &[name, write_sequence] : write_sequences_)
    write_sequence->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all ProcessorItineraries records.
//----------------------------------------------------------------------------
void MachineDescription::DumpProcessorItineraries() {
  DumpHeader("ProcessorItinerary Descriptions", processor_itineraries_.size());
  for (const auto &[name, processor_itinerary] : processor_itineraries_)
    processor_itinerary->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all InstrStage records.
//----------------------------------------------------------------------------
void MachineDescription::DumpInstrStage() {
  DumpHeader("InstrStage Descriptions", instr_stages_.size());
  for (const auto &[name, instr_stage] : instr_stages_)
    instr_stage->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all InstrItinClasses records.
//----------------------------------------------------------------------------
void MachineDescription::DumpInstrItineraryClasses() {
  DumpHeader("InstrItinClass Descriptions", instr_itinerary_class_.size());
  std::string out;
  for (const auto &[name, members] : instr_itinerary_class_) {
    std::cout << formatv("  InstrItinClass {0}:\n", name);
    for (auto *member : members)
      member->Dump(this);
    std::cout << "\n";
  }
}

// ----------------------------------------------------------------------------
// Dump out all InstrItinData records.
//----------------------------------------------------------------------------
void MachineDescription::DumpInstrItineraryData() {
  DumpHeader("InstrItinData Descriptions", instr_itinerary_data_.size());
  for (const auto &[name, instr_itin_data] : instr_itinerary_data_)
    instr_itin_data->Dump(this);
}

// ----------------------------------------------------------------------------
// Dump out all Bypasses records.
//----------------------------------------------------------------------------
void MachineDescription::DumpBypasses() {
  DumpHeader("Bypass Descriptions", bypasses_.size());
  for (const auto &name : bypasses_)
    std::cout << name << ", ";
  std::cout << "\n";
}

// ----------------------------------------------------------------------------
// Dump out information about all instruction definitions.
//----------------------------------------------------------------------------
void MachineDescription::DumpInstructions() {
  DumpHeader("Instruction Descriptions", instructions_.size());
  for (const auto &[name, instr] : instructions_)
    instr->Dump(this);
}

} // namespace scan
} // namespace mdl
} // namespace mpact

//===- scan.h - Definitions for scanning tablegen files -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Read through an llvm table-gen-generated "reports.txt" input file, and
// compile a hash of instructions and their attributes (operands, syntax,
// etc), operand definitions, and register/register class definitions.
//
// Since the input file is machine-generated, we assume the file is (very)
// well-formed, but still do some simple sanity checks to avoid crashes.
//
// For each instruction we find, we collect:
//    - its name.
//    - its output operand names and types.
//    - its input operand names and types.
//    - its assembly formatting rule.
//    - its llvm matching pattern.
//    - its parent instruction (if present).
//    - its subunit identifier.
//
// We also look for register definitions, and register class definitions.
//
// Currently we write out a file suitable for importing into a spreadsheet.
// For building outsize of blaze: clang++ scan.cc -o scan
//
//===----------------------------------------------------------------------===//

#ifndef TDSCAN_SCAN_H_
#define TDSCAN_SCAN_H_

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/FormatVariadic.h"

namespace mpact {
namespace mdl {
namespace scan {

//-------------------------------------------------------------------------
// Classes that hold the scraped architecture model.
//-------------------------------------------------------------------------
class MachineDescription;
class ProcessorModel;
struct SchedModelRef;
class ProcResource;
class FuncComboData;
class SchedMachineModel;
class SchedReadWrite;
class SchedAlias;

class SchedVariant;
class SchedVar;
class SchedPredicate;
class MCSchedPredicate;
class MCInstPredicate;
class MCStatement;
class MCOpcodeSwitchCase;

class WriteSequence;
class InstRW;
class ItinRW;

class ProcessorItineraries;
class InstrItineraryData;
class InstrStage;
class ItineraryFuncUnit;

class LatencyInfo;

class Instruction;
class Operand;
class RegDefinition;
class RegisterClass;

using StringSet = std::set<std::string>;
using StringVec = std::vector<std::string>;

// Represent a vector of operand references, including a reference type,
// a string representing the operand reference, and the original operand id.
using OperandRefs = std::vector<std::tuple<std::string, std::string, int>>;

//-------------------------------------------------------------------------
// Formatting and string search functions.
//-------------------------------------------------------------------------
template <typename... Ts>
inline std::string formatv(const char *fmt, Ts &&...vals) {
  return std::string(llvm::formatv(fmt, vals...));
}

inline bool str_contains(const std::string &str, const char *search) {
  return strstr(str.c_str(), search);
}

//-------------------------------------------------------------------------
// We don't like attributes with ? in them, just return an empty string.
//-------------------------------------------------------------------------
inline std::string FixAttribute(const std::string &attribute) {
  return (str_contains(attribute, "?")) ? "" : attribute;
}

//-------------------------------------------------------------------------
// The definition of a CPU class defined in tablegen files.
// A cpu record in tablegen files contains the following relevant
// information: cpu name, and the schedule model. The member `func_units_`
// are the functional units supported by cpu.
//-------------------------------------------------------------------------
class ProcessorModel {
public:
  ProcessorModel(const std::string &name, const std::string &sched_model)
      : name_(name), sched_model_(sched_model) {}

  // Accessor methods.
  const std::string &name() const { return name_; }
  const std::string &sched_model() const { return sched_model_; }
  void set_issue_width(int iw) { issue_width_ = iw; }

  // Print Utilities.
  std::string ToString(MachineDescription *md) const;
  void Dump(MachineDescription *md) { std::cout << ToString(md); }

private:
  int issue_width_ = -1;
  std::string name_;
  std::string sched_model_;
};

//-------------------------------------------------------------------------
// The definition of a machine model class defined in tablegen files.
// A single machine-model record in tablegen files contains the following
// relevant information: issue-width, and the default load latency.
//-------------------------------------------------------------------------
class SchedMachineModel {
public:
  SchedMachineModel(const std::string name, const std::string &issue_width,
                    const std::string &load_latency,
                    const std::string &high_latency, int micro_op_buffer_size,
                    int mispredict_penalty, const std::string &itineraries,
                    const std::string family)
      : name_(name), output_name_(name), issue_width_(std::stoi(issue_width)),
        load_latency_(std::stoi(load_latency)),
        high_latency_(std::stoi(high_latency)),
        micro_op_buffer_size_(micro_op_buffer_size),
        mispredict_penalty_(mispredict_penalty), itineraries_(itineraries) {
    itineraries_ = fix_itin_name(itineraries_);
    output_name_ = fix_output_name(output_name_, family);
  }

  SchedMachineModel(const std::string name, const std::string &itineraries,
                    const std::string family)
      : name_(name), output_name_(name), itineraries_(itineraries) {
    itineraries_ = fix_itin_name(itineraries_);
    output_name_ = fix_output_name(output_name_, family);
    load_latency_ = llvm::MCSchedModel::DefaultLoadLatency;
    high_latency_ = llvm::MCSchedModel::DefaultHighLatency;
    micro_op_buffer_size_ = llvm::MCSchedModel::DefaultMicroOpBufferSize;
    issue_width_ = llvm::MCSchedModel::DefaultIssueWidth;
    mispredict_penalty_ = llvm::MCSchedModel::DefaultMispredictPenalty;
  }

  // Fix up itinerary and model names to be more user-friendly
  static std::string fix_itin_name(std::string name) {
    return (name == "NoItineraries") ? "" : name;
  }

  static std::string fix_output_name(std::string name,
                                     std::string family = "") {
    if (name == "NoSchedModel")
      return family;
    size_t itin = name.find("Itineraries");
    if (itin != std::string::npos)
      name.erase(itin, 11);
    size_t model = name.find("Model");
    if (model != std::string::npos)
      name.erase(model, 5);
    return name;
  }

  void fix_up_names(const std::string &family) {
    // Fix up the model name to be more user-friendly
    if (output_name_ == "NoSchedModel")
      output_name_ = family;
    if (itineraries_ == "NoItineraries")
      itineraries_ = "";
    size_t itin = output_name_.find("Itineraries");
    if (itin != std::string::npos)
      output_name_.erase(itin, 11);
    size_t model = output_name_.find("Model");
    if (model != std::string::npos)
      output_name_.erase(model, 5);
  }

  int issue_width() const { return issue_width_; }
  int load_latency() const { return load_latency_; }
  int high_latency() const { return high_latency_; }
  int micro_op_buffer_size() const { return micro_op_buffer_size_; }
  int mispredict_penalty() const { return mispredict_penalty_; }

  std::string name() const { return name_; }
  const std::string &output_name() const { return output_name_; }
  std::set<std::string> &cpus() { return cpus_; }
  void add_cpu(std::string cpu) { cpus_.insert(cpu); }
  StringSet &func_units() { return func_units_; }
  void add_func_unit(const std::string &unit) { func_units_.insert(unit); }
  bool has_itineraries() const { return !itineraries().empty(); }
  const std::string &itineraries() const { return itineraries_; }

  std::vector<ItineraryFuncUnit *> &itinerary_fus() { return itinerary_fus_; }
  void add_itinerary_func_unit(ItineraryFuncUnit *unit) {
    if (std::find(itinerary_fus_.begin(), itinerary_fus_.end(), unit) ==
        itinerary_fus_.end())
      itinerary_fus_.push_back(unit);
  }

  // Print Utilities.
  std::string ToString(const MachineDescription *md) const;
  void Dump(MachineDescription *md) { std::cout << ToString(md); }
  std::string Format(MachineDescription *md);

private:
  std::string name_;             // name scraped from tablegen
  std::string output_name_;      // name for the output file
  int issue_width_ = 1;          // default issue width is 1
  int load_latency_ = 1;         // default load_latency is 1
  int high_latency_ = 1;         // default high latency is 1
  int micro_op_buffer_size_ = 0; // default micro-op buffer size
  int mispredict_penalty_ = 0;   // default misprediction penalty
  std::string itineraries_;

  // derived information
  StringSet func_units_;
  std::vector<ItineraryFuncUnit *> itinerary_fus_;
  std::set<std::string> cpus_; // which CPUs use this model
};

//-------------------------------------------------------------------------
// SuperNames are used to build names of functional units which are
// superunits in the tablegen file.
//-------------------------------------------------------------------------
class SuperName {
public:
  explicit SuperName(const std::string &name) : name_(name), num_units_(0) {}
  bool operator<(const SuperName &rhs) const {
    if (num_units_ != rhs.num_units_)
      return num_units_ < rhs.num_units_;
    return name_ < rhs.name_;
  }
  std::string &name() { return name_; }
  int num_units() const { return num_units_; }
  void add_name(std::string name) {
    name_ += ":" + name;
    num_units_++;
  }

private:
  std::string name_; // appended names of subunits
  int num_units_;
};

//-------------------------------------------------------------------------
// The definition of a functional-unit class defined in tablegen files.
// A single functional unit record in tablegen files contains the following
// relevant information: functional unit name, and number of units. The member
// `subunits_` are the sub-units supported by the functional unit.
//-------------------------------------------------------------------------
class ProcResource {
public:
  ProcResource(const std::string &name, const std::string &num_instances,
               const std::string &buffer_size, std::string sched_model,
               std::vector<std::string> group_names, std::string fu_super,
               bool is_proc_res_group)
      : name_(name), num_instances_(std::stoi(num_instances)),
        buffer_size_(std::stoi(buffer_size)), sched_model_(sched_model),
        group_names_(group_names), fu_super_(fu_super),
        is_proc_res_group_(is_proc_res_group) {}

  // Accessor methods.
  const std::string &name() const { return name_; }
  int num_instances() const { return num_instances_; }
  int buffer_size() const { return buffer_size_; }
  const std::string &sched_model() { return sched_model_; }
  bool is_proc_res_group() { return is_proc_res_group_; }
  const std::string &fu_super() { return fu_super_; }
  std::vector<std::string> &group_names() { return group_names_; }
  bool is_resource_group() const { return !group_names_.empty(); }
  StringSet &resource_group() { return resource_group_; }

  StringVec &base_func_units() { return base_func_units_; }
  void add_base_unit(const std::string &func_unit) {
    base_func_units_.push_back(func_unit);
  }
  StringVec &child_func_units() { return child_units_; }
  void add_child_unit(const std::string &func_unit) {
    child_units_.push_back(func_unit);
  }

  void add_subunit(int subunit) { subunits_.insert(subunit); }
  void SortSubunits();
  std::vector<SuperName> &super_names() { return super_names_; }

  // Print Utilities.
  std::string ToString(MachineDescription *md) const;
  void Dump(MachineDescription *md) { std::cout << ToString(md); }
  std::string Format(MachineDescription *md) const;

private:
  std::string name_;
  unsigned num_instances_;  // how many instances
  int buffer_size_;         // resource buffering parameter
  std::string sched_model_; // associated sched model
  StringVec group_names_;
  std::string fu_super_;
  bool is_proc_res_group_ = false;

  std::vector<SuperName> super_names_;
  StringVec child_units_; // units that name this unit as Super.

  // derived information about the processor
  StringVec base_func_units_;
  StringSet resource_group_;
  std::set<int> subunits_; // which subunits are supported.
};

//-------------------------------------------------------------------------
// For a ProcReadAdvance tablegen record, the class is used to store the
// latency and schedule-write resource list.
//-------------------------------------------------------------------------
struct ProcReadAdvance {
  std::set<std::string> valid_writes;
  int latency;
  std::string sched_model;
};

//-------------------------------------------------------------------------
// For a ProcWriteResources tablegen record, the class is used to store the
// latency and functional unit list.
//-------------------------------------------------------------------------
struct ProcWriteResources {
  std::set<std::string> proc_resources;
  int latency;
  std::string sched_model;
};

//-------------------------------------------------------------------------
// For a ProcWriteResources tablegen record, the class is used to store the
// latency and functional unit list. For a ProcReadAdvance tablegen record, the
// class is used to store the latency and valid writes information.
//-------------------------------------------------------------------------
struct SchedModelRef {
  SchedModelRef() = default;
  SchedModelRef(const std::string &latency, const std::string &micro_ops,
                bool is_write, bool is_begin_group, bool is_end_group,
                bool is_single_issue, bool retire_ooo,
                const std::vector<std::string> &func_units,
                const std::vector<std::string> &res_cycles,
                const std::vector<std::string> &start_cycles) :
   input_latency_(latency), latency_(0),
   micro_ops_(std::stoi(micro_ops)), is_write_(is_write),
   is_begin_group_(is_begin_group), is_end_group_(is_end_group),
   is_single_issue_(is_single_issue), retire_ooo_(retire_ooo),
   func_names_(func_units) {
     for (auto cycle : res_cycles)
       res_cycles_.push_back(std::stoi(cycle));
     for (auto cycle : start_cycles)
       start_cycles_.push_back(std::stoi(cycle));
     if (!input_latency_.empty())
       latency_ = std::stoi(input_latency_);
   }

  std::string input_latency_;            // the original stated latency
  int latency_;                          // latency of the reference
  int micro_ops_;                        // number of micro ops in the ref
  bool is_write_;                        // true if this is a write record
  bool is_begin_group_;                  // must this ref begin an issue group
  bool is_end_group_;                    // must this ref end an issue group
  bool is_single_issue_;                 // must this ref be issued alone
  bool retire_ooo_;                      // can this ref retire out-of-order
  std::vector<std::string> func_names_;  // functional unit names referenced
  std::vector<int> res_cycles_;          // how many cycles are the FUs used
  std::vector<int> start_cycles_;        // when do the FUs start being used

  bool is_valid_write_latency() const { return !input_latency_.empty(); }
  int latency() const { return latency_; }
  int micro_ops() const { return micro_ops_; }
  int is_write() const { return is_write_; }
  bool is_begin_group() const { return is_begin_group_; }
  bool is_end_group() const { return is_end_group_; }
  bool is_single_issue() const { return is_single_issue_; }
  bool retire_ooo() const { return retire_ooo_; }
  const std::vector<std::string> &func_units() const { return func_names_; }
  const std::vector<std::string> &valid_writes() const { return func_names_; }
  const std::vector<int> &res_cycles() const { return res_cycles_; }
  const std::vector<int> &start_cycles() const { return start_cycles_; }

  // Format add Functional unit reference attributes for a reference.
  std::string FormatFusFlags() const {
    std::string out;
    if (is_begin_group_)
      out += ", BeginGroup";
    if (is_end_group_)
      out += ", EndGroup";
    if (is_single_issue_)
      out += ", SingleIssue";
    if (retire_ooo_)
      out += ", RetireOOO";
    return out;
  }
};

//-------------------------------------------------------------------------
// Every SchedReadWrite object contains a dictionary of SchedModelRef's
// indexed by a Model name.
//-------------------------------------------------------------------------
using SchedModelDict = std::map<std::string, SchedModelRef>;

//-------------------------------------------------------------------------
// The definition of a SchedReadWrite record defined in tablegen files.
// A SchedReadWrite record in tablegen files contains the following relevant
// information: name,  schedule-model name, latency, and a list of
// functional-unit names where the Read or Write can be executed. Note that
// the same SchedReadWrite name can appear for multiple schedule models, in
// which case we need to keep track of the functional units and corresponding
// latency for each schedule model.
//-------------------------------------------------------------------------
class SchedReadWrite {
public:
  SchedReadWrite(const std::string &name, const std::string &sched_model,
                 const std::vector<std::string> &func_units,
                 const std::vector<std::string> &res_cycles,
                 const std::vector<std::string> &start_cycles,
                 const std::string &latency, const std::string &micro_ops,
                 bool is_write, bool is_begin_group, bool is_end_group,
                 bool is_single_issue, bool retire_ooo)
      : name_(name), is_write_(is_write) {
    AddSchedModel(sched_model, func_units, res_cycles, start_cycles, latency,
                  micro_ops, is_write, is_begin_group, is_end_group,
                  is_single_issue, retire_ooo);
  }

  void AddSchedModel(const std::string &sched_model,
                     const std::vector<std::string> &func_units,
                     const std::vector<std::string> &res_cycles,
                     const std::vector<std::string> &start_cycles,
                     const std::string &latency, const std::string &micro_ops,
                     bool is_write, bool is_begin_group, bool is_end_group,
                     bool is_single_issue, bool retire_ooo);

  // Accessor methods.
  const std::string &name() const { return name_; }
  SchedModelDict &sched_model_info() { return sched_model_info_; }
  bool is_write() const { return is_write_; }
  bool is_read() const { return !is_write_; }

  // Some processors define resources with an empty model name.  These are
  // essentially wildcards to be used for any processor not explicitly listed.
  SchedModelRef *HasModel(const std::string &model) {
    if (sched_model_info_.count(model))
      return &(sched_model_info_[model]);
    if (sched_model_info_.count(""))
      return &(sched_model_info_[""]);
    return nullptr;
  }

  // Print Utilities.
  std::string ToString(MachineDescription *md, const std::string &model,
                       bool all_info) const;
  void Dump(MachineDescription *md, bool all_info) {
    std::cout << ToString(md, "", all_info);
  }

private:
  std::string name_;
  SchedModelDict sched_model_info_;
  bool is_write_;
};

//-------------------------------------------------------------------------
// For each SchedVariant tablegen record, the class is used to store the
// predicate (which is some C code) and a list of SchedReadWrite.
//-------------------------------------------------------------------------
struct PredicatedVariants {
  PredicatedVariants(const std::string &fn, const std::vector<std::string> &rwu)
      : code(fn), rw_units(rwu) {}
  std::string code;
  std::vector<std::string> rw_units;
};

//-------------------------------------------------------------------------
// The definition of a SchedVariant record defined in tablegen files.
//-------------------------------------------------------------------------
class SchedVariant {
public:
  SchedVariant(const std::string &name, const std::string &sched_model,
               const std::vector<std::string> &variants, bool is_write,
               bool is_variadic)
      : name_(name), sched_model_(sched_model), variants_(variants),
        is_write_(is_write), is_variadic_(is_variadic) {}

  // Accessor Functions.
  const std::vector<std::string> &variants() const { return variants_; }
  const std::string &sched_model() const { return sched_model_; }
  const std::string &name() const { return name_; }
  bool is_write() const { return is_write_; }
  bool is_read() const { return !is_write_; }
  bool is_variadic() const { return is_variadic_; }

  // Print Utilities.
  std::string ToString(MachineDescription *md, std::string prefix) const;
  void Dump(MachineDescription *md) { std::cout << ToString(md, ""); }

private:
  std::string name_;
  std::string sched_model_;
  std::vector<std::string> variants_;
  bool is_write_ = false;
  bool is_variadic_ = false;
};

//-------------------------------------------------------------------------
// The definition of a SchedVar record defined in tablegen files.
//-------------------------------------------------------------------------
class SchedVar {
public:
  SchedVar(const std::string &name, const std::string &predicate,
           const std::vector<std::string> &selected)
      : name_(name), predicate_(predicate), selected_(selected) {}

  // Accessor Functions.
  const std::vector<std::string> &selected() const { return selected_; }
  const std::string &predicate() const { return predicate_; }
  const std::string &name() const { return name_; }

  // Print Utilities.
  std::string ToString(MachineDescription *md, std::string prefix) const;
  void Dump(MachineDescription *md) { std::cout << ToString(md, ""); }

private:
  std::string name_;
  std::string predicate_;
  std::vector<std::string> selected_;
};

//----------------------------------------------------------------------------
// Names that correspond to LLVM predicate operators.
//----------------------------------------------------------------------------
constexpr auto kCheckAny = "CheckAny";
constexpr auto kCheckAll = "CheckAll";
constexpr auto kCheckNot = "CheckNot";
constexpr auto kCheckOpcode = "CheckOpcode";
constexpr auto kCheckIsRegOperand = "CheckIsRegOperand";
constexpr auto kCheckRegOperand = "CheckRegOperand";
constexpr auto kCheckInvalidRegOperand = "CheckInvalidRegOperand";
constexpr auto kCheckSameRegOperand = "CheckSameRegOperand";
constexpr auto kCheckIsImmOperand = "CheckIsImmOperand";
constexpr auto kCheckImmOperand = "CheckImmOperand";
constexpr auto kCheckZeroOperand = "CheckZeroOperand";
constexpr auto kCheckFunctionPredicate = "CheckFunctionPredicate";
constexpr auto kCheckFunctionPredicateWithTII = "CheckFunctionPredicateWithTII";
constexpr auto kCheckNumOperands = "CheckNumOperands";
constexpr auto kOpcodeSwitchStmt = "OpcodeSwitchStatement";
constexpr auto kOpcodeSwitchCase = "OpcodeSwitchCase";
constexpr auto kReturnStatement = "ReturnStatement";
constexpr auto kName = "Name";
constexpr auto kNumber = "Number";
constexpr auto kString = "String";
constexpr auto kCode = "Code";
constexpr auto kTrue = "TruePred";
constexpr auto kFalse = "FalsePred";
constexpr auto kEmpty = "Empty";

//-------------------------------------------------------------------------
// The definition of a SchedPredicate record defined in tablegen files.
//-------------------------------------------------------------------------
class SchedPredicate {
public:
  SchedPredicate(const std::string &name, const std::string &sched_model,
                 const std::string &predicate)
      : name_(name), sched_model_(sched_model), predicate_(predicate) {}

  // Accessor Functions.
  const std::string &predicate() const { return predicate_; }
  const std::string &sched_model() const { return sched_model_; }
  const std::string &name() const { return name_; }

  // Print Utilities.
  std::string ToString(MachineDescription *md) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md); }

private:
  std::string name_;
  std::string sched_model_;
  std::string predicate_;
};

//-------------------------------------------------------------------------
// The definition of a MCSchedPredicate record defined in tablegen files.
//-------------------------------------------------------------------------
class MCSchedPredicate {
public:
  MCSchedPredicate(const std::string &name, const std::string &sched_model,
                   const std::string &inst_predicate)
      : name_(name), sched_model_(sched_model),
        inst_predicate_(inst_predicate) {}

  // Accessor Functions.
  const std::string &inst_predicate() const { return inst_predicate_; }
  const std::string &sched_model() const { return sched_model_; }
  const std::string &name() const { return name_; }
  bool is_referenced() const { return is_referenced_; }
  void set_is_referenced() { is_referenced_ = true; }

  // Print Utilities.
  std::string ToString(MachineDescription *md) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md); }

private:
  std::string name_;
  std::string sched_model_;
  std::string inst_predicate_;
  bool is_referenced_ = false; // was this referenced by name
};

//-------------------------------------------------------------------------
// The definition of a MCInstPredicate record defined in tablegen files.
//-------------------------------------------------------------------------
class MCInstPredicate {
public:
  MCInstPredicate(const std::string &name, const std::string &function_name,
                  const std::string function_mapper, const std::string opindex,
                  const std::string immval,
                  const std::vector<std::string> &predicates,
                  const std::vector<std::string> &valid_opcodes,
                  const std::string &register_name,
                  const std::vector<std::string> &attributes,
                  const std::string &statement)
      : name_(name), function_name_(function_name),
        function_mapper_(function_mapper), opindex_(opindex), immval_(immval),
        predicates_(predicates), valid_opcodes_(valid_opcodes),
        register_name_(register_name), attributes_(attributes),
        statement_(statement) {}

  // Accessor Functions.
  const std::string &function_name() const { return function_name_; }
  const std::string &name() const { return name_; }

  // Print Utilities.
  std::string ToString(MachineDescription *md, int indent) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md, 0); }
  std::string Format(MachineDescription *md, int indent) const;
  bool is_referenced() const { return is_referenced_; }
  void set_is_referenced() { is_referenced_ = true; }

private:
  std::string name_;
  std::string function_name_;
  std::string function_mapper_;
  std::string opindex_;
  std::string immval_;
  std::vector<std::string> predicates_;
  std::vector<std::string> valid_opcodes_;
  std::string register_name_;
  std::vector<std::string> attributes_;
  std::string statement_;
  bool is_referenced_ = false; // was this referenced by name
};

//-------------------------------------------------------------------------
// The definition of a MCStatement record defined in tablegen files.
//-------------------------------------------------------------------------
class MCStatement {
public:
  MCStatement(const std::string &name, const std::string attribute,
              const std::string &predicate,
              const std::vector<std::string> &cases,
              const std::string &default_case)
      : name_(name), attribute_(attribute), cases_(cases),
        predicate_(predicate), default_(default_case) {}

  bool is_referenced() const { return is_referenced_; }
  void set_is_referenced() { is_referenced_ = true; }

  // Print Utilities.
  std::string ToString(MachineDescription *md, int indent) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md, 0); }
  std::string Format(MachineDescription *md, int indent) const;

private:
  std::string name_;
  std::string attribute_;
  std::vector<std::string> cases_;
  std::string predicate_;
  std::string default_;
  bool is_referenced_ = false; // was this referenced by name
};

//-------------------------------------------------------------------------
// The definition of a MCOpcodeSwitchCase record defined in tablegen files.
//-------------------------------------------------------------------------
class MCOpcodeSwitchCase {
public:
  MCOpcodeSwitchCase(const std::string &name,
                     const std::vector<std::string> &cases,
                     const std::string &case_stmt)
      : name_(name), cases_(cases), case_stmt_(case_stmt) {}

  bool is_referenced() const { return is_referenced_; }
  void set_is_referenced() { is_referenced_ = true; }

  // Print Utilities.
  std::string ToString(MachineDescription *md, int indent) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md, 0); }
  std::string Format(MachineDescription *md, int indent) const;

private:
  std::string name_;
  std::vector<std::string> cases_;
  std::string case_stmt_;
  bool is_referenced_ = false; // was this referenced by name
};

//-------------------------------------------------------------------------
// The definition of a WriteSequence record defined in tablegen files.
//-------------------------------------------------------------------------
class WriteSequence {
public:
  WriteSequence(const std::string &name, const std::string &sched_model,
                const std::vector<std::string> &writes, int repeat)
      : name_(name), sched_model_(sched_model), writes_(writes),
        repeat_(repeat) {}

  // Accessor Functions.
  const std::vector<std::string> &writes() const { return writes_; }
  int repeat() const { return repeat_; }
  const std::string &name() const { return name_; }
  std::string &sched_model() { return sched_model_; }

  // Print Utilities.
  std::string ToString(MachineDescription *md) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md); }

private:
  std::string name_;
  std::string sched_model_;
  std::vector<std::string> writes_;
  int repeat_;
};

//-------------------------------------------------------------------------
// The definition of a SchedAlias record defined in tablegen files.
//-------------------------------------------------------------------------
class SchedAlias {
public:
  SchedAlias(const std::string &name, const std::string &sched_model,
             const std::string &match_rw, const std::string &alias_rw)
      : name_(name), sched_model_(sched_model), match_rw_(match_rw),
        alias_rw_(alias_rw) {}

  // Accessor Functions.
  const std::string &sched_model() const { return sched_model_; }
  const std::string &match_rw() const { return match_rw_; }
  const std::string &alias_rw() const { return alias_rw_; }
  const std::string &name() const { return name_; }

  // Print Utilities.
  std::string ToString(MachineDescription *md) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md); }

private:
  std::string name_;
  std::string sched_model_;
  std::string match_rw_;
  std::string alias_rw_;
};

//-------------------------------------------------------------------------
// Forwarding information for an InstRW record. Each entry in a
// ForwardUnits set is a tuple of FUs and a latency, and an optional predicate.
// An entry in a ForwardSet is an operand id, a latency, a definition
// functional unit, a use functional unit, and an optional predicate.
// For each potentially forwarded operand, we have a graph of FU pairs
// that support forwarding.
//-------------------------------------------------------------------------
using ForwardUnits = std::set<std::tuple<std::string, int, std::string>>;
using ForwardItem = std::tuple<int, int, std::string, std::string, std::string>;
using ForwardSet = std::set<ForwardItem>;

// Describe the overall forwarding graph for a processor. Its implementation
// is an array indexed by SchedModel, DefFU, and UseFU, and the integer
// represents the latency between the two functional units (for the specified
// model).
using ForwardGraph =
    std::map<std::tuple<std::string, std::string, std::string>, std::set<int>>;

class ForwardingNetwork {
public:
  ForwardingNetwork() {}
  // Add an edge to the graph indicating the forwarding latency between the
  // two units. We need to find the minimum latency between the two units,
  // while ignoring negative latencies.
  void AddEdge(const std::string &model, const std::string &def_unit,
               const std::string &use_unit, int latency) {
    auto tup = std::make_tuple(model, def_unit, use_unit);
    graph_[tup].insert(latency);
  }
  std::set<int> GetEdge(const std::string &model, const std::string &def_unit,
                        const std::string &use_unit) {
    auto tup = std::make_tuple(model, def_unit, use_unit);
    if (!graph_.count(tup))
      return {};
    return graph_[tup];
  }
  ForwardGraph &graph() { return graph_; }

private:
  ForwardGraph graph_;
};

//-------------------------------------------------------------------------
// The definition of a InstRW record defined in tablegen files.
//-------------------------------------------------------------------------
class InstRW {
public:
  InstRW(const std::string &name, const std::vector<std::string> &rw_units,
         const std::string &sched_model,
         const std::vector<std::string> &instregex,
         const std::vector<std::string> &instrs)
      : name_(name), rw_units_(rw_units), sched_model_(sched_model),
        instregex_(instregex), instrs_(instrs) {}

  // Accessor Functions.
  const std::vector<std::string> &rw_units() const { return rw_units_; }
  const std::string &name() const { return name_; }
  const std::string &sched_model() const { return sched_model_; }
  const std::vector<std::string> &instregex() const { return instregex_; }
  std::vector<std::string> &instrs() { return instrs_; }
  ForwardSet &forwarding_info() { return forwarding_info_; }

  // Note which functional units this InstRW applies to.
  std::set<std::string> &func_units() { return func_units_; }
  void add_func_unit(std::string name) { func_units_.insert(name); }

  // Print Utilities.
  std::string ToString(const MachineDescription *md, bool full) const;
  std::string FwdString(const MachineDescription *md) const;
  void Dump(MachineDescription *md, bool full) const {
    std::cout << ToString(md, full);
  }
  void DumpFwd(MachineDescription *md) const { std::cout << FwdString(md); }

private:
  std::string name_;
  std::vector<std::string> rw_units_;
  std::string sched_model_;
  std::set<std::string> func_units_; // Which FUs does it run on
  std::vector<std::string> instregex_;
  std::vector<std::string> instrs_;
  ForwardSet forwarding_info_;
};

//-------------------------------------------------------------------------
// The definition of a ItinRW records defined in tablegen files.  This is
// apparently only used for ARM.
//-------------------------------------------------------------------------
class ItinRW {
public:
  ItinRW(std::string name, const std::vector<std::string> &itin_classes,
         const std::vector<std::string> &rw_units, std::string sched_model)
      : name_(name), itin_classes_(itin_classes), rw_units_(rw_units),
        sched_model_(sched_model) {}

  const std::string &name() const { return name_; }
  const std::vector<std::string> &itin_classes() const { return itin_classes_; }
  const std::vector<std::string> &rw_units() const { return rw_units_; }
  const std::string &sched_model() const { return sched_model_; }

  std::string ToString(const MachineDescription *md) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md); }

private:
  std::string name_;
  std::vector<std::string> itin_classes_;
  std::vector<std::string> rw_units_;
  std::string sched_model_;
};

//-------------------------------------------------------------------------
// The definition of a ProcessorItineraries record defined in tablegen files.
//-------------------------------------------------------------------------
class ProcessorItineraries {
public:
  ProcessorItineraries(const std::string name, const StringSet &resources,
                       const std::vector<std::string> &bypass,
                       const StringSet &iid)
      : name_(name), resources_(resources), bypass_(bypass), iid_(iid) {}

  // Accessor Functions.
  const StringSet &resources() const { return resources_; }
  const std::vector<std::string> &bypass() const { return bypass_; }
  const StringSet &iid() const { return iid_; }

  // Print Utilities.
  std::string ToString(const MachineDescription *md) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md); }

private:
  std::string name_;
  StringSet resources_;
  std::vector<std::string> bypass_;
  StringSet iid_;
};

//------------------------------------------------------------------------
// Object representing a single resource expression node.
// Note: we depend on the order of these operators.
//------------------------------------------------------------------------
enum class ResOp { kNop, kRes, kAnd, kOr };

class ResExpr {
public:
  ResExpr() : op_(ResOp::kNop) {}
  explicit ResExpr(ResOp op) : op_(op) {}
  explicit ResExpr(std::string resource)
      : op_(ResOp::kRes), resource_(resource) {}
  ResExpr(ResOp opcode, std::vector<ResExpr> &operands)
      : op_(opcode), opnds_(operands) {
    std::sort(opnds_.begin(), opnds_.end());
  }
  ResExpr(ResOp opcode, std::vector<std::string> &units) : op_(opcode) {
    for (auto &unit : units)
      opnds_.emplace_back(unit);
    std::sort(opnds_.begin(), opnds_.end());
  }
  ResExpr(ResOp opcode, ResExpr &lhs, ResExpr &rhs) : op_(opcode) {
    if (lhs.op() == opcode)
      opnds_ = lhs.opnds_;
    else
      opnds_.push_back(lhs);
    if (rhs.op() == opcode)
      opnds_.insert(opnds_.end(), rhs.opnds_.begin(), rhs.opnds_.end());
    else
      opnds_.push_back(rhs);
    std::sort(opnds_.begin(), opnds_.end());
  }

  ResOp op() const { return op_; }
  std::vector<ResExpr> &opnds() { return opnds_; }
  std::string resource() const { return resource_; }
  bool isRes() const { return op_ == ResOp::kRes; }
  bool isOr() const { return op_ == ResOp::kOr; }
  bool isAnd() const { return op_ == ResOp::kAnd; }
  bool isNop() const { return op_ == ResOp::kNop; }

  std::string ToString(const std::vector<ResExpr> &expr,
                       std::string sep) const {
    std::string out;
    for (auto &item : expr)
      out += (out.empty() ? "" : sep) + item.ToString();
    return out;
  }

  bool operator==(const ResExpr &rhs) const {
    if (op_ != rhs.op())
      return false;
    if (op_ == ResOp::kRes)
      return resource_ == rhs.resource();
    if (opnds_.size() != rhs.opnds_.size())
      return false;
    for (unsigned idx = 0; idx < opnds_.size(); idx++)
      if (opnds_[idx] != rhs.opnds_[idx])
        return false;
    return true;
  }

  bool operator!=(const ResExpr &rhs) const { return !(*this == rhs); }

  bool operator<(const ResExpr &rhs) const {
    if (op_ < rhs.op())
      return true;
    if (op_ == ResOp::kRes)
      return resource_ < rhs.resource();
    for (unsigned idx = 0; idx < rhs.opnds_.size(); idx++) {
      if (idx == opnds_.size())
        return true;
      if (opnds_[idx] < rhs.opnds_[idx])
        return true;
      if (opnds_[idx] > rhs.opnds_[idx])
        return false;
    }
    if (opnds_.size() > rhs.opnds_.size())
      return false;
    return true;
  }
  bool operator>(const ResExpr &rhs) const { return rhs < *this; }

  std::string ToString() const {
    if (isRes())
      return resource_;
    if (isOr())
      return ToString(opnds_, " | ");
    if (isAnd())
      return ToString(opnds_, " & ");
    return "nop";
  }

private:
  ResOp op_;                   // what kind of op is this
  std::string resource_;       // name of resource
  std::vector<ResExpr> opnds_; // operands of this operation
};

//------------------------------------------------------------------------
// Represent a single functional unit template argument.
//------------------------------------------------------------------------
class ResArg {
public:
  ResArg(ResExpr expr, int phase, int cycles)
      : exprs_(1, expr), phase_(phase), cycles_(cycles) {}
  int cycles() const { return cycles_; }
  int phase() const { return phase_; }
  std::vector<ResExpr> &exprs() { return exprs_; }
  void set_expr(ResExpr &expr) { exprs_[0] = expr; }
  std::string ToString() const {
    std::string out = formatv(" phase={0} cycles={1} :", phase_, cycles_);
    for (auto &expr : exprs_)
      out += expr.ToString() + " || ";
    out = out.substr(0, out.size() - 4);
    return out;
  }

private:
  std::vector<ResExpr> exprs_; // set of alternative resource specifications
  int phase_, cycles_;         // what phase is it used in, for now long
};

//-------------------------------------------------------------------------
// The definition of an InstrItinData record defined in tablegen files.
//-------------------------------------------------------------------------
class InstrItineraryData {
public:
  InstrItineraryData(const std::string name,
                     const std::string &instr_itinerary_class,
                     int num_micro_ops, const std::vector<std::string> &stages,
                     const std::vector<std::string> &operand_cycles,
                     const std::vector<std::string> &bypasses)
      : name_(name), instr_itinerary_class_(instr_itinerary_class),
        num_micro_ops_(num_micro_ops), stages_(stages),
        operand_cycles_(operand_cycles), bypasses_(bypasses) {}

  // Accessor Functions.
  const std::string &name() const { return name_; }
  const std::string &instr_itinerary_class() const {
    return instr_itinerary_class_;
  }
  std::vector<std::string> &stages() { return stages_; }
  const std::vector<std::string> &bypasses() const { return bypasses_; }
  const std::vector<std::string> &operand_cycles() const {
    return operand_cycles_;
  }
  int num_micro_ops() const { return num_micro_ops_; }
  void add_model(std::string model) { models_.insert(model); }
  const std::set<std::string> &models() { return models_; }

  std::vector<ResExpr> &issue_stages() { return issue_stages_; }
  void set_issue_stage(ResExpr expr) { issue_stages_.push_back(expr); }
  std::vector<ResArg> &resource_args() { return resource_args_; }
  void set_resource_args(std::vector<ResArg> &args) { resource_args_ = args; }

  // Print Utilities.
  std::string ToString(MachineDescription *md) const;
  void Dump(MachineDescription *md) { std::cout << ToString(md); }

private:
  std::string name_;
  std::string instr_itinerary_class_;
  int num_micro_ops_;
  std::vector<std::string> stages_;
  std::vector<std::string> operand_cycles_;
  std::vector<std::string> bypasses_;
  std::set<std::string> models_; // Models that use this itindata

  std::vector<ResExpr> issue_stages_; // alternative issue slot expressions
  std::vector<ResArg> resource_args_; // set of template arguments
};

//-------------------------------------------------------------------------
// The definition of an InstrStage record defined in tablegen files.
//-------------------------------------------------------------------------
class InstrStage {
public:
  InstrStage(const std::string name, int cycles, const StringVec &func_units,
             int timeinc)
      : name_(name), cycles_(cycles), func_units_(func_units),
        timeinc_(timeinc) {}

  // Accessor Functions.
  const std::string &name() const { return name_; }
  StringVec &func_units() { return func_units_; }
  int cycles() const { return cycles_; }
  int timeinc() const { return timeinc_; }
  int cycle_increment() const { return (timeinc_ == -1) ? cycles_ : timeinc_; }
  ResExpr &resource_expr() { return resource_expr_; }
  void set_resource_expr(ResExpr expr) { resource_expr_ = expr; }

  void clear_issue_flag() { is_issue_stage_ = false; }
  void set_issue_flag() { is_issue_stage_ = true; }
  bool is_issue_stage() const { return is_issue_stage_; }

  // Print Utilities.
  std::string ToString(const MachineDescription *md) const;
  void Dump(MachineDescription *md) const { std::cout << ToString(md); }

private:
  std::string name_;            // Tablegen name of stage
  int cycles_;                  // number of cycles resource is used
  StringVec func_units_;        // Set of alternative resources used
  int timeinc_;                 // time increment to next stage (-1, 0, or +)
  ResExpr resource_expr_;       // expression represented by this stage
  bool is_issue_stage_ = false; // true if only accessing slot resources
};

//-------------------------------------------------------------------------
// The definition of a functional unit defined for some set of itineraries.
//-------------------------------------------------------------------------
class ItineraryFuncUnit {
public:
  ItineraryFuncUnit(std::string &name, InstrItineraryData *itinerary)
      : name_(name), itinerary_(itinerary) {}

  std::string name() const { return name_; }
  std::vector<std::string> &stages() { return itinerary_->stages(); }
  std::set<std::pair<int, std::string>> &subunits() { return subunits_; }
  InstrItineraryData *itinerary() const { return itinerary_; }

  void add_subunit(int subunit, InstrItineraryData *itin) {
    subunits_.insert({subunit, itin->name()});
  }
  int FormatInstance(std::string *out, int fu_id, MachineDescription *md);
  std::string FormatTemplate(MachineDescription *md);

private:
  std::string name_;
  InstrItineraryData *itinerary_; // pointer to itinerary data
  std::set<std::pair<int, std::string>> subunits_;
};

class FlatOperand {
 public:
  FlatOperand(const std::string type, const std::string name,
              std::string subname, bool is_register):
    type_(type), name_(name), is_register_(is_register) {
      if (subname.empty()) format_ = name;
      else format_ = formatv("{0}.{1}", name, subname);
    }
  FlatOperand(const std::string type, const std::string name, int index,
              bool is_register):
    type_(type), name_(name), is_register_(is_register) {
      format_ = formatv("{0}.{1}", name, index);
    }

  std::string name() const { return name_; }
  std::string type() const { return type_; }
  std::string format() const { return format_; }
  bool is_register() const { return is_register_; }

 private:
  std::string type_;             // Defined type of the operand
  std::string name_;             // Defined name of operand, or index
  std::string format_;           // formatted name of operand (in use/def)
  bool is_register_;             // True if this is a register or a class
};

//-------------------------------------------------------------------------
// Instruction describes an instruction definition in the input file.
// An instruction def record is a collection of field definitions in an
// arbitrary order. We scan the entire record looking for particular entries.
//
//    def <name> { // <attribute> <attribute> ...
//      field <attribute-name> = ...
//      int <attribute-name> = ...
//      string <attribute-name> = "..."
//      dag <attribute-name> = (ins ...)
//      dag <attribute-name> = (outs ...)
//      list<string> SubUnits = ["subunit"];
//      list ...
//      bit <name> = ...
//      ...
//    }
//-------------------------------------------------------------------------
class Instruction {
public:
  Instruction(const std::string &name, std::vector<std::string> outs,
              std::vector<std::string> ins, std::string assembly,
              std::string pattern, std::string parent,
              std::vector<std::string> *subunit,
              std::vector<std::string> rw_units, std::vector<std::string> uses,
              std::vector<std::string> defs, std::string base_instr,
              std::string itinerary, bool may_load, bool pseudo, bool generic)
      : name_(name), outs_(outs), ins_(ins), assembly_(assembly),
        pattern_(pattern), subunit_(subunit), rw_units_(rw_units),
        parent_(parent), base_instr_(base_instr), implicit_uses_(uses),
        implicit_defs_(defs), itinerary_(itinerary), may_load_(may_load),
        pseudo_(pseudo), generic_(generic) {
    if (itinerary_ == "NoItinerary")
      itinerary_ = "";
  }

  std::string ToCsvString() const;
  void WriteCsv() { std::cout << ToCsvString(); }
  std::string Format(bool full_definition) const;

  std::string ToString(MachineDescription *md);
  void Dump(MachineDescription *md) { std::cout << ToString(md); }

  bool HasSubunits() const { return subunit_ != nullptr && !subunit_->empty(); }
  bool HasChildren() const { return !children_.empty(); }

  static std::string opnd_type(std::string opnd) {
    return opnd.substr(0, opnd.find_first_of(' '));
  }
  static std::string opnd_name(std::string opnd) {
    if (opnd.find_last_of(' ') == std::string::npos) return "";
    return opnd.substr(opnd.find_last_of(' ') + 1);
  }
  std::vector<std::string> &ins() { return ins_; }
  std::vector<std::string> &outs() { return outs_; }
  std::vector<FlatOperand> &flattened_ins() { return flattened_ins_; }
  std::vector<FlatOperand> &flattened_outs() { return flattened_outs_; }
  std::vector<std::string> &implicit_uses() { return implicit_uses_; }
  std::vector<std::string> &implicit_defs() { return implicit_defs_; }
  bool has_variable_ops() const {
    return !ins_.empty() && ins_.back() == "...";
  }

  std::string opnd_name(unsigned index) const {
    if (index < flattened_outs_.size())
      return flattened_outs_[index].format();
    index -= flattened_outs_.size();
    if (index < flattened_ins_.size())
      return flattened_ins_[index].format();
    return "";
  }

  // Return the symbolic operand name for the nth output operand.
  std::string out(unsigned index) {
#if 0
    unsigned opnd_idx = 0;
    for (auto &opnd : flattened_outs())
      if (opnd.is_register()) {
        if (opnd_idx == index) return opnd.format();
        opnd_idx++;
      }
    if (index - opnd_idx < implicit_defs_.size())
      return implicit_defs_[index - opnd_idx];
#else
    if (index < flattened_outs_.size())
      return flattened_outs_[index].format();
    if (index - flattened_outs_.size() < implicit_defs_.size())
      return implicit_defs_[index - flattened_outs_.size()];
#endif
    return "";
  }
  // Return the symbolic operand name for the nth input operand.
  std::string in(unsigned index) {
    unsigned opnd_idx = 0;
    for (auto &opnd : flattened_ins())
      if (opnd.is_register()) {
        if (opnd_idx == index) return opnd.format();
        opnd_idx++;
      }
    if (index - opnd_idx < implicit_uses_.size())
      return implicit_uses_[index - opnd_idx];
#if 0
    if (index < flattened_ins_.size())
      return flattened_ins_[index].format();
    if (index - flattened_ins_.size() < implicit_uses_.size())
      return implicit_uses_[index - flattened_ins_.size()];
#endif
    return "";
  }
  // Return the flattened operand index for the nth output operand.
  int out_index(unsigned index) const {
    if (index < flattened_outs_.size())
      return index;
    if (index - flattened_outs_.size() < implicit_defs_.size())
      return flattened_ins_.size() - has_variable_ops() + index;
    return -1;
  }
  // Return the flattened operand index for the nth input operand.
  int in_index(unsigned index) const {
    unsigned ins_size = flattened_ins_.size() - has_variable_ops();
    if (index < ins_size)
      return index + flattened_outs_.size();
    if (index - ins_size < implicit_uses_.size())
      return flattened_outs_.size() + implicit_defs_.size() + index;
    return -1;
  }

  std::string name() const { return name_; }
  std::vector<Instruction *> children() { return children_; }

  // When adding InstRWs to instructions, only the first item associated with
  // a particular SchedModel is valid.
  void add_inst_rw(InstRW *item) {
    if (!has_inst_rws(item->sched_model())) {
      inst_rws_[item->sched_model()] = item;
    }
  }
  bool has_inst_rws(const std::string &model) const {
    auto model_name = SchedMachineModel::fix_output_name(model);
    for (auto &[name, item] : inst_rws_)
      if (model_name == SchedMachineModel::fix_output_name(name))
        return true;
    return false;
  }

  void add_subunit(int subunit_id) { subunits_.insert(subunit_id); }
  const std::set<int> &subunits() const { return subunits_; }

  const std::vector<std::string> &rw_units() { return rw_units_; }
  std::map<std::string, InstRW *> &inst_rws() { return inst_rws_; }
  std::string itinerary() const { return itinerary_; }
  bool may_load() const { return may_load_; }

public:
  std::string name_;                         // name of instruction
  std::vector<std::string> outs_;            // output operands
  std::vector<std::string> ins_;             // input operands
  std::vector<FlatOperand> flattened_outs_;  // flattened output operands
  std::vector<FlatOperand> flattened_ins_;   // flattened input operands
  std::string assembly_;                     // assembly syntax
  std::string pattern_;                      // dag pattern that this matches
  std::vector<std::string> *subunit_;        // subunit identifier(s)
  std::vector<std::string> rw_units_;        // rw_units specified in instr
  std::map<std::string, InstRW *> inst_rws_; // InstRWs for this model/instr

  std::string parent_; // this is a copy of another instruction
  std::vector<Instruction *> children_;
  std::string base_instr_;
  std::vector<std::string> implicit_uses_, implicit_defs_;
  std::string itinerary_;
  bool may_load_; // true if instruction may do a load
  bool pseudo_;   // true if this instruction is marked as a pseudo instruction
  bool generic_;  // true if this is a "generic" instruction

  // derived information
  std::set<int> subunits_; // subunits for this instruction.
};

//-------------------------------------------------------------------------
// An Operand captures an Operand definition record.
//    def <name> { // DAGOperand ...
//      ValueType Type = ...;
//      dag MIOperandInfo = (ops ...";
//      ...
//    }
//-------------------------------------------------------------------------
class Operand {
public:
  Operand(const std::string &name, std::vector<std::string> ops,
          const std::string &type)
      : name_(name), ops_(ops), type_(type) {}

  std::string Format() const;

  bool referenced() const { return referenced_; }
  void set_referenced() { referenced_ = true; }
  std::vector<std::string> &ops() { return ops_; }

private:
  std::string name_;             // operand name
  std::vector<std::string> ops_; // set of components
  std::string type_;             // "type" of operand (ala I32)
  bool referenced_ = false;      // true if any instructions use it
};

//-------------------------------------------------------------------------
// Definitions for registers and register classes.
//-------------------------------------------------------------------------
using RegDefinitions = std::set<RegDefinition>;
using RegSet = std::vector<RegDefinition>;

std::string GetRegisterPrefix(const std::string &reg, int &regid);

// Definition of a single register.
// We extract the prefix and index so that we can do reasonable sorting.
class RegDefinition {
public:
  explicit RegDefinition(std::string name) : name_(name) {
    prefix_ = GetRegisterPrefix(name, index_);
    last_index_ = index_;
  }
  RegDefinition() {}

  std::string name() const { return name_; }
  std::string prefix() const { return prefix_; }
  int index() const { return index_; }
  int last_index() const { return last_index_; }
  void set_last_index(int index) { last_index_ = index; }

  std::string Format() const;
  bool operator<(const RegDefinition &rhs) const {
    return prefix() < rhs.prefix() ||
           (prefix() == rhs.prefix() && index() < rhs.index());
  }
  bool operator>(const RegDefinition &rhs) const { return rhs < *this; }
  bool operator==(const RegDefinition &rhs) const {
    return rhs.name_ == this->name_;
  }

private:
  std::string name_;
  std::string prefix_;
  int index_;
  int last_index_;
};

//-------------------------------------------------------------------------
// The definition of a register class defined in tablegen files.
//-------------------------------------------------------------------------
class RegisterClass {
public:
  RegisterClass(const std::string &name, const char *members)
      : name_(name), member_list_(nullptr) {
    if (members) {
      member_list_ = new char[strlen(members) + 1];
      memcpy(member_list_, members, strlen(members) + 1);
    }
  }

  std::string Format();
  void dump() { std::cout << Format(); }

  bool referenced() const { return referenced_; }
  void set_referenced() { referenced_ = true; }
  const RegSet &registers() const { return registers_; }
  void set_registers(RegSet regs) { registers_ = regs; }
  char *member_list() const { return member_list_; }
  bool parsed() const { return parsed_; }
  void set_parsed() { parsed_ = true; }

private:
  std::string name_;        // name of register class
  RegSet registers_;        // Order can matter for these (because of trunc)
  char *member_list_;       // Expression that defines class members.
  bool referenced_ = false; // true if any instructions/operands use it
  bool parsed_ = false;     // true if we've parsed the member expression
};

//--------------------------------------------------------------------------
// Container for all the information we scrape from tablegen files.
//--------------------------------------------------------------------------
class MachineDescription {
public:
  MachineDescription(std::string family, std::string output_name,
                     bool ignore_subunits, bool no_warnings,
                     bool gen_subunit_bases, bool gen_forwarding_info,
                     bool gen_operand_indexes)
      : family_(family), output_name_(output_name),
        ignore_subunits_(ignore_subunits), no_warnings_(no_warnings),
        gen_subunit_bases_(gen_subunit_bases),
        gen_forwarding_info_(gen_forwarding_info),
        gen_operand_indexes_(gen_operand_indexes) {}

  std::string family() const { return family_; }
  bool ignore_subunits() const { return ignore_subunits_; }
  bool no_warnings() const { return no_warnings_; }
  bool gen_subunit_bases() const { return gen_subunit_bases_; }
  bool gen_forwarding_info() const { return gen_forwarding_info_; }
  bool gen_operand_indexes() const { return gen_operand_indexes_; }

  void OpenOutputFiles(std::string &input_file, bool gen_arch_spec,
                       std::string &output_dir);

  // Functions that write MDL records to the output file.
  void WriteMDL(std::string &input_file, bool gen_arch_spec,
                std::string &output_dir);
  void WriteFileHeaders();
  void WriteArchHeader(const char *section, int entries);
  void WriteInstHeader(const char *section);
  void WritePipelinePhase();
  void WriteProcessorModel();
  void WriteFUInfo();
  void WriteSubunits();
  void WriteLatencies();
  void WritePredicates();
  void WriteRegisterSets();
  void WriteRegisterClasses();
  void WriteOperandsMdl();
  void WriteInstructionsMdl();

  void WriteInstructionsCsv();

  // Functions for dumping TG records for debug
  void DumpTableGenInfo();
  void DumpForwardingGraph();
  void DumpInstrForwardingInfo();
  void DumpStats();
  void DumpSchedReadWrite(bool summary);
  std::string DumpSchedRW(const std::string &name, const std::string &model,
                          std::string prefix);
  void DumpInstRW();
  void DumpItinRW();
  void DumpForwardingInfo();
  void DumpSchedVariant();
  void DumpSchedVar();
  void DumpPredicates();
  void DumpSchedAlias();

  void DumpWriteSequence();
  void DumpProcessorItineraries();
  void DumpInstrStage();
  void DumpInstrItineraryData();
  void DumpBypasses();
  void DumpInstrItineraryClasses();
  void DumpProcessorModel();
  void DumpSchedMachineModel();
  void DumpFUInfo();
  void DumpLatInfo();
  void DumpInstructions();

  bool FormatRef(std::string &out, const Instruction *inst,
                 const std::string &sched_model, const std::string &unit_name,
                 const std::string &operand, int repeat, int &latency,
                 bool &fu_seen, std::string indent = "   ");
  std::string FormatReferences(Instruction *instr,
                               const std::string &sched_model,
                               const InstRW *inst_rw);

  // Functions for scanning various parts of the file.
  void SkipRecord(std::ifstream &in);
  char *GetLine(std::ifstream &in, std::string &input);

  std::string GetOperandName(const std::string &opnd);
  std::string GetOperandType(const std::string &opnd);

  OperandRefs GetRegisterOperands(Instruction *instr);
  void GetRegisterOperands(std::string prefix, std::string ref_type,
                           int opnd_id, StringVec &opnds, OperandRefs &result);
  OperandRefs GetNonRegisterOperands(Instruction *instr);

  void FindReferencedOperands();
  void FindRegisterSets();
  void PopulateResourceGroup(ProcResource *func);
  void PopulateCPUsWithFUs();
  ProcResource *CreateSuperUnitNames(const std::string &unit_name);

  bool IsReadUnit(const std::string &name, const std::string &model);
  void FindReadUnits(const std::string &name, const std::string &model,
                     ForwardUnits &units, const std::string &predicate);
  void FindWriteUnits(const std::string &name, const std::string &model,
                      ForwardUnits &units, int latency,
                      const std::string &predicate);

  void ExtractForwardingInfo(InstRW *instrw);

  void FlattenOperand(std::string type, std::string subtype, std::string name,
                      std::vector<FlatOperand> &flat_operands);
  std::vector<FlatOperand> FlattenInstrOperands(Instruction *instr,
                       const std::vector<std::string> &operands);
  void ExpandInstRWs();
  void ProcessInstRW();
  void ProcessItinRW();
  void GenerateInstRWs(Instruction *instr,
                       const std::vector<std::string> &rw_units,
                       const std::string &first, const std::string &model);
  void CheckSchedulingInfo();

  bool IsIssueSlot(const std::string &name) const;
  void PreprocessItinStages();
  void PreprocessInstrItineraryData();
  void ProcessItineraries();

  // Processing register member expressions
  RegSet ParseExpr(char *&input);
  RegSet RegClassAdd(char *&input);
  RegSet RegClassSub(char *&input);
  RegSet RegClassAnd(char *&input);
  RegSet RegClassShl(char *&input);
  RegSet RegClassRotl(char *&input);
  RegSet RegClassRotr(char *&input);
  RegSet RegClassTrunc(char *&input);
  RegSet RegClassSequence(char *&input);
  RegSet RegClassDecimate(char *&input);

  RegSet GetMemberList(RegisterClass *reg_class);
  void ScanRegisterClass(std::ifstream &in, const std::string &name);
  void ScanUnknownRegisterClass(std::ifstream &in, const std::string &name);
  void ScanRegister(std::ifstream &in, const std::string &name);

  std::vector<std::string> *ScanInstructSubunit(char *input);
  std::string ScanInstructParent(char *input);
  std::string ScanInstructPattern(char *input);
  std::string ScanInstructAssy(char *input);
  std::string ScanType(char *input);
  std::vector<std::string> ScanInstructOpnds(char *input);
  std::vector<std::string> ScanImplicitDefsUses(char *input);
  void ScanProcessorModel(std::ifstream &in);
  void ScanProcResource(std::ifstream &in, const std::string &name,
                        const char *paren);
  void ScanComboFuncData(std::ifstream &in);
  void ScanSchedMachineModel(std::ifstream &in, const std::string &name);
  void ScanSchedReadWrite(std::ifstream &in, const std::string &def_name,
                          const char *paren, bool is_write);
  void AddSchedReadWrite(const std::string &name,
                         const std::string &sched_model,
                         const std::vector<std::string> &func_units,
                         const std::vector<std::string> &res_cycles,
                         const std::vector<std::string> &start_cycles,
                         const std::string &latency,
                         const std::string &micro_ops, bool is_write,
                         bool is_begin_group, bool is_end_group,
                         bool is_single_issue, bool retire_ooo);
  std::string ScanMember(char *input);
  std::string ScanFixedMember(char *input);
  std::string ScanCode(std::ifstream &in, std::string code);
  StringVec ScanList(char *input);
  StringSet ScanSet(char *input);
  StringVec ScanStrings(char *input);
  void ScanInstrs(char *input, StringVec &instrs, StringVec &regex);

  void ScanOperand(std::ifstream &in, const std::string &name);
  void ScanRegisterOperand(std::ifstream &in, const std::string &name);
  void ScanPatternOperator(std::ifstream &in, const std::string &name);
  void ScanValueType(std::ifstream &in, const std::string &name);
  void ScanPointerLikeRegClass(std::ifstream &in, const std::string &name);
  void ScanInstruction(std::ifstream &in, const std::string &name,
                       const char *paren);
  std::string ScanName(char *input);
  void ScanInstRW(std::ifstream &in, const std::string &name);
  void ScanItinRW(std::ifstream &in, const std::string &name);
  void ScanSchedVariant(std::ifstream &in, const std::string &name,
                        const char *paren);
  void ScanSchedVar(std::ifstream &in, const std::string &name);
  void ScanMCSchedPredicate(std::ifstream &in, const std::string &name);
  void ScanSchedPredicate(std::ifstream &in, const std::string &name);
  void ScanMCInstPredicate(std::ifstream &in, const std::string &name,
                           const char *paren);
  void ScanMCStatement(std::ifstream &in, const std::string &name,
                       const char *paren);
  void ScanMCOpcodeSwitchCase(std::ifstream &in, const std::string &name);
  void ScanSchedAlias(std::ifstream &in, const std::string &name);
  void ScanWriteSequence(std::ifstream &in, const std::string &name);
  void ScanProcessorItineraries(std::ifstream &in, const std::string &name);
  void ScanInstrStage(std::ifstream &in, const std::string &name);
  void ScanInstrItineraryData(std::ifstream &in, const std::string &name);
  void ScanBypasses(std::ifstream &in, const std::string &name);
  void ScanInstrItineraryClasses(std::ifstream &in, const std::string &name);
  void AddImplicitOperands();

  void ScanClass(std::ifstream &in, char *input);
  void ScanDef(std::ifstream &in, char *input);
  void ScanFile(const char *file);

  bool IsPredicateReferenced(const std::string &name);
  bool OutputPredicate(const std::string &name);
  void SetPredicateReferenced(const std::string &name);

  std::string PredName(const std::string &name);
  std::string FormatPred(const std::string &name, int indent);

  std::map<std::string, MCInstPredicate *> mc_inst_predicates() {
    return mc_inst_predicates_;
  }
  std::map<std::string, MCSchedPredicate *> mc_sched_predicates() {
    return mc_sched_predicates_;
  }
  std::map<std::string, MCStatement *> statements() { return statements_; }
  std::map<std::string, MCOpcodeSwitchCase *> switch_cases() {
    return switch_cases_;
  }
  std::map<std::string, SchedVar *> sched_vars() { return sched_vars_; }
  std::map<std::string, SchedReadWrite *> rw_units() { return rw_units_; }
  std::vector<InstRW *> &instrw_info() { return instrw_info_; }
  std::vector<ItinRW *> &itinrw_info() { return itinrw_info_; }

  std::map<std::string, ProcessorModel *> cpus() { return cpus_; }
  std::map<std::string, SchedMachineModel *> sched_models() {
    return sched_models_;
  }
  std::map<std::string, ProcessorItineraries *> processor_itineraries() {
    return processor_itineraries_;
  }
  ProcessorItineraries *processor_itinerary(std::string &name) {
    if (!processor_itineraries_.count(name))
      return nullptr;
    return processor_itineraries_[name];
  }
  std::map<std::string, ProcResource *> &proc_resources() {
    return proc_resources_;
  }
  ForwardingNetwork &forwarding_network() { return forwarding_network_; }

  std::string FormatName(const std::string &name) const;

  bool IsInstruction(const std::string &name) const {
    return instructions_.count(name);
  }
  bool IsOperand(const std::string &name) const {
    return operands_.count(name);
  }
  bool IsRegister(const std::string &name) const {
    return register_dict_.count(name);
  }
  bool IsRegisterClass(const std::string &name) const {
    return register_class_list_.count(name);
  }
  bool IsProcessorModel(const std::string &name) const {
    return cpus_.count(name);
  }
  bool IsSchedModel(const std::string &name) const {
    return sched_models_.count(name);
  }
  bool IsProcResource(const std::string &name) const {
    return proc_resources_.count(name);
  }
  bool IsProcResourceGroup(const std::string &name) const {
    return IsProcResource(name) &&
           proc_resources_.at(name)->is_resource_group();
  }
  bool IsComboUnit(const std::string &name) const {
    return combo_units_.count(name);
  }
  bool IsSchedReadWrite(const std::string &name) const {
    return rw_units_.count(name);
  }
  bool IsSchedVariant(const std::string &name) const {
    return sched_variants_.count(name);
  }
  bool IsSchedVar(const std::string &name) const {
    return sched_vars_.count(name);
  }
  bool IsMCSchedPredicate(const std::string &name) const {
    return mc_sched_predicates_.count(name);
  }
  bool IsSchedPredicate(const std::string &name) const {
    return sched_predicates_.count(name);
  }
  bool IsMCInstPredicate(const std::string &name) const {
    return mc_inst_predicates_.count(name);
  }
  bool IsMCStatement(const std::string &name) const {
    return statements_.count(name);
  }
  bool IsMCOpcodeSwitchCase(const std::string &name) const {
    return switch_cases_.count(name);
  }
  bool IsWriteSequence(const std::string &name) const {
    return write_sequences_.count(name);
  }
  bool IsSchedAliasMatch(const std::string &name) const {
    return sched_alias_matches_.count(name);
  }
  bool IsProcessorItinerary(const std::string &name) const {
    return processor_itineraries_.count(name);
  }
  bool IsInstrItineraryClass(const std::string &name) const {
    return instr_itinerary_classes_.count(name);
  }
  bool IsInstrItineraryData(const std::string &name) const {
    return instr_itinerary_data_.count(name);
  }
  bool IsInstrStage(const std::string &name) const {
    return instr_stages_.count(name);
  }
  bool IsBypass(const std::string &name) const { return bypasses_.count(name); }

  SchedReadWrite *GetSchedReadWrite(const std::string &name,
                                    const std::string &model) const;
  std::string GetSchedRef(const std::string &name,
                          const std::string &model) const;
  bool IsWrite(const std::string &name, const std::string &model) const;
  bool IsVariadic(const std::string &name, const std::string &model) const;

  void ExpandCombos(StringVec &result, std::vector<StringVec *> &input,
                    std::string item, int level);

  std::vector<std::string> ResourceCombos(std::vector<std::string> &stages,
                                          int slot_stages);

  std::map<std::vector<std::string>, ItineraryFuncUnit *> &itinerary_fus() {
    return itinerary_fus_;
  }

  // Create a new functional unit template based on an itinerary.
  ItineraryFuncUnit *add_itinerary_fu(InstrItineraryData *itin);

  std::map<std::string, int> &subunits() { return subunits_; }
  int add_subunit(const std::string &subunit) {
    if (subunits_.count(subunit))
      return subunits_[subunit];
    int size = subunits_.size();
    subunits_.insert({subunit, size});
    return size;
  }

  std::map<std::string, int> &subunit_bases() { return subunit_bases_; }
  int add_subunit_base(const std::string &base) {
    if (subunit_bases_.count(base))
      return subunit_bases_[base];
    int size = subunit_bases_.size();
    subunit_bases_.insert({base, size});
    return size;
  }

  std::map<std::string, int> &latencies() { return latencies_; }
  int add_latency(const std::string &latency) {
    if (latencies_.count(latency))
      return latencies_[latency];
    int size = latencies_.size();
    latencies_.insert({latency, size});
    return size;
  }

  int max_pipeline_phase() const { return max_pipeline_phase_; }
  void set_max_pipeline_phase(int phase) {
    max_pipeline_phase_ = std::max(phase, max_pipeline_phase_);
  }

  InstrItineraryData *instr_itinerary_data(const std::string &name) {
    return instr_itinerary_data_[name];
  }
  std::vector<InstrItineraryData *> &
  instr_itinerary_class(const std::string name) {
    return instr_itinerary_class_[name];
  }
  InstrStage *instr_stages(const std::string &name) {
    return instr_stages_[name];
  }
  std::string FormatItinSubunit(Instruction *inst, InstrItineraryData *itin);

  std::fstream &output_arch() const { return *output_arch_; }
  std::fstream &output_inst() const { return *output_inst_; }

private:
  std::string family_;         // which processor family we're looking for
  std::string output_name_;    // output file name stem
  bool ignore_subunits_;       // don't check for subunits
  bool no_warnings_;           // suppress warnings
  bool gen_subunit_bases_;     // generate instruction bases for subunits
  bool gen_forwarding_info_;   // generate forwarding information
  bool gen_operand_indexes_;   // generate indexes in references (vs names)
  std::fstream *output_arch_;  // generated architecture description
  std::fstream *output_inst_;  // generate instruction info
  std::string arch_file_name_; // main output file name
  std::string inst_file_name_; // instruction output file name

  //-------------------------------------------------------------------------
  // Instruction information we're collecting globally from the input file:
  //   - Instruction definitions.
  //   - Operand definitions.
  //   - Register class definitions.
  //   - Register definitions.
  //   - Value Type definitions.
  //-------------------------------------------------------------------------
  std::map<std::string, Instruction *> instructions_;
  std::map<std::string, Operand *> operands_;
  std::map<std::string, RegisterClass *> register_class_list_;
  StringSet value_type_set_;
  RegDefinitions registers_;
  StringSet register_dict_;
  RegSet register_sets_;
  int max_pipeline_phase_ = 0;

  //-------------------------------------------------------------------------
  // Architectural information we're scraping from the TableGen file:
  //   - CPU definitions.
  //   - SchedModel definitions.
  //   - Functional unit definitions. (ProcResources)
  //   - InstRW definitions.
  //   - ItinRW definitions.
  //   - SchedReadWrite definitions.
  //     - SchedVariant definitions.
  //     - SchedVar definitions.
  //     - [MC]SchedPredicate definitions.
  //     - SchedAlias definitions.
  //     - WriteSequence definitions.
  //   - Forwarding network description.
  //   - ProcessorItineraries definitions.
  //     - InstrItinData definitions.
  //     - Itinerary Bypass definitions.
  //     - InstrStage definitions.
  //-------------------------------------------------------------------------
  std::map<std::string, ProcessorModel *> cpus_;
  std::map<std::string, SchedMachineModel *> sched_models_;
  std::map<std::string, ProcResource *> proc_resources_;
  std::map<std::string, std::vector<std::string>> combo_units_;

  std::vector<InstRW *> instrw_info_;
  std::vector<ItinRW *> itinrw_info_;
  std::map<std::string, SchedReadWrite *> rw_units_;
  std::map<std::string, SchedVariant *> sched_variants_;
  std::map<std::string, SchedVar *> sched_vars_;

  std::map<std::string, MCSchedPredicate *> mc_sched_predicates_;
  std::map<std::string, SchedPredicate *> sched_predicates_;
  std::map<std::string, MCInstPredicate *> mc_inst_predicates_;
  std::map<std::string, MCStatement *> statements_;
  std::map<std::string, MCOpcodeSwitchCase *> switch_cases_;

  std::map<std::string, SchedAlias *> sched_alias_;
  std::map<std::string, std::map<std::string, std::string>>
      sched_alias_matches_;

  ForwardingNetwork forwarding_network_;

  std::map<std::string, WriteSequence *> write_sequences_;

  std::map<std::string, ProcessorItineraries *> processor_itineraries_;
  StringSet instr_itinerary_classes_;
  StringSet bypasses_;
  std::map<std::string, InstrItineraryData *> instr_itinerary_data_;
  std::map<std::string, std::vector<InstrItineraryData *>>
      instr_itinerary_class_;
  std::map<std::string, InstrStage *> instr_stages_;

  //-------------------------------------------------------------------------
  // Subunits and latencies created for schedules.
  //-------------------------------------------------------------------------
  std::map<std::string, int> subunit_bases_;
  std::map<std::string, int> subunits_;
  std::map<std::string, int> latencies_;

  //-------------------------------------------------------------------------
  // Subunit ids associated with each sched model.
  //-------------------------------------------------------------------------
  std::map<int, std::string> sched_model_subunits_;

  //-------------------------------------------------------------------------
  // Functional units and subunits create for itineraries.
  // These objects are indexed with a vector of stage names.
  //-------------------------------------------------------------------------
  std::map<std::vector<std::string>, ItineraryFuncUnit *> itinerary_fus_;
  std::map<std::vector<std::string>, int> itinerary_subunits_;
};

} // namespace scan
} // namespace mdl
} // namespace mpact

#endif // TDSCAN_SCAN_H_

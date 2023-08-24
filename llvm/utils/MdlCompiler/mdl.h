//===- mdl.h - Definitions for organizing a machine description -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a set of class definitions that correspond to
// constructs in the parsed machine description language (MDL), and are
// used to collect and organize all the machine details from the Antlr
// parse tree, so that they are in a more convenient, accessible format.
//
//===----------------------------------------------------------------------===//

#ifndef MDL_COMPILER_MDL_H_
#define MDL_COMPILER_MDL_H_

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "antlr4-runtime.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
namespace mpact {
namespace mdl {

//----------------------------------------------------------------------------
// Definitions of objects to hold components of the description.
//----------------------------------------------------------------------------
class PipePhases;
class Identifier;
class PhaseName;
class RegisterDef;
class RegisterClass;
class RegisterClassRef;
class ResourceDef;
class ResourceRef;
class CpuInstance;
class ClusterInstance;
class FuncUnitInstance;
class ForwardStmt;
class SubUnitInstance;
class LatencyInstance;
class Params;
class FuncUnitTemplate;
class FuncUnitGroup;
class Connect;
class FuncUnitUse;
class SubUnitTemplate;
class LatencyTemplate;
class Reference;
class ConditionalRef;
class PhaseExpr;
class OperandRef;
class InstructionDef;
class OperandDef;
class OperandAttribute;
class PredValue;
class OperandDecl;
class PredExpr;

//----------------------------------------------------------------------------
// Containers for managing instantiation of CPUs, clusters, functional units,
// subunits, and latencies.
//----------------------------------------------------------------------------
class MdlSpec;
class FuncUnitInstantiation;
class SubUnitInstantiation;
class LatencyInstantiation;

// Descriptor of the overall compiler output state.
class OutputState;

//----------------------------------------------------------------------------
// This represents a map of all subunit instantiations.  For each subunit
// template, we have a list of every instance of that subunit, and the
// context in which it was instantiated.
//----------------------------------------------------------------------------
using SubUnitInstantiations =
    std::map<std::string, std::vector<SubUnitInstantiation *> *>;

//----------------------------------------------------------------------------
// This represents a map of functional unit templates to client functional
// unit instances. We build one of these for each CPU.
//----------------------------------------------------------------------------
using FuncUnitInstances =
    std::map<std::string, std::vector<FuncUnitInstantiation *>>;

//----------------------------------------------------------------------------
// This represents a map of functional unit instantiations for each cluster.
//----------------------------------------------------------------------------
using FuncUnitInstantiations = std::vector<FuncUnitInstantiation *>;

//----------------------------------------------------------------------------
// Containers for collections of components.
//----------------------------------------------------------------------------
using IdList = std::vector<Identifier *>;
using RegisterDefList = std::vector<RegisterDef *>;
using RegisterClassList = std::vector<RegisterClass *>;
using PipeDefList = std::vector<PipePhases *>;
using PhaseNameList = std::vector<PhaseName *>;
using ResourceDefList = std::vector<ResourceDef *>;
using ResourceRefList = std::vector<ResourceRef *>;
using CpuList = std::vector<CpuInstance *>;
using ParamsList = std::vector<Params *>;
using FuncUnitInstList = std::vector<FuncUnitInstance *>;
using SubUnitInstList = std::vector<SubUnitInstance *>;
using ForwardStmtList = std::vector<ForwardStmt *>;
using FuncUnitList = std::vector<FuncUnitTemplate *>;
using FuncUnitGroupList = std::vector<FuncUnitGroup *>;
using FuncUnitUseSet = std::vector<std::vector<FuncUnitUse *>>;
using SubUnitList = std::vector<SubUnitTemplate *>;
using LatencyList = std::vector<LatencyTemplate *>;
using LatencyInstList = std::vector<LatencyInstance *>;
using ClusterList = std::vector<ClusterInstance *>;
using ConnectList = std::vector<Connect *>;
using ReferenceList = std::vector<Reference *>;
using ConditionalRefList = std::vector<ConditionalRef *>;
using OperandRefList = std::vector<OperandRef *>;
using InstructionList = std::vector<InstructionDef *>;
using OperandDefList = std::vector<OperandDef *>;
using OperandDeclList = std::vector<OperandDecl *>;
using OperandAttributeList = std::vector<OperandAttribute *>;
using PredValueList = std::vector<PredValue *>;

using IdDict = std::map<std::string, Identifier *>;
using CpuDict = std::map<std::string, CpuInstance *>;
using FuncUnitDict = std::map<std::string, FuncUnitTemplate *>;
using FuncUnitGroupDict = std::map<std::string, FuncUnitGroup *>;
using SubUnitDict = std::map<std::string, SubUnitTemplate *>;
using LatencyDict = std::map<std::string, LatencyTemplate *>;
using OperandDict = std::map<std::string, OperandDef *>;
using InstructionDict = std::map<std::string, InstructionDef *>;
using SubUnitInstrs = std::map<std::string, InstructionList>;
using StringList = std::vector<std::string>;

using ResourceRefDict = std::map<std::string, ResourceRef *>;
using ResourceDefDict = std::map<std::string, ResourceDef *>;
using RegisterClassDict = std::map<std::string, RegisterClass *>;
using RegisterClassRefDict = std::map<std::string, RegisterClassRef *>;

//----------------------------------------------------------------------------
// Template for writing out vectors of pointers to objects.
//----------------------------------------------------------------------------
template <typename T>
std::ostream &PrintVec(std::ostream &out, const std::vector<T> *v,
                       std::string head = "", std::string sep = "\n",
                       std::string end = "\n") {
  if (!v)
    return out;
  out << head;

  for (auto *item : *v) {
    out << *item;
    if (item != v->back())
      out << sep;
  }
  return out << end;
}

//----------------------------------------------------------------------------
// Template function for stringizing vectors of pointers to objects.
//----------------------------------------------------------------------------
template <typename T>
std::string StringVec(const std::vector<T> *v, std::string head = "",
                      std::string sep = "\n", std::string end = "\n") {
  if (!v)
    return "";
  std::string out = head;

  for (auto *item : *v) {
    out += item->ToString();
    if (item != v->back())
      out += sep;
  }

  return out + end;
}
//----------------------------------------------------------------------------
// Template function for stringizing sets of strings.
//----------------------------------------------------------------------------
template <typename T>
std::string StringSet(const std::set<T> *v, std::string head = "",
                      std::string sep = "\n", std::string end = "\n") {
  if (!v)
    return "";
  std::string out = head;

  for (auto &item : *v) {
    out += item;
    if (item != *v->rbegin())
      out += sep;
  }

  return out + end;
}

//----------------------------------------------------------------------------
// Template function to find an MDL item in a vector, by name.
//----------------------------------------------------------------------------
template <typename A>
A *FindItem(std::vector<A *> &items, const std::string &name) {
  for (auto *a_item : items)
    if (a_item->name() == name)
      return a_item;
  return nullptr;
}

//----------------------------------------------------------------------------
// Template function to find an MDL item in a map of pointers, by name.
//----------------------------------------------------------------------------
template <typename A>
A *FindItem(std::map<std::string, A *> &items, const std::string &name) {
  auto it = items.find(name);
  return (it == items.end()) ? nullptr : it->second;
}

//----------------------------------------------------------------------------
// "Internal" FU names contain a leading colon character, so that they never
// conflict with user-defined names.
//----------------------------------------------------------------------------
inline bool is_catchall_name(const std::string &name) {
  return name[0] == ':';
}
inline std::string catchall_name(const std::string &name) {
  return llvm::formatv(":{0}", name);
}

//----------------------------------------------------------------------------
// FU Instances with explicit bases.
//----------------------------------------------------------------------------
inline bool is_based_fu_instance(const std::string &name) {
  auto loc = name.find(':');
  return loc != 0 && loc != std::string::npos;
}

inline std::string based_fu_instance(const IdList *bases) {
  return StringVec<Identifier *>(bases, "", ":", "");
}

//----------------------------------------------------------------------------
// Define a base class that contains source information for each object.
//----------------------------------------------------------------------------
class MdlItem {
public:
  explicit MdlItem(antlr4::ParserRuleContext *ctx)
      : line_(ctx->getStart()->getLine()),
        column_(ctx->getStart()->getCharPositionInLine()) {}
  explicit MdlItem(antlr4::ParserRuleContext *ctx, std::string &file_name)
      : line_(ctx->getStart()->getLine()),
        column_(ctx->getStart()->getCharPositionInLine()),
        file_name_(file_name) {}
  MdlItem(const MdlItem &item)
      : line_(item.line()), column_(item.column()),
        file_name_(item.file_name_) {}
  MdlItem() : line_(0), column_(0) {}

  int line() const { return line_; }
  int column() const { return column_; }
  const std::string &file_name() const { return file_name_; }
  std::string Location() const {
    return llvm::formatv("{0}:{1}:{2}", file_name_, std::to_string(line_),
                         std::to_string(column_ + 1));
  }

private:
  int line_;   // Lexical line number of this item.
  int column_; // Lexical column number of this item.
  std::string file_name_;
};

//----------------------------------------------------------------------------
// An instance of a name. Used anywhere that an identifier is used
// in the machine description.
//----------------------------------------------------------------------------
class Identifier : public MdlItem {
public:
  // Create a general identifier.
  Identifier(const MdlItem &item, std::string name)
      : MdlItem(item), name_(name) {}
  // Used to create identifiers used in resource groups.
  Identifier(Identifier *item, int index)
      : MdlItem(*item), name_(item->name_), index_(index) {}
  // Used to generate internal names that don't map back to source code.
  explicit Identifier(std::string name) : MdlItem(), name_(name) {}

  bool operator!=(const Identifier &rhs) { return name_ != rhs.name(); }
  std::string ToString() const;
  std::string const &name() const { return name_; }
  void set_index(int index) { index_ = index; }
  int index() const { return index_; }
  bool is_vararg() const { return name_[0] == '$'; }
  int vararg_index() const { return std::stoi(name_.substr(1)); }
  bool is_number() const { return isdigit(name_[0]); }
  int get_number() const { return std::stoi(name_); }

private:
  const std::string name_; // Name used anywhere in machine description.
  int index_ = -1;         // If in an IdList, its 0-based position in list.
};

//----------------------------------------------------------------------------
// An instance of a pipe phase name, defined in a pipeline definition.
//----------------------------------------------------------------------------
class PhaseName : public MdlItem {
public:
  PhaseName(const MdlItem &item, std::string name, bool is_protected,
            bool is_hard)
      : MdlItem(item), name_(name), is_protected_(is_protected),
        is_hard_(is_hard) {}
  explicit PhaseName(std::string name) : MdlItem(), name_(name) {}

  std::string ToString() const;
  std::string FormatProtection() const;
  std::string const &name() const { return name_; }
  void set_index(int index) { index_ = index; }
  int index() const { return index_; }
  bool is_protected() const { return is_protected_; }
  bool is_unprotected() const { return !is_protected_; }
  bool is_hard() const { return is_hard_; }

private:
  const std::string name_;   // name of the pipeline phase
  int index_ = -1;           // If in an IdList, its 0-based position in list
  bool is_protected_ = true; // true if this is in a protected pipeline
  bool is_hard_ = false;     // true if this is in a "hard" pipeline
};

//----------------------------------------------------------------------------
// An instance of a register definition.
//----------------------------------------------------------------------------
class RegisterDef : public MdlItem {
public:
  RegisterDef(const MdlItem &item, Identifier *id) : MdlItem(item), id_(id) {}

  std::string ToString() const;
  std::string const &name() const { return id_->name(); }

private:
  const Identifier *id_; // Identifier associated with register.
};

//----------------------------------------------------------------------------
// Description of a register class.
//----------------------------------------------------------------------------
class RegisterClass : public MdlItem {
public:
  RegisterClass(const MdlItem &item, Identifier *id, RegisterDefList *members)
      : MdlItem(item), id_(id), members_(members) {}
  explicit RegisterClass(std::string name) : id_(new Identifier(name)) {}

  // Return true if decl is a superset of this class.
  bool IsSupersetOf(const RegisterClass *decl) const {
    for (auto *reg : *decl->members_)
      if (!FindItem(*members_, reg->name()))
        return false;
    return true;
  }
  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  RegisterDefList *members() const { return members_; }
  bool IsNull() const { return name() == "__"; }

private:
  Identifier *id_;           // Name of the class.
  RegisterDefList *members_; // List of registers included in class.
};

//----------------------------------------------------------------------------
// An instance argument which refers to a register class.
//----------------------------------------------------------------------------
class RegisterClassRef : public MdlItem {
public:
  explicit RegisterClassRef(RegisterClass *item)
      : MdlItem(*item), id_(item->id()), regs_(item) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  RegisterClass *regs() { return regs_; }

private:
  Identifier *id_ = nullptr;      // name of the referenced class
  RegisterClass *regs_ = nullptr; // link to the referenced class
};

//----------------------------------------------------------------------------
// Description of a pipeline phase group defined in the MDL.
//      phases <name> { phase1, phase2, ... };
// Phases defined as ranges (E[3..5]) are expanded in this object.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class PipePhases : public MdlItem {
public:
  PipePhases(const MdlItem &item, Identifier *id, PhaseNameList *phases,
             PhaseName *first_execute_phase, bool is_protected, bool is_hard)
      : MdlItem(item), id_(id), phase_names_(phases),
        first_execute_phase_name_(first_execute_phase),
        is_protected_(is_protected), is_hard_(is_hard) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  PhaseNameList *phase_names() const { return phase_names_; }
  bool is_protected() const { return is_protected_; }
  bool is_hard() const { return is_hard_; }
  PhaseName *first_execute_phase_name() const {
    return first_execute_phase_name_;
  }

private:
  Identifier *const id_ = nullptr;             // name of pipeline phase group
  PhaseNameList *const phase_names_ = nullptr; // names of each phase
  PhaseName *const first_execute_phase_name_;  // first execute phase
  const bool is_protected_ = true; // true if the pipeline is protected
  const bool is_hard_ = false;     // true if the pipeline is hard vs soft
};

//----------------------------------------------------------------------------
// Types of references used in Reference objects, and three function
// declarations for converting between these ids and strings. The functions
// (declared below) must be kept in sync with these definitions.  The order
// of the definitions is significant: it represents the order reference
// lists are written out.
//----------------------------------------------------------------------------
struct RefTypes {
  using Item = int16_t;
  static constexpr int kNull = 0;
  static constexpr int kPred = 1;     // use of a predicate operand
  static constexpr int kUse = 2;      // use of an operand and/or resources
  static constexpr int kDef = 4;      // def of an operand and use of resources
  static constexpr int kKill = 8;     // kill of an operand
  static constexpr int kUseDef = 16;  // operand use and def (use of a resource)
  static constexpr int kHold = 32;    // hold on availability of resources
  static constexpr int kReserve = 64; // reserve resources until a given cycle
  static constexpr int kFus = 128;    // use a set of functional units
  static constexpr int kCond = 256;   // conditional reference

  static constexpr int kAnyDef = kDef | kUseDef | kKill;
  static constexpr int kAnyUse = kPred | kUse | kUseDef;
  static constexpr int kAnyUseDef = kAnyDef | kAnyUse;
  static constexpr int kHoldReserve = kHold | kReserve;
};
using RefType = RefTypes::Item;

// Map a string from the mdl input file to a RefType.
extern RefType StringToRefType(const std::string &ref_type);
// Format a RefType for debug output.
extern std::string RefTypeToString(RefType ref_type);
// Format a RefType for database generation.
extern std::string FormatReferenceType(RefType ref_type);
// Format an aggregated RefType for database generation.
extern std::string FormatReferenceTypes(int ref_type);
// Format a reference flags field.
extern std::string FormatReferenceFlags(const Reference *ref);

//----------------------------------------------------------------------------
// A set of flags for describing scheduling attributes for operand, resource,
// and explicit functional unit references.  These values are passed through
// the generated database, so their values must correspond to same-named
// values in MDLInfo.h.
//----------------------------------------------------------------------------
struct RefFlags {
  using Item = int8_t;

  // Reference flags for operand and resource references.
  static constexpr int kNone = 0;
  static constexpr int kProtected = 1;
  static constexpr int kUnprotected = 2;
  static constexpr int kDuplicate = 4;

  // Reference flags for explicit functional unit references.
  static constexpr int kUnbuffered = 1;
  static constexpr int kInOrder = 2;
  static constexpr int kOutOfOrder = 4;
  static constexpr int kBeginGroup = 8;
  static constexpr int kEndGroup = 16;
  static constexpr int kSingleIssue = 32;
  static constexpr int kRetireOOO = 64;

  static bool is_protected(Item flag) { return flag & kProtected; }
  static bool is_unprotected(Item flag) { return flag & kUnprotected; }
  static bool is_duplicate(Item flag) { return flag & kDuplicate; }
  static bool is_unbuffered(Item flag) { return flag & kUnbuffered; }
  static bool is_out_of_order(Item flag) { return flag & kOutOfOrder; }
  static bool is_in_order(Item flag) { return flag & kInOrder; }
  static bool is_begin_group(Item flag) { return flag & kBeginGroup; }
  static bool is_end_group(Item flag) { return flag & kEndGroup; }
  static bool is_single_issue(Item flag) { return flag & kSingleIssue; }
  static bool is_retire_ooo(Item flag) { return flag & kRetireOOO; }
};

//----------------------------------------------------------------------------
// Resource pools can have subpools, we collect information about them.
// SubPools have an ordering based on how restrictive they are (number of
// resources they qualify for).  The most restrictive pools are allocated
// first.
//----------------------------------------------------------------------------
class SubPool {
public:
  explicit SubPool(const ResourceRef *res);
  int first() const { return first_; }
  int last() const { return last_; }
  int size() const { return last_ - first_; }
  bool operator<(const SubPool &rhs) const {
    return size() < rhs.size() ||
           (size() == rhs.size() &&
            (first() < rhs.first() || last() < rhs.last()));
  }
  bool operator>(const SubPool &item) const { return item < *this; }
  std::string ToString() const;

private:
  int first_; // id of first member of the subpool
  int last_;  // id of last member of the subpool
};

// Information about a single subpool.
class SubPoolInfo {
public:
  void set_subpool_id(int id) { subpool_id_ = id; }
  void add_count(int count) {
    if (count)
      counts_.emplace(count); // Don't add zeros.
  }
  int subpool_id() const { return subpool_id_; }
  const std::set<int> &counts() const { return counts_; }

  std::string ToString(std::string subpool) const;

private:
  int subpool_id_ = -1;
  std::set<int> counts_; // the set of all non-zero count requests
};

// For each pooled reference, keep track of how many resources were requested.
using SubPools = std::map<SubPool, SubPoolInfo>;

enum class GroupType { kUseAll, kUseSingle };

//----------------------------------------------------------------------------
// Definition of a single resource object defined in the MDL.
//      resource <name>;
//      resource <name> { <member>, <member>, ... };
//      resource <name>:<bits>;
//      resource <name>[pool_size>];
//      resource <name>:<bits>[pool_size>];
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class ResourceDef : public MdlItem {
public:
  ResourceDef(const MdlItem &item, Identifier *id, int bits, int pool_size,
              Identifier *start, Identifier *end)
      : MdlItem(item), id_(id), bit_size_(bits), pool_size_(pool_size),
        start_phase_(start), end_phase_(end) {}
  ResourceDef(const MdlItem &item, Identifier *id, int bits, IdList *members,
              Identifier *start, Identifier *end)
      : MdlItem(item), id_(id), bit_size_(bits), members_(*members),
        start_phase_(start), end_phase_(end) {}
  ResourceDef(const MdlItem &item, Identifier *id) : MdlItem(item), id_(id) {}
  ResourceDef(Identifier *const id, FuncUnitInstantiation *fu)
      : MdlItem(*id), id_(id), fu_(fu) {}
  explicit ResourceDef(Identifier *const id) : MdlItem(*id), id_(id) {}
  explicit ResourceDef(std::string name)
      : MdlItem(), id_(new Identifier(name)) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  IdList &members() { return members_; }
  int bit_size() const { return bit_size_; }
  bool has_shared_bits() const { return bit_size_ > 0; }
  int pool_size() const { return pool_size_; }
  Identifier *start_phase() const { return start_phase_; }
  Identifier *end_phase() const { return end_phase_; }
  bool IsNull() const { return name() == "__"; }

  bool IsPoolDef() const { return IsGroupDef() || pool_size_ > 0; }
  bool IsGroupDef() const { return !members_.empty(); }
  int size() const {
    if (IsGroupDef()) return members_.size();
    if (IsPoolDef()) return pool_size_;
    return 1;
  }

  void set_resource_id(int id) { resource_id_ = id; }
  int get_resource_id() const { return resource_id_; }
  int pool_id() const { return pool_id_; }
  void set_pool_id(int pool_id) { pool_id_ = pool_id; }
  void add_alloc_size(int size) { alloc_sizes_.emplace(size); }
  std::set<int> &alloc_sizes() { return alloc_sizes_; }
  SubPoolInfo &sub_pool(SubPool &pool) { return sub_pools_[pool]; }
  SubPools &sub_pools() { return sub_pools_; }
  void AddReferenceSizeToPool(const ResourceRef *resource, const Reference *ref,
                              const SubUnitInstantiation *subunit);

  std::string resource_format();

  int GetMemberId(const Identifier *member) const {
    for (auto *mem : members_)
      if (mem->name() == member->name())
        return mem->index();
    return -1;
  }

  RegisterClass *reg_class() const { return reg_class_; }
  void set_reg_class(RegisterClass *regs) { reg_class_ = regs; }
  ResourceRef *port_resource() const { return port_resource_; }
  void set_port_resource(ResourceRef *res) { port_resource_ = res; }

  void RecordReference(RefType type, const PhaseExpr *expr,
                       const ResourceRef *resource, const Reference *ref,
                       const SubUnitInstantiation *subunit);
  void RecordFuReference() { ref_types_ |= RefTypes::kFus; }

  void set_debug_name(std::string type, const CpuInstance *cpu,
                      const ClusterInstance *cluster,
                      const FuncUnitInstantiation *fu);
  std::string debug_name() const { return debug_name_; }
  std::string ref_summary() const;
  int ref_types() const { return ref_types_; }
  bool is_used() const { return ref_types_ != 0; }
  bool only_held() const {
    return (ref_types_ & (RefTypes::kHoldReserve)) == RefTypes::kHold;
  }
  bool only_reserved() const {
    return (ref_types_ & (RefTypes::kHoldReserve)) == RefTypes::kReserve;
  }

  int latest_ref() const { return latest_ref_; }
  bool phase_expr_seen() const { return phase_expr_seen_; }

  std::vector<ResourceDef *> &member_defs() { return member_defs_; }
  void add_member_def(ResourceDef *def) { member_defs_.push_back(def); }
  ResourceDef *get_member_def(int index) const { return member_defs_[index]; }
  bool implicit_group() const { return implicit_group_; }
  void set_implicit_group() { implicit_group_ = true; }
  GroupType group_type() const { return group_type_; }
  void set_group_type(GroupType type) { group_type_ = type; }
  void set_pool_size(unsigned size) { pool_size_ = size; }
  FuncUnitInstantiation *get_fu() const { return fu_; }

private:
  Identifier *const id_ = nullptr;    // name of the referenced resource
  const int bit_size_ = -1;           // number of bits represented
  IdList members_;                    // members of a named resource pool
  int pool_size_ = 0;                 // number of elements in pool (or 0)
  Identifier *start_phase_ = nullptr; // optional start phase id
  Identifier *end_phase_ = nullptr;   // optional end phase id
  int resource_id_ = 0;               // resource index for this object
  int pool_id_ = -1;                  // id, if resource is first in a pool

  std::vector<ResourceDef *> member_defs_;
  bool implicit_group_ = false; // True if this is an implicit group def
  GroupType group_type_ = GroupType::kUseSingle;

  ResourceRef *port_resource_ = nullptr; // resource port is connected to
  RegisterClass *reg_class_ = nullptr;   // optional constraint for a port
  FuncUnitInstantiation *fu_ = nullptr;  // instantated func_unit names

  int earliest_ref_ = -1;        // earliest seen reference
  int latest_ref_ = -1;          // latest seen reference
  bool phase_expr_seen_ = false; // true if there are phase expressions
  int ref_types_ = 0;            // OR of all seen reference types
  std::string debug_name_;       // pretty name for printing
  std::set<int> alloc_sizes_;    // set of all pool size requests
  SubPools sub_pools_;           // Map of all subpools for this pool
};

//----------------------------------------------------------------------------
// Use of a resource (used in a functional- or sub-unit instantiation.
//   ... <name> ...                  // Reference entire resource.
//   ... <name>.<member> ...         // Reference a member.
//   ... <name>:<count> ...          // Reference <count> pool members.
//   ... <name>[<first>..<last>]...  // Reference part of a pool.
// This object owns all the data pointed to by member pointers, except
// for the definition_ member.
//----------------------------------------------------------------------------
class ResourceRef : public MdlItem {
public:
  ResourceRef(const MdlItem &item, Identifier *id) : MdlItem(item), id_(id) {}
  ResourceRef(const MdlItem &item, Identifier *id, int pool_count,
              Identifier *pool_count_name, Identifier *value_name)
      : MdlItem(item), id_(id), pool_count_(pool_count),
        pool_count_name_(pool_count_name), value_name_(value_name) {}
  ResourceRef(const MdlItem &item, Identifier *id, Identifier *member)
      : MdlItem(item), id_(id), member_(member) {}
  ResourceRef(const MdlItem &item, Identifier *id, int first, int last)
      : MdlItem(item), id_(id), first_(first), last_(last) {}
  explicit ResourceRef(std::string name)
      : MdlItem(), id_(new Identifier(name)) {}
  explicit ResourceRef(ResourceDef *def)
      : MdlItem(*def), id_(def->id()), first_(def->pool_size() > 0 ? 0 : -1),
        last_(def->pool_size() > 0 ? def->pool_size() - 1 : -1),
        definition_(def) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  void rename(Identifier *id) { id_ = id; }
  std::string const &name() const { return id_->name(); }
  Identifier *member() const { return member_; }
  int pool_count() const { return pool_count_; }
  Identifier *pool_count_name() const { return pool_count_name_; }

  bool HasCount() const {
    return pool_count_ != -1 || pool_count_name_ != nullptr;
  }
  Identifier *value_name() const { return value_name_; }

  int first() const { return first_; }
  int last() const { return last_; }
  int pool_size() const {
    if (IsGroupDef())
      return definition_->members().size();
    return last_ - first_ + 1;
  }

  bool IsNull() const { return name() == "__"; }
  bool IsPool() const { return first_ != -1; }
  bool IsGroupRef() const { return IsGroupDef() && !member(); }
  bool IsGroupDef() const {
    return definition_ != nullptr && definition_->IsGroupDef();
  }
  bool implicit_group() const {
    return IsGroupDef() && definition_->implicit_group();
  }

  bool IsArrayDef() const {
    return definition_ != nullptr && IsPool() && definition_->pool_size();
  }
  bool IsPooledResourceRef() const {
    return (IsGroupRef() || (IsArrayDef() && !IsIndexed())) && !HasCount();
  }
  bool IsUnqualifiedRef() const { return !member() && first() == -1; }
  bool HasAllocation() const { return HasCount(); }
  bool HasValueName() const { return value_name_ != nullptr; }

  int IsSubrange() const { return first_ != -1 && last_ != first_; }
  int IsIndexed() const { return first_ != -1 && first_ == last_; }

  int member_id() const { return member_id_; }

  void set_first(int first) { first_ = first; }
  void set_last(int last) { last_ = last; }
  void set_pool_count(int count) { pool_count_ = count; }
  void set_pool_count_name(Identifier *count) { pool_count_name_ = count; }
  void set_use_all_members() { use_all_members_ = true; }
  bool use_all_members() const { return use_all_members_; }
  void set_value_name(Identifier *mask) { value_name_ = mask; }
  void set_subrange(int first, int last) {
    set_first(first);
    set_last(last);
  }
  void set_fu_pool(ResourceDef *pool) { fu_pool_ = pool; }
  ResourceDef *fu_pool() const { return fu_pool_; }
  bool is_fu_pool() const { return fu_pool_; }
  ResourceDef *definition() const { return definition_; }
  void set_definition(ResourceDef *def) { definition_ = def; }
  ResourceDef *get_port_definition() const { return definition_; }
  int get_resource_id() const {
    return definition_ ? definition_->get_resource_id() : -1;
  }
  int get_final_resource_id() const {
    if (first() != -1 && first() == last())
      return get_resource_id() + first();
    return get_resource_id();
  }

  Params *get_parameter() { return arg_parameter_link_; }
  void set_parameter(Params *parameter) { arg_parameter_link_ = parameter; }
  int operand_index() const { return operand_index_; }
  void set_operand_index(int id) { operand_index_ = id; }
  bool has_operand_index() const { return operand_index_ != -1; }

private:
  Identifier *id_ = nullptr;              // name of the referenced resource
  Identifier *member_ = nullptr;          // which member is named (a.b)
  int pool_count_ = -1;                   // how many pool members (a:count)
  bool use_all_members_ = false;          // was "resource:*" syntax specified
  Identifier *pool_count_name_ = nullptr; // symbolic count of members
  Identifier *value_name_ = nullptr;      // name of operand value attribute
  int first_ = -1, last_ = -1;            // subrange of pool (a[2..4] or a[3])
  int member_id_ = -1;                    // index of a member reference
  ResourceDef *fu_pool_ = nullptr;        // for pooled fu references

  // Links to related objects.
  ResourceDef *definition_ = nullptr;    // link to resource definition
  Params *arg_parameter_link_ = nullptr; // arguments are linked to parameters
  int operand_index_ = -1; // pooled resources are tied to an operand
};

//----------------------------------------------------------------------------
// Reflect what is in the MDL for a single processor definition.
// This object owns all the data pointed to by const member pointers.
//----------------------------------------------------------------------------
class CpuInstance : public MdlItem {
public:
  CpuInstance(const MdlItem &item, Identifier *id, PipeDefList *pipe_phases,
              ResourceDefList *issues, ResourceDefList *res,
              int reorder_buffer_size, ClusterList *clusters,
              ForwardStmtList *forward_stmts,
              std::vector<std::string> &llvm_names)
      : MdlItem(item), id_(id), pipe_phases_(pipe_phases), issues_(issues),
        resources_(res), reorder_buffer_size_(reorder_buffer_size),
        clusters_(clusters), forward_stmts_(forward_stmts),
        llvm_names_(llvm_names) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  ResourceDefList *issues() const { return issues_; }
  ResourceDefList *resources() const { return resources_; }
  int reorder_buffer_size() const { return reorder_buffer_size_; }
  ClusterList *clusters() const { return clusters_; }
  ForwardStmtList *forward_stmts() const { return forward_stmts_; }
  std::vector<std::string> &llvm_names() { return llvm_names_; }
  bool needs_slot_resources() const { return needs_slot_resources_; }
  void set_needs_slot_resources(bool value) { needs_slot_resources_ = value; }

  void add_cpu_resource(ResourceDef *resource, std::string type,
                        const CpuInstance *cpu, const ClusterInstance *cluster,
                        const FuncUnitInstantiation *fu) {
    all_resources_.push_back(resource);
    resource->set_debug_name(type, cpu, cluster, fu);
  }
  void add_fu_pool_size(int size) { fu_pool_sizes_.insert(size); }
  std::set<int> &fu_pool_sizes() { return fu_pool_sizes_; }

  ResourceDefList &all_resources() { return all_resources_; }
  void add_pool_resource(ResourceDef *pool) { pool_resources_.push_back(pool); }
  ResourceDefList &pool_resources() { return pool_resources_; }
  PipeDefList *pipe_phases() { return pipe_phases_; }

  int max_resource_phase() const { return max_resource_phase_; }
  void set_max_used_resource_id(int id) { max_used_resource_id_ = id; }
  int max_used_resource_id() const { return max_used_resource_id_; }
  void set_max_resource_phase(int phase) { max_resource_phase_ = phase; }
  int max_issue() const { return max_issue_; }
  void set_max_issue(int issue) { max_issue_ = issue; }
  int pool_count() const { return pool_count_; }
  void set_pool_count(int pool_count) { pool_count_ = pool_count; }
  int max_pool_allocation() const { return max_pool_allocation_; }
  void set_max_pool_allocation(int size) { max_pool_allocation_ = size; }
  int early_use_phase() const { return early_use_phase_; }
  void set_early_use_phase(int phase) { early_use_phase_ = phase; }
  int load_phase() {
    for (const auto *p1 : *pipe_phases())
      if (const auto *item = FindItem(*p1->phase_names(), "LOAD_PHASE"))
        return item->index();
    return MCSchedModel::DefaultLoadLatency;
  }
  int high_latency_def_phase() {
    for (const auto *p1 : *pipe_phases())
      if (const auto *item = FindItem(*p1->phase_names(), "HIGH_PHASE"))
        return item->index();
    return MCSchedModel::DefaultHighLatency;
  }
  void set_max_fu_id(int last_id) { max_fu_id_ = last_id; }
  int max_fu_id() const { return max_fu_id_; }
  void set_max_resource_id(int last_id) { max_resource_id_ = last_id; }
  int max_resource_id() const { return max_resource_id_; }
  void set_instr_count(int instr_count) { instr_count_ = instr_count; }
  int get_instr_count() const { return instr_count_; }

  FuncUnitInstances &func_unit_instances() { return func_unit_instances_; }

private:
  Identifier *const id_ = nullptr;             // name of this CPU
  PipeDefList *pipe_phases_ = nullptr;         // locally defined pipe phases
  ResourceDefList *const issues_ = nullptr;    // issue slot resources
  ResourceDefList *const resources_ = nullptr; // resources defined locally
  int reorder_buffer_size_ = MCSchedModel::DefaultMicroOpBufferSize;
  ClusterList *const clusters_ = nullptr;      // clusters defined
  ForwardStmtList *const forward_stmts_ = nullptr; // forward statements
  std::vector<std::string> llvm_names_;            // optional llvm names
  ResourceDefList all_resources_;         // all resources defined for CPU
  ResourceDefList pool_resources_;        // all pooled resources for CPU
  FuncUnitInstances func_unit_instances_; // map of templates to instances
  std::set<int> fu_pool_sizes_;           // set of fu allocation pools
  int instr_count_;                       // number of valid instructions

  int max_used_resource_id_ = 0;      // number of "used" resources
  int max_resource_phase_ = 0;        // latest resource "use"
  int max_issue_ = 0;                 // maximum parallel issue size
  int pool_count_ = 0;                // number of pooled resources
  int max_pool_allocation_ = 0;       // max pool allocation size
  int early_use_phase_ = -1;          // earliest named "use" phase
  bool needs_slot_resources_ = false; // True if we must model slots
  int max_fu_id_ = 0;                 // Id of last func unit
  int max_resource_id_ = 0;           // Id of last resource
};

//----------------------------------------------------------------------------
// Instance of a cluster defined in a processor description.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class ClusterInstance : public MdlItem {
public:
  ClusterInstance(const MdlItem &item, Identifier *id, ResourceDefList *issue,
                  ResourceDefList *res, FuncUnitInstList *fus,
                  ForwardStmtList *forward_stmts)
      : MdlItem(item), id_(id), issues_(issue), resources_(res),
        func_units_(fus), forward_stmts_(forward_stmts) {}
  explicit ClusterInstance(FuncUnitInstance *func_unit)
      : MdlItem(), id_(new Identifier("__")), issues_(new ResourceDefList),
        resources_(new ResourceDefList),
        func_units_(new FuncUnitInstList(1, func_unit)) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  bool IsNull() const { return name() == "__"; }
  ResourceDefList *issues() const { return issues_; }
  ResourceDefList *resources() const { return resources_; }
  FuncUnitInstList *func_units() const { return func_units_; }
  ForwardStmtList *forward_stmts() const { return forward_stmts_; }
  void AddFuncUnitInstantiation(FuncUnitInstantiation *fu) {
    fu_instantiations_.push_back(fu);
  }
  FuncUnitInstantiations &fu_instantiations() { return fu_instantiations_; }
  // Debug: Dump out functional unit instantiations for this cluster.
  void DumpFuncUnitInstantiations();

private:
  Identifier *const id_ = nullptr;                 // name of this CPU
  ResourceDefList *const issues_ = nullptr;        // issue entries, if any
  ResourceDefList *const resources_ = nullptr;     // resources defined locally
  FuncUnitInstList *const func_units_ = nullptr;   // func units instantiated
  ForwardStmtList *const forward_stmts_ = nullptr; // forward statements

  FuncUnitInstantiations fu_instantiations_;
};

//----------------------------------------------------------------------------
// Instance of a functional unit referenced in a CPU or cluster.
// An instance of a functional unit can be "unreserved" - this is used to
// model itineraries, which don't directly tie instructions to functional units.
//
// A functional unit instance can be "pinned" to issue/encoding slots, which
// are specified in an "issue" statement:
//  - f() -> slot : pin an instance to a specific slot.
//  - f() -> slot1 | slot2 : pin an instance to one of several slots.
//  - f() -> slot & slot : pin an instance to more than one slot.
// If there is no pinning specification, the instance can be issued in
// any slot.
// This object owns all the data pointed to by member pointers, except for
// the fu_template member.
//----------------------------------------------------------------------------
class FuncUnitInstance : public MdlItem {
public:
  FuncUnitInstance(const MdlItem &item, Identifier *type, Identifier *id,
                   bool unreserved, std::map<std::string, int> buffer_sizes,
                   ResourceRefList *args, IdList *any, IdList *all)
     : MdlItem(item), id_(id), type_(type), unreserved_(unreserved),
       buffer_sizes_(buffer_sizes), args_(args), pin_all_(all), pin_any_(any) {}
  explicit FuncUnitInstance(const std::string type)
     : MdlItem(), id_(new Identifier(type)), type_(new Identifier(type)),
       args_(new ResourceRefList), pin_all_(nullptr), pin_any_(nullptr) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  Identifier *type() const { return type_; }
  bool is_unreserved() const { return unreserved_; }

  int buffer_size(std::string fu) { return buffer_sizes_[fu]; }
  bool is_unbuffered(std::string fu) { return buffer_sizes_[fu] == 0; }
  bool is_in_order(std::string fu) { return buffer_sizes_[fu] == 1; }
  bool is_out_of_order(std::string fu) { return buffer_sizes_[fu] > 1; }

  ResourceRefList *args() const { return args_; }
  IdList *pin_any() const { return pin_any_; }
  IdList *pin_all() const { return pin_all_; }

  void set_template(FuncUnitTemplate *temp) { fu_template_ = temp; }
  FuncUnitTemplate *get_template() const { return fu_template_; }

  ResourceRefList *get_resource_slots_any() const { return fu_pin_any_; }
  ResourceRefList *get_resource_slots_all() const { return fu_pin_all_; }
  void set_resource_slots_any(ResourceRefList *res) { fu_pin_any_ = res; }
  void set_resource_slots_all(ResourceRefList *res) { fu_pin_all_ = res; }

  // "catchall" unit names use a colon followed by their associated CPU name.
  bool is_catchall_unit() const { return is_catchall_name(name()); }

private:
  Identifier *const id_ = nullptr;            // name of this unit (optional)
  Identifier *const type_ = nullptr;          // template of this unit
  bool unreserved_ = false;                   // Is this an unreserved FU?
  std::map<std::string, int> buffer_sizes_;   // buffer sizes for each base
  ResourceRefList *const args_ = nullptr;     // arguments to this instance
  IdList *const pin_all_ = nullptr;           // FU needs more than one slot
  IdList *const pin_any_ = nullptr;           // slots FU can be pinned to
  FuncUnitTemplate *fu_template_ = nullptr;   // link to template
  ResourceRefList *fu_pin_any_ = nullptr;     // slot resource list
  ResourceRefList *fu_pin_all_ = nullptr;     // slot resource list
};

//----------------------------------------------------------------------------
// Instance of a single functional unit forwarding statement.
//----------------------------------------------------------------------------
using ForwardToSet = std::vector<std::pair<Identifier *, int>>;

class ForwardStmt : public MdlItem {
public:
  ForwardStmt(const MdlItem &item, Identifier *from_unit, ForwardToSet to_units)
      : from_unit_(from_unit), to_units_(to_units) {}

  Identifier *from_unit() const { return from_unit_; }
  const ForwardToSet &to_units() const { return to_units_; }
  std::string ToString() const;

private:
  Identifier *from_unit_;
  ForwardToSet to_units_;
};

//----------------------------------------------------------------------------
// Instance of a sub-unit referenced in a functional unit.
// This object owns all the data pointed to by member pointers, except for
// the su_template member.
//----------------------------------------------------------------------------
class SubUnitInstance : public MdlItem {
public:
  SubUnitInstance(const MdlItem &item, Identifier *id, ResourceRefList *args,
                  IdList *predicates)
      : MdlItem(item), id_(id), args_(args), predicates_(predicates) {}
  SubUnitInstance(const MdlItem &item, Identifier *id)
      : MdlItem(item), id_(id), args_(new ResourceRefList),
        predicates_(nullptr) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  ResourceRefList *args() const { return args_; }
  IdList *predicates() const { return predicates_; }
  void set_template(SubUnitTemplate *temp) { su_template_ = temp; }
  SubUnitTemplate *get_template() { return su_template_; }

private:
  Identifier *const id_ = nullptr;         // name of subunit template
  ResourceRefList *const args_ = nullptr;  // arguments passed to the instance
  IdList *const predicates_ = nullptr;     // predicates guarding instance
  SubUnitTemplate *su_template_ = nullptr; // link to subunit template
};

//----------------------------------------------------------------------------
// Instance of a latency referenced in a subunit.
// This object owns all the data pointed to by member pointers, except for
// the lat_template member.
//----------------------------------------------------------------------------
class LatencyInstance : public MdlItem {
public:
  LatencyInstance(const MdlItem &item, Identifier *id, ResourceRefList *args,
                  IdList *predicates)
      : MdlItem(item), id_(id), args_(args), predicates_(predicates) {}
  explicit LatencyInstance(const std::string name)
      : MdlItem(), id_(new Identifier(name)), args_(new ResourceRefList),
        predicates_(nullptr) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  ResourceRefList *args() const { return args_; }
  IdList *predicates() const { return predicates_; }

  void set_template(LatencyTemplate *temp) { lat_template_ = temp; }
  LatencyTemplate *get_template() { return lat_template_; }

private:
  Identifier *const id_ = nullptr;          // which latency to instantiate
  ResourceRefList *const args_ = nullptr;   // instantiation arguments
  IdList *const predicates_ = nullptr;      // predicates guarding instance
  LatencyTemplate *lat_template_ = nullptr; // link to template
};

//----------------------------------------------------------------------------
// Template parameters for functional units, subunits, and latencies.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
enum ParamType { kParamPort, kParamClass, kParamResource };

class Params : public MdlItem {
public:
  Params(const MdlItem &item, Identifier *id, ParamType type)
      : MdlItem(item), id_(id), type_(type) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  ParamType type() const { return type_; }

  bool IsClass() const { return type_ == kParamClass; }
  bool IsPort() const { return type_ == kParamPort; }
  bool IsResource() const { return type_ == kParamResource; }

private:
  Identifier *const id_ = nullptr;    // name of this parameter
  const ParamType type_ = kParamPort; // port, register class, or resource
};

//----------------------------------------------------------------------------
// Template definition of a functional unit.
// This object owns all the data pointed to by member pointers, except for
// the template definition pointers in the unit_bases_ vector.
//----------------------------------------------------------------------------
class FuncUnitTemplate : public MdlItem {
public:
  FuncUnitTemplate(const MdlItem &item, Identifier *id, IdList *bases,
                   ParamsList *params, IdList *ports, ResourceDefList *res,
                   ConnectList *conn, SubUnitInstList *su)
      : MdlItem(item), id_(id), bases_(bases), params_(params), ports_(ports),
        resources_(res), connections_(conn), subunits_(su) {}
  FuncUnitTemplate(MdlItem &item, Identifier *id, IdList *bases)
      : MdlItem(item), id_(id), bases_(bases), params_(new ParamsList),
        ports_(new IdList), resources_(new ResourceDefList),
        connections_(new ConnectList), subunits_(new SubUnitInstList),
        is_implicitly_defined_(true) {}
  explicit FuncUnitTemplate(Identifier *id)
      : MdlItem(*id), id_(id), bases_(new IdList), params_(new ParamsList),
        ports_(new IdList), resources_(new ResourceDefList),
        connections_(new ConnectList), subunits_(new SubUnitInstList),
        is_implicitly_defined_(true) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  IdList *bases() const { return bases_; }
  ParamsList *params() const { return params_; }
  IdList *ports() const { return ports_; }
  ResourceDefList *resources() const { return resources_; }
  ConnectList *connections() const { return connections_; }
  SubUnitInstList *subunits() const { return subunits_; }
  void clone_params(ParamsList *params) {
    params_ = new ParamsList;
    for (auto *param : *params)
      params_->push_back(new Params(*param));
  }
  void add_subunit_instance(SubUnitInstance *su) { subunits_->push_back(su); }

  void add_base(FuncUnitTemplate *base) { unit_bases_.push_back(base); }
  FuncUnitList &unit_bases() { return unit_bases_; }

  // Note implicitly defined templates, and whether they are referenced.
  bool is_implicitly_defined() const { return is_implicitly_defined_; }
  bool is_referenced() const { return is_referenced_; }
  void set_referenced() { is_referenced_ = true; }

  std::set<std::string> &client_cpus() { return client_cpus_; }
  void add_client_cpu(const std::string &cpu) { client_cpus_.insert(cpu); }

private:
  Identifier *const id_ = nullptr;             // name of this template
  IdList *const bases_ = nullptr;              // base template ids, if any
  ParamsList *params_ = nullptr;               // parameters defined for unit
  IdList *const ports_ = nullptr;              // ports defined in this unit
  ResourceDefList *const resources_ = nullptr; // resources defined locally
  ConnectList *const connections_ = nullptr;   // connect statements in unit
  SubUnitInstList *const subunits_ = nullptr;  // subunits instantiated
  FuncUnitList unit_bases_;                    // functional unit bases
  bool is_implicitly_defined_ = false;         // was unit implicitly defined
  bool is_referenced_ = false;                 // was unit referenced
  std::set<std::string> client_cpus_;          // cpus that use this FU.
};

//----------------------------------------------------------------------------
// Definition of a functional unit template group.
// Each item assigns a name to a group of functional units.
//----------------------------------------------------------------------------
class FuncUnitGroup : public MdlItem {
public:
  FuncUnitGroup(const MdlItem &item, Identifier *id, int buffer_size,
                IdList *members)
      : MdlItem(item), id_(id), buffer_size_(buffer_size), members_(members) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  int buffer_size() const { return buffer_size_; }
  IdList *members() const { return members_; }
  FuncUnitList &fu_members() { return fu_members_; }
  void add_unit(FuncUnitTemplate *unit) { fu_members_.push_back(unit); }

private:
  Identifier *const id_ = nullptr;  // name of the group
  int buffer_size_ = -1;            // size of input buffer
  IdList *const members_ = nullptr; // members of the group
  FuncUnitList fu_members_;         // links to templates
};

//----------------------------------------------------------------------------
// Describes each connect statement in a functional unit template.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class Connect : public MdlItem {
public:
  Connect(const MdlItem &item, Identifier *id, Identifier *rclass,
          ResourceRef *resource)
      : MdlItem(item), id_(id), reg_class_(rclass), resource_(resource) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  Identifier *reg_class() const { return reg_class_; }
  ResourceRef *resource() const { return resource_; }

private:
  Identifier *const id_ = nullptr;        // name of referenced port
  Identifier *const reg_class_ = nullptr; // register class connected to
  ResourceRef *const resource_ = nullptr; // resource being referenced
};

//----------------------------------------------------------------------------
// Description of a single functional unit reservation in subunit templates.
//----------------------------------------------------------------------------
class FuncUnitUse : public MdlItem {
public:
  FuncUnitUse(const MdlItem &item, Identifier *predicate, Identifier *func_unit,
              int cycles)
      : MdlItem(item), predicate_(predicate), func_unit_(func_unit),
        cycles_(cycles) {}

  Identifier *predicate() const { return predicate_; }
  Identifier *func_unit() const { return func_unit_; }
  std::string name() const { return func_unit_->name(); }
  int cycles() const { return cycles_; }
  std::string ToString() const {
    std::string out;
    if (predicate_)
      out += predicate_->name() + ":";
    out += func_unit_->name();
    if (cycles_ != -1)
      out += llvm::formatv("<{0}>", cycles_);
    return out;
  }

private:
  Identifier *predicate_; // optional predicate
  Identifier *func_unit_; // used functional unit name
  int cycles_;            // number of cycles reserved
};

//----------------------------------------------------------------------------
// Template definition of a subunit.
// This object owns all the data pointed to by member pointers, except for
// the su_base member.
//----------------------------------------------------------------------------
class SubUnitTemplate : public MdlItem {
public:
  SubUnitTemplate(const MdlItem &item, Identifier *type, IdList *bases,
                  StringList *regex_bases, ParamsList *params,
                  LatencyInstList *latencies, LatencyTemplate *inline_latency)
      : MdlItem(item), type_(type), bases_(bases), regex_bases_(regex_bases),
        params_(params), latencies_(latencies),
        inline_latency_(inline_latency) {}
  SubUnitTemplate(const std::string type, LatencyInstance *latency,
                  LatencyTemplate *inline_latency)
      : MdlItem(), type_(new Identifier(type)), bases_(nullptr),
        params_(new ParamsList), latencies_(new LatencyInstList(1, latency)),
        inline_latency_(inline_latency) {}

  std::string ToString() const;

  Identifier *type() const { return type_; }
  std::string const &name() const { return type_->name(); }
  IdList *bases() const { return bases_; }
  StringList *regex_bases() const { return regex_bases_; }
  ParamsList *params() const { return params_; }
  LatencyInstList *latencies() const { return latencies_; }
  LatencyTemplate *inline_latency() { return inline_latency_; }

  void add_base(SubUnitTemplate *unit) { unit_bases_.push_back(unit); }
  SubUnitList &unit_bases() { return unit_bases_; }
  void add_derived_subunit(SubUnitTemplate *derived) {
    if (FindItem(derived_subunits_, derived->name()))
      return;
    derived_subunits_.push_back(derived);
  }
  SubUnitList &derived_subunits() { return derived_subunits_; }

  int use_count() const { return use_count_; }
  void inc_use() { use_count_++; }

private:
  Identifier *const type_ = nullptr;                // type of this subunit
  IdList *const bases_ = nullptr;                   // base subunits (or empty)
  StringList *const regex_bases_ = nullptr;         // matching instructions
  ParamsList *const params_ = nullptr;              // unit parameters
  LatencyInstList *const latencies_ = nullptr;      // which latencies to use
  LatencyTemplate *const inline_latency_ = nullptr; // inline latency template
  SubUnitList unit_bases_;                          // link to base templates
  SubUnitList derived_subunits_;                    // derived subunits
  int use_count_ = 0;                               // was it ever referenced?
};

//----------------------------------------------------------------------------
// A class which traces which functional units are explicitly referenced
// in a latency template.  The map is indexed by CPU name or Functional unit
// name for predicated references, and by the empty string for unconditional
// references.
//----------------------------------------------------------------------------
class LatencyFuncUnits {
  std::map<std::string, std::set<std::string>> func_units_;

 public:
  void AddUnit(std::string func_unit, std::string predicate) {
    func_units_[predicate].insert(func_unit);
  }
  void AddUnit(std::string func_unit, IdList *predicates) {
    if (predicates == nullptr)
      AddUnit(func_unit, "");
    else
      for (auto *predicate : *predicates)
        AddUnit(func_unit, predicate->name());
  }
  void AddBaseUnits(LatencyFuncUnits *base) {
    for (auto &[predicate, units] : base->func_units_)
      func_units_[predicate].insert(units.begin(), units.end());
  }
  std::map<std::string, std::set<std::string>> &func_units() {
    return func_units_;
  }
};

//----------------------------------------------------------------------------
// Template definition of a latency.
// This object owns all the data pointed to by member pointers, except for
// the lat_base member.
//----------------------------------------------------------------------------
class LatencyTemplate : public MdlItem {
public:
  LatencyTemplate(const MdlItem &item, Identifier *id, IdList *bases,
                  ParamsList *params, ReferenceList *refs)
      : MdlItem(item), id_(id), base_ids_(bases), params_(params),
        references_(refs) {}
  LatencyTemplate(std::string name, ReferenceList *refs)
      : MdlItem(), id_(new Identifier(name)), base_ids_(nullptr),
        params_(new ParamsList), references_(refs) {}

  std::string ToString() const;

  Identifier *id() const { return id_; }
  std::string const &name() const { return id_->name(); }
  IdList *base_ids() const { return base_ids_; }
  ParamsList *params() const { return params_; }
  ReferenceList *references() const { return references_; }

  LatencyFuncUnits *referenced_fus() { return referenced_fus_; }
  void set_referenced_fus(LatencyFuncUnits *fus) { referenced_fus_ = fus; }

  void add_base(LatencyTemplate *temp) { unit_bases_.push_back(temp); }
  LatencyList &unit_bases() { return unit_bases_; }

private:
  Identifier *const id_ = nullptr;              // which latency to instantiate
  IdList *const base_ids_ = nullptr;            // base latencies (or empty)
  ParamsList *const params_ = nullptr;          // parameters for this unit
  ReferenceList *const references_ = nullptr;   // all refs in template
  LatencyList unit_bases_;                      // links to base templates
  LatencyFuncUnits *referenced_fus_ = nullptr;  // set of referenced FUs
};

//----------------------------------------------------------------------------
// Description of an instruction operand reference, used in latency
// rules to explicitly reference an operand, and for immediate operands
// in phase expressions. This object owns all the data pointed to by member
// pointers.
//----------------------------------------------------------------------------
class OperandRef : public MdlItem {
public:
  OperandRef(const MdlItem &item, Identifier *type, IdList *names)
      : MdlItem(item), op_type_(type), op_names_(names) {}
  OperandRef(Identifier *type, IdList *names, int operand_index)
      : MdlItem(), op_type_(type), op_names_(names),
        operand_index_(operand_index) {}
  OperandRef(Identifier *type, IdList *names, RegisterClass *reg_class)
      : MdlItem(), op_type_(type), op_names_(names), reg_class_(reg_class) {}
  OperandRef(Identifier *type, IdList *names, RegisterClass *reg_class,
             int operand_index)
      : MdlItem(), op_type_(type), op_names_(names), reg_class_(reg_class),
        operand_index_(operand_index) {}
  explicit OperandRef(std::string default_name)
      : MdlItem(), op_type_(new Identifier(default_name)),
        op_names_(new IdList({new Identifier(default_name)})) {}

  std::string ToString() const;

  Identifier *op_type() const { return op_type_; }
  IdList *op_names() const { return op_names_; }
  std::string const &name() const { return (*op_names_)[0]->name(); }
  std::string type_name() const;

  int operand_index() const { return operand_index_; }
  void set_operand_index(int index) { operand_index_ = index; }

  OperandDef *operand() const { return operand_; }
  void set_operand(OperandDef *operand) { operand_ = operand; }
  RegisterClass *reg_class() const { return reg_class_; }
  void set_regclass(RegisterClass *reg_class) { reg_class_ = reg_class; }
  OperandDecl *operand_decl() const { return operand_decl_; }
  void set_operand_decl(OperandDecl *decl) { operand_decl_ = decl; }

private:
  // Basic information that reflects directly what was in the input spec.
  Identifier *const op_type_ = nullptr; // name of operand type (or null)
  IdList *const op_names_ = nullptr;    // names of operand and suboperands

  // The type of a reference can be either an operand type or a register class.
  // These link the reference to one of those object types.
  OperandDef *operand_ = nullptr;      // pointer to associated operand type
  RegisterClass *reg_class_ = nullptr; // pointer to associated register class

  // Links to more detailed information about how the reference is used.
  // This information is generated when we generate instruction information.
  int operand_index_ = -1;              // index of operand in instruction
  OperandDecl *operand_decl_ = nullptr; // pointer to operand declaration
};

//----------------------------------------------------------------------------
// Description of an expression used to specify a pipeline phase in a
// latency rule. This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
enum PhaseOp {
  kPlus,
  kMinus,
  kMult,
  kDiv,
  kNeg,
  kPositive,
  kPhase,
  kInt,
  kOpnd
};

class PhaseExpr : public MdlItem {
public:
  PhaseExpr(const MdlItem &item, PhaseOp op, PhaseExpr *left, PhaseExpr *right)
      : MdlItem(item), operation_(op), left_(left), right_(right) {}
  PhaseExpr(const MdlItem &item, PhaseOp op, int number)
      : MdlItem(item), operation_(op), number_(number) {}
  PhaseExpr(const MdlItem &item, PhaseOp op, Identifier *phase)
      : MdlItem(item), operation_(op), phase_(phase) {}
  PhaseExpr(const MdlItem &item, PhaseOp op, OperandRef *operand)
      : MdlItem(item), operation_(op), operand_(operand) {}
  PhaseExpr(Identifier *name, PhaseName *phase)
      : MdlItem(), operation_(kPhase), phase_name_(phase), phase_(name) {}
  explicit PhaseExpr(PhaseName *phase)
      : MdlItem(), operation_(kPhase), phase_name_(phase),
        phase_(new Identifier(phase->name())) {}
  explicit PhaseExpr(int phase) : MdlItem(), operation_(kInt), number_(phase) {}

  PhaseExpr *clone() {
    if (operation() == kPhase)
      return new PhaseExpr(phase_, phase_name_);
    if (operation() == kInt)
      return this;
    if (operation() == kOpnd)
      return new PhaseExpr(*this, kOpnd, new OperandRef(*operand_));

    PhaseExpr *nleft = left() ? left()->clone() : nullptr;
    PhaseExpr *nright = right() ? right()->clone() : nullptr;
    return new PhaseExpr(*this, operation(), nleft, nright);
  }

  static PhaseExpr *DefaultLatency() { return new PhaseExpr(-1); }
  bool IsDefaultLatency() { return operation_ == kInt && number_ == -1; }

  // Methods for evaluating and checking validity/const-ness of expressions.
  bool IsExpressionConstant() const;
  llvm::ErrorOr<int> EvaluateConstantExpression() const;

  int ConstantPhase() {
    if (!IsExpressionConstant())
      return -1;
    auto result = EvaluateConstantExpression();
    if (!result)
      return -1;
    return *result;
  }

  // Are two phase expressions identical?
  // We don't handle the general case of operand references here, those are
  // considered unequal if they have different operand ids.
  bool operator==(const PhaseExpr &item) {
    if (operation_ != item.operation_)
      return false;
    if (left_ && item.left_ && *left_ != *item.left_)
      return false;
    if (right_ && item.right_ && *right_ != *item.right_)
      return false;
    if (operation_ == PhaseOp::kPhase)
      return phase_name_ == item.phase_name_;
    if (operation_ == PhaseOp::kInt)
      return number_ == item.number_;
    if (operation_ == PhaseOp::kOpnd)
      return operand_ && item.operand_ &&
             operand_->operand_index() == item.operand_->operand_index();
    return true;
  }
  bool operator!=(const PhaseExpr &item) { return !(*this == item); }

  // Add a small constant to a phase expression.
  PhaseExpr *increment(int increment) {
    if (increment == 0)
      return this->clone();
    return new PhaseExpr(*this, PhaseOp::kPlus, this->clone(),
                         new PhaseExpr(*this, kInt, increment));
  }
  std::string ToString() const;
  std::string FormatProtection() const {
    auto *phase_name = GetPhaseName();
    if (phase_name == nullptr)
      return "";
    return phase_name->FormatProtection();
  }
  bool IsProtected() const { return GetPhaseName()->is_protected(); }
  bool IsUnprotected() const { return !GetPhaseName()->is_protected(); }

  PhaseOp operation() const { return operation_; }
  PhaseExpr *left() const { return left_; }
  PhaseExpr *right() const { return right_; }
  int number() const { return number_; }
  OperandRef *operand() const { return operand_; }
  Identifier *phase() const { return phase_; }
  int phase_id() const { return phase_name_->index(); }
  PhaseName *GetPhaseName() const;
  bool HasPhaseName() const;
  void set_phase_name(PhaseName *name) { phase_name_ = name; }

private:
  const PhaseOp operation_;          // operation of the expression
  PhaseExpr *const left_ = nullptr;  // child operations
  PhaseExpr *const right_ = nullptr; // child operations
  PhaseName *phase_name_ = nullptr;  // Pointer to phase name item.
  union {
    const int number_;          // integer constant
    OperandRef *const operand_; // reference to an instruction operand
    Identifier *const phase_;   // reference to a phase name
  };
};

//----------------------------------------------------------------------------
// Enumerate the kinds of predicate expressions we support.
//----------------------------------------------------------------------------
enum class PredOp {
  kCheckAny,               // Compound OR predicate
  kCheckAll,               // Compound AND predicate
  kCheckNot,               // Logical NOT predicate
  kCheckOpcode,            // Check instruction against a list of opcodes
  kCheckIsRegOperand,      // Check that an operand is a register
  kCheckRegOperand,        // Check that an operand is a particular register
  kCheckInvalidRegOperand, // Check that an operand is an invalid register
  kCheckSameRegOperand,    // Check if two operands are the same register
  kCheckIsImmOperand,      // Check that an operand is an immediate
  kCheckImmOperand,        // Check for a particular immediate operand
  kCheckZeroOperand,       // Check that an operand is zero
  kCheckFunctionPredicate, // Function to call to implement predicate
  kCheckFunctionPredicateWithTII, // Function to call to implement predicate
  kCheckNumOperands, // Check that an instr has some number of opnds
  kOpcodeSwitchStmt, // Switch statement
  kOpcodeSwitchCase, // Single case statement
  kReturnStatement,  // Switch return statement
  kCode,             // String representing C code
  kTrue,             // Predicate always returns TRUE
  kFalse,            // Predicate always returns FALSE
  kEmpty,            // Undefined predicate
  kName,             // Register name, Predicate name, Opcode Name, etc
  kNumber,           // An integer operand
  kOperandRef,       // A named operand reference.
  kString,           // A string operand
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
constexpr auto kOperand = "Operand";
constexpr auto kString = "String";
constexpr auto kCode = "Code";
constexpr auto kTrue = "TruePred";
constexpr auto kFalse = "FalsePred";
constexpr auto kEmpty = "Empty";

//----------------------------------------------------------------------------
// Definition of a predicate expression.
//----------------------------------------------------------------------------
class PredExpr : public MdlItem {
public:
  explicit PredExpr(PredOp opcode) : MdlItem(), opcode_(opcode) {}
  PredExpr(const MdlItem &item, PredOp opcode)
      : MdlItem(item), opcode_(opcode) {}
  PredExpr(const MdlItem &item, PredOp opcode, std::string value)
      : MdlItem(item), opcode_(opcode), value_(value) {
        if (IsInteger()) int_value_ = std::stoi(value);
      }
  PredExpr(const MdlItem &item, PredOp opcode, OperandRef *opnd)
      : MdlItem(item), opcode_(opcode), opnd_(opnd) {}
  PredExpr(const MdlItem &item, PredOp opcode, PredExpr *opnd)
      : MdlItem(item), opcode_(opcode), operands_({opnd}) {}
  PredExpr(const MdlItem &item, PredOp opcode, std::vector<PredExpr *> &opnds)
      : MdlItem(item), opcode_(opcode), operands_(opnds) {}
  PredExpr(PredOp opcode, bool negate) : MdlItem(), opcode_(opcode) {
    if (negate)
      opcode = IsTrue() ? PredOp::kFalse : PredOp::kTrue;
  }

  PredOp opcode() const { return opcode_; }
  void set_opcode(PredOp opcode) { opcode_ = opcode; }
  std::string value() const { return value_; }
  OperandRef *opnd() const { return opnd_; }
  std::vector<PredExpr *> &operands() { return operands_; }
  std::string PredName();
  std::string ToString(int indent);
  bool negate() const { return negate_; }
  void set_negate() { negate_ = !negate_; }
  void reset_negate() { negate_ = false; }
  int64_t int_value() const { return int_value_; }

  bool IsTrue() const { return opcode_ == PredOp::kTrue; }
  bool IsFalse() const { return opcode_ == PredOp::kFalse; }
  bool IsEmpty() const { return opcode_ == PredOp::kEmpty; }
  bool IsInteger() const { return opcode_ == PredOp::kNumber; }
  bool IsName() const { return opcode_ == PredOp::kName; }
  bool IsString() const { return opcode_ == PredOp::kString; }
  bool IsOperand() const { return opcode_ == PredOp::kOperandRef; }
  bool IsPred() const { return opcode_ <= PredOp::kName; }
  bool IsIntegerOrOperand() const { return IsInteger() || IsOperand(); }

  // Functions to generate code for reference predicates.
  std::string GetOperand(PredExpr *index) const;
  std::string OperandType() const;
  std::string InvalidRegOperand() const;
  std::string RegOperand(const std::string &family) const;
  std::string SameRegOperand() const;
  std::string ImmOperand() const;
  std::string ImmZeroOperand() const;
  std::string FunctionPredicate(bool withTII, OutputState *spec) const;
  std::string NumOperands() const;
  std::string CheckCompound(OutputState *spec);
  std::string CheckCode(OutputState *spec) const;

private:
  PredOp opcode_;
  bool negate_ = false;                // perform a logical NOT of operation
  int64_t int_value_;                  // Integer value of this op
  union {                              // Variant versions of the op
    std::string value_;                // Value of this op
    OperandRef *opnd_;                 // Reference to a named operand
    std::vector<PredExpr *> operands_; // Operands of this op (0..n)
  };
};

//----------------------------------------------------------------------------
// Description of a single latency rule as described in MDL.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class Reference : public MdlItem {
public:
  // This constructor is used by visitors to create a new basic reference.
  Reference(const MdlItem &item, IdList *predicates, RefType ref_type,
            PhaseExpr *phase_expr, int repeat, int delay, int use_cycles,
            OperandRef *operand, ResourceRefList *resources)
      : MdlItem(item), predicates_(predicates), conditional_ref_(nullptr),
        ref_type_(ref_type), phase_expr_(phase_expr), use_cycles_(use_cycles),
        repeat_(repeat), delay_(delay), operand_(operand),
        resources_(resources) {}

  // This constructor creates a conditional reference.
  Reference(const MdlItem &item, IdList *predicates, ConditionalRef *ref)
      : MdlItem(item), predicates_(predicates), conditional_ref_(ref),
        resources_(new ResourceRefList) {}

  // This constructor is used to generate RefFus entries, which includes
  // an explicit "micro_op" value.
  Reference(MdlItem &item, IdList *predicates, RefType ref_type,
            PhaseExpr *phase_expr, int cycles, int micro_ops,
            RefFlags::Item fu_flags, ResourceRef *unit)
      : MdlItem(item), predicates_(predicates), ref_type_(ref_type),
        phase_expr_(phase_expr), use_cycles_(cycles), repeat_(0), delay_(0),
        micro_ops_(micro_ops), fu_flags_(fu_flags),
        resources_(new ResourceRefList(1, unit)) {}
  // This constructor creates a RefFus entry that -only- has a micro-op.
  Reference(MdlItem &item, IdList *predicates, int micro_ops,
            RefFlags::Item fu_flags)
      : MdlItem(item), predicates_(predicates), ref_type_(RefTypes::kFus),
        phase_expr_(new PhaseExpr(new PhaseName("E1"))), phase_value_(1),
        use_cycles_(0), repeat_(0), delay_(0), micro_ops_(micro_ops),
        fu_flags_(fu_flags), resources_(new ResourceRefList) {}

  // This constructor creates default references to a "pseudo" functional unit
  // for instructions which have no functional unit specifications.
  Reference(RefType ref_type, PhaseName *phase, std::string func_unit_name)
      : MdlItem(), ref_type_(ref_type), phase_expr_(new PhaseExpr(phase)),
        resources_(new ResourceRefList(1, new ResourceRef(func_unit_name))) {}

  // This constructor is used while instantiating subunits to create a
  // copy of a latency reference. We don't copy normal resources at this
  // point, since they need to be bound to template parameters, and this is
  // done in the caller.  We do copy functional unit resource references tho.
  Reference(Reference *item, PhaseExpr *phase)
      : MdlItem(*item), predicates_(item->predicates()),
        conditional_ref_(item->conditional_ref()), ref_type_(item->ref_type()),
        phase_expr_(phase ? phase : item->phase_expr()),
        use_cycles_(item->use_cycles()), repeat_(item->repeat()),
        delay_(item->delay()), micro_ops_(item->micro_ops()),
        fu_flags_(item->fu_flags()), operand_(item->operand()),
        resources_(new ResourceRefList), port_(nullptr), base_(item) {
    if (item->IsFuncUnitRef() && !item->resources_->empty())
      resources_->push_back(new ResourceRef(*(*item->resources_)[0]));
  }

  // This constructor is used while instantiating subunits to create a copy
  // of a conditional latency reference.
  Reference(Reference *item, ConditionalRef *cond)
      : MdlItem(*item), predicates_(item->predicates()), conditional_ref_(cond),
        ref_type_(item->ref_type()), phase_expr_(item->phase_expr()),
        resources_(new ResourceRefList) {}

  // This constructor is used when creating the instruction database, and
  // we want to specialize operand references to the instruction they are
  // associated with. Note that we don't need to copy repeat and delay values.
  Reference(Reference &item, int delay)
      : MdlItem(item), predicates_(item.predicates()),
        conditional_ref_(item.conditional_ref()), ref_type_(item.ref_type()),
        phase_expr_(item.phase_expr() ? item.phase_expr()->increment(delay)
                                      : nullptr),
        use_cycles_(item.use_cycles()), micro_ops_(item.micro_ops()),
        fu_flags_(item.fu_flags()),
        operand_(item.operand() ? new OperandRef(*item.operand()) : nullptr),
        resources_(new ResourceRefList), port_(item.port()), base_(&item) {
    for (auto *ref : *item.resources())
      resources_->push_back(new ResourceRef(*ref));
  }

  Reference(RefType ref_type, PhaseExpr *phase, OperandRef *operand)
      : MdlItem(), ref_type_(ref_type), phase_expr_(phase->clone()),
        operand_(operand), resources_(new ResourceRefList) {}
  Reference(RefType ref_type, PhaseName *phase, OperandRef *operand)
      : MdlItem(), ref_type_(ref_type), phase_expr_(new PhaseExpr(phase)),
        operand_(operand), resources_(new ResourceRefList) {}

  std::string ToString() const;
  IdList *predicates() const { return predicates_; }
  ConditionalRef *conditional_ref() const { return conditional_ref_; }
  bool IsConditionalRef() const { return conditional_ref_ != nullptr; }

  PhaseExpr *phase_expr() const { return phase_expr_; }
  void set_phase_expr(PhaseName *name) { phase_expr_ = new PhaseExpr(name); }

  int use_cycles() const { return use_cycles_; }
  int repeat() const { return repeat_; }
  int delay() const { return delay_; }
  int micro_ops() const { return micro_ops_; }
  int fu_flags() const { return fu_flags_; }
  bool is_unbuffered() const { return buffer_size_ == 0; }
  bool is_in_order() const { return buffer_size_ == 1; }
  bool is_out_of_order() const { return buffer_size_ > 1; }
  void set_buffer_size(int buffer_size) { buffer_size_ = buffer_size; }
  int get_buffer_size() const { return buffer_size_; }

  OperandRef *operand() const { return operand_; }
  ResourceRefList *resources() const { return resources_; }

  RefType ref_type() const { return ref_type_; }
  void set_ref_type(RefType type) { ref_type_ = type; }

  bool IsOperandRefType() const {
    return ref_type_ > RefTypes::kNull && ref_type_ < RefTypes::kHold;
  }
  bool IsResourceRef() const {
    return ref_type_ == RefTypes::kHold || ref_type_ == RefTypes::kReserve;
  }
  bool IsDef() const { return ref_type_ & RefTypes::kDef; }
  bool IsUse() const { return ref_type_ & RefTypes::kUse; }
  bool IsFuncUnitRef() const { return ref_type_ == RefTypes::kFus; }

  RefType AdjustResourceReferenceType() const {
    if (ref_type_ & (RefTypes::kHold | RefTypes::kReserve | RefTypes::kFus))
      return ref_type_;
    return !operand() ? ref_type_ : RefTypes::kUse;
  }
  bool IsDefaultOperandRef() {
    return operand_ && operand_->operand_index() == -1;
  }

  void add_resource(ResourceRef *res) { resources_->push_back(res); }
  void add_port(ResourceDef *port) { port_ = port; }
  ResourceDef *port() const { return port_; }
  void set_used() {
    this->used_ = true;
    for (auto *item = this; item->base_; item = item->base_)
      item->base_->used_ = true;
  }
  bool used() const { return used_; }
  void set_seen() {
    this->seen_ = true;
    for (auto *item = this; item->base_; item = item->base_)
      item->base_->seen_ = true;
  }
  bool seen() const { return seen_; }

  // References are ordered by pipeline phase, then by reference type.
  // If the pipeline phase is non-trivial, its value is -1, and ordered last.
  bool operator<(const Reference &item) const {
    if (phase_value_ != item.phase_value_) {
      if (phase_value_ == -1)
        return false;
      if (item.phase_value_ == -1)
        return true;
      return phase_value_ < item.phase_value_;
    }
    if (phase_expr_ != nullptr && item.phase_expr_ != nullptr)
      return phase_expr_->ToString() < item.phase_expr_->ToString();

    if (ref_type() != item.ref_type())
      return ref_type_ < item.ref_type_;

    if (operand() != nullptr && item.operand() != nullptr &&
        operand()->operand_index() != item.operand()->operand_index())
      return operand()->operand_index() < item.operand()->operand_index();

    return ToString() < item.ToString();
  }
  bool operator>(const Reference &item) const { return item < *this; }

  void SetConstantPhase() { phase_value_ = phase_expr_->ConstantPhase(); }
  bool IsProtected() const { return phase_expr_->IsProtected(); }
  bool IsUnprotected() const { return phase_expr_->IsUnprotected(); }
  bool IsDuplicate() const { return is_duplicate_; }
  void SetDuplicate() { is_duplicate_ = true; }

private:
  IdList *const predicates_ = nullptr;         // list of predicates for rule
  ConditionalRef *conditional_ref_ = nullptr;  // if/then/else reference
  RefType ref_type_ = RefTypes::kNull;         // type of reference
  PhaseExpr *phase_expr_ = nullptr;            // pipeline phase of reference
  int32_t phase_value_ = -1;                   // phase if expression is const
  int use_cycles_ = 1;                         // # cycles resource is used
  int repeat_ = 1;                             // default repeat count
  int delay_ = 1;                              // default repeat delay cycles
  int micro_ops_ = 0;                          // Fus entry micro ops
  int buffer_size_ = -1;                       // Fus reference is buffered
  RefFlags::Item fu_flags_ = RefFlags::kNone;  // Fus reference attributes
  OperandRef *const operand_ = nullptr;        // operand we are referencing
  ResourceRefList *const resources_ = nullptr; // resources we are referencing
  ResourceDef *port_ = nullptr;                // port we are referencing
  Reference *base_ = nullptr;                  // base ref for copied objects
  bool used_ = false;                          // was this reference ever used?
  bool seen_ = false;                          // ever considered for a SU?
  bool is_duplicate_ = false;                  // duplicate resource reference?
};

//---------------------------------------------------------------------------
// Describe a conditional reference, corresponding to an if/then/else
// latency statement.
//---------------------------------------------------------------------------
class ConditionalRef : public MdlItem {
public:
  ConditionalRef(MdlItem &item, Identifier *predicate, ReferenceList *refs,
                 ConditionalRef *else_clause)
      : MdlItem(item), predicate_(predicate), refs_(*refs),
        else_clause_(else_clause) {}
  // This constructor is used to copy conditional references when we
  // instantiate a latency reference.
  explicit ConditionalRef(ConditionalRef *item, ConditionalRef *else_clause)
      : MdlItem(*item), predicate_(item->predicate_),
        instr_predicate_(item->instr_predicate_), else_clause_(else_clause) {}

  Identifier *predicate() const { return predicate_; }
  PredExpr *instr_predicate() const { return instr_predicate_; }
  void SetInstrPredicate(PredExpr *pred) { instr_predicate_ = pred; }

  ReferenceList &refs() { return refs_; }
  ConditionalRef *else_clause() const { return else_clause_; }

  // Determine if there is a single operand ref in a conditional clause.
  // We don't support anything in the clause except Uses, Defs, and Fus.
  // If found, return it.
  Reference *FindSingleRef() const {
    Reference *found = nullptr;
    for (auto *ref : refs_)
      if (ref->IsDef() || ref->IsUse()) {
        if (found) return nullptr;
        if (!ref->operand()) return nullptr;
        if (ref->use_cycles() != 1) return nullptr;
        if (!ref->resources()->empty()) return nullptr;
        found = ref;
      } else if (!ref->IsFuncUnitRef()) return nullptr;
    return found;
  }

  bool IsSingleOperand(Reference *ref) const {
    if (auto *found = FindSingleRef()) {
      if (found->ref_type() != ref->ref_type()) return false;
      if (found->operand()->name() != ref->operand()->name()) return false;
      if (!else_clause()) return true;
      return else_clause()->IsSingleOperand(ref);
    }
    return false;
  }

  // Return true if all ConditionalRefs in this object reference same operand.
  // We need at least one else clause to make this worthwhile.
  bool IsSingleOperand() const {
    if (auto *ref = FindSingleRef())
      if (else_clause())
        return else_clause()->IsSingleOperand(ref);
    return false;
  }

  // Return true if a conditional reference contains any operand refs.
  bool HasOperandRefs() {
    if (HasOperandRefs(refs_))
      return true;
    if (else_clause_ != nullptr)
      return else_clause_->HasOperandRefs();
    return false;
  }
  // Return true if a set of references contains any operand refs.
  bool HasOperandRefs(ReferenceList &refs) {
    for (auto *ref : refs_) {
      if (ref->IsConditionalRef() && ref->conditional_ref()->HasOperandRefs())
        return true;
      if (ref->operand() != nullptr)
        return true;
    }
    return false;
  }

  // Return true if a conditional reference contains any resource refs.
  bool HasResourceRefs() {
    if (HasResourceRefs(refs_))
      return true;
    if (else_clause_ != nullptr)
      return else_clause_->HasResourceRefs();
    return false;
  }

  // Return true if a set of references contains any operand refs.
  bool HasResourceRefs(ReferenceList &refs) {
    for (auto *ref : refs_) {
      if (ref->IsFuncUnitRef())
        return true;
      if (ref->IsConditionalRef() && ref->conditional_ref()->HasResourceRefs())
        return true;
      if (!ref->resources()->empty())
        return true;
    }
    return false;
  }

  std::string ToString(bool is_else);

private:
  Identifier *predicate_ = nullptr;       // Named predicate
  PredExpr *instr_predicate_ = nullptr;   // predicate expression
  ReferenceList refs_;                    // list of conditional refs
  ConditionalRef *else_clause_ = nullptr; // else clause of if stmt
};

//---------------------------------------------------------------------------
// Describe a container for instruction information needed by the database
// generation code. For each instruction, we need:
//    - The instruction name - the name used in the target compiler backend.
//    - For each operand (in the order defined in the compiler backend)
//        - The operand type name (immediate, register class, etc).
//        - The operand name.
//    - The subunit name as defined in the target's mdl file.
//    - (Future) What processors it's valid for.
//
// The instruction name must match the symbolic name used for the
// instruction in the target compiler backend. For LLVM, this would
// correspond to the tablegen-generated emumerated name for each
// instruction. This is used by the mdl compiler to connect generated
// information with target instruction ids in the back-end.
//
// The order of operands must match the order of operands as defined by
// the compiler back-end - the MDL compiler generates code that can
// access these operands by index.
//
// The operand names are arbitrary, but ideally these are generated from
// the actual backend description file (such as td files for LLVM). The
// mdl latency rules use these names to refer to specific operands.
//
// The operand type names should also match the target compiler
// instruction descriptions. For registers, it should match a defined
// register class. Others are some kind of constant value.
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Description of a single operand declaration.
//----------------------------------------------------------------------------
class OperandDecl : public MdlItem {
public:
  OperandDecl(const MdlItem &item, Identifier *type, Identifier *name,
              bool ellipsis, bool input, bool output)
      : MdlItem(item), types_(new IdList({type})), names_(new IdList({name})),
        ellipsis_(ellipsis), input_(input), output_(output) {}

  OperandDecl(OperandDecl *item, OperandDecl *parent)
      : MdlItem(),
        types_(new IdList((*parent->types_).begin(), (*parent->types_).end())),
        names_(new IdList((*parent->names_).begin(), (*parent->names_).end())),
        is_implied_register_(item->is_implied_register_),
        ellipsis_(parent->ellipsis_), input_(parent->input_),
        output_(parent->output_), operand_(item->operand_),
        reg_class_(item->reg_class_) {}

  // Set this to true if we want to see more detail (for debugging).
  const bool print_fully_qualified_declaration_ = true;
  std::string ToString() const;

  std::string const &name() const { return (*names_)[0]->name(); }
  std::string const type_name() const { return (*types_)[0]->name(); }
  Identifier *type() const { return (*types_)[0]; }
  IdList *types() const { return types_; }
  Identifier *base_type() const { return types_->back(); }
  Identifier *op_name() const { return (*names_)[0]; }
  IdList *op_names() const { return names_; }
  void add_type(Identifier *type) { types_->push_back(type); }
  void add_name(Identifier *name) { names_->push_back(name); }
  bool is_implied_register() const { return is_implied_register_; }
  bool is_ellipsis() const { return ellipsis_; }
  bool is_input() const { return input_; }
  bool is_output() const { return output_; }

  OperandDef *operand() const { return operand_; }
  RegisterClass *reg_class() const { return reg_class_; }
  void set_operand(OperandDef *operand) { operand_ = operand; }
  void set_regclass(RegisterClass *reg_class) { reg_class_ = reg_class; }
  void set_is_implied_register() { is_implied_register_ = true; }

private:
  IdList *types_ = nullptr;          // type(s) of operand
  IdList *names_ = nullptr;          // name(s) of operand
  bool is_implied_register_ = false; // is this operand an implied register?
  bool ellipsis_ = false;            // was this operand an ellipsis?
  bool input_ = false;               // was the operand tagged as an input?
  bool output_ = false;              // was the operand tagged as an output?

  OperandDef *operand_ = nullptr;      // pointer to associated operand type
  RegisterClass *reg_class_ = nullptr; // pointer to associated register class
};

//----------------------------------------------------------------------------
// Description of a single operand definition.
//----------------------------------------------------------------------------
class OperandDef : public MdlItem {
public:
  OperandDef(const MdlItem &item, Identifier *name, OperandDeclList *operands,
             Identifier *type, OperandAttributeList *attributes, IdList *bases)
      : MdlItem(item), name_(name), bases_(bases), operands_(operands),
        type_(type), attributes_(attributes),
        base_operands_(new OperandDefList) {}

  std::string ToString() const;
  std::string const &name() const { return name_->name(); }
  OperandDeclList *operands() const { return operands_; }
  Identifier const *type() const { return type_; }
  IdList *bases() const { return bases_; }
  OperandDefList *base_operands() const { return base_operands_; }
  void add_base_operand(OperandDef *base) { base_operands_->push_back(base); }
  OperandAttributeList *attributes() const { return attributes_; }
  bool IsDerivedOperand() const { return bases_ != nullptr; }

private:
  Identifier *name_ = nullptr;                 // name of the operand
  IdList *bases_ = nullptr;                    // base operands (for derived)
  OperandDeclList *operands_ = nullptr;        // list of operand declarations
  Identifier *type_ = nullptr;                 // type of operand
  OperandAttributeList *attributes_ = nullptr; // attributes defined
  OperandDefList *base_operands_ = nullptr;    // base, if this opnd has one
};

//----------------------------------------------------------------------------
// Description of a single operand attribute definition.
// Currently, attributes must have integer values.  We could extend this if
// necessary.
//----------------------------------------------------------------------------
class OperandAttribute : public MdlItem {
public:
  OperandAttribute(MdlItem &item, Identifier *name, std::vector<int> *values,
                   std::string type, PredValueList *predicate_values,
                   IdList *predicate)
      : MdlItem(item), name_(name), values_(values), type_(type),
        predicate_values_(predicate_values), predicate_(predicate) {}

  std::string ToString() const;
  std::string const &name() const { return name_->name(); }
  std::vector<int> *values() const { return values_; }
  int values(int i) const { return (*values_)[i]; }
  std::string type() const { return type_; }
  IdList *predicate() const { return predicate_; }
  PredValueList *predicate_values() const { return predicate_values_; }

private:
  Identifier *name_ = nullptr;                // name of attribute
  std::vector<int> *values_;                  // integer values of attribute
  std::string type_;                          // type of operand value
  PredValueList *predicate_values_ = nullptr; // predicate values (if any)
  IdList *predicate_ = nullptr;               // attribute predicate
};

//----------------------------------------------------------------------------
// Description of a single predicated value for operand attributes. A value
// is an integer, a range of values, or a mask.
//----------------------------------------------------------------------------
class PredValue : public MdlItem {
public:
  enum PredValueType { kValue, kRange, kMask };

  PredValue(MdlItem &item, uint64_t mask)
      : MdlItem(item), type_(kMask), mask_(mask) {}
  PredValue(MdlItem &item, int64_t low, int64_t high)
      : MdlItem(item), type_(kRange), low_(low), high_(high) {
    if (low == high)
      type_ = kValue;
  }

  std::string ToString() const;
  bool IsRange() const { return type_ == kRange; }
  bool IsValue() const { return type_ == kValue; }
  bool IsMask() const { return type_ == kMask; }

  // Pretty print a predicate value.
  std::string FormatValue(int64_t value) const;

  int64_t value() const { return low_; }
  int64_t low() const { return low_; }
  int64_t high() const { return high_; }
  uint64_t mask() const { return mask_; }

private:
  PredValueType type_;         // is this a value, range, or mask
  uint64_t mask_ = 0;          // mask bits
  int64_t low_ = 0, high_ = 0; // range of values (or value if same)
};

//----------------------------------------------------------------------------
// Description of a single instruction.
//----------------------------------------------------------------------------
class InstructionDef : public MdlItem {
public:
  InstructionDef(const MdlItem &item, Identifier *name,
                 OperandDeclList *operands, IdList *subunits, IdList *derived,
                 bool has_ellipsis)
      : MdlItem(item), name_(name), operands_(operands),
        subunits_(subunits ? subunits : new IdList), derived_(derived),
        has_ellipsis_(has_ellipsis) {}

  std::string ToString() const;
  std::string const &name() const { return name_->name(); }
  IdList *subunits() const { return subunits_; }
  OperandDeclList *operands() const { return operands_; }
  OperandDeclList *flat_operands() const { return flat_operands_; }
  IdList *derived() const { return derived_; }
  void set_flat_operands(OperandDeclList *opnds) { flat_operands_ = opnds; }
  bool has_ellipsis() const { return has_ellipsis_; }
  int num_operands() const { return operands()->size(); }
  int num_flat_operands() const { return flat_operands()->size(); }

  // Get the operand declaratopm of the nth operand.
  // Note: variable arguments never have declared types.
  OperandDecl *GetOperandDecl(unsigned index) const {
    if (index >= flat_operands_->size())
      return nullptr;
    return (*flat_operands_)[index];
  }

  // Get the operand type of the nth operand.
  OperandDef *GetOperandType(int index) const {
    return GetOperandDecl(index)->operand();
  }
  void add_subunit(SubUnitTemplate *subunit) {
    if (FindItem(*subunits_, subunit->name()))
      return;
    subunits_->push_back(new Identifier(subunit->name()));
  }

private:
  Identifier *name_ = nullptr;               // name of the instruction
  OperandDeclList *operands_ = nullptr;      // list of operand declarations
  OperandDeclList *flat_operands_ = nullptr; // flattened operand declarations
  IdList *subunits_ = nullptr;               // subunits this instr uses
  IdList *derived_ = nullptr;                // instrs derived from this one
  bool has_ellipsis_ = false;                // true if instruction has varargs
};

//----------------------------------------------------------------------------
// Capture a single functional unit instantiation and the context of how
// it was instantiated (CPU, Cluster, Parent FU).
//----------------------------------------------------------------------------
class FuncUnitInstantiation {
public:
  // This constructor is for instantiating top-level functional units.
  FuncUnitInstantiation(MdlSpec *spec, CpuInstance *cpu,
                        ClusterInstance *cluster, FuncUnitInstance *instance)
      : spec_(spec), cpu_(cpu), cluster_(cluster), instance_(instance),
        func_type_(instance->get_template()) {
    InstantiateLocalDefs();
  }

  // This constructor is for instantiating base functional units, which
  // reuse ports, resources, and the instance of the parent functional unit.
  FuncUnitInstantiation(FuncUnitInstantiation *fu, FuncUnitTemplate *base)
      : spec_(fu->spec()), cpu_(fu->cpu()), cluster_(fu->cluster()),
        instance_(fu->instance()),
        func_type_(base), // Note - not instance->get_template()!!
        resource_args_(fu->resource_args()), class_args_(fu->class_args()),
        parent_(fu) {
    InstantiateLocalDefs();
  }

  // Error check a merged resource reference that has an allocation.
  ResourceRef *CheckAllocation(ResourceRef *def, ResourceRef *ref);
  // Create a merged resource reference from a definition and a reference.
  ResourceRef *MergeRefs(ResourceRef *def, ResourceRef *ref);
  // Return the template for this instantiation.
  FuncUnitTemplate *get_template() { return func_type_; }
  // Create definition objects for locally define references and ports.
  void InstantiateLocalDefs();
  // Look up a register class in the templates parameter list.
  RegisterClass *FindRegClass(Identifier *item);
  // Bind a functional unit instantiation parameter to a register class.
  void BindClassArg(ResourceRef *arg);
  // Bind a functional unit instantiation parameter to a resource reference.
  void BindResourceArg(ResourceRef *arg);
  // Map a functional unit instantiation parameter id to its bound resource.
  ResourceRef *GetResourceArg(int param_id);
  // Map a functional unit instantation parameter id to its bound class.
  RegisterClassRef *GetClassArg(int param_id);
  // Determine if a predicate matches the instantiation context's cpu name or
  // functional unit name.  Return true if its valid.
  bool ValidPredicate(IdList *predicates) const;
  // For each subunit instance in a functional unit instantiation, create a
  // subunit instantiation, bind its instance parameters, and instantiate
  // all of its latency instances.
  void InstantiateSubunits();
  // For each connect statement, find the connected resources and register
  // classes, annotate the associated port.
  void ProcessConnects();
  // Bind a subunit port argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceDef *BindSubUnitPort(ResourceRef *arg);
  // Bind a subunit resource argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceRef *BindSubUnitResource(ResourceRef *arg);
  // Bind a functional unit instance resource argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceRef *BindFuncUnitResource(ResourceRef *arg);
  // Bind a functional unit instance register class argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  RegisterClassRef *BindFuncUnitClass(ResourceRef *arg);

  // Bind functional unit instantiation parameters to resources and classes.
  void BindFuncUnitParameters();
  // Bind any slot pinning resources.
  void BindFuncUnitSlotResources();
  // Bind subunit instantiation parameters to ports and resources.
  void BindSubUnitParameters(SubUnitInstantiation *su);

  // Error logging - forward error messages to MdlSpec logger.
  template <typename... Ts>
  bool ErrorLog(const MdlItem *item, const char *fmt, Ts... params) const;
  template <typename... Ts>
  void WarningLog(const MdlItem *item, const char *fmt, Ts... params) const;
  int ErrorsSeen() const;

  // Debug - dump a functional unit instantiation.
  void DumpFuncUnitInstantiation();

  std::string const &name() const { return instance_->name(); }
  MdlSpec *spec() const { return spec_; }
  CpuInstance *cpu() const { return cpu_; }
  ClusterInstance *cluster() const { return cluster_; }
  FuncUnitInstance *instance() const { return instance_; }
  FuncUnitTemplate *func_type() const { return func_type_; }
  ResourceRefDict &resource_args() { return resource_args_; }
  RegisterClassRefDict &class_args() { return class_args_; }
  ResourceDefList &resources() { return resources_; }
  ResourceDefList &ports() { return ports_; }
  FuncUnitInstantiation *parent() const { return parent_; }

  // Create an implicit resource for this instance.
  void set_resource() { fu_resource_ = new ResourceDef(instance_->id(), this); }
  ResourceDef *get_resource() const { return fu_resource_; }
  ResourceDef *get_root_resource() const {
    auto *item = this;
    for (; item->parent_ != nullptr; item = item->parent_)
      ;
    return item->fu_resource_;
  }
  // Get this instance's implied resource, and all of its parents' resource.
  ResourceDefList get_resources() const {
    ResourceDefList resources;
    resources.push_back(fu_resource_);
    for (auto *parent = parent_; parent; parent = parent->parent_)
      resources.push_back(parent->fu_resource_);
    return resources;
  }

private:
  MdlSpec *spec_;                      // pointer to entire file description
  CpuInstance *cpu_;                   // the parent CPU
  ClusterInstance *cluster_;           // the parent cluster
  FuncUnitInstance *instance_;         // the functional unit instance
  FuncUnitTemplate *func_type_;        // the functional unit type, or base
  ResourceRefDict resource_args_;      // resource arguments to instance
  RegisterClassRefDict class_args_;    // register class arguments to instance
  ResourceDefList resources_;          // resources defined for this instance
  ResourceDefList ports_;              // ports defined for this instance
  ResourceDef *fu_resource_ = nullptr; // implicit resource for this FU
  FuncUnitInstantiation *parent_ = nullptr; // Parent functional unit
};


//----------------------------------------------------------------------------
// Capture a single subunit instantiation and the context of how it was
// instantiated.
//----------------------------------------------------------------------------
class SubUnitInstantiation {
public:
  SubUnitInstantiation(FuncUnitInstantiation *func, SubUnitInstance *subunit)
      : spec_(func->spec()), func_unit_(func), subunit_(subunit) {
    su_template_ = subunit->get_template();
  }

  // Return the implicit functional unit resource associated with this instance.
  ResourceDefList GetFuncUnitResources() const {
    return func_unit()->get_resources();
  }

  // Return slots resources associated with this subunit.
  ResourceRefList *GetSlotResourcesAny() const {
    return func_unit()->instance()->get_resource_slots_any();
  }
  ResourceRefList *GetSlotResourcesAll() const {
    return func_unit()->instance()->get_resource_slots_all();
  }
  // Bind a port definition to the associated instantiation parameter.
  void BindPortArg(ResourceRef *arg);
  // Bind a resource definition to the associated instantiation parameter.
  void BindResourceArg(ResourceRef *arg);
  // Map a subunit instantiation parameter id to its bound resource.
  ResourceRef *GetResourceArg(int param_id);
  // Map a subunit instantiation parameter id to its bound port.
  ResourceDef *GetPortArg(int param_id);

  // Determine if a latency predicate matches the instantiation context's
  // cpu name or functional unit name.
  bool ValidPredicate(IdList *predicates) const;
  // Bind a latency latency instance port argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceDef *BindLatPort(ResourceRef *arg);
  // Bind a latency resource argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceRef *BindLatResource(ResourceRef *arg);
  // Bind latency instantation parameters to ports and resources.
  void BindLatencyParams(LatencyInstantiation *lat);
  // Bind latency reference resources to latency template parameters.
  void BindLatencyResources(LatencyInstantiation &lat, Reference *reference,
                            ResourceRefList *resources);
  ConditionalRef *CopyLatencyCondReference(LatencyInstantiation &lat,
                                           ConditionalRef *cond);
  void CopyLatencyReference(LatencyInstantiation &lat,
                            ReferenceList &references, Reference *ref);
  // Add references from a single latency template to a subunit instantiation.
  void InstantiateLatency(LatencyInstantiation &lat,
                          LatencyTemplate *lat_template);
  // Add references from a parent latency to a subunit instantiation, then
  // add all of its bases, recursively.
  void InstantiateLatencyBases(LatencyInstantiation &lat,
                               LatencyTemplate *parent, LatencyList &bases);
  // Instantiation all the latencies (and latency bases) associated with
  // a subunit instantiation.
  void InstantiateLatencies();

  // Error logging - forward error messages to MdlSpec logger.
  template <typename... Ts>
  bool ErrorLog(const MdlItem *item, const char *fmt, Ts... params) const;
  template <typename... Ts>
  void WarningLog(const MdlItem *item, const char *fmt, Ts... params) const;
  int ErrorsSeen() const;

  // Debug: dump all subunit instantiations.
  void DumpSubUnitInstantiation();

  MdlSpec *spec() const { return spec_; }
  CpuInstance *cpu() const { return func_unit_->cpu(); }
  FuncUnitInstantiation *func_unit() const { return func_unit_; }
  SubUnitInstance *subunit() const { return subunit_; }
  SubUnitTemplate *su_template() const { return su_template_; }
  ResourceRefDict &resource_args() { return resource_args_; }
  ResourceDefDict &port_args() { return port_args_; }
  ReferenceList &references() { return references_; }

private:
  MdlSpec *spec_;                    // pointer to entire file description
  FuncUnitInstantiation *func_unit_; // context of this subunits instantiation
  SubUnitInstance *subunit_;         // the subunit instance
  SubUnitTemplate *su_template_;     // the template for this subunit
  ResourceRefDict resource_args_;    // resource arguments to this instance
  ResourceDefDict port_args_;        // port arguments to this instance
  ReferenceList references_;         // instantiated list of references
};

//----------------------------------------------------------------------------
// Capture a single latency instantiation and the context of how it was
// instantiated.
//----------------------------------------------------------------------------
class LatencyInstantiation {
public:
  LatencyInstantiation(SubUnitInstantiation *su, LatencyInstance *latency)
      : subunit_(su), latency_(latency) {
    lat_template_ = latency_->get_template();
  }

  // Bind a resource definition to the associated instantiation parameter.
  void BindResourceArg(ResourceRef *arg);
  // Bind a port definition to the associated instantiation parameter.
  void BindPortArg(ResourceRef *arg);
  // Map a latency instantiation parameter to its bound resource.
  ResourceRef *GetResourceArg(int param_id);
  // Map a latency instantiation parameter to its bound resource.
  ResourceDef *GetPortArg(int param_id);
  // Debug: dump this latency instantiation.
  void DumpLatencyInstantiation();

  SubUnitInstantiation *subunit() const { return subunit_; }
  LatencyInstance *latency() const { return latency_; }
  LatencyTemplate *lat_template() const { return lat_template_; }
  ResourceRefDict &resource_args() { return resource_args_; }
  ResourceDefDict &port_args() { return port_args_; }

private:
  SubUnitInstantiation *subunit_; // context of this instantiation
  LatencyInstance *latency_;      // latency instance
  LatencyTemplate *lat_template_; // template for this latency
  ResourceRefDict resource_args_; // resource arguments to this instance
  ResourceDefDict port_args_;     // port arguments to this instance
};

//----------------------------------------------------------------------------
// Container that captures all the contents of a machine description.
// MdlSpec owns all of these vectors and their contents.
//----------------------------------------------------------------------------
class MdlSpec {
public:
  MdlSpec(bool print_warnings, bool warnings_are_fatal)
      : print_warnings_(print_warnings),
        warnings_are_fatal_(warnings_are_fatal) {
    AddBuiltinPredicates();
  }

  void AddSubUnitInstantiation(SubUnitInstantiation *su) {
    su_instantiations_[su->subunit()->name()]->push_back(su);
  }

  // Create default subunits to instructions that don't have subunits.
  void CheckInstructionSubunits();

  // Add a subunit instance to a "catchall" functional unit, and add it to the
  // specified cpu.
  void AddSubunitToCpu(CpuInstance *cpu, SubUnitTemplate *subunit);

  // Scan latency templates to find which functional units they reference,
  // then tie each client subunit to any referenced functional units.
  void FindLatencyFuncUnits(ReferenceList *references,
                                             LatencyFuncUnits &func_units);
  void FindLatencyFuncUnits(LatencyTemplate *lat);
  void FindFunctionalUnitClientCpus(FuncUnitTemplate *funit, CpuInstance *cpu);
  void FindFunctionalUnitClientCpus();
  void TieSubUnitsToFunctionalUnits();

  // Tie a subunit to a set of instructions that match a set of
  // regular expressions.
  void TieSubUnitToInstructions(SubUnitTemplate *su, StringList *regex_bases);
  // Tie a derived subunit to any instruction associated with any of its bases.
  void TieDerivedSubUnitsToInstructions();

  // Check that the input spec has some basic required components.
  void CheckInputStructure();

  // Create a function unit instance object and add to the functional unit
  // instance table.
  FuncUnitInstantiation *AddFuncUnitInstantiation(CpuInstance *cpu,
                                                  ClusterInstance *cluster,
                                                  FuncUnitInstance *fu_inst);

  // Create a base function unit instance object and add to table.
  FuncUnitInstantiation *
  AddFuncUnitBaseInstantiation(FuncUnitInstantiation *parent,
                               FuncUnitTemplate *base);

  // Create dictionaries for functional units, subunits, and latencies.
  // We don't care here about duplicate names (checked separately).
  // Also build instance tables for functional units and subunits.
  void BuildDictionaries();
  void FindImplicitFuncUnitTemplates();
  void FindValidPredicateNames();
  void IsValidPredicateName(const Identifier *name);

  // First-round semantic checking of the input machine description.
  void SameParams(const ParamsList *params, const ParamsList *base_params,
                  MdlItem *item);
  void ValidateArgs(const ParamsList *params, const ResourceRefList *instance,
                    MdlItem *item);
  void CheckForDuplicateDefs();
  void CheckTemplateBases();

  bool ExpandGroup(FuncUnitGroup *group, IdList *members, unsigned depth);
  void CheckInstantiations();
  void CheckIssueSlots();
  void CheckInstructions();
  void CheckOperand(OperandDecl *operand_decl);
  bool CheckRecursiveOperands(OperandDef *opnd, OperandDefList &seen);
  void CheckOperandDerivations(OperandDef *opnd);
  void CheckOperands();
  void CheckConditionalReferences(ConditionalRef *cond_ref);
  void CheckReferences();
  void CheckReferenceUse();                      // Look for unused references.
  void CheckSubunitUse();                        // Look for unused subunits.
  void CheckResourceDef(const ResourceDef *def); // Check a single resource.
  void CheckResourceDefs(); // Make sure shared pools are properly declared.
  void CheckResourceUse();  // Look for suspect resource use.

  // Add global resource definitions to each CPU.
  void PromoteGlobalResources();
  // Scan resource definitions for CPUs, Clusters, and Functional Unit
  // Templates and promote any group member to a general resource.
  void PromoteResourceGroupMembers(ResourceDefList *resources,
                                   ResourceDefList *outer_scope,
                                   ResourceRefDict *args);
  void PromoteFuncUnitGroupAregs(ClusterInstance *cluster);
  void PromoteResourceGroups();
  void CheckPromotedMember(ResourceDef *group, Identifier *member,
                           ResourceDef *promoted);

  void FlattenOperand(OperandDecl *opnd, OperandDeclList *flat_ops);
  void FlattenInstructionOperands();
  void CheckPhaseDefinitions(PipeDefList *pipes);
  void SpecializePhaseExpr(PhaseExpr *expr, CpuInstance *cpu);
  void CheckReferencePhases(ReferenceList *refs);
  void CheckPipeReferences();
  void CheckPipeReference(ResourceDef *def, CpuInstance *cpu);
  bool CheckSubOperands(OperandRef *ref, const Identifier *opnd, int idx);
  PhaseName *SearchPipeReference(Identifier *phase, CpuInstance *cpu);
  PhaseName *FindPipeReference(Identifier *phase, CpuInstance *cpu);

  // Return the first phase of the first pipeline definition.
  PhaseName *FindFirstPhase();
  // Return the first phase identified as an "execute" phase.
  PhaseName *FindFirstExecutePhase(CpuInstance *cpu);

  // Instantiate base functional unit instances.
  void AddFunctionalUnitBases(FuncUnitInstantiation *parent);
  // Instantiate a single functional unit instance.
  void InstantiateFunctionalUnit(CpuInstance *cpu, ClusterInstance *cluster,
                                 FuncUnitInstance *fu);
  // Iterate over the spec and instantiate every functional unit instance.
  void InstantiateFunctionalUnits();
  // For every CPU, build a map of instances for each functional unit template.
  void BuildFuncUnitInstancesMap();
  // Assign ids to every resource.
  void AssignResourceIds();
  // Assign ids to each pooled resource.
  void AssignPoolIds();
  // Debug: Dump every resource id and its context.
  void DumpResourceIds();

  // Print out the entire specification.
  std::string ToString() const;
  // Debug: dump out all subunit instantiations.
  void DumpSubUnitInstantiations();
  // Debug: dump out all functional unit instantiations.
  void DumpFuncUnitInstantiations();
  // Debug: dump out all user-defined predicates.
  void DumpPredicates();

  void AddBuiltinPredicates() {
    std::string ktrue = "TruePred", kfalse = "FalsePred";
    EnterPredicate(ktrue, new PredExpr(PredOp::kTrue));
    EnterPredicate(kfalse, new PredExpr(PredOp::kFalse));
  }

public:
  // Template function to check for duplicate entries in two symbol definition
  // lists. Print an error message for each duplicate found.
  template <typename A, typename B>
  void FindDuplicates(const std::vector<A *> &a, const std::vector<B *> &b) {
    for (auto *a_item : a)
      for (auto *b_item : b)
        if (a_item->name() == b_item->name())
          ErrorLog(
              a_item,
              "Duplicate definition of {0}\n     Previously defined at {1}",
              a_item->name(), b_item->Location());
  }

  // Template function to check a vector of definitions to make sure
  // each name is unique. Print an error message for each duplicate found.
  // Don't bother checking empty names associated with implied operands.
  template <typename A> void FindDuplicates(const std::vector<A *> &items) {
    for (unsigned i = 0; i < items.size(); i++)
      for (unsigned j = i + 1; j < items.size(); j++)
        if (!items[i]->name().empty() && items[i]->name() == items[j]->name()) {
          ErrorLog(
              items[j],
              "Duplicate definition of {0}\n     Previously defined at {1}",
              items[j]->name(), items[i]->Location());
          break; // We only need to find the first duplicate for each item.
        }
  }

  // Check the member list of a resource definition for duplicate members.
  void FindDuplicateMembers(ResourceDefList &items) {
    for (auto *item : items)
      if (item->IsGroupDef())
        FindDuplicates(item->members());
  }

  // Methods for looking up and matching operands in instructions.
  int GetOperandIndex(const InstructionDef *instr, const OperandRef *operand,
                      RefType ref_type);
  bool CompareOpndNames(const OperandDecl *opnd, const IdList &names);
  int FindOperandName(const InstructionDef *instruct, const IdList &names,
                      RefType type);
  int FindOperand(const InstructionDef *instr, const IdList &name,
                  const std::string &type, RefType ref_type);
  bool FindOperandDerivation(const OperandDef *derived,
                             const OperandDef *operand) const;

  // Accessors (all return references, so can't be const).
  PipeDefList &pipe_phases() { return pipe_phases_; }
  ResourceDefList &resources() { return resources_; }
  RegisterDefList &registers() { return registers_; }
  RegisterClassList &reg_classes() { return reg_classes_; }
  CpuList &cpus() { return cpus_; }
  FuncUnitList &func_units() { return func_units_; }
  FuncUnitGroupList &func_unit_groups() { return func_unit_groups_; }
  SubUnitList &subunits() { return subunits_; }
  LatencyList &latencies() { return latencies_; }
  InstructionList &instructions() { return instructions_; }
  OperandDefList &operands() { return operands_; }

  FuncUnitDict &fu_map() { return fu_map_; }
  FuncUnitGroupDict &fu_group_map() { return fu_group_map_; }
  SubUnitDict &su_map() { return su_map_; }
  LatencyDict &lat_map() { return lat_map_; }
  OperandDict &operand_map() { return operand_map_; }
  InstructionDict &instruction_map() { return instruction_map_; }
  RegisterClassDict &reg_class_map() { return register_class_map_; }
  SubUnitInstantiations &su_instantiations() { return su_instantiations_; }

  bool IsFuncUnitTemplate(const std::string &name) const {
    return fu_map_.count(name);
  }
  bool IsFuncUnitGroup(const std::string &name) const {
    return fu_group_map_.count(name);
  }

  void set_family_name(const Identifier *name) {
    if (!family_name_.empty() && family_name_ != name->name())
      ErrorLog(name, "Incompatible family name specification");
    else
      family_name_ = name->name();
  }
  std::string family_name() const { return family_name_; }

  // Error and Warning management.
  template <typename... Ts>
  bool ErrorLog(const MdlItem *item, const char *fmt, Ts... params);
  bool ErrorLog(const MdlItem *item, const std::string &msg) {
    WriteMessage(item, msg);
    error_count_++;
    return true;
  }

  template <typename... Ts>
  void WarningLog(const MdlItem *item, const char *fmt, Ts... params);
  void WarningLog(const MdlItem *item, const std::string &msg) {
    if (print_warnings_ || warnings_are_fatal_) {
      std::string prefix = !warnings_are_fatal_ ? "Warning: " : "";
      WriteMessage(item, prefix + msg);
      warning_count_++;
    }
  }

  int ErrorsSeen() const {
    return error_count_ + (warnings_are_fatal_ ? warning_count_ : 0);
  }
  int WarningsSeen() const { return warning_count_; }

  // Error logging: Avoid printing identical error messages.
  void WriteMessage(const MdlItem *item, const std::string &msg);

  int PredOperandIndex(const PredExpr *pred, const InstructionDef *instr);

  // Functions to manipulate user-defined predicates. Predicates are defined
  // in an instruction-independent manner, yet in the MDL compiler we apply
  // each predicate to each associated instruction, so that we can partially
  // (and often completely) eliminate the predicate at compiler-build time.
  PredExpr *EvaluatePredicate(std::string name, const InstructionDef *instr);
  PredExpr *EvaluatePredicate(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredSimple(PredExpr *pred, const InstructionDef *instr) {
    return pred;
  }
  PredExpr *PredEvalName(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckAny(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckAll(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckNot(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckOpcode(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckIsReg(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckReg(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckInvalidReg(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckSameReg(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckNumOperand(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckIsImm(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckImm(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredCheckZero(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredOpcodeSwitchStmt(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredOpcodeSwitchCase(PredExpr *pred, const InstructionDef *instr);
  PredExpr *PredReturnStatement(PredExpr *pred, const InstructionDef *instr);

  // Functions to simpify predicates (this largely implements De Morgan's laws
  // on predicate expressions.
  void SimplifyPredicates();
  PredExpr *PredSimplify(PredExpr *expr);

  // Interfaces to the instruction predicate table.
  bool IsValidInstructionPredicate(const std::string &name) const {
    return predicate_table_.count(name);
  }
  // Look up a predicate by name, and return the associated predicate.
  // If the predicate maps to a name, recur on that name.
  PredExpr *LookupPredicate(PredExpr *pred);

  void EnterPredicate(std::string &name, PredExpr *pred) {
    if (!IsValidInstructionPredicate(name)) {
      predicate_table_[name] = pred;
      return;
    }
    if (name == "TruePred" || name == "FalsePred")
      return;
    ErrorLog(pred, "Redefinition of predicate: {0}", name);
  }
  std::map<std::string, PredExpr *> &predicate_table() {
    return predicate_table_;
  }
  bool has_explicit_fu_refs() const { return has_explicit_fu_refs_; }
  void set_explicit_fu_refs() { has_explicit_fu_refs_ = true; }

private:
  std::string family_name_;            // Family name of processors.
  PipeDefList pipe_phases_;            // List of pipe specs defined in mdl.
  ResourceDefList resources_;          // List of resources defined in mdl.
  RegisterDefList registers_;          // List of registers defined in mdl.
  RegisterClassList reg_classes_;      // List of register classes defined.
  CpuList cpus_;                       // List of CPU's defined.
  FuncUnitList func_units_;            // List of functional unit templates.
  FuncUnitGroupList func_unit_groups_; // List of functional unit groups.
  SubUnitList subunits_;               // List of subunit templates defined.
  LatencyList latencies_;              // List of latency templates defined.
  InstructionList instructions_;       // List of instruction definitions.
  OperandDefList operands_;            // List of operand definitions.

  FuncUnitDict fu_map_;             // Dictionary of functional unit templates.
  FuncUnitGroupDict fu_group_map_;  // Dictional of functional unit groups.
  SubUnitDict su_map_;              // Dictionary of subunit templates.
  LatencyDict lat_map_;             // Dictionary of latency templates.
  OperandDict operand_map_;         // Dictionary of operand definitions.
  InstructionDict instruction_map_; // Dictionary of instruction definitions.
  RegisterClassDict register_class_map_;    // Dictionary of register classes.
  SubUnitInstantiations su_instantiations_; // Table of all su instances.

  // Set of all names that can be used as mdl predicates.
  std::unordered_set<std::string> valid_predicate_names_;

  // Dictionary of user-defined predicate expressions, indexed by name.
  std::map<std::string, PredExpr *> predicate_table_;

  // Cache the first phase name found in the spec.
  PhaseName *first_phase_name_ = nullptr; // lowest pipeline phase.

  // Objects to manage error logging.
  std::unordered_set<std::string> error_messages_;
  int error_count_ = 0; // Fatal error count.
  int warning_count_ = 0;
  bool print_warnings_ = true;
  bool warnings_are_fatal_ = false;
  bool has_explicit_fu_refs_ = false;
};

//----------------------------------------------------------------------------
// Error logging template function definitions.
//----------------------------------------------------------------------------
template <typename... Ts>
inline std::string formatv(const char *fmt, Ts &&...vals) {
  return std::string(llvm::formatv(fmt, vals...));
}

template <typename... Ts>
bool SubUnitInstantiation::ErrorLog(const MdlItem *item, const char *fmt,
                                    Ts... params) const {
  return spec()->ErrorLog(item, fmt, params...);
}

template <typename... Ts>
void SubUnitInstantiation::WarningLog(const MdlItem *item, const char *fmt,
                                      Ts... params) const {
  spec()->WarningLog(item, fmt, params...);
}

template <typename... Ts>
bool FuncUnitInstantiation::ErrorLog(const MdlItem *item, const char *fmt,
                                     Ts... params) const {
  return spec()->ErrorLog(item, fmt, params...);
}
template <typename... Ts>
void FuncUnitInstantiation::WarningLog(const MdlItem *item, const char *fmt,
                                       Ts... params) const {
  spec()->WarningLog(item, fmt, params...);
}

template <typename... Ts>
bool MdlSpec::ErrorLog(const MdlItem *item, const char *fmt, Ts... params) {
  return ErrorLog(item, formatv(fmt, params...));
}

template <typename... Ts>
void MdlSpec::WarningLog(const MdlItem *item, const char *fmt, Ts... params) {
  WarningLog(item, formatv(fmt, params...));
}

//----------------------------------------------------------------------------
// External definitions.
//----------------------------------------------------------------------------
void Abort();

extern ResourceRef *NullResourceRef;
extern RegisterClass *NullRegisterClass;
extern ResourceDef *NullPortDef;

bool FindDerivation(OperandDef *ref, const OperandDef *decl,
                    OperandDefList &opnds);
OperandAttribute *FindAttribute(const std::string &name, const OperandDef *opnd,
                                const SubUnitInstantiation *subunit);

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_H_

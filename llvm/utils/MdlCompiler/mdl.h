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
std::ostream &PrintVec(std::ostream &Out, const std::vector<T> *V,
                       std::string Head = "", std::string Sep = "\n",
                       std::string End = "\n") {
  if (!V)
    return Out;
  Out << Head;

  for (auto *Item : *V) {
    Out << *Item;
    if (Item != V->back())
      Out << Sep;
  }
  return Out << End;
}

//----------------------------------------------------------------------------
// Template function for stringizing vectors of pointers to objects.
//----------------------------------------------------------------------------
template <typename T>
std::string StringVec(const std::vector<T> *V, std::string Head = "",
                      std::string Sep = "\n", std::string End = "\n") {
  if (!V)
    return "";
  std::string Out = Head;

  for (auto *Item : *V) {
    Out += Item->ToString();
    if (Item != V->back())
      Out += Sep;
  }

  return Out + End;
}
//----------------------------------------------------------------------------
// Template function for stringizing sets of strings.
//----------------------------------------------------------------------------
template <typename T>
std::string StringSet(const std::set<T> *V, std::string Head = "",
                      std::string Sep = "\n", std::string End = "\n") {
  if (!V)
    return "";
  std::string Out = Head;

  for (auto &Item : *V) {
    Out += Item;
    if (Item != *V->rbegin())
      Out += Sep;
  }

  return Out + End;
}

//----------------------------------------------------------------------------
// Template function to find an MDL item in a vector, by name.
//----------------------------------------------------------------------------
template <typename A>
A *FindItem(std::vector<A *> &Items, const std::string &Name) {
  for (auto *Item : Items)
    if (Item->getName() == Name)
      return Item;
  return nullptr;
}

//----------------------------------------------------------------------------
// Template function to find an MDL item in a map of pointers, by name.
//----------------------------------------------------------------------------
template <typename A>
A *FindItem(std::map<std::string, A *> &Items, const std::string &Name) {
  auto It = Items.find(Name);
  return (It == Items.end()) ? nullptr : It->second;
}

//----------------------------------------------------------------------------
// "Internal" FU names contain a leading colon character, so that they never
// conflict with user-defined names.
//----------------------------------------------------------------------------
inline bool isCatchallName(const std::string &Name) {
  return Name[0] == ':';
}
inline std::string catchallName(const std::string &Name) {
  return llvm::formatv(":{0}", Name);
}

//----------------------------------------------------------------------------
// FU Instances with explicit bases.
//----------------------------------------------------------------------------
inline bool isBasedFuInstance(const std::string &Name) {
  auto Loc = Name.find(':');
  return Loc != 0 && Loc != std::string::npos;
}

inline std::string basedFuInstance(const IdList *Bases) {
  return StringVec<Identifier *>(Bases, "", ":", "");
}

//----------------------------------------------------------------------------
// Define a base class that contains source information for each object.
//----------------------------------------------------------------------------
class MdlItem {
  std::string *FileName = nullptr;    // Pointer to current file name.
  int Line = 0;                       // Lexical line number of this item.
  int Column = 0;                     // Lexical column number of this item.

public:
  MdlItem(std::string *FileName, int Line, int Column) :
    FileName(FileName), Line(Line), Column(Column) {}
  MdlItem() : Line(0), Column(0) {}

  const std::string getFileName() const { return *FileName; }
  int getLine() const { return Line; }
  int getColumn() const { return Column; }
  std::string getLocation() const {
    return llvm::formatv("{0}:{1}:{2}", *FileName, std::to_string(Line),
                         std::to_string(Column + 1));
  }
  void setLocation(std::string *NewFileName, int NewLine, int NewColumn) {
    FileName = NewFileName;
    Line = NewLine;
    Column = NewColumn;
  }
};

//----------------------------------------------------------------------------
// An instance of a name. Used anywhere that an identifier is used
// in the machine description.
//----------------------------------------------------------------------------
class Identifier : public MdlItem {
  const std::string Name;   // Name used anywhere in machine description.
  int Index = -1;           // If in an IdList, its 0-based position in list.

public:
  // Create a general identifier.
  Identifier(const MdlItem &Item, std::string Name)
      : MdlItem(Item), Name(Name) {}
  // Used to create identifiers used in resource groups.
  Identifier(Identifier *Item, int Index)
      : MdlItem(*Item), Name(Item->getName()), Index(Index) {}
  // Used to generate internal names that don't map back to source code.
  explicit Identifier(std::string Name) : MdlItem(), Name(Name) {}

  bool operator!=(const Identifier &Rhs) { return Name != Rhs.getName(); }
  std::string ToString() const;
  std::string const &getName() const { return Name; }
  void setIndex(int NewIndex) { Index = NewIndex; }
  int getIndex() const { return Index; }
  bool isVararg() const { return Name[0] == '$'; }
  int getVarargIndex() const { return std::stoi(Name.substr(1)); }
  bool isNumber() const { return isdigit(Name[0]); }
  int getNumber() const { return std::stoi(Name); }
};

//----------------------------------------------------------------------------
// An instance of a pipe phase name, defined in a pipeline definition.
//----------------------------------------------------------------------------
class PhaseName : public MdlItem {
  const std::string Name;    // name of the pipeline phase
  int Index = -1;            // If in an IdList, its 0-based position in list
  bool IsProtected = true;   // true if this is in a protected pipeline
  bool IsHard = false;       // true if this is in a "hard" pipeline

public:
  PhaseName(const MdlItem &Item, std::string Name, bool IsProtected,
            bool IsHard)
      : MdlItem(Item), Name(Name), IsProtected(IsProtected),
        IsHard(IsHard) {}
  explicit PhaseName(std::string Name) : MdlItem(), Name(Name) {}

  std::string ToString() const;
  std::string formatProtection() const;
  std::string const &getName() const { return Name; }
  void setIndex(int NewIndex) { Index = NewIndex; }
  int getIndex() const { return Index; }
  bool isProtected() const { return IsProtected; }
  bool isUnprotected() const { return !IsProtected; }
  bool isHard() const { return IsHard; }
};

//----------------------------------------------------------------------------
// An instance of a register definition.
//----------------------------------------------------------------------------
class RegisterDef : public MdlItem {
  const Identifier *Id;           // Identifier associated with register.

public:
  RegisterDef(const MdlItem &Item, Identifier *Id) : MdlItem(Item), Id(Id) {}

  std::string ToString() const;
  std::string const &getName() const { return Id->getName(); }
};

//----------------------------------------------------------------------------
// Description of a register class.
//----------------------------------------------------------------------------
class RegisterClass : public MdlItem {
  Identifier *Id;                 // Name of the class.
  RegisterDefList *Members;       // List of registers included in class.

public:
  RegisterClass(const MdlItem &Item, Identifier *Id, RegisterDefList *Members)
      : MdlItem(Item), Id(Id), Members(Members) {}
  explicit RegisterClass(std::string Name) : Id(new Identifier(Name)) {}

  // Return true if decl is a superset of this class.
  bool isSupersetOf(const RegisterClass *Decl) const {
    for (auto *Reg : *Decl->Members)
      if (!FindItem(*Members, Reg->getName()))
        return false;
    return true;
  }
  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  RegisterDefList *getMembers() const { return Members; }
  bool IsNull() const { return getName() == "__"; }
};

//----------------------------------------------------------------------------
// An instance argument which refers to a register class.
//----------------------------------------------------------------------------
class RegisterClassRef : public MdlItem {
  Identifier *Id = nullptr;             // name of the referenced class
  RegisterClass *Regs = nullptr;        // link to the referenced class

public:
  explicit RegisterClassRef(RegisterClass *Item)
      : MdlItem(*Item), Id(Item->getId()), Regs(Item) {}

  std::string ToString() const;
  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  RegisterClass *getRegs() { return Regs; }
};

//----------------------------------------------------------------------------
// Description of a pipeline phase group defined in the MDL.
//      phases <name> { phase1, phase2, ... };
// Phases defined as ranges (E[3..5]) are expanded in this object.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class PipePhases : public MdlItem {
  Identifier *const Id = nullptr;               // name of pipeline phase group
  PhaseNameList *const PhaseNames = nullptr;    // names of each phase
  PhaseName *const FirstExecutePhaseName;       // first execute phase
  const bool IsProtected = true;        // true if the pipeline is protected
  const bool IsHard = false;            // true if the pipeline is hard vs soft

public:
  PipePhases(const MdlItem &Item, Identifier *Id, PhaseNameList *Phases,
             PhaseName *FirstExecutePhase, bool IsProtected, bool IsHard)
      : MdlItem(Item), Id(Id), PhaseNames(Phases),
        FirstExecutePhaseName(FirstExecutePhase),
        IsProtected(IsProtected), IsHard(IsHard) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  PhaseNameList *getPhaseNames() const { return PhaseNames; }
  bool isProtected() const { return IsProtected; }
  bool isHard() const { return IsHard; }
  PhaseName *getFirstExecutePhaseName() const {
    return FirstExecutePhaseName;
  }
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
extern RefType convertStringToRefType(const std::string &Type);
// Format a RefType for debug output.
extern std::string convertRefTypeToString(RefType Type);
// Format a RefType for database generation.
extern std::string formatReferenceType(RefType Type);
// Format an aggregated RefType for database generation.
extern std::string formatReferenceTypes(int Type);
// Format a reference flags field.
extern std::string FormatReferenceFlags(const Reference *Ref);

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

  static bool isProtected(Item Flag) { return Flag & kProtected; }
  static bool isUnprotected(Item Flag) { return Flag & kUnprotected; }
  static bool isDuplicate(Item Flag) { return Flag & kDuplicate; }
  static bool isUnbuffered(Item Flag) { return Flag & kUnbuffered; }
  static bool isOut_of_order(Item Flag) { return Flag & kOutOfOrder; }
  static bool isIn_order(Item Flag) { return Flag & kInOrder; }
  static bool isBeginGroup(Item Flag) { return Flag & kBeginGroup; }
  static bool isEndGroup(Item Flag) { return Flag & kEndGroup; }
  static bool isSingleIssue(Item Flag) { return Flag & kSingleIssue; }
  static bool isRetireOOO(Item Flag) { return Flag & kRetireOOO; }
};

//----------------------------------------------------------------------------
// Resource pools can have subpools, we collect information about them.
// SubPools have an ordering based on how restrictive they are (number of
// resources they qualify for).  The most restrictive pools are allocated
// first.
//----------------------------------------------------------------------------
class SubPool {
  int First;    // id of first member of the subpool
  int Last;     // id of last member of the subpool

public:
  explicit SubPool(const ResourceRef *Res);
  int getFirst() const { return First; }
  int getLast() const { return Last; }
  int getSize() const { return Last - First; }
  bool operator<(const SubPool &Rhs) const {
    return getSize() < Rhs.getSize() ||
           (getSize() == Rhs.getSize() &&
            (getFirst() < Rhs.getFirst() || getLast() < Rhs.getLast()));
  }
  bool operator>(const SubPool &Item) const { return Item < *this; }
  std::string ToString() const;
};

// Information about a single subpool.
class SubPoolInfo {
  int SubpoolId = -1;
  std::set<int> Counts; // the set of all non-zero count requests

public:
  void setSubpoolId(int Id) { SubpoolId = Id; }
  void addCount(int NewCount) {
    if (NewCount)               // Don't add zeros.
      Counts.emplace(NewCount);
  }
  int getSubpoolId() const { return SubpoolId; }
  const std::set<int> &getCounts() const { return Counts; }

  std::string ToString(std::string subpool) const;
};

// For each pooled reference, keep track of how many resources were requested.
using SubPools = std::map<SubPool, SubPoolInfo>;

enum class GroupType { kUseAll, kUseSingle, kUseNull };

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
  Identifier *const Id = nullptr;        // name of the referenced resource
  const int BitSize = -1;                // number of bits represented
  IdList Members;                        // members of a named resource pool
  int PoolSize = 0;                      // number of elements in pool (or 0)
  Identifier *StartPhase = nullptr;      // optional start phase id
  Identifier *EndPhase = nullptr;        // optional end phase id
  int ResourceId = 0;                    // resource index for this object
  int PoolId = -1;                       // id, if resource is first in a pool

  std::vector<ResourceDef *> MemberDefs;
  bool ImplicitGroup = false;            // Is this is an implicit group def?
  GroupType GrpType = GroupType::kUseSingle;

  ResourceRef *PortResource = nullptr;   // resource port is connected to
  RegisterClass *RegClass = nullptr;     // optional constraint for a port
  FuncUnitInstantiation *Fu = nullptr;   // instantated func_unit names

  int EarliestRef = -1;                  // earliest seen reference
  int LatestRef = -1;                    // latest seen reference
  bool PhaseExprSeen = false;            // true if there are phase expressions
  int Types = 0;                         // OR of all seen reference types
  std::string DebugName;                 // pretty name for printing
  std::set<int> AllocSizes;              // set of all pool size requests
  SubPools Pools;                        // Map of all subpools for this pool

public:
  ResourceDef(const MdlItem &Item, Identifier *Id, int Bits, int PoolSize,
              Identifier *Start, Identifier *End)
      : MdlItem(Item), Id(Id), BitSize(Bits), PoolSize(PoolSize),
        StartPhase(Start), EndPhase(End) {}
  ResourceDef(const MdlItem &Item, Identifier *Id, int Bits, IdList *Members,
              Identifier *Start, Identifier *End)
      : MdlItem(Item), Id(Id), BitSize(Bits), Members(*Members),
        StartPhase(Start), EndPhase(End) {}
  ResourceDef(const MdlItem &Item, Identifier *Id) : MdlItem(Item), Id(Id) {}
  ResourceDef(Identifier *const Id, FuncUnitInstantiation *Fu)
      : MdlItem(*Id), Id(Id), Fu(Fu) {}
  explicit ResourceDef(Identifier *const Id) : MdlItem(*Id), Id(Id) {}
  explicit ResourceDef(std::string Name)
      : MdlItem(), Id(new Identifier(Name)) {}

  std::string ToString() const;
  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  IdList &getMembers() { return Members; }
  int getBitSize() const { return BitSize; }
  bool hasSharedBits() const { return BitSize > 0; }
  int getPoolSize() const { return PoolSize; }
  Identifier *getStartPhase() const { return StartPhase; }
  Identifier *getEndPhase() const { return EndPhase; }
  bool isNull() const { return getName() == "__"; }

  bool isPoolDef() const { return isGroupDef() || PoolSize > 0; }
  bool isGroupDef() const { return !Members.empty(); }
  int getSize() const {
    if (isGroupDef()) return Members.size();
    if (isPoolDef()) return PoolSize;
    return 1;
  }

  void setResourceId(int Id) { ResourceId = Id; }
  int getResourceId() const { return ResourceId; }
  int getPoolId() const { return PoolId; }
  void setPoolId(int Id) { PoolId = Id; }
  void addAllocSize(int Size) { AllocSizes.emplace(Size); }
  std::set<int> &getAllocSizes() { return AllocSizes; }
  SubPoolInfo &getSubPool(SubPool &Pool) { return Pools[Pool]; }
  SubPools &getSubPools() { return Pools; }
  void addReferenceSizeToPool(const ResourceRef *Resource, const Reference *Ref,
                              const SubUnitInstantiation *Subunit);

  std::string formatResource();
  int getMemberId(const Identifier *Member) const {
    for (auto *Mem : Members)
      if (Mem->getName() == Member->getName())
        return Mem->getIndex();
    return -1;
  }
  RegisterClass *getRegClass() const { return RegClass; }
  void setRegClass(RegisterClass *Regs) { RegClass = Regs; }
  ResourceRef *getPortResource() const { return PortResource; }
  void setPortResource(ResourceRef *Res) { PortResource = Res; }

  void recordReference(RefType Type, const PhaseExpr *Expr,
                       const ResourceRef *Resource, const Reference *Ref,
                       const SubUnitInstantiation *Subunit);
  void recordFuReference() { Types |= RefTypes::kFus; }

  void setDebugName(std::string Type, const CpuInstance *Cpu,
                      const ClusterInstance *Cluster,
                      const FuncUnitInstantiation *Fu);
  std::string getDebugName() const { return DebugName; }
  std::string getRefSummary() const;
  int getRefTypes() const { return Types; }
  bool isUsed() const { return Types != 0; }
  bool isOnlyHeld() const {
    return (Types & (RefTypes::kHoldReserve)) == RefTypes::kHold;
  }
  bool isOnlyReserved() const {
    return (Types & (RefTypes::kHoldReserve)) == RefTypes::kReserve;
  }

  int getLatestRef() const { return LatestRef; }
  bool phaseExprSeen() const { return PhaseExprSeen; }

  std::vector<ResourceDef *> &getMemberDefs() { return MemberDefs; }
  void addMemberDef(ResourceDef *Def) { MemberDefs.push_back(Def); }
  ResourceDef *getMemberDef(int Index) const { return MemberDefs[Index]; }
  bool isImplicitGroup() const { return ImplicitGroup; }
  void setImplicitGroup() { ImplicitGroup = true; }
  GroupType getGroupType() const { return GrpType; }
  void setGroupType(GroupType Type) { GrpType = Type; }
  void setPoolSize(unsigned Size) { PoolSize = Size; }
  FuncUnitInstantiation *getFu() const { return Fu; }
};

//----------------------------------------------------------------------------
// Use of a resource (used in a functional- or sub-unit instantiation.
//   ... <name> ...                  // Reference entire resource.
//   ... <name>.<member> ...         // Reference a member.
//   ... <name>:<count> ...          // Reference <count> pool members.
//   ... <name>[<first>..<last>]...  // Reference part of a pool.
//----------------------------------------------------------------------------
class ResourceRef : public MdlItem {
  Identifier *Id = nullptr;              // name of the referenced resource
  Identifier *Member = nullptr;          // which member is named (a.b)
  int PoolCount = -1;                    // how many pool members (a:count)
  bool UseAllMembers = false;            // was "resource:*" syntax specified
  Identifier *PoolCountName = nullptr;   // symbolic count of members
  Identifier *ValueName = nullptr;       // name of operand value attribute
  int First = -1, Last = -1;             // subrange of pool (a[2..4] or a[3])
  int MemberId = -1;                     // index of a member reference
  ResourceDef *FuPool = nullptr;       // for pooled fu references

  // Links to related objects.
  ResourceDef *Definition = nullptr;     // link to resource definition
  Params *ArgParameterLink = nullptr;    // arguments are linked to parameters
  int OperandIndex = -1;             // pooled resources are tied to an operand

public:
  ResourceRef(const MdlItem &Item, Identifier *Id) : MdlItem(Item), Id(Id) {}
  // Create a reference to some number of a pooled resource.
  ResourceRef(const MdlItem &Item, Identifier *Id, int PoolCount,
              Identifier *PoolCountName, Identifier *ValueName)
      : MdlItem(Item), Id(Id), PoolCount(PoolCount),
        PoolCountName(PoolCountName), ValueName(ValueName) {}
  // Create a reference to a member of a defined resource (a.member)
  ResourceRef(const MdlItem &Item, Identifier *Id, Identifier *Member)
      : MdlItem(Item), Id(Id), Member(Member) {}
  // Create a reference to a subpool of a pooled resource
  ResourceRef(const MdlItem &Item, Identifier *Id, int First, int last)
      : MdlItem(Item), Id(Id), First(First), Last(last) {}
  explicit ResourceRef(std::string Name)
      : MdlItem(), Id(new Identifier(Name)) {}
  // Create a reference to a defined resource.
  explicit ResourceRef(ResourceDef *Def)
      : MdlItem(*Def), Id(Def->getId()),
        First(Def->getPoolSize() > 0 ? 0 : -1),
        Last(Def->getPoolSize() > 0 ? Def->getPoolSize() - 1 : -1),
        Definition(Def) {}

  std::string ToString() const;
  Identifier *getId() const { return Id; }
  void rename(Identifier *NewId) { Id = NewId; }
  std::string const &getName() const { return Id->getName(); }
  Identifier *getMember() const { return Member; }
  int getPoolCount() const { return PoolCount; }
  Identifier *getPoolCountName() const { return PoolCountName; }

  bool hasCount() const {
    return PoolCount != -1 || PoolCountName != nullptr;
  }
  Identifier *getValueName() const { return ValueName; }

  int getFirst() const { return First; }
  int getLast() const { return Last; }
  int getPoolSize() const {
    if (isGroupDef())
      return Definition->getMembers().size();
    return Last - First + 1;
  }

  bool isNull() const { return getName() == "__"; }
  bool isPool() const { return First != -1; }
  bool isGroupRef() const { return isGroupDef() && !getMember(); }
  bool isGroupDef() const {
    return Definition != nullptr && Definition->isGroupDef();
  }
  bool isImplicitGroup() const {
    return isGroupDef() && Definition->isImplicitGroup();
  }

  bool isArrayDef() const {
    return Definition != nullptr && isPool() && Definition->getPoolSize();
  }
  bool isPooledResourceRef() const {
    return (isGroupRef() || (isArrayDef() && !isIndexed())) && !hasCount();
  }
  bool isUnqualifiedRef() const { return !getMember() && getFirst() == -1; }
  bool hasAllocation() const { return hasCount(); }
  bool hasValueName() const { return ValueName != nullptr; }

  int isSubrange() const { return First != -1 && Last != First; }
  int isIndexed() const { return First != -1 && First == Last; }

  int getMemberId() const { return MemberId; }

  void setFirst(int Value) { First = Value; }
  void setLast(int Value) { Last = Value; }
  void setPoolCount(int Count) { PoolCount = Count; }
  void setPoolCountName(Identifier *Count) { PoolCountName = Count; }
  void setUseAllMembers() { UseAllMembers = true; }
  bool useAllMembers() const { return UseAllMembers; }
  void setValueName(Identifier *Mask) { ValueName = Mask; }
  void setSubrange(int First, int Last) {
    setFirst(First);
    setLast(Last);
  }
  void setFuPool(ResourceDef *Pool) { FuPool = Pool; }
  ResourceDef *getFuPool() const { return FuPool; }
  bool isFuPool() const { return FuPool; }
  ResourceDef *getDefinition() const { return Definition; }
  void setDefinition(ResourceDef *Def) { Definition = Def; }
  ResourceDef *getPortDefinition() const { return Definition; }
  int getResourceId() const {
    return Definition ? Definition->getResourceId() : -1;
  }
  int getFinalResourceId() const {
    if (getFirst() != -1 && getFirst() == getLast())
      return getResourceId() + getFirst();
    return getResourceId();
  }

  Params *getParameter() { return ArgParameterLink; }
  void setParameter(Params *parameter) { ArgParameterLink = parameter; }
  int getOperandIndex() const { return OperandIndex; }
  void setOperandIndex(int Id) { OperandIndex = Id; }
  bool hasOperandIndex() const { return OperandIndex != -1; }
};

//----------------------------------------------------------------------------
// Reflect what is in the MDL for a single processor definition.
// This object owns all the data pointed to by const member pointers.
//----------------------------------------------------------------------------
class CpuInstance : public MdlItem {
  Identifier *const Id = nullptr;                // name of this CPU
  PipeDefList *Phases = nullptr;                 // locally defined pipe phases
  ResourceDefList *const Issues = nullptr;       // issue slot resources
  ResourceDefList *const Resources = nullptr;    // resources defined locally
  int ReorderBufferSize = MCSchedModel::DefaultMicroOpBufferSize;
  ClusterList *const Clusters = nullptr;         // clusters defined
  ForwardStmtList *const ForwardStmts = nullptr; // forward statements
  std::vector<std::string> LLVMNames;            // optional llvm cpu names

  ResourceDefList AllResources;           // all resources defined for CPU
  ResourceDefList PoolResources;          // all pooled resources for CPU
  FuncUnitInstances FuInstances;          // map of templates to instances
  std::set<int> FuPoolSizes;              // set of fu allocation pools
  int InstrCount;                         // number of valid instructions

  int MaxUsedResourceId = 0;              // number of "used" resources
  int MaxResourcePhase = 0;               // latest resource "use"
  int MaxIssue = 0;                       // maximum parallel issue size
  int PoolCount = 0;                      // number of pooled resources
  int MaxPoolAllocation = 0;              // max pool allocation size
  int EarlyUsePhase = -1;                 // earliest named "use" phase
  bool NeedsSlotResources = false;        // True if we must model slots
  int MaxFuId = 0;                        // Id of last func unit
  int MaxResourceId = 0;                  // Id of last resource

public:
  CpuInstance(const MdlItem &Item, Identifier *Id, PipeDefList *Phase,
              ResourceDefList *Issues, ResourceDefList *Res,
              int ReorderBufferSize, ClusterList *Clusters,
              ForwardStmtList *ForwardStmts,
              std::vector<std::string> &LLVMNames)
      : MdlItem(Item), Id(Id), Phases(Phase), Issues(Issues),
        Resources(Res), ReorderBufferSize(ReorderBufferSize),
        Clusters(Clusters), ForwardStmts(ForwardStmts),
        LLVMNames(LLVMNames) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  ResourceDefList *getIssues() const { return Issues; }
  ResourceDefList *getResources() const { return Resources; }
  int getReorderBufferSize() const { return ReorderBufferSize; }
  ClusterList *getClusters() const { return Clusters; }
  ForwardStmtList *getForwardStmts() const { return ForwardStmts; }
  std::vector<std::string> &getLlvmNames() { return LLVMNames; }
  bool needsSlotResources() const { return NeedsSlotResources; }
  void setNeedsSlotResources(bool Value) { NeedsSlotResources = Value; }

  void addCpuResource(ResourceDef *Res, std::string Type,
                      const CpuInstance *Cpu, const ClusterInstance *Cluster,
                      const FuncUnitInstantiation *Fu) {
    AllResources.push_back(Res);
    Res->setDebugName(Type, Cpu, Cluster, Fu);
  }
  void addFuPoolSize(int Size) { FuPoolSizes.insert(Size); }
  std::set<int> &getFuPoolSizes() { return FuPoolSizes; }

  ResourceDefList &getAllResources() { return AllResources; }
  void addPoolResource(ResourceDef *Pool) { PoolResources.push_back(Pool); }
  ResourceDefList &getPoolResources() { return PoolResources; }
  PipeDefList *getPipePhases() { return Phases; }

  int getMaxResourcePhase() const { return MaxResourcePhase; }
  void setMaxUsedResourceId(int Id) { MaxUsedResourceId = Id; }
  int getMaxUsedResourceId() const { return MaxUsedResourceId; }
  void setMaxResourcePhase(int Phase) { MaxResourcePhase = Phase; }
  int getMaxIssue() const { return MaxIssue; }
  void setMaxIssue(int Issue) { MaxIssue = Issue; }
  int getPoolCount() const { return PoolCount; }
  void setPoolCount(int Count) { PoolCount = Count; }
  int getMaxPoolAllocation() const { return MaxPoolAllocation; }
  void setMaxPoolAllocation(int Size) { MaxPoolAllocation = Size; }
  int getEarlyUsePhase() const { return EarlyUsePhase; }
  void setEarlyUsePhase(int Phase) { EarlyUsePhase = Phase; }
  int getLoadPhase() {
    for (const auto *Pipe : *getPipePhases())
      if (const auto *Item = FindItem(*Pipe->getPhaseNames(), "LOAD_PHASE"))
        return Item->getIndex();
    return MCSchedModel::DefaultLoadLatency;
  }
  int getHighLatencyDefPhase() {
    for (const auto *Pipe : *getPipePhases())
      if (const auto *Item = FindItem(*Pipe->getPhaseNames(), "HIGH_PHASE"))
        return Item->getIndex();
    return MCSchedModel::DefaultHighLatency;
  }
  void setMaxFuId(int LastId) { MaxFuId = LastId; }
  int getMaxFuId() const { return MaxFuId; }
  void setMaxResourceId(int LastId) { MaxResourceId = LastId; }
  int getMaxResourceId() const { return MaxResourceId; }
  void setInstrCount(int Count) { InstrCount = Count; }
  int getInstrCount() const { return InstrCount; }

  FuncUnitInstances &getFuncUnitInstances() { return FuInstances; }
};

//----------------------------------------------------------------------------
// Instance of a cluster defined in a processor description.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class ClusterInstance : public MdlItem {
  Identifier *const Id = nullptr;                 // name of this CPU
  ResourceDefList *const Issues = nullptr;        // issue entries, if any
  ResourceDefList *const Resources = nullptr;     // resources defined locally
  FuncUnitInstList *const FuncUnits = nullptr;    // func units instantiated
  ForwardStmtList *const ForwardStmts = nullptr;  // forward statements
  FuncUnitInstantiations FuInstantiations;

public:
  ClusterInstance(const MdlItem &Item, Identifier *Id, ResourceDefList *Issue,
                  ResourceDefList *Res, FuncUnitInstList *Fus,
                  ForwardStmtList *ForwardStmts)
      : MdlItem(Item), Id(Id), Issues(Issue), Resources(Res),
        FuncUnits(Fus), ForwardStmts(ForwardStmts) {}
  explicit ClusterInstance(FuncUnitInstance *FuncUnit)
      : MdlItem(), Id(new Identifier("__")), Issues(new ResourceDefList),
        Resources(new ResourceDefList),
        FuncUnits(new FuncUnitInstList(1, FuncUnit)) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  bool isNull() const { return getName() == "__"; }
  ResourceDefList *getIssues() const { return Issues; }
  ResourceDefList *getResources() const { return Resources; }
  FuncUnitInstList *getFuncUnits() const { return FuncUnits; }
  ForwardStmtList *getForwardStmts() const { return ForwardStmts; }
  void addFuncUnitInstantiation(FuncUnitInstantiation *fu) {
    FuInstantiations.push_back(fu);
  }
  FuncUnitInstantiations &getFuInstantiations() { return FuInstantiations; }
  // Debug: Dump out functional unit instantiations for this cluster.
  void dumpFuncUnitInstantiations();
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
//----------------------------------------------------------------------------
class FuncUnitInstance : public MdlItem {
  Identifier *const Id = nullptr;             // name of this unit (optional)
  Identifier *const Type = nullptr;           // template of this unit
  bool Unreserved = false;                    // Is this an unreserved FU?
  std::map<std::string, int> BufferSizes;     // buffer sizes for each base
  ResourceRefList *const Args = nullptr;      // arguments to this instance
  IdList *const PinAll = nullptr;             // FU needs more than one slot
  IdList *const PinAny = nullptr;             // slots FU can be pinned to
  FuncUnitTemplate *FuTemplate = nullptr;     // link to template
  ResourceRefList *FuPinAny = nullptr;        // slot resource list
  ResourceRefList *FuPinAll = nullptr;        // slot resource list

public:
  FuncUnitInstance(const MdlItem &Item, Identifier *Type, Identifier *Id,
                   bool Unreserved, std::map<std::string, int> BufferSizes,
                   ResourceRefList *Args, IdList *Any, IdList *All)
     : MdlItem(Item), Id(Id), Type(Type), Unreserved(Unreserved),
       BufferSizes(BufferSizes), Args(Args), PinAll(All), PinAny(Any) {}
  explicit FuncUnitInstance(const std::string Type)
     : MdlItem(), Id(new Identifier(Type)), Type(new Identifier(Type)),
       Args(new ResourceRefList), PinAll(nullptr), PinAny(nullptr) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  Identifier *getType() const { return Type; }
  bool isUnreserved() const { return Unreserved; }

  int getBufferSize(std::string Fu) { return BufferSizes[Fu]; }
  bool isUnbuffered(std::string Fu) { return BufferSizes[Fu] == 0; }
  bool isInOrder(std::string Fu) { return BufferSizes[Fu] == 1; }
  bool isOutOfOrder(std::string Fu) { return BufferSizes[Fu] > 1; }

  ResourceRefList *getArgs() const { return Args; }
  IdList *getPinAny() const { return PinAny; }
  IdList *getPinAll() const { return PinAll; }

  void setTemplate(FuncUnitTemplate *Temp) { FuTemplate = Temp; }
  FuncUnitTemplate *getTemplate() const { return FuTemplate; }

  ResourceRefList *getResourceSlotsAny() const { return FuPinAny; }
  ResourceRefList *getResourceSlotsAll() const { return FuPinAll; }
  void setResourceSlotsAny(ResourceRefList *Pins) { FuPinAny = Pins; }
  void setResourceSlotsAll(ResourceRefList *Pins) { FuPinAll = Pins; }

  // "catchall" unit names use a colon followed by their associated CPU name.
  bool isCatchallUnit() const { return isCatchallName(getName()); }
};

//----------------------------------------------------------------------------
// Instance of a single functional unit forwarding statement.
//----------------------------------------------------------------------------
using ForwardToSet = std::vector<std::pair<Identifier *, int>>;

class ForwardStmt : public MdlItem {
  Identifier *FromUnit;
  ForwardToSet ToUnits;

public:
  ForwardStmt(const MdlItem &Item, Identifier *FromUnit, ForwardToSet ToUnits)
      : MdlItem(Item), FromUnit(FromUnit), ToUnits(ToUnits) {}

  Identifier *getFromUnit() const { return FromUnit; }
  const ForwardToSet &getToUnits() const { return ToUnits; }
  std::string ToString() const;
};

//----------------------------------------------------------------------------
// Instance of a sub-unit referenced in a functional unit.
//----------------------------------------------------------------------------
class SubUnitInstance : public MdlItem {
  Identifier *const Id = nullptr;          // name of subunit template
  ResourceRefList *const Args = nullptr;   // arguments passed to the instance
  IdList *const Predicates = nullptr;      // predicates guarding instance
  SubUnitTemplate *SuTemplate = nullptr;   // link to subunit template

public:
  SubUnitInstance(const MdlItem &Item, Identifier *Id, ResourceRefList *Args,
                  IdList *Predicates)
      : MdlItem(Item), Id(Id), Args(Args), Predicates(Predicates) {}
  SubUnitInstance(const MdlItem &Item, Identifier *Id)
      : MdlItem(Item), Id(Id), Args(new ResourceRefList), Predicates(nullptr) {}

  std::string ToString() const;
  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  ResourceRefList *getArgs() const { return Args; }
  IdList *getPredicates() const { return Predicates; }
  void setTemplate(SubUnitTemplate *Temp) { SuTemplate = Temp; }
  SubUnitTemplate *getTemplate() { return SuTemplate; }
};

//----------------------------------------------------------------------------
// Instance of a latency referenced in a subunit.
//----------------------------------------------------------------------------
class LatencyInstance : public MdlItem {
  Identifier *const Id = nullptr;          // which latency to instantiate
  ResourceRefList *const Args = nullptr;   // instantiation arguments
  IdList *const Predicates = nullptr;      // predicates guarding instance
  LatencyTemplate *LatTemplate = nullptr;  // link to template

public:
  LatencyInstance(const MdlItem &Item, Identifier *Id, ResourceRefList *Args,
                  IdList *Predicates)
      : MdlItem(Item), Id(Id), Args(Args), Predicates(Predicates) {}
  explicit LatencyInstance(const std::string Name)
      : MdlItem(), Id(new Identifier(Name)), Args(new ResourceRefList),
        Predicates(nullptr) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  ResourceRefList *getArgs() const { return Args; }
  IdList *getPredicates() const { return Predicates; }

  void setTemplate(LatencyTemplate *Temp) { LatTemplate = Temp; }
  LatencyTemplate *getTemplate() { return LatTemplate; }
};

//----------------------------------------------------------------------------
// Template parameters for functional units, subunits, and latencies.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
enum ParamType { kParamPort, kParamClass, kParamResource };

class Params : public MdlItem {
  Identifier *const Id = nullptr;    // name of this parameter
  const ParamType Type = kParamPort; // port, register class, or resource

public:
  Params(const MdlItem &Item, Identifier *Id, ParamType Type)
      : MdlItem(Item), Id(Id), Type(Type) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  ParamType getType() const { return Type; }

  bool isClass() const { return Type == kParamClass; }
  bool isPort() const { return Type == kParamPort; }
  bool isResource() const { return Type == kParamResource; }
};

//----------------------------------------------------------------------------
// Template definition of a functional unit.
// This object owns all the data pointed to by member pointers, except for
// the template definition pointers in the unit_bases_ vector.
//----------------------------------------------------------------------------
class FuncUnitTemplate : public MdlItem {
  Identifier *const Id = nullptr;               // name of this template
  IdList *const Bases = nullptr;                // base template ids, if any
  ParamsList *FuParams = nullptr;                 // parameters defined for unit
  IdList *const Ports = nullptr;                // ports defined in this unit
  ResourceDefList *const Resources = nullptr;   // resources defined locally
  ConnectList *const Connections = nullptr;     // connect statements in unit
  SubUnitInstList *const Subunits = nullptr;    // subunits instantiated
  FuncUnitList UnitBases;                       // functional unit bases
  bool IsImplicitlyDefined = false;             // was unit implicitly defined
  bool IsReferenced = false;                    // was unit referenced
  std::set<std::string> ClientCpus;             // cpus that use this FU.

public:
  // Create a functional unit template from an MDL description.
  FuncUnitTemplate(const MdlItem &Item, Identifier *Id, IdList *Bases,
                   ParamsList *FuParams, IdList *Ports, ResourceDefList *Res,
                   ConnectList *Conn, SubUnitInstList *Su)
      : MdlItem(Item), Id(Id), Bases(Bases), FuParams(FuParams), Ports(Ports),
        Resources(Res), Connections(Conn), Subunits(Su) {}
  // Create an implicitly defined functional unit template.
  FuncUnitTemplate(MdlItem &Item, Identifier *Id, IdList *Bases)
      : MdlItem(Item), Id(Id), Bases(Bases), FuParams(new ParamsList),
        Ports(new IdList), Resources(new ResourceDefList),
        Connections(new ConnectList), Subunits(new SubUnitInstList),
        IsImplicitlyDefined(true) {}
  explicit FuncUnitTemplate(Identifier *Id)
      : MdlItem(*Id), Id(Id), Bases(new IdList), FuParams(new ParamsList),
        Ports(new IdList), Resources(new ResourceDefList),
        Connections(new ConnectList), Subunits(new SubUnitInstList),
        IsImplicitlyDefined(true) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  IdList *getBases() const { return Bases; }
  ParamsList *getParams() const { return FuParams; }
  IdList *getPorts() const { return Ports; }
  ResourceDefList *getResources() const { return Resources; }
  ConnectList *getConnections() const { return Connections; }
  SubUnitInstList *getSubunits() const { return Subunits; }
  void addSubunitInstance(SubUnitInstance *Su) { Subunits->push_back(Su); }

  void addBase(FuncUnitTemplate *Base) { UnitBases.push_back(Base); }
  FuncUnitList &getUnitBases() { return UnitBases; }

  // Note implicitly defined templates, and whether they are referenced.
  bool isImplicitlyDefined() const { return IsImplicitlyDefined; }
  bool isReferenced() const { return IsReferenced; }
  void setReferenced() { IsReferenced = true; }

  std::set<std::string> &getClientCpus() { return ClientCpus; }
  void addClientCpu(const std::string &Cpu) { ClientCpus.insert(Cpu); }
};

//----------------------------------------------------------------------------
// Definition of a functional unit template group.
// Each item assigns a name to a group of functional units.
//----------------------------------------------------------------------------
class FuncUnitGroup : public MdlItem {
  Identifier *const Id = nullptr;     // name of the group
  int BufferSize = -1;                // size of input buffer
  IdList *const Members = nullptr;    // names of members of the group
  FuncUnitList FuMembers;             // links to members' templates

public:
  FuncUnitGroup(const MdlItem &Item, Identifier *id, int Size, IdList *Members)
      : MdlItem(Item), Id(id), BufferSize(Size), Members(Members) {}

  std::string ToString() const;

  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  int getBufferSize() const { return BufferSize; }
  IdList *getMembers() const { return Members; }
  FuncUnitList &getFuMembers() { return FuMembers; }
  void addUnit(FuncUnitTemplate *Unit) { FuMembers.push_back(Unit); }
};

//----------------------------------------------------------------------------
// Describes each connect statement in a functional unit template.
// This object owns all the data pointed to by member pointers.
//----------------------------------------------------------------------------
class Connect : public MdlItem {
  Identifier *const Id = nullptr;           // name of referenced port
  Identifier *const RegClass = nullptr;     // register class connected to
  ResourceRef *const Resource = nullptr;    // resource being referenced

public:
  Connect(const MdlItem &Item, Identifier *Id, Identifier *RClass,
          ResourceRef *Resource)
      : MdlItem(Item), Id(Id), RegClass(RClass), Resource(Resource) {}

  std::string ToString() const;
  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  Identifier *getRegClass() const { return RegClass; }
  ResourceRef *getResource() const { return Resource; }
};

//----------------------------------------------------------------------------
// Template definition of a subunit.
// This object owns all the data pointed to by member pointers, except for
// the su_base member.
//----------------------------------------------------------------------------
class SubUnitTemplate : public MdlItem {
  Identifier *const Type = nullptr;                 // type of this subunit
  IdList *const Bases = nullptr;                    // base subunits (or empty)
  StringList *const RegExBases = nullptr;           // matching instructions
  ParamsList *const SuParams = nullptr;             // unit parameters
  LatencyInstList *const Latencies = nullptr;       // which latencies to use
  LatencyTemplate *const InlineLatency = nullptr;   // inline latency template
  SubUnitList UnitBases;                            // link to base templates
  SubUnitList DerivedSubunits;                      // derived subunits
  int UseCount = 0;                                 // was it ever referenced?

public:
  SubUnitTemplate(const MdlItem &item, Identifier *type, IdList *bases,
                  StringList *regex_bases, ParamsList *params,
                  LatencyInstList *latencies, LatencyTemplate *inline_latency)
      : MdlItem(item), Type(type), Bases(bases), RegExBases(regex_bases),
        SuParams(params), Latencies(latencies),
        InlineLatency(inline_latency) {}
  SubUnitTemplate(const std::string type, LatencyInstance *latency,
                  LatencyTemplate *inline_latency)
      : MdlItem(), Type(new Identifier(type)), Bases(nullptr),
        SuParams(new ParamsList), Latencies(new LatencyInstList(1, latency)),
        InlineLatency(inline_latency) {}

  std::string ToString() const;
  Identifier *getType() const { return Type; }
  std::string const &getName() const { return Type->getName(); }
  IdList *getBases() const { return Bases; }
  StringList *getRegexBases() const { return RegExBases; }
  ParamsList *getParams() const { return SuParams; }
  LatencyInstList *getLatencies() const { return Latencies; }
  LatencyTemplate *getInlineLatency() { return InlineLatency; }

  void addBase(SubUnitTemplate *unit) { UnitBases.push_back(unit); }
  SubUnitList &getUnitBases() { return UnitBases; }
  void addDerivedSubunit(SubUnitTemplate *derived) {
    if (FindItem(DerivedSubunits, derived->getName()))
      return;
    DerivedSubunits.push_back(derived);
  }
  SubUnitList &getDerivedSubunits() { return DerivedSubunits; }

  int getUseCount() const { return UseCount; }
  void incUse() { UseCount++; }
};

//----------------------------------------------------------------------------
// A class which traces which functional units are explicitly referenced
// in a latency template.  The map is indexed by CPU name or Functional unit
// name for predicated references, and by the empty string for unconditional
// references.
//----------------------------------------------------------------------------
class LatencyFuncUnits {
  std::map<std::string, std::set<std::string>> FuncUnits;

 public:
  void addUnit(std::string FuncUnit, std::string Predicate) {
    FuncUnits[Predicate].insert(FuncUnit);
  }
  void addUnit(std::string FuncUnit, IdList *Predicates) {
    if (Predicates == nullptr)
      addUnit(FuncUnit, "");
    else
      for (auto *Predicate : *Predicates)
        addUnit(FuncUnit, Predicate->getName());
  }
  void addBaseUnits(LatencyFuncUnits *base) {
    for (auto &[Predicate, Units] : base->FuncUnits)
      FuncUnits[Predicate].insert(Units.begin(), Units.end());
  }
  std::map<std::string, std::set<std::string>> &getFuncUnits() {
    return FuncUnits;
  }
};

//----------------------------------------------------------------------------
// Template definition of a latency.
// This object owns all the data pointed to by member pointers, except for
// the lat_base member.
//----------------------------------------------------------------------------
class LatencyTemplate : public MdlItem {
  Identifier *const Id = nullptr;               // which latency to instantiate
  IdList *const BaseIds = nullptr;              // base latencies (or empty)
  ParamsList *const LatParams = nullptr;        // parameters for this unit
  ReferenceList *const References = nullptr;    // all refs in template
  LatencyList UnitBases;                        // links to base templates
  LatencyFuncUnits *ReferencedFus = nullptr;    // set of referenced FUs

public:
  LatencyTemplate(const MdlItem &Item, Identifier *Id, IdList *Bases,
                  ParamsList *LatParams, ReferenceList *Refs)
      : MdlItem(Item), Id(Id), BaseIds(Bases), LatParams(LatParams),
        References(Refs) {}
  LatencyTemplate(std::string Name, ReferenceList *Refs)
      : MdlItem(), Id(new Identifier(Name)), BaseIds(nullptr),
        LatParams(new ParamsList), References(Refs) {}

  std::string ToString() const;
  Identifier *getId() const { return Id; }
  std::string const &getName() const { return Id->getName(); }
  IdList *getBaseIds() const { return BaseIds; }
  ParamsList *getParams() const { return LatParams; }
  ReferenceList *getReferences() const { return References; }

  LatencyFuncUnits *getReferencedFus() { return ReferencedFus; }
  void setReferencedFus(LatencyFuncUnits *fus) { ReferencedFus = fus; }

  void addBase(LatencyTemplate *temp) { UnitBases.push_back(temp); }
  LatencyList &getUnitBases() { return UnitBases; }
};

//----------------------------------------------------------------------------
// Description of an instruction operand reference, used in latency
// rules to explicitly reference an operand, and for immediate operands
// in phase expressions.
//----------------------------------------------------------------------------
class OperandRef : public MdlItem {
  // Basic information that reflects directly what was in the input spec.
  Identifier *const OpType = nullptr;   // name of operand type (or null)
  IdList *const OpNames = nullptr;      // names of operand and suboperands

  // The type of a reference can be either an operand type or a register class.
  // These link the reference to one of those object types.
  OperandDef *Operand = nullptr;        // pointer to associated operand type
  RegisterClass *RegClass = nullptr;    // pointer to associated register class

  // Links to more detailed information about how the reference is used.
  // This information is generated when we generate instruction information.
  int OperandIndex = -1;                // index of operand in instruction
  OperandDecl *Decl = nullptr;          // pointer to operand declaration

public:
  OperandRef(const MdlItem &Item, Identifier *Type, IdList *Names)
      : MdlItem(Item), OpType(Type), OpNames(Names) {}
  OperandRef(Identifier *Type, IdList *Names, int OperandIndex)
      : MdlItem(), OpType(Type), OpNames(Names),
        OperandIndex(OperandIndex) {}
  OperandRef(Identifier *Type, IdList *Names, RegisterClass *RegClass)
      : MdlItem(), OpType(Type), OpNames(Names), RegClass(RegClass) {}
  OperandRef(Identifier *Type, IdList *Names, RegisterClass *RegClass,
             int OperandIndex)
      : MdlItem(), OpType(Type), OpNames(Names), RegClass(RegClass),
        OperandIndex(OperandIndex) {}
  explicit OperandRef(std::string DefaultName)
      : MdlItem(), OpType(new Identifier(DefaultName)),
        OpNames(new IdList({new Identifier(DefaultName)})) {}

  std::string ToString() const;
  Identifier *getOpType() const { return OpType; }
  IdList *getOpNames() const { return OpNames; }
  std::string const &getName() const { return (*OpNames)[0]->getName(); }
  std::string getTypeName() const;

  int getOperandIndex() const { return OperandIndex; }
  void setOperandIndex(int Index) { OperandIndex = Index; }

  OperandDef *getOperand() const { return Operand; }
  void setOperand(OperandDef *Opnd) { Operand = Opnd; }
  RegisterClass *getRegClass() const { return RegClass; }
  void setRegClass(RegisterClass *Regs) { RegClass = Regs; }
  OperandDecl *getOperandDecl() const { return Decl; }
  void setOperandDecl(OperandDecl *NewDecl) { Decl = NewDecl; }
};

//----------------------------------------------------------------------------
// Description of an expression used to specify a pipeline phase in a
// latency rule.  This is very basic, just the typical 4 operators, plus
// a "positive" op that brackets expressions to positive numbers. Terminals
// can be numbers, phase names, or instruction operand names.
//----------------------------------------------------------------------------
enum PhaseOp {
  kPlus, kMinus, kMult, kDiv, kNeg, kPositive,
  kPhase, kInt, kOpnd
};

class PhaseExpr : public MdlItem {
  const PhaseOp Operation;              // operation of the expression
  PhaseExpr *const Left = nullptr;      // child operations
  PhaseExpr *const Right = nullptr;     // child operations
  PhaseName *Phase = nullptr;           // Pointer to phase name item.
  union {
    const int Number;                   // integer constant
    OperandRef *const Operand;          // reference to an instruction operand
    Identifier *const PhaseId;          // reference to a phase name
  };

public:
  // Create a binary operator.
  PhaseExpr(const MdlItem &Item, PhaseOp Op, PhaseExpr *Left, PhaseExpr *Right)
      : MdlItem(Item), Operation(Op), Left(Left), Right(Right) {}
  // Create an expression that represents a number.
  PhaseExpr(const MdlItem &Item, PhaseOp Op, int Number)
      : MdlItem(Item), Operation(Op), Number(Number) {}
  // Create an expression that represents a named phase.
  PhaseExpr(const MdlItem &Item, PhaseOp Op, Identifier *PhaseId)
      : MdlItem(Item), Operation(Op), PhaseId(PhaseId) {}
  // Create an expression that represents a specified operand reference.
  PhaseExpr(const MdlItem &Item, PhaseOp Op, OperandRef *Operand)
      : MdlItem(Item), Operation(Op), Operand(Operand) {}
  // Create an expression that represents a verified phase name.
  PhaseExpr(Identifier *Name, PhaseName *Phase)
      : MdlItem(), Operation(kPhase), Phase(Phase), PhaseId(Name) {}
  explicit PhaseExpr(PhaseName *Phase)
      : MdlItem(), Operation(kPhase), Phase(Phase),
        PhaseId(new Identifier(Phase->getName())) {}
  explicit PhaseExpr(int Phase) : MdlItem(), Operation(kInt), Number(Phase) {}

  PhaseExpr *clone() {
    if (Operation == kPhase)
      return new PhaseExpr(PhaseId, Phase);
    if (Operation == kInt)
      return this;
    if (Operation == kOpnd)
      return new PhaseExpr(*this, kOpnd, new OperandRef(*Operand));

    PhaseExpr *Nleft = Left ? Left->clone() : nullptr;
    PhaseExpr *Nright = Right ? Right->clone() : nullptr;
    return new PhaseExpr(*this, Operation, Nleft, Nright);
  }

  static PhaseExpr *getDefaultLatency() { return new PhaseExpr(-1); }
  bool isDefaultLatency() { return Operation == kInt && Number == -1; }

  // Methods for evaluating and checking validity/const-ness of expressions.
  bool isExpressionConstant() const;
  llvm::ErrorOr<int> evaluateConstantExpression() const;

  int getConstantPhase() {
    if (!isExpressionConstant())
      return -1;
    auto Result = evaluateConstantExpression();
    if (!Result)
      return -1;
    return *Result;
  }

  // Are two phase expressions identical?
  // We don't handle the general case of operand references here, those are
  // considered unequal if they have different operand ids.
  bool operator==(const PhaseExpr &item) {
    if (Operation != item.Operation)
      return false;
    if (Left && item.Left && *Left != *item.Left)
      return false;
    if (Right && item.Right && *Right != *item.Right)
      return false;
    if (Operation == PhaseOp::kPhase)
      return Phase == item.Phase;
    if (Operation == PhaseOp::kInt)
      return Number == item.Number;
    if (Operation == PhaseOp::kOpnd)
      return Operand && item.Operand &&
             Operand->getOperandIndex() == item.Operand->getOperandIndex();
    return true;
  }
  bool operator!=(const PhaseExpr &item) { return !(*this == item); }

  // Add a small constant to a phase expression.
  PhaseExpr *increment(int Increment) {
    if (Increment == 0)
      return this->clone();
    return new PhaseExpr(*this, PhaseOp::kPlus, this->clone(),
                         new PhaseExpr(*this, kInt, Increment));
  }
  std::string ToString() const;
  std::string formatProtection() const {
    auto *Name = getPhaseName();
    if (Name == nullptr)
      return "";
    return Name->formatProtection();
  }
  bool isProtected() const { return getPhaseName()->isProtected(); }
  bool isUnprotected() const { return !getPhaseName()->isProtected(); }

  PhaseOp getOperation() const { return Operation; }
  PhaseExpr *getLeft() const { return Left; }
  PhaseExpr *getRight() const { return Right; }
  int getNumber() const { return Number; }
  OperandRef *getOperand() const { return Operand; }
  Identifier *getPhase() const { return PhaseId; }
  int getPhaseId() const { return Phase->getIndex(); }
  PhaseName *getPhaseName() const;
  bool hasPhaseName() const;
  void setPhaseName(PhaseName *Name) { Phase = Name; }
};

//----------------------------------------------------------------------------
// Definition of a predicate expression. These mirror the predicate
// expressions in tablegen, with all the same capability.
//----------------------------------------------------------------------------

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
  kCheckNumOperands,       // Check that an instr has some number of opnds
  kOpcodeSwitchStmt,       // Switch statement
  kOpcodeSwitchCase,       // Single case statement
  kReturnStatement,        // Switch return statement
  kCode,                   // String representing C code
  kTrue,                   // Predicate always returns TRUE
  kFalse,                  // Predicate always returns FALSE
  kEmpty,                  // Undefined predicate
  kName,                   // Register name, Predicate name, Opcode Name, etc
  kNumber,                 // An integer operand
  kOperandRef,             // A named operand reference.
  kString,                 // A string operand
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

// Definition of a predicate expression object.
class PredExpr : public MdlItem {
  PredOp Opcode;
  bool Negate = false;                 // perform a logical NOT of operation
  int64_t IntValue;                    // Integer value of this op
  union {                              // Variant versions of the op
    std::string Value;                 // Value of this op
    OperandRef *Opnd;                  // Reference to a named operand
    std::vector<PredExpr *> Operands;  // Operands of this op (0..n)
  };
public:
  explicit PredExpr(PredOp Opcode) : MdlItem(), Opcode(Opcode) {}
  PredExpr(const MdlItem &Item, PredOp Opcode)
      : MdlItem(Item), Opcode(Opcode) {}
  PredExpr(const MdlItem &Item, PredOp Opcode, std::string Value)
      : MdlItem(Item), Opcode(Opcode), Value(Value) {
        if (isInteger()) IntValue = std::stoi(Value);
      }
  PredExpr(const MdlItem &Item, PredOp Opcode, OperandRef *Opnd)
      : MdlItem(Item), Opcode(Opcode), Opnd(Opnd) {}
  PredExpr(const MdlItem &Item, PredOp Opcode, PredExpr *Opnd)
      : MdlItem(Item), Opcode(Opcode), Operands({Opnd}) {}
  PredExpr(const MdlItem &Item, PredOp Opcode, std::vector<PredExpr *> &Opnds)
      : MdlItem(Item), Opcode(Opcode), Operands(Opnds) {}
  PredExpr(PredOp Opcode, bool IsNegate) : MdlItem(), Opcode(Opcode) {
    if (IsNegate)
      Opcode = isTrue() ? PredOp::kFalse : PredOp::kTrue;
  }

  PredOp getOpcode() const { return Opcode; }
  void setOpcode(PredOp Op) { Opcode = Op; }
  std::string getValue() const { return Value; }
  OperandRef *getOpnd() const { return Opnd; }
  std::vector<PredExpr *> &getOperands() { return Operands; }
  std::string getPredName();
  std::string ToString(int Indent);
  bool getNegate() const { return Negate; }
  void setNegate() { Negate = !Negate; }
  void resetNegate() { Negate = false; }
  int64_t getIntValue() const { return IntValue; }

  bool isTrue() const { return Opcode == PredOp::kTrue; }
  bool isFalse() const { return Opcode == PredOp::kFalse; }
  bool isEmpty() const { return Opcode == PredOp::kEmpty; }
  bool isInteger() const { return Opcode == PredOp::kNumber; }
  bool isName() const { return Opcode == PredOp::kName; }
  bool isString() const { return Opcode == PredOp::kString; }
  bool isOperand() const { return Opcode == PredOp::kOperandRef; }
  bool isPred() const { return Opcode <= PredOp::kName; }
  bool isIntegerOrOperand() const { return isInteger() || isOperand(); }

  // Functions to generate code for reference predicates.
  std::string fmtGetOperand(PredExpr *Index) const;
  std::string fmtOperandType() const;
  std::string fmtInvalidRegOperand() const;
  std::string fmtRegOperand(const std::string &Family) const;
  std::string fmtSameRegOperand() const;
  std::string fmtImmOperand() const;
  std::string fmtImmZeroOperand() const;
  std::string fmtFunctionPredicate(bool WithTII, OutputState *Outspec) const;
  std::string fmtNumOperands() const;
  std::string fmtCheckCompound(OutputState *Outspec);
  std::string fmtCheckCode(OutputState *Outspec) const;
};

//----------------------------------------------------------------------------
// Description of a single latency rule as described in MDL.
//----------------------------------------------------------------------------
class Reference : public MdlItem {
  IdList *const Predicates = nullptr;          // list of predicates for rule
  ConditionalRef *CondRef = nullptr;           // if/then/else reference
  RefType Type = RefTypes::kNull;              // type of reference
  PhaseExpr *Phase = nullptr;                  // pipeline phase of reference
  int32_t PhaseValue = -1;                     // phase if expression is const
  int UseCycles = 1;                           // # cycles resource is used
  int Repeat = 1;                              // default repeat count
  int Delay = 1;                               // default repeat delay cycles
  int MicroOps = 0;                            // Fus entry micro ops
  int BufferSize = -1;                         // Fus reference is buffered
  RefFlags::Item FuFlags = RefFlags::kNone;    // Fus reference attributes
  OperandRef *const Operand = nullptr;         // operand we are referencing
  ResourceRefList *const Resources = nullptr;  // resources we are referencing
  ResourceDef *Port = nullptr;                 // port we are referencing
  Reference *Base = nullptr;                   // base ref for copied objects
  bool Used = false;                           // was this reference ever used?
  bool Seen = false;                           // ever considered for a SU?
  bool IsDuplicate = false;                    // duplicate resource reference?

public:
  // This constructor is used by the parser to create a new basic reference.
  Reference(const MdlItem &Item, IdList *Predicates, RefType Type,
            PhaseExpr *Phase, int Repeat, int Delay, int UseCycles,
            OperandRef *Operand, ResourceRefList *Resources)
      : MdlItem(Item), Predicates(Predicates), CondRef(nullptr),
        Type(Type), Phase(Phase), UseCycles(UseCycles),
        Repeat(Repeat), Delay(Delay), Operand(Operand),
        Resources(Resources) {}
  // This constructor creates a conditional reference.
  Reference(const MdlItem &Item, IdList *Predicates, ConditionalRef *Ref)
      : MdlItem(Item), Predicates(Predicates), CondRef(Ref),
        Resources(new ResourceRefList) {}
  // This constructor is used to generate RefFus entries, which includes
  // an explicit "micro_op" value.
  Reference(MdlItem &Item, IdList *Predicates, RefType Type,
            PhaseExpr *Phase, int Cycles, int MicroOps,
            RefFlags::Item FuFlags, ResourceRef *Unit)
      : MdlItem(Item), Predicates(Predicates), Type(Type),
        Phase(Phase), UseCycles(Cycles), Repeat(0), Delay(0),
        MicroOps(MicroOps), FuFlags(FuFlags),
        Resources(new ResourceRefList(1, Unit)) {}
  // This constructor creates a RefFus entry that -only- has a micro-op.
  Reference(MdlItem &Item, IdList *Predicates, int MicroOps,
            RefFlags::Item FuFlags)
      : MdlItem(Item), Predicates(Predicates), Type(RefTypes::kFus),
        Phase(new PhaseExpr(new PhaseName("E1"))), PhaseValue(1),
        UseCycles(0), Repeat(0), Delay(0), MicroOps(MicroOps),
        FuFlags(FuFlags), Resources(new ResourceRefList) {}
  // This constructor creates default references to a "pseudo" functional unit
  // for instructions which have no functional unit specifications.
  Reference(RefType Type, PhaseName *Phase, std::string FuName)
      : MdlItem(), Type(Type), Phase(new PhaseExpr(Phase)),
        Resources(new ResourceRefList(1, new ResourceRef(FuName))) {}
  // This constructor is used while instantiating subunits to create a
  // copy of a latency reference. We don't copy normal resources at this
  // point, since they need to be bound to template parameters, and this is
  // done in the caller.  We do copy functional unit resource references tho.
  Reference(Reference *Item, PhaseExpr *Phase)
      : MdlItem(*Item), Predicates(Item->getPredicates()),
        CondRef(Item->getConditionalRef()),
        Type(Item->getRefType()),
        Phase(Phase ? Phase : Item->getPhaseExpr()),
        UseCycles(Item->getUseCycles()), Repeat(Item->getRepeat()),
        Delay(Item->getDelay()), MicroOps(Item->getMicroOps()),
        FuFlags(Item->getFuFlags()), Operand(Item->getOperand()),
        Resources(new ResourceRefList), Port(nullptr), Base(Item) {
    if (Item->isFuncUnitRef() && !Item->Resources->empty())
      Resources->push_back(new ResourceRef(*(*Item->Resources)[0]));
  }
  // This constructor is used while instantiating subunits to create a copy
  // of a conditional latency reference.
  Reference(Reference *Item, ConditionalRef *Cond)
      : MdlItem(*Item), Predicates(Item->getPredicates()),
        CondRef(Cond), Type(Item->getRefType()),
        Phase(Item->getPhaseExpr()), Resources(new ResourceRefList) {}
  // This constructor is used when creating the instruction database, and
  // we want to specialize operand references to the instruction they are
  // associated with. Note that we don't need to copy repeat and delay values.
  Reference(Reference &Item, int Delay)
      : MdlItem(Item), Predicates(Item.getPredicates()),
        CondRef(Item.getConditionalRef()), Type(Item.getRefType()),
        Phase(Item.getPhaseExpr() ?
                    Item.getPhaseExpr()->increment(Delay) : nullptr),
        UseCycles(Item.getUseCycles()), MicroOps(Item.getMicroOps()),
        FuFlags(Item.getFuFlags()),
        Operand(Item.getOperand() ?
                 new OperandRef(*Item.getOperand()) : nullptr),
        Resources(new ResourceRefList), Port(Item.getPort()), Base(&Item) {
    for (auto *Ref : *Item.getResources())
      Resources->push_back(new ResourceRef(*Ref));
  }
  Reference(RefType Type, PhaseExpr *Phase, OperandRef *Operand)
      : MdlItem(), Type(Type), Phase(Phase->clone()),
        Operand(Operand), Resources(new ResourceRefList) {}
  Reference(RefType Type, PhaseName *Phase, OperandRef *Operand)
      : MdlItem(), Type(Type), Phase(new PhaseExpr(Phase)),
        Operand(Operand), Resources(new ResourceRefList) {}

  std::string ToString() const;
  IdList *getPredicates() const { return Predicates; }
  ConditionalRef *getConditionalRef() const { return CondRef; }
  bool isConditionalRef() const { return CondRef != nullptr; }

  PhaseExpr *getPhaseExpr() const { return Phase; }
  void setPhaseExpr(PhaseName *Name) { Phase = new PhaseExpr(Name); }

  int getUseCycles() const { return UseCycles; }
  int getRepeat() const { return Repeat; }
  int getDelay() const { return Delay; }
  int getMicroOps() const { return MicroOps; }
  void setMicroOps(int Ops) { MicroOps = Ops; }
  int getFuFlags() const { return FuFlags; }
  void setFuFlags(RefFlags::Item Flags) { FuFlags = Flags; }
  bool isUnbuffered() const { return BufferSize == 0; }
  bool isInOrder() const { return BufferSize == 1; }
  bool isOutOfOrder() const { return BufferSize > 1; }
  void setBufferSize(int Size) { BufferSize = Size; }
  int getBufferSize() const { return BufferSize; }

  OperandRef *getOperand() const { return Operand; }
  ResourceRefList *getResources() const { return Resources; }

  RefType getRefType() const { return Type; }
  void setRefType(RefType NewType) { Type = NewType; }

  bool isOperandRefType() const {
    return Type > RefTypes::kNull && Type < RefTypes::kHold;
  }
  bool isResourceRef() const {
    return Type == RefTypes::kHold || Type == RefTypes::kReserve;
  }
  bool isDef() const { return Type & RefTypes::kDef; }
  bool isUse() const { return Type & RefTypes::kUse; }
  bool isFuncUnitRef() const { return Type == RefTypes::kFus; }

  RefType adjustResourceReferenceType() const {
    if (Type & (RefTypes::kHold | RefTypes::kReserve | RefTypes::kFus))
      return Type;
    return !getOperand() ? Type : RefTypes::kUse;
  }
  bool isDefaultOperandRef() {
    return Operand && Operand->getOperandIndex() == -1;
  }

  void addResource(ResourceRef *Res) { Resources->push_back(Res); }
  void addPort(ResourceDef *Def) { Port = Def; }
  ResourceDef *getPort() const { return Port; }
  void setUsed() {
    this->Used = true;
    for (auto *Item = this; Item->Base; Item = Item->Base)
      Item->Base->Used = true;
  }
  bool getUsed() const { return Used; }
  void setSeen() {
    this->Seen = true;
    for (auto *Item = this; Item->Base; Item = Item->Base)
      Item->Base->Seen = true;
  }
  bool getSeen() const { return Seen; }

  // References are ordered by pipeline phase, then by reference type.
  // If the pipeline phase is non-trivial, its value is -1, and ordered last.
  bool operator<(const Reference &Item) const {
    if (PhaseValue != Item.PhaseValue) {
      if (PhaseValue == -1)
        return false;
      if (Item.PhaseValue == -1)
        return true;
      return PhaseValue < Item.PhaseValue;
    }
    if (Phase != nullptr && Item.Phase != nullptr)
      return Phase->ToString() < Item.Phase->ToString();

    if (Type != Item.getRefType())
      return Type < Item.Type;

    if (Operand != nullptr && Item.getOperand() != nullptr &&
        Operand->getOperandIndex() != Item.getOperand()->getOperandIndex())
      return Operand->getOperandIndex() < Item.getOperand()->getOperandIndex();

    return ToString() < Item.ToString();
  }
  bool operator>(const Reference &Item) const { return Item < *this; }

  void setConstantPhase() { PhaseValue = Phase->getConstantPhase(); }
  bool isProtected() const { return Phase->isProtected(); }
  bool isUnprotected() const { return Phase->isUnprotected(); }
  bool isDuplicate() const { return IsDuplicate; }
  void setDuplicate() { IsDuplicate = true; }
};

//---------------------------------------------------------------------------
// Describe a conditional reference, corresponding to an if/then/else
// latency statement.
//---------------------------------------------------------------------------
class ConditionalRef : public MdlItem {
  Identifier *Predicate = nullptr;           // named predicate
  PredExpr *InstrPredicate = nullptr;        // predicate expression
  ReferenceList Refs;                        // list of conditional refs
  ConditionalRef *ElseClause = nullptr;      // else clause of if stmt

public:
  ConditionalRef(MdlItem &Item, Identifier *Predicate, ReferenceList *Refs,
                 ConditionalRef *ElseClause)
      : MdlItem(Item), Predicate(Predicate), Refs(*Refs),
        ElseClause(ElseClause) {}
  // This constructor is used to copy conditional references when we
  // instantiate a latency reference.
  explicit ConditionalRef(ConditionalRef *Item, ConditionalRef *ElseClause)
      : MdlItem(*Item), Predicate(Item->Predicate),
        InstrPredicate(Item->InstrPredicate), ElseClause(ElseClause) {}

  Identifier *getPredicate() const { return Predicate; }
  PredExpr *getInstrPredicate() const { return InstrPredicate; }
  void setInstrPredicate(PredExpr *Pred) { InstrPredicate = Pred; }

  ReferenceList &getRefs() { return Refs; }
  ConditionalRef *getElseClause() const { return ElseClause; }

  // Determine if there is a single operand ref in a conditional clause.
  // We don't support anything in the clause except Uses, Defs, and Fus.
  // If found, return it.
  Reference *findSingleRef() const {
    Reference *Found = nullptr;
    for (auto *Ref : Refs)
      if (Ref->isDef() || Ref->isUse()) {
        if (Found) return nullptr;
        if (!Ref->getOperand()) return nullptr;
        if (Ref->getUseCycles() != 1) return nullptr;
        if (!Ref->getResources()->empty()) return nullptr;
        Found = Ref;
      } else if (!Ref->isFuncUnitRef()) return nullptr;
    return Found;
  }

  bool isSingleOperand(Reference *Ref) const {
    if (auto *Found = findSingleRef()) {
      if (Found->getRefType() != Ref->getRefType()) return false;
      if (Found->getOperand()->getName() != Ref->getOperand()->getName())
        return false;
      if (!ElseClause) return true;
      return ElseClause->isSingleOperand(Ref);
    }
    return false;
  }

  // Return true if all ConditionalRefs in this object reference same operand.
  // We need at least one else clause to make this worthwhile.
  bool isSingleOperand() const {
    if (auto *Ref = findSingleRef())
      if (ElseClause)
        return ElseClause->isSingleOperand(Ref);
    return false;
  }

  // Return true if a conditional reference contains any operand refs.
  bool hasOperandRefs() {
    if (hasOperandRefs(Refs))
      return true;
    if (ElseClause != nullptr)
      return ElseClause->hasOperandRefs();
    return false;
  }
  // Return true if a set of references contains any operand refs.
  bool hasOperandRefs(ReferenceList &refs) {
    for (auto *Ref : Refs) {
      if (Ref->isConditionalRef() && Ref->getConditionalRef()->hasOperandRefs())
        return true;
      if (Ref->getOperand() != nullptr)
        return true;
    }
    return false;
  }

  // Return true if a conditional reference contains any resource refs.
  bool hasResourceRefs() {
    if (hasResourceRefs(Refs))
      return true;
    if (ElseClause != nullptr)
      return ElseClause->hasResourceRefs();
    return false;
  }

  // Return true if a set of references contains any operand refs.
  bool hasResourceRefs(ReferenceList &refs) {
    for (auto *Ref : Refs) {
      if (Ref->isFuncUnitRef())
        return true;
      if (Ref->isConditionalRef() &&
          Ref->getConditionalRef()->hasResourceRefs())
        return true;
      if (!Ref->getResources()->empty())
        return true;
    }
    return false;
  }

  std::string ToString(bool IsElse);
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
  IdList *Types = nullptr;           // type(s) of operand
  IdList *Names = nullptr;           // name(s) of operand
  bool IsImpliedRegister = false;    // is this operand an implied register?
  bool Ellipsis = false;             // was this operand an ellipsis?
  bool Input = false;                // was the operand tagged as an input?
  bool Output = false;               // was the operand tagged as an output?

  OperandDef *Operand = nullptr;      // pointer to associated operand type
  RegisterClass *RegClass = nullptr; // pointer to associated register class

public:
  OperandDecl(const MdlItem &Item, Identifier *Type, Identifier *Name,
              bool Ellipsis, bool Input, bool Output)
      : MdlItem(Item), Types(new IdList({Type})), Names(new IdList({Name})),
        Ellipsis(Ellipsis), Input(Input), Output(Output) {}

  OperandDecl(OperandDecl *Item, OperandDecl *Parent)
      : MdlItem(),
        Types(new IdList((*Parent->Types).begin(), (*Parent->Types).end())),
        Names(new IdList((*Parent->Names).begin(), (*Parent->Names).end())),
        IsImpliedRegister(Item->IsImpliedRegister),
        Ellipsis(Parent->Ellipsis), Input(Parent->Input),
        Output(Parent->Output), Operand(Item->Operand),
        RegClass(Item->RegClass) {}

  // Set this to true if we want to see more detail (for debugging).
  const bool PrintFullyQualifiedDeclaration = true;
  std::string ToString() const;

  std::string const &getName() const { return (*Names)[0]->getName(); }
  std::string const getTypeName() const { return (*Types)[0]->getName(); }
  Identifier *getType() const { return (*Types)[0]; }
  IdList *getTypes() const { return Types; }
  Identifier *getBaseType() const { return Types->back(); }
  Identifier *getOpName() const { return (*Names)[0]; }
  IdList *getOpNames() const { return Names; }
  void addType(Identifier *Type) { Types->push_back(Type); }
  void addName(Identifier *Name) { Names->push_back(Name); }
  bool isImpliedRegister() const { return IsImpliedRegister; }
  bool isEllipsis() const { return Ellipsis; }
  bool isInput() const { return Input; }
  bool isOutput() const { return Output; }

  OperandDef *getOperand() const { return Operand; }
  RegisterClass *getRegClass() const { return RegClass; }
  void setOperand(OperandDef *opnd) { Operand = opnd; }
  void setRegclass(RegisterClass *regs) { RegClass = regs; }
  void setIsImpliedRegister() { IsImpliedRegister = true; }
};

//----------------------------------------------------------------------------
// Description of a single operand definition.
//----------------------------------------------------------------------------
class OperandDef : public MdlItem {
  Identifier *Name = nullptr;                 // name of the operand
  IdList *Bases = nullptr;                    // base operands (for derived)
  OperandDeclList *Operands = nullptr;        // list of operand declarations
  Identifier *Type = nullptr;                 // type of operand
  OperandAttributeList *Attributes = nullptr; // attributes defined
  OperandDefList *BaseOperands = nullptr;     // base, if this opnd has one

public:
  OperandDef(const MdlItem &Item, Identifier *Name, OperandDeclList *Operands,
             Identifier *Type, OperandAttributeList *Attributes, IdList *Bases)
      : MdlItem(Item), Name(Name), Bases(Bases), Operands(Operands),
        Type(Type), Attributes(Attributes),
        BaseOperands(new OperandDefList) {}

  std::string ToString() const;
  std::string const &getName() const { return Name->getName(); }
  OperandDeclList *getOperands() const { return Operands; }
  Identifier const *getType() const { return Type; }
  IdList *getBases() const { return Bases; }
  OperandDefList *getBaseOperands() const { return BaseOperands; }
  void addBaseOperand(OperandDef *Base) { BaseOperands->push_back(Base); }
  OperandAttributeList *getAttributes() const { return Attributes; }
  bool isDerivedOperand() const { return Bases != nullptr; }
};

//----------------------------------------------------------------------------
// Description of a single operand attribute definition.
// Currently, attributes must have integer values.  We could extend this if
// necessary.
//----------------------------------------------------------------------------
class OperandAttribute : public MdlItem {
  Identifier *Name = nullptr;                // name of attribute
  std::vector<int> *Values;                  // integer values of attribute
  std::string Type;                          // type of operand value
  PredValueList *PredicateValues = nullptr;  // predicate values (if any)
  IdList *Predicate = nullptr;               // attribute predicate

public:
  OperandAttribute(MdlItem &Item, Identifier *Name, std::vector<int> *Values,
                   std::string Type, PredValueList *PredicateValues,
                   IdList *Predicate)
      : MdlItem(Item), Name(Name), Values(Values), Type(Type),
        PredicateValues(PredicateValues), Predicate(Predicate) {}

  std::string ToString() const;
  std::string const &getName() const { return Name->getName(); }
  std::vector<int> *getValues() const { return Values; }
  int getValues(int I) const { return (*Values)[I]; }
  std::string getType() const { return Type; }
  IdList *getPredicate() const { return Predicate; }
  PredValueList *getPredicateValues() const { return PredicateValues; }
};

//----------------------------------------------------------------------------
// Description of a single predicated value for operand attributes. A value
// is an integer, a range of values, or a mask.
//----------------------------------------------------------------------------
class PredValue : public MdlItem {
  enum PredValueType { kValue, kRange, kMask };

  PredValueType Type;               // is this a value, range, or mask
  uint64_t Mask = 0;                // mask bits
  int64_t Low = 0, High = 0;        // range of values (or value if same)

public:
  PredValue(MdlItem &Item, uint64_t Mask)
      : MdlItem(Item), Type(kMask), Mask(Mask) {}
  PredValue(MdlItem &Item, int64_t Low, int64_t High)
      : MdlItem(Item), Type(kRange), Low(Low), High(High) {
    if (Low == High)
      Type = kValue;
  }

  std::string ToString() const;
  bool isRange() const { return Type == kRange; }
  bool isValue() const { return Type == kValue; }
  bool isMask() const { return Type == kMask; }

  // Pretty print a predicate value.
  std::string formatValue(int64_t value) const;

  int64_t getValue() const { return Low; }
  int64_t getLow() const { return Low; }
  int64_t getHigh() const { return High; }
  uint64_t getMask() const { return Mask; }
};

//----------------------------------------------------------------------------
// Description of a single instruction.
//----------------------------------------------------------------------------
class InstructionDef : public MdlItem {
  Identifier *Name = nullptr;                // name of the instruction
  OperandDeclList *Operands = nullptr;       // list of operand declarations
  OperandDeclList *FlatOperands = nullptr;   // flattened operand declarations
  IdList *Subunits = nullptr;                // subunits this instr uses
  IdList *Derived = nullptr;                 // instrs derived from this one
  bool HasEllipsis = false;                  // true if instruction has varargs

public:
  InstructionDef(const MdlItem &item, Identifier *name,
                 OperandDeclList *operands, IdList *subunits, IdList *derived,
                 bool has_ellipsis)
      : MdlItem(item), Name(name), Operands(operands),
        Subunits(subunits ? subunits : new IdList), Derived(derived),
        HasEllipsis(has_ellipsis) {}

  std::string ToString() const;
  std::string const &getName() const { return Name->getName(); }
  IdList *getSubunits() const { return Subunits; }
  OperandDeclList *getOperands() const { return Operands; }
  OperandDeclList *getFlatOperands() const { return FlatOperands; }
  IdList *getDerived() const { return Derived; }
  void setFlatOperands(OperandDeclList *Opnds) { FlatOperands = Opnds; }
  bool hasEllipsis() const { return HasEllipsis; }
  int getNumOperands() const { return Operands->size(); }
  int getNumFlatOperands() const { return FlatOperands->size(); }

  // Get the operand declaratopm of the nth operand.
  // Note: variable arguments never have declared types.
  OperandDecl *getOperandDecl(unsigned Index) const {
    if (Index >= FlatOperands->size())
      return nullptr;
    return (*FlatOperands)[Index];
  }

  // Get the operand type of the nth operand.
  OperandDef *getOperandType(int Index) const {
    return getOperandDecl(Index)->getOperand();
  }
  void addSubunit(SubUnitTemplate *subunit) {
    if (FindItem(*Subunits, subunit->getName()))
      return;
    Subunits->push_back(new Identifier(subunit->getName()));
  }
};

//----------------------------------------------------------------------------
// Capture a single functional unit instantiation and the context of how
// it was instantiated (CPU, Cluster, Parent FU).
//----------------------------------------------------------------------------
class FuncUnitInstantiation {
  MdlSpec *Spec;                       // pointer to entire file description
  CpuInstance *Cpu;                    // the parent CPU
  ClusterInstance *Cluster;            // the parent cluster
  FuncUnitInstance *Instance;          // the functional unit instance
  FuncUnitTemplate *FuncType;          // the functional unit type, or base
  ResourceRefDict ResourceArgs;        // resource arguments to instance
  RegisterClassRefDict ClassArgs;      // register class arguments to instance
  ResourceDefList Resources;           // resources defined for this instance
  ResourceDefList Ports;               // ports defined for this instance
  ResourceDef *FuResource = nullptr;   // implicit resource for this FU
  FuncUnitInstantiation *Parent = nullptr; // Parent functional unit

public:
  // This constructor is for instantiating top-level functional units.
  FuncUnitInstantiation(MdlSpec *Spec, CpuInstance *Cpu,
                        ClusterInstance *Cluster, FuncUnitInstance *Instance)
      : Spec(Spec), Cpu(Cpu), Cluster(Cluster), Instance(Instance),
        FuncType(Instance->getTemplate()) {
    instantiateLocalDefs();
  }
  // This constructor is for instantiating base functional units, which
  // reuse ports, resources, and the instance of the parent functional unit.
  FuncUnitInstantiation(FuncUnitInstantiation *Fu, FuncUnitTemplate *Base)
      : Spec(Fu->getSpec()), Cpu(Fu->getCpu()), Cluster(Fu->getCluster()),
        Instance(Fu->getInstance()),
        FuncType(Base),      // Note - not instance->get_template()!!
        ResourceArgs(Fu->getResourceArgs()), ClassArgs(Fu->getClassArgs()),
        Parent(Fu) {
    instantiateLocalDefs();
  }

  // Error check a merged resource reference that has an allocation.
  ResourceRef *checkAllocation(ResourceRef *Def, ResourceRef *Ref);
  // Create a merged resource reference from a definition and a reference.
  ResourceRef *mergeRefs(ResourceRef *Def, ResourceRef *Ref);
  // Create definition objects for locally define references and ports.
  void instantiateLocalDefs();
  // Look up a register class in the templates parameter list.
  RegisterClass *findRegClass(Identifier *Item);
  // Bind a functional unit instantiation parameter to a register class.
  void bindClassArg(ResourceRef *Arg);
  // Bind a functional unit instantiation parameter to a resource reference.
  void bindResourceArg(ResourceRef *Arg);
  // Map a functional unit instantiation parameter id to its bound resource.
  ResourceRef *getResourceArg(int ParmId);
  // Map a functional unit instantation parameter id to its bound class.
  RegisterClassRef *getClassArg(int ParmId);
  // Determine if a predicate matches the instantiation context's cpu name or
  // functional unit name.  Return true if its valid.
  bool isValidPredicate(IdList *Predicates) const;
  // For each subunit instance in a functional unit instantiation, create a
  // subunit instantiation, bind its instance parameters, and instantiate
  // all of its latency instances.
  void instantiateSubunits();
  // For each connect statement, find the connected resources and register
  // classes, annotate the associated port.
  void processConnects();
  // Bind a subunit port argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceDef *bindSubUnitPort(ResourceRef *Arg);
  // Bind a subunit resource argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceRef *bindSubUnitResource(ResourceRef *Arg);
  // Bind a functional unit instance resource argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceRef *bindFuncUnitResource(ResourceRef *Arg);
  // Bind a functional unit instance register class argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  RegisterClassRef *bindFuncUnitClass(ResourceRef *Arg);
  // Bind functional unit instantiation parameters to resources and classes.
  void bindFuncUnitParameters();
  // Bind any slot pinning resources.
  void bindFuncUnitSlotResources();
  // Bind subunit instantiation parameters to ports and resources.
  void bindSubUnitParameters(SubUnitInstantiation *Su);

  // Error logging - forward error messages to MdlSpec logger.
  template <typename... Ts>
  bool ErrorLog(const MdlItem *Item, const char *Fmt, Ts... Args) const;
  template <typename... Ts>
  void WarningLog(const MdlItem *Item, const char *Fmt, Ts... Args) const;
  int ErrorsSeen() const;

  // Debug - dump a functional unit instantiation.
  void dumpFuncUnitInstantiation();

  FuncUnitTemplate *getTemplate() { return FuncType; }
  std::string const &getName() const { return Instance->getName(); }
  MdlSpec *getSpec() const { return Spec; }
  CpuInstance *getCpu() const { return Cpu; }
  ClusterInstance *getCluster() const { return Cluster; }
  FuncUnitInstance *getInstance() const { return Instance; }
  FuncUnitTemplate *getFuncType() const { return FuncType; }
  ResourceRefDict &getResourceArgs() { return ResourceArgs; }
  RegisterClassRefDict &getClassArgs() { return ClassArgs; }
  ResourceDefList &getResources() { return Resources; }
  ResourceDefList &getPorts() { return Ports; }
  FuncUnitInstantiation *getParent() const { return Parent; }

  // Create an implicit resource for this instance.
  void setResource() {
    FuResource = new ResourceDef(Instance->getId(), this);
  }
  ResourceDef *getFuResource() const { return FuResource; }
  ResourceDef *getRootResource() const {
    auto *Item = this;
    for (; Item->Parent != nullptr; Item = Item->Parent)
      ;
    return Item->FuResource;
  }
  // Get this instance's implied resource, and all of its parents' resource.
  ResourceDefList getFuncUnitResources() const {
    ResourceDefList ResList;
    ResList.push_back(FuResource);
    for (auto *ParentFu = Parent; ParentFu; ParentFu = ParentFu->Parent)
      ResList.push_back(ParentFu->FuResource);
    return ResList;
  }
};


//----------------------------------------------------------------------------
// Capture a single subunit instantiation and the context of how it was
// instantiated.
//----------------------------------------------------------------------------
class SubUnitInstantiation {
  MdlSpec *Spec;                    // pointer to entire file description
  FuncUnitInstantiation *FuncUnit; // context of this subunits instantiation
  SubUnitInstance *SubUnit;         // the subunit instance
  SubUnitTemplate *SuTemplate;     // the template for this subunit
  ResourceRefDict ResourceArgs;    // resource arguments to this instance
  ResourceDefDict PortArgs;        // port arguments to this instance
  ReferenceList References;         // instantiated list of references

public:
  SubUnitInstantiation(FuncUnitInstantiation *Func, SubUnitInstance *Subunit)
      : Spec(Func->getSpec()), FuncUnit(Func), SubUnit(Subunit) {
    SuTemplate = Subunit->getTemplate();
  }

  // Return the implicit functional unit resource associated with this instance.
  ResourceDefList getFuncUnitResources() const {
    return FuncUnit->getFuncUnitResources();
  }
  // Return slots resources associated with this subunit.
  ResourceRefList *getSlotResourcesAny() const {
    return FuncUnit->getInstance()->getResourceSlotsAny();
  }
  ResourceRefList *getSlotResourcesAll() const {
    return FuncUnit->getInstance()->getResourceSlotsAll();
  }
  // Bind a port definition to the associated instantiation parameter.
  void bindPortArg(ResourceRef *Arg);
  // Bind a resource definition to the associated instantiation parameter.
  void bindResourceArg(ResourceRef *Arg);
  // Map a subunit instantiation parameter id to its bound resource.
  ResourceRef *getResourceArg(int ParamId);
  // Map a subunit instantiation parameter id to its bound port.
  ResourceDef *getPortArg(int ParamId);

  // Determine if a latency predicate matches the instantiation context's
  // cpu name or functional unit name.
  bool isValidPredicate(IdList *Predicates) const;
  // Bind a latency latency instance port argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceDef *bindLatPort(ResourceRef *Arg);
  // Bind a latency resource argument to its definition.
  // Return the definition if found, otherwise return nullptr.
  ResourceRef *bindLatResource(ResourceRef *Arg);
  // Bind latency instantation parameters to ports and resources.
  void bindLatencyParams(LatencyInstantiation *Lat);
  // Bind latency reference resources to latency template parameters.
  void bindLatencyResources(LatencyInstantiation &Lat, Reference *Reference,
                            ResourceRefList *Resources);
  ConditionalRef *copyLatencyCondReference(LatencyInstantiation &Lat,
                                           ConditionalRef *Cond);
  void copyLatencyReference(LatencyInstantiation &Lat,
                            ReferenceList &References, Reference *Ref);
  // Add references from a single latency template to a subunit instantiation.
  void instantiateLatency(LatencyInstantiation &Lat,
                          LatencyTemplate *LatTemplate);
  // Add references from a parent latency to a subunit instantiation, then
  // add all of its bases, recursively.
  void instantiateLatencyBases(LatencyInstantiation &Lat,
                               LatencyTemplate *Parent, LatencyList &Bases);
  // Instantiation all the latencies (and latency bases) associated with
  // a subunit instantiation.
  void instantiateLatencies();

  // Error logging - forward error messages to MdlSpec logger.
  template <typename... Ts>
  bool ErrorLog(const MdlItem *Item, const char *Fmt, Ts... Args) const;
  template <typename... Ts>
  void WarningLog(const MdlItem *Item, const char *Fmt, Ts... Args) const;
  int ErrorsSeen() const;

  // Debug: dump all subunit instantiations.
  void dumpSubUnitInstantiation();

  MdlSpec *getSpec() const { return Spec; }
  CpuInstance *getCpu() const { return FuncUnit->getCpu(); }
  FuncUnitInstantiation *getFuncUnit() const { return FuncUnit; }
  SubUnitInstance *getSubunit() const { return SubUnit; }
  SubUnitTemplate *getSuTemplate() const { return SuTemplate; }
  ResourceRefDict &getResourceArgs() { return ResourceArgs; }
  ResourceDefDict &getPortArgs() { return PortArgs; }
  ReferenceList &getReferences() { return References; }
};

//----------------------------------------------------------------------------
// Capture a single latency instantiation and the context of how it was
// instantiated.
//----------------------------------------------------------------------------
class LatencyInstantiation {
  SubUnitInstantiation *Subunit;      // context of this instantiation
  LatencyInstance *Latency;           // latency instance
  LatencyTemplate *LatTemplate;      // template for this latency
  ResourceRefDict ResourceArgs;      // resource arguments to this instance
  ResourceDefDict PortArgs;          // port arguments to this instance

public:
  LatencyInstantiation(SubUnitInstantiation *Su, LatencyInstance *Latency)
      : Subunit(Su), Latency(Latency), LatTemplate(Latency->getTemplate()) {}

  // Bind a resource definition to the associated instantiation parameter.
  void bindResourceArg(ResourceRef *Arg);
  // Bind a port definition to the associated instantiation parameter.
  void bindPortArg(ResourceRef *Arg);
  // Map a latency instantiation parameter to its bound resource.
  ResourceRef *getResourceArg(int ParamId);
  // Map a latency instantiation parameter to its bound resource.
  ResourceDef *getPortArg(int ParamId);
  // Debug: dump this latency instantiation.
  void dumpLatencyInstantiation();

  SubUnitInstantiation *getSubunit() const { return Subunit; }
  LatencyInstance *getLatency() const { return Latency; }
  LatencyTemplate *getLatTemplate() const { return LatTemplate; }
  ResourceRefDict &getResourceArgs() { return ResourceArgs; }
  ResourceDefDict &getPortArgs() { return PortArgs; }
};

//----------------------------------------------------------------------------
// Container that captures all the contents of a machine description.
// MdlSpec owns all of these vectors and their contents.
//----------------------------------------------------------------------------
class MdlSpec {
  std::string FamilyName;              // Family name of processors.
  PipeDefList PipeDefs;                // List of pipe specs defined in mdl.
  ResourceDefList Resources;           // List of resources defined in mdl.
  RegisterDefList Registers;           // List of registers defined in mdl.
  RegisterClassList RegClasses;        // List of register classes defined.
  CpuList Cpus;                        // List of CPU's defined.
  FuncUnitList FuncUnits;              // List of functional unit templates.
  FuncUnitGroupList FuncUnitGroups;    // List of functional unit groups.
  SubUnitList Subunits;                // List of subunit templates defined.
  LatencyList Latencies;               // List of latency templates defined.
  InstructionList Instructions;        // List of instruction definitions.
  OperandDefList Operands;             // List of operand definitions.

  FuncUnitDict FuMap;                  // Dictionary of func unit templates.
  FuncUnitGroupDict FuGroupMap;        // Dictional of func unit groups.
  SubUnitDict SuMap;                   // Dictionary of subunit templates.
  LatencyDict LatMap;                  // Dictionary of latency templates.
  OperandDict OperandMap;              // Dictionary of operand definitions.
  InstructionDict InstructionMap;      // Dictionary of instruction definitions.
  RegisterClassDict RegisterClassMap;  // Dictionary of register classes.
  SubUnitInstantiations SuInstantiations; // Table of all su instances.

  // Set of all names that can be used as mdl predicates.
  std::unordered_set<std::string> ValidPredicateNames;

  // Dictionary of user-defined predicate expressions, indexed by name.
  std::map<std::string, PredExpr *> PredicateTable;

  // Cache the first phase name found in the spec.
  PhaseName *FirstPhaseName = nullptr; // lowest pipeline phase.

  // Objects to manage error logging.
  std::unordered_set<std::string> ErrorMessages;
  int ErrorCount = 0; // Fatal error count.
  int WarningCount = 0;
  bool PrintWarnings = true;
  bool WarningsAreFatal = false;
  bool HasExplicitFuRefs = false;

public:
  MdlSpec(bool PrintWarnings, bool WarningsAreFatal)
      : PrintWarnings(PrintWarnings), WarningsAreFatal(WarningsAreFatal) {
    addBuiltinPredicates();
  }

  void addSubUnitInstantiation(SubUnitInstantiation *Su) {
    SuInstantiations[Su->getSubunit()->getName()]->push_back(Su);
  }

  // Create default subunits to instructions that don't have subunits.
  void checkInstructionSubunits();

  // Add a subunit instance to a "catchall" functional unit, and add it to the
  // specified cpu.
  void addSubunitToCpu(CpuInstance *Cpu, SubUnitTemplate *Subunit);

  // Scan latency templates to find which functional units they reference,
  // then tie each client subunit to any referenced functional units.
  void findLatencyFuncUnits(ReferenceList *References,
                                             LatencyFuncUnits &FuncUnits);
  void findLatencyFuncUnits(LatencyTemplate *Lat);
  void findFunctionalUnitClientCpus(FuncUnitTemplate *Funit, CpuInstance *Cpu);
  void findFunctionalUnitClientCpus();
  void tieSubUnitsToFunctionalUnits();

  // Tie a subunit to a set of instructions that match a set of
  // regular expressions.
  void tieSubUnitToInstructions(SubUnitTemplate *su, StringList *RegExBases);
  // Tie a derived subunit to any instruction associated with any of its bases.
  void tieDerivedSubUnitsToInstructions();

  // Check that the input spec has some basic required components.
  void checkInputStructure();

  // Create a function unit instance object and add to the functional unit
  // instance table.
  FuncUnitInstantiation *addFuncUnitInstantiation(CpuInstance *Cpu,
                                                  ClusterInstance *Cluster,
                                                  FuncUnitInstance *FuInst);

  // Create a base function unit instance object and add to table.
  FuncUnitInstantiation *
  addFuncUnitBaseInstantiation(FuncUnitInstantiation *Parent,
                               FuncUnitTemplate *Base);

  // Create dictionaries for functional units, subunits, and latencies.
  // We don't care here about duplicate names (checked separately).
  // Also build instance tables for functional units and subunits.
  void buildDictionaries();
  void findImplicitFuncUnitTemplates();
  void findValidPredicateNames();
  void isValidPredicateName(const Identifier *Name);

  // First-round semantic checking of the input machine description.
  void sameParams(const ParamsList *TempParams, const ParamsList *BaseParams,
                  MdlItem *Item);
  void validateArgs(const ParamsList *Args,
                    const ResourceRefList *Instance, MdlItem *Item);
  void checkForDuplicateDefs();
  void checkTemplateBases();

  bool expandGroup(FuncUnitGroup *Group, IdList *Members, unsigned Depth);
  void checkInstantiations();
  void checkIssueSlots();
  void checkInstructions();
  void checkOperand(OperandDecl *Opnd);
  bool checkRecursiveOperands(OperandDef *Opnd, OperandDefList &Seen);
  void checkOperandDerivations(OperandDef *Opnd);
  void checkOperands();
  void checkConditionalReferences(ConditionalRef *CondRef);
  void checkReferences();
  void checkReferenceUse();                      // Look for unused references.
  void checkSubunitUse();                        // Look for unused subunits.
  void checkResourceDef(const ResourceDef *Def); // Check a single resource.
  void checkResourceDefs(); // Make sure shared pools are properly declared.
  void checkResourceUse();  // Look for suspect resource use.

  // Add global resource definitions to each CPU.
  void promoteGlobalResources();
  // Scan resource definitions for CPUs, Clusters, and Functional Unit
  // Templates and promote any group member to a general resource.
  void promoteResourceGroupMembers(ResourceDefList *Resources,
                                   ResourceDefList *OuterScope,
                                   ResourceRefDict *Args);
  void promoteResourceGroups();
  void checkPromotedMember(ResourceDef *Group, Identifier *Member,
                           ResourceDef *Promoted);

  void flattenOperand(OperandDecl *Opnd, OperandDeclList *FlatOps);
  void flattenInstructionOperands();
  void checkPhaseDefinitions(PipeDefList *Pipes);
  void specializePhaseExpr(PhaseExpr *Expr, CpuInstance *Cpu);
  void checkReferencePhases(ReferenceList *Refs);
  void checkPipeReferences();
  void checkPipeReference(ResourceDef *Def, CpuInstance *Cpu);
  bool checkSubOperands(OperandRef *Ref, const Identifier *Opnd, int Idx);
  PhaseName *searchPipeReference(Identifier *Phase, CpuInstance *Cpu);
  PhaseName *findPipeReference(Identifier *Phase, CpuInstance *Cpu);

  // Return the first phase of the first pipeline definition.
  PhaseName *findFirstPhase();
  // Return the first phase identified as an "execute" phase.
  PhaseName *findFirstExecutePhase(CpuInstance *Cpu);

  // Instantiate base functional unit instances.
  void addFunctionalUnitBases(FuncUnitInstantiation *Parent);
  // Instantiate a single functional unit instance.
  void instantiateFunctionalUnit(CpuInstance *Cpu, ClusterInstance *Cluster,
                                 FuncUnitInstance *Fu);
  // Iterate over the spec and instantiate every functional unit instance.
  void instantiateFunctionalUnits();
  // For every CPU, build a map of instances for each functional unit template.
  void buildFuncUnitInstancesMap();
  // Assign ids to every resource.
  void assignResourceIds();
  // Assign ids to each pooled resource.
  void assignPoolIds();
  // Debug: Dump every resource id and its context.
  void dumpResourceIds();

  // Print out the entire specification.
  std::string ToString() const;
  // Debug: dump out all subunit instantiations.
  void dumpSubUnitInstantiations();
  // Debug: dump out all functional unit instantiations.
  void dumpFuncUnitInstantiations();
  // Debug: dump out all user-defined predicates.
  void dumpPredicates();

  void addBuiltinPredicates() {
    std::string Ktrue = "TruePred", Kfalse = "FalsePred";
    enterPredicate(Ktrue, new PredExpr(PredOp::kTrue));
    enterPredicate(Kfalse, new PredExpr(PredOp::kFalse));
  }

public:
  // Template function to check for duplicate entries in two symbol definition
  // lists. Print an error message for each duplicate found.
  template <typename A, typename B>
  void findDuplicates(const std::vector<A *> &Va, const std::vector<B *> &Vb) {
    for (auto *AItem : Va)
      for (auto *BItem : Vb)
        if (AItem->getName() == BItem->getName())
          ErrorLog(
              AItem,
              "Duplicate definition of {0}\n     Previously defined at {1}",
              AItem->getName(), BItem->getLocation());
  }

  // Template function to check a vector of definitions to make sure
  // each name is unique. Print an error message for each duplicate found.
  // Don't bother checking empty names associated with implied operands.
  template <typename A> void findDuplicates(const std::vector<A *> &Items) {
    for (unsigned I = 0; I < Items.size(); I++)
      for (unsigned J = I + 1; J < Items.size(); J++)
        if (!Items[I]->getName().empty() &&
            Items[I]->getName() == Items[J]->getName()) {
          ErrorLog(
              Items[J],
              "Duplicate definition of {0}\n     Previously defined at {1}",
              Items[J]->getName(), Items[I]->getLocation());
          break; // We only need to find the first duplicate for each item.
        }
  }

  // Check the member list of a resource definition for duplicate members.
  void findDuplicateMembers(ResourceDefList &Items) {
    for (auto *Item : Items)
      if (Item->isGroupDef())
        findDuplicates(Item->getMembers());
  }

  // Methods for looking up and matching operands in instructions.
  int getOperandIndex(const InstructionDef *Instr, const OperandRef *Operand,
                      RefType Type);
  bool compareOpndNames(const OperandDecl *Opnd, const IdList &Names);
  int findOperandName(const InstructionDef *Instruct, const IdList &Names,
                      RefType Type);
  int findOperand(const InstructionDef *Instr, const IdList &Name,
                  const std::string &OpndType, RefType Type);
  bool findOperandDerivation(const OperandDef *Derived,
                             const OperandDef *Operand) const;

  // Error and Warning management.
  template <typename... Ts>
  bool ErrorLog(const MdlItem *Item, const char *Fmt, Ts... Args);
  bool ErrorLog(const MdlItem *Item, const std::string &Msg) {
    writeMessage(Item, Msg);
    ErrorCount++;
    return true;
  }
  template <typename... Ts>
  void WarningLog(const MdlItem *Item, const char *Fmt, Ts... Args);
  void WarningLog(const MdlItem *Item, const std::string &Msg) {
    if (PrintWarnings || WarningsAreFatal) {
      std::string Prefix = !WarningsAreFatal ? "Warning: " : "";
      writeMessage(Item, Prefix + Msg);
      WarningCount++;
    }
  }

  int ErrorsSeen() const {
    return ErrorCount + (WarningsAreFatal ? WarningCount : 0);
  }
  int WarningsSeen() const { return WarningCount; }

  // Error logging: Avoid printing identical error messages.
  void writeMessage(const MdlItem *Item, const std::string &Msg);

  // Functions to manipulate user-defined predicates. Predicates are defined
  // in an instruction-independent manner, yet in the MDL compiler we apply
  // each predicate to each associated instruction, so that we can partially
  // (and often completely) eliminate the predicate at compiler-build time.
  int predOperandIndex(const PredExpr *Pred, const InstructionDef *Instr);

  PredExpr *evaluatePredicate(std::string Name, const InstructionDef *Instr);
  PredExpr *evaluatePredicate(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predSimple(PredExpr *Pred, const InstructionDef *Instr) {
    return Pred;
  }
  PredExpr *predEvalName(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckAny(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckAll(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckNot(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckOpcode(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckIsReg(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckReg(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckInvalidReg(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckSameReg(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckNumOperand(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckIsImm(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckImm(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predCheckZero(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predOpcodeSwitchStmt(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predOpcodeSwitchCase(PredExpr *Pred, const InstructionDef *Instr);
  PredExpr *predReturnStatement(PredExpr *Pred, const InstructionDef *Instr);

  // Functions to simpify predicates (this largely implements De Morgan's laws
  // on predicate expressions.
  void simplifyPredicates();
  PredExpr *predSimplify(PredExpr *Expr);

  // Interfaces to the instruction predicate table.
  bool isValidInstructionPredicate(const std::string &Name) const {
    return PredicateTable.count(Name);
  }
  // Look up a predicate by name, and return the associated predicate.
  // If the predicate maps to a name, recur on that name.
  PredExpr *lookupPredicate(PredExpr *Pred);

  void enterPredicate(const std::string &Name, PredExpr *Pred) {
    if (!isValidInstructionPredicate(Name)) {
      PredicateTable[Name] = Pred;
      return;
    }
    if (Name == "TruePred" || Name == "FalsePred")
      return;
    ErrorLog(Pred, "Redefinition of predicate: {0}", Name);
  }

  // Class member accessors
  std::map<std::string, PredExpr *> &getPredicateTable() {
    return PredicateTable;
  }
  PipeDefList &getPipePhases() { return PipeDefs; }
  ResourceDefList &getResources() { return Resources; }
  RegisterDefList &getRegisters() { return Registers; }
  RegisterClassList &getRegClasses() { return RegClasses; }
  CpuList &getCpus() { return Cpus; }
  FuncUnitList &getFuncUnits() { return FuncUnits; }
  FuncUnitGroupList &getFuncUnitGroups() { return FuncUnitGroups; }
  SubUnitList &getSubunits() { return Subunits; }
  LatencyList &getLatencies() { return Latencies; }
  InstructionList &getInstructions() { return Instructions; }
  OperandDefList &getOperands() { return Operands; }

  FuncUnitDict &getFuMap() { return FuMap; }
  FuncUnitGroupDict &getFuGroupMap() { return FuGroupMap; }
  SubUnitDict &getSuMap() { return SuMap; }
  LatencyDict &getLatMap() { return LatMap; }
  OperandDict &getOperandMap() { return OperandMap; }
  InstructionDict &getInstructionMap() { return InstructionMap; }
  RegisterClassDict &getRegClassMap() { return RegisterClassMap; }
  SubUnitInstantiations &getSuInstantiations() { return SuInstantiations; }

  bool isFuncUnitTemplate(const std::string &Name) const {
    return FuMap.count(Name);
  }
  bool isFuncUnitGroup(const std::string &Name) const {
    return FuGroupMap.count(Name);
  }

  void setFamilyName(const Identifier *Name) {
    if (!FamilyName.empty() && FamilyName != Name->getName())
      ErrorLog(Name, "Incompatible family name specification");
    else
      FamilyName = Name->getName();
  }
  std::string getFamilyName() const { return FamilyName; }

  bool hasExplicitFuRefs() const { return HasExplicitFuRefs; }
  void setExplicitFuRefs() { HasExplicitFuRefs = true; }
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
  return getSpec()->ErrorLog(item, fmt, params...);
}

template <typename... Ts>
void SubUnitInstantiation::WarningLog(const MdlItem *item, const char *fmt,
                                      Ts... params) const {
  getSpec()->WarningLog(item, fmt, params...);
}

template <typename... Ts>
bool FuncUnitInstantiation::ErrorLog(const MdlItem *item, const char *fmt,
                                     Ts... params) const {
  return getSpec()->ErrorLog(item, fmt, params...);
}
template <typename... Ts>
void FuncUnitInstantiation::WarningLog(const MdlItem *item, const char *fmt,
                                       Ts... params) const {
  getSpec()->WarningLog(item, fmt, params...);
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

bool findDerivation(OperandDef *ref, const OperandDef *decl,
                    OperandDefList &Opnds);
OperandAttribute *findAttribute(const std::string &name, const OperandDef *Opnd,
                                const SubUnitInstantiation *subunit);

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_H_

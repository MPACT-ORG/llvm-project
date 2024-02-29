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
PhaseName *MdlSpec::findFirstPhase() {
  if (FirstPhaseName != nullptr)
    return FirstPhaseName;
  if (PipeDefs.empty() || PipeDefs[0]->getPhaseNames()->empty())
    return FirstPhaseName = new PhaseName("E1");

  return FirstPhaseName = (*PipeDefs[0]->getPhaseNames())[0];
}

//---------------------------------------------------------------------------
// Find the first pipeline phase from the first pipeline definition.  This
// is used as a "default" pipe phase identifier for implicit resources.
// First look in the specified CPU, then the top-level spec.  If you don't
// find it there, try "E1".  If you don't find that, use first phase.
//---------------------------------------------------------------------------
PhaseName *MdlSpec::findFirstExecutePhase(CpuInstance *Cpu) {
  if (Cpu != nullptr && !Cpu->getPipePhases()->empty())
    for (auto *PipeDef : *Cpu->getPipePhases())
      if (PipeDef->getFirstExecutePhaseName() != nullptr)
        return PipeDef->getFirstExecutePhaseName();

  for (auto *PipeDef : PipeDefs)
    if (PipeDef->getFirstExecutePhaseName() != nullptr)
      return PipeDef->getFirstExecutePhaseName();

  auto First = Identifier("E1");
  if (auto *Phase = searchPipeReference(&First, Cpu))
    return Phase;
  return findFirstPhase();
}

//---------------------------------------------------------------------------
// Conversions between strings and reference types.
//---------------------------------------------------------------------------
// For parsing the mdl input file.
RefType convertStringToRefType(const std::string &Type) {
  static std::map<std::string, RefType> *Mapping =
      new std::map<std::string, RefType>({{"predicate", RefTypes::kPred},
                                          {"use", RefTypes::kUse},
                                          {"def", RefTypes::kDef},
                                          {"kill", RefTypes::kKill},
                                          {"usedef", RefTypes::kUseDef},
                                          {"hold", RefTypes::kHold},
                                          {"res", RefTypes::kReserve},
                                          {"fus", RefTypes::kFus}});
  if (Mapping->count(Type))
    return Mapping->at(Type);
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
std::string convertRefTypeToString(RefType Type) {
  static const char *Names[] = {"null",   "predicate", "use",     "def", "kill",
                               "usedef", "hold",      "reserve", "fus"};
  if (Type < RefTypes::kNull || Type > RefTypes::kFus)
    return "RefNull";
  return Names[RefMap[static_cast<int>(Type)]];
}

// For writing out the database.
std::string formatReferenceType(RefType Type) {
  static const char *Names[] = {"RefNull", "RefPred",    "RefUse",
                               "RefDef",  "RefKill",    "RefUseDef",
                               "RefHold", "RefReserve", "RefFus"};
  if (Type < RefTypes::kNull || Type > RefTypes::kFus)
    return "RefNull";
  return Names[RefMap[static_cast<int>(Type)]];
}

// For writing out aggregate references (where they are ORed together).
std::string formatReferenceTypes(int Type) {
  std::string out;
  if (Type & static_cast<int>(RefTypes::kPred))
    out += " Predicate";
  if (Type & static_cast<int>(RefTypes::kUse))
    out += " Use";
  if (Type & static_cast<int>(RefTypes::kDef))
    out += " Def";
  if (Type & static_cast<int>(RefTypes::kKill))
    out += " Kill";
  if (Type & static_cast<int>(RefTypes::kUseDef))
    out += " UseDef";
  if (Type & static_cast<int>(RefTypes::kHold))
    out += " Hold";
  if (Type & static_cast<int>(RefTypes::kReserve))
    out += " Reserve";
  if (Type & static_cast<int>(RefTypes::kFus))
    out += " Fus";
  return formatv("<{0}>", out.substr(1));
}

//---------------------------------------------------------------------------
// Create a subpool descriptor for a resource reference.
//---------------------------------------------------------------------------
SubPool::SubPool(const ResourceRef *Res) {
  if (Res->isGroupRef()) {
    First = 0;
    Last = Res->getDefinition()->getMembers().size() - 1;
  } else {
    First = Res->getFirst();
    Last = Res->getLast();
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
void MdlSpec::checkInstructionSubunits() {
  MdlItem Item;
  std::string Unit = "$pseudo_unit";
  int PseudoSubunits = 0;

  for (auto *Instr : Instructions)
    if (Instr->getSubunits()->empty()) {
      auto *Refs = new ReferenceList;
      // Create reference objects for each register-based operand. There's a
      // few complexities to this. Register class operands can be embedded
      // in other operands, and we need to properly represent the operand
      // hierarchy in the reference. Register-specific references can just
      // reference the register directly.
      for (const auto *Opnd : *Instr->getFlatOperands()) {
        auto *Back = Opnd->getTypes()->back();
        auto *Front = Opnd->getTypes()->front();
        OperandRef *RefOpnd = nullptr;
        if (auto *RClass = FindItem(RegClasses, Back->getName())) {
          RefOpnd = new OperandRef(Front, Opnd->getOpNames(), RClass);
        } else if (FindItem(Registers, Back->getName()) != nullptr) {
          RefOpnd = new OperandRef(Item, nullptr, new IdList(1, Back));
        }
        RefType Type = Opnd->isInput() ? RefTypes::kUse : RefTypes::kDef;
        auto *Phase = findFirstExecutePhase(nullptr);
        Refs->push_back(new Reference(Type, Phase, RefOpnd));
      }
      // Create an explicit reference to the functional unit.
      auto *Phase = findFirstPhase();
      Refs->push_back(new Reference(RefTypes::kFus, Phase, Unit));

      // We create new templates for the latency and subunit, and new
      // instances for both, then add them to the appropriate spec tables.
      auto LatName = formatv("$latency{0}", PseudoSubunits);
      auto SuName = formatv("$subunit{0}", PseudoSubunits++);
      auto *Latency = new LatencyTemplate(LatName, Refs);
      auto *Instance = new LatencyInstance(LatName);
      auto *Subunit = new SubUnitTemplate(SuName, Instance, Latency);

      // Add the latency and subunit templates to the global sets of units.
      Latencies.push_back(Latency);
      Subunits.push_back(Subunit);
      SuInstantiations[SuName] = new std::vector<SubUnitInstantiation *>;

      // Add the latency and subunit templates to the dictionaries.
      LatMap.emplace(Latency->getName(), Latency);
      SuMap.emplace(Subunit->getName(), Subunit);

      // Add a subunit instance to the instruction.
      Instr->getSubunits()->push_back(new Identifier(SuName));
    }

  if (PseudoSubunits == 0)
    return;

  // Add an implicitly defined functional unit template to the dictionary.
  FuMap.emplace(Unit, new FuncUnitTemplate(new Identifier(Unit)));

  // Add the pseudo unit to the first cluster of each cpu instance.
  for (auto *Cpu : Cpus)
    if (!Cpu->getClusters()->empty())
      Cpu->getClusters()->front()->getFuncUnits()->push_back(
          new FuncUnitInstance(Unit));
}

//---------------------------------------------------------------------------
// If a subunit referenced a CPU in an fus clause, create a "fake" functional
// unit and add the subunit to that.
//---------------------------------------------------------------------------
void MdlSpec::addSubunitToCpu(CpuInstance *Cpu, SubUnitTemplate *subunit) {
  auto FuTempName = catchallName(Cpu->getName());

  // If the CPU doesn't have a catchall functional unit, create a CPU-specific
  // functional unit template, and add an instance of it to the CPU.
  auto *Cluster = (*Cpu->getClusters())[0];
  auto *Fu = FindItem(*Cluster->getFuncUnits(), FuTempName);
  if (Fu == nullptr) {
    auto *FuTemplate = new FuncUnitTemplate(new Identifier(FuTempName));
    FuMap[FuTempName] = FuTemplate;
    Cluster->getFuncUnits()->push_back(Fu = new FuncUnitInstance(FuTempName));
    Fu->setTemplate(FuTemplate);
  }

  // If the subunit hasn't been added previously, add it now.
  if (!FindItem(*Fu->getTemplate()->getSubunits(), subunit->getName())) {
    auto *Instance = new SubUnitInstance(*subunit, subunit->getType());
    Instance->setTemplate(subunit);
    Fu->getTemplate()->addSubunitInstance(Instance);
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
void MdlSpec::findLatencyFuncUnits(ReferenceList *References,
                                   LatencyFuncUnits &FuncUnits) {
  for (auto *Ref : *References) {
    if (Ref->isConditionalRef()) {
      for (auto *Cr = Ref->getConditionalRef(); Cr; Cr = Cr->getElseClause())
        findLatencyFuncUnits(&Cr->getRefs(), FuncUnits);
    } else if (Ref->getRefType() == RefTypes::kFus) {
      for (auto *Res : *Ref->getResources()) {
        if (FindItem(FuMap, Res->getName()) ||
            FindItem(Cpus, Res->getName())) {
          FuncUnits.addUnit(Res->getName(), Ref->getPredicates());
        } else if (auto *Group = FindItem(FuGroupMap, Res->getName())) {
          if (Group->getMembers()->size() > 1) {
            for (auto *Fu : *Group->getMembers())
              FuncUnits.addUnit(Fu->getName(), Ref->getPredicates());
          } else {
            Res->rename((*Group->getMembers())[0]);
            FuncUnits.addUnit(Res->getName(), Ref->getPredicates());
          }
        } else {
          ErrorLog(Res, "Invalid functional unit specifier: {0}",
                   Res->getName());
        }
      }
    }
  }
}

//---------------------------------------------------------------------------
// Find the set of explicitly referenced FUS for each latency template.
//---------------------------------------------------------------------------
void MdlSpec::findLatencyFuncUnits(LatencyTemplate *Lat) {
  if (Lat->getReferencedFus()) return;

  // Find all the functional units mentioned by this latency template.
  auto *fus = new LatencyFuncUnits;
  Lat->setReferencedFus(fus);
  findLatencyFuncUnits(Lat->getReferences(), *fus);

  // Find referenced fu sets for base units.
  if (Lat->getBaseIds())
    for (auto *Base : *Lat->getBaseIds()) {
      findLatencyFuncUnits(LatMap[Base->getName()]);
      fus->addBaseUnits(LatMap[Base->getName()]->getReferencedFus());
    }
}

//---------------------------------------------------------------------------
// For each functional unit template, enumerate all CPUs that instantiate it,
// including all uses of the unit as a subunit of another template.
//---------------------------------------------------------------------------
void MdlSpec::findFunctionalUnitClientCpus(FuncUnitTemplate *Funit,
                                           CpuInstance *Cpu) {
  Funit->addClientCpu(Cpu->getName());
  for (auto *Base : Funit->getUnitBases())
    findFunctionalUnitClientCpus(Base, Cpu);
}

void MdlSpec::findFunctionalUnitClientCpus() {
  for (auto *Cpu : Cpus)
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *Funit : *Cluster->getFuncUnits())
        findFunctionalUnitClientCpus(Funit->getTemplate(), Cpu);
}

//---------------------------------------------------------------------------
// For each CPU, build a vector of functional unit instances for each used
// functional unit template.  This is used when we're writing out fus()
// records.
//---------------------------------------------------------------------------
void MdlSpec::buildFuncUnitInstancesMap() {
  for (auto *Cpu : Cpus)
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *Funit : Cluster->getFuInstantiations()) {
        auto &Name = Funit->getFuncType()->getName();
        if (!isCatchallName(Name) && !isBasedFuInstance(Name))
          Cpu->getFuncUnitInstances()[Name].push_back(Funit);
      }
}

bool hasValidPredicate(IdList *Predicates, CpuInstance *Cpu,
                       MdlSpec *spec) {
  if (Predicates == nullptr) return true;
  for (auto *Pred : *Predicates)
    if (Pred->getName() == Cpu->getName()) return true;
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
void MdlSpec::tieSubUnitsToFunctionalUnits() {
  // If there were no Fus records seen, we can skip this (expensive) work.
  if (!hasExplicitFuRefs()) return;

  // For each latency template, create a set of functional units it references.
  for (auto *Lat : Latencies)
    findLatencyFuncUnits(Lat);

  // For each functional unit template, find the CPU's that instantiate it.
  findFunctionalUnitClientCpus();

  // For each CPU, for each subunit template, for each latency, find any
  // explicit references to functional units.  We reiterate the search for
  // subunit (for each CPU) so that we can look for valid CPU-based predicates.
  for (auto *Cpu : Cpus) {
    for (auto *Subunit : Subunits) {
      std::set<std::string> FuncUnits;
      if (Subunit->getLatencies()) {
        for (auto *Latency : *Subunit->getLatencies())
          if (hasValidPredicate(Latency->getPredicates(), Cpu, this))
            if (auto *LatFus = Latency->getTemplate()->getReferencedFus())
              for (auto &[Pred, Funits] : LatFus->getFuncUnits())
                if (Pred.empty() || Pred == Cpu->getName())
                  for (auto &Funit : Funits)
                    FuncUnits.insert(Funit);
      }

      if (FuncUnits.empty()) continue;
      // If we found functional units for this subunit (for this CPU), add the
      // subunit to that CPU.
      // If the subunit template has parameters, print an error, since we
      // can't support that (implicitly defined functional units can't pass
      // parameters to the subunit instance.
      if (!Subunit->getParams()->empty()) {
        ErrorLog(Subunit, "Subunits/Latencies with parameters cannot reference"
                          " implicitly defined functional units: {0}",
                          Subunit->getName());
        break;
      }
      bool Found = true;
      for (auto &Unit : FuncUnits) {
        // If the latency references a CPU name, add the subunit to that CPU.
        if (auto *Cpu = FindItem(Cpus, Unit)) {
          addSubunitToCpu(Cpu, Subunit);
          continue;
        }
        // Annotate this template as explicitly referenced.
        FuMap[Unit]->setReferenced();

        // We only allow explicit functional unit references (fus) to
        // reference implicitly defined functional units. Referencing an
        // explicitly defined functional unit template is not supported,
        // so we issue a warning and ignore the reference.
        if (!FuMap[Unit]->isImplicitlyDefined()) {
          WarningLog(Subunit, "Invalid reference to an explicitly defined "
                              "functional unit \"{0}\"", Unit);
          Found = false;
          continue;
        }
        // If the current CPU is not a client of any mentioned unit, we will
        // not add this subunit to this CPU.
        if (!FuMap[Unit]->getClientCpus().count(Cpu->getName()))
          Found = false;
      }
      if (Found) addSubunitToCpu(Cpu, Subunit);
    }
  }

  // Since we've defined all implicitly defined functional units, and now
  // checked for references to them, check that implicitly defined templates
  // have references. Unreferenced implicit functional unit templates are
  // reported as a warning.  Implicitly defined based functional units must
  // have at least one base referenced.
  for (auto [Name, Fu] : FuMap)
    if (Fu->isImplicitlyDefined() && !Fu->isReferenced() &&
        !isCatchallName(Name)) {
      bool FoundRef = false;
      for (auto *Base : *Fu->getBases())
        if (FuMap[Base->getName()]->isReferenced()) {
          FoundRef = true;
          break;
        }
      if (!FoundRef)
        WarningLog(Fu, "Undefined functional unit \"{0}\"", Name);
    }
}

// Helper function for recursively adding derived subunits to instructions.
static void addDerivedSubUnits(InstructionDef *Instruction,
                               SubUnitTemplate *Subunit) {
  for (auto *DerivedUnit : Subunit->getDerivedSubunits()) {
    Instruction->addSubunit(DerivedUnit);
    addDerivedSubUnits(Instruction, DerivedUnit);
  }
}

// Helper function to determine if a regular expression has a prefix that we
// can search for. Generally, this is anything up to the first metacharacter.
// However, if the expression has a top level | or ? operator, we can't
// define a prefix.
static std::string getPrefix(std::string &Regex) {
  static const char meta[] = "()^$*+?.[]\\{}";
  auto FirstMeta = Regex.find_first_of(meta);
  if (FirstMeta == std::string::npos)
    return Regex;

  int Param = 0;
  for (char Ch : Regex) {
    if (Ch == '(')
      Param++;
    else if (Ch == ')')
      Param--;
    else if ((Ch == '|' || Ch == '?') && Param == 0)
      return "";
  }
  return Regex.substr(0, FirstMeta);
}

//---------------------------------------------------------------------------
// Given a list of regular expressions, add the subunit to each matched
// instruction.  Following tablegen's format, these aren't *quite* regular
// expressions in that they are always prefix searches - we must match
// the whole instruction name.
//---------------------------------------------------------------------------
void MdlSpec::tieSubUnitToInstructions(SubUnitTemplate *Subunit,
                                       StringList *RegExBases) {
  if (RegExBases == nullptr)
    return;

  // We can speed the searches where the expression has an alphanumeric prefix,
  // by only searching names that begin with that prefix.
  for (auto &regex : *RegExBases) {
    auto Prefix = getPrefix(regex);
    auto Pattern = regex.substr(Prefix.size());

    std::optional<llvm::Regex> Rex;
    if (!Pattern.empty()) {
      if (Pattern[0] != '^')
        Pattern = formatv("^({0})", Pattern);
      Rex = llvm::Regex(Pattern);
    }

    // If we see a prefix, we can narrow the range of instructions searched.
    bool Match = false;
    auto End = InstructionMap.end();
    auto Begin = InstructionMap.begin();
    if (!Prefix.empty())
      Begin = InstructionMap.lower_bound(Prefix);

    // If we don't have a prefix, we need to search every single instruction.
    if (Prefix.empty()) {
      for (auto Itr = Begin; Itr != End; ++Itr)
        if (Rex->match(Itr->first)) {
          Itr->second->addSubunit(Subunit);
          Match = true;
        }
    } else {
      // If we have a prefix, only search instructions with that prefix.
      for (auto Itr = Begin; Itr != End; ++Itr) {
        if (Itr->first.compare(0, Prefix.size(), Prefix) != 0)
          break;
        if (!Rex || Rex->match(Itr->first.substr(Prefix.size()))) {
          Itr->second->addSubunit(Subunit);
          Match = true;
        }
      }
    }
    if (!Match)
      ErrorLog(Subunit, "Unmatched base instruction expression \"{0}\"", regex);
  }
}

//---------------------------------------------------------------------------
// Tie each derived subunit to each instruction that uses any of its base
// subunits (recursively).
//---------------------------------------------------------------------------
void MdlSpec::tieDerivedSubUnitsToInstructions() {
  for (auto *Instruction : Instructions)
    if (auto *Subunits = Instruction->getSubunits()) {
      IdList BaseSubunits = *Subunits; // We're going to add to vector...
      for (auto *Subunit : BaseSubunits)
        addDerivedSubUnits(Instruction, SuMap[Subunit->getName()]);
    }
}

//---------------------------------------------------------------------------
// If a merged definition has allocation information, make sure it's correct.
//---------------------------------------------------------------------------
ResourceRef *FuncUnitInstantiation::checkAllocation(ResourceRef *Def,
                                                    ResourceRef *Ref) {
  int Count = Ref->getPoolCount();
  Identifier *CountName = Ref->getPoolCountName();
  Identifier *ValueName = Ref->getValueName();

  // Copy any allocation information from the reference to the definition.
  Def->setValueName(ValueName);
  Def->setPoolCountName(CountName);
  Def->setPoolCount(Count);

  // Return if there is no allocation request, or the request is symbolic.
  // (We will check symbolic sizes later).
  if (!Ref->hasCount() || CountName)
    return Def;

  // Check array references.
  // Array allocations must be non-zero and between 1 and the pool size.
  if (Def->isArrayDef()) {
    if (Count == 0 || Count > Def->getPoolSize()) {
      ErrorLog(Ref, "Invalid resource allocation size: {0}", Count);
      return nullptr;
    }
    // The pool size must evenly divide the number of entries in the pool.
    if (Def->getPoolSize() % Count != 0) {
      ErrorLog(Ref, "Pool count must evenly divide the resource pool size");
      return nullptr;
    }
    return Def;
  }
  // Allocation for everything else must be 1.
  if (Count != 1) {
    ErrorLog(Ref, "Invalid resource allocation size: {0}", Count);
    return nullptr;
  }
  return Def;
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
ResourceRef *FuncUnitInstantiation::mergeRefs(ResourceRef *Def,
                                              ResourceRef *Ref) {
  if (Def->isNull())
    return new ResourceRef(*Def);

  // Case 1: if the reference is unqualified, just return the Def.
  if (Ref->isUnqualifiedRef())
    return checkAllocation(new ResourceRef(*Def), Ref);

  // Case 2: look up the member reference, and return a reference to the
  // group's promoted resource.
  if (Def->isGroupRef() && Ref->getMember()) {
    auto *Mem = FindItem(Def->getDefinition()->getMembers(),
                         Ref->getMember()->getName());
    if (Mem == nullptr) {
      ErrorLog(Ref, "Resource member not found: {0}",
               Ref->getMember()->getName());
      return nullptr;
    }
    auto *Member = Def->getDefinition()->getMemberDef(Mem->getIndex());
    return new ResourceRef(Member);
  }

  // Case 3 and 4: Ensure the subrange is a subset of the def's range.
  // Note: All subranges are zero based relative to the original pool def.
  // But in general we don't want successive qualifications to make the
  // subrange larger.
  if (Def->isArrayDef() && (Ref->isSubrange() || Ref->isIndexed())) {
    if (Ref->getFirst() < Def->getFirst() || Ref->getLast() > Def->getLast()) {
      if (Ref->isIndexed())
        ErrorLog(Ref, "Invalid resource pool index: {0}; expected [{1}..{2}]",
                 Ref->getFirst(), Def->getFirst(), Def->getLast());
      else
        ErrorLog(Ref, "Invalid resource pool subrange");
      return nullptr;
    }
    auto *QualifiedRef = new ResourceRef(*Def);
    QualifiedRef->setSubrange(Ref->getFirst(), Ref->getLast());
    return checkAllocation(QualifiedRef, Ref);
  }

  // Member references cannot be further qualified.
  if (Def->getMember()) {
    ErrorLog(Ref, "Invalid member reference qualification");
    return nullptr;
  }
  // Member references can only be used with grouped resources.
  if (Ref->getMember()) {
    ErrorLog(Ref, "Invalid member reference: {0}", Ref->getMember()->getName());
    return nullptr;
  }

  // For everything else, check any pool allocations.
  if (Def->isGroupDef() || Def->isArrayDef())
    return checkAllocation(new ResourceRef(*Def), Ref);

  ErrorLog(Ref, "Invalid resource qualifiers");
  return nullptr;
}

//----------------------------------------------------------------------------
// Implementations of FuncUnitInstantiation methods.
//----------------------------------------------------------------------------

// Create definition objects for each locally defined reference and port.
void FuncUnitInstantiation::instantiateLocalDefs() {
  for (auto *Res : *getTemplate()->getResources())
    getResources().push_back(new ResourceDef(*Res));
  for (auto *Port : *getTemplate()->getPorts())
    getPorts().push_back(new ResourceDef(Port));
}

// Look up a register class in the template's parameter list.
RegisterClass *FuncUnitInstantiation::findRegClass(Identifier *Item) {
  if (auto *Arg = FindItem(getClassArgs(), Item->getName()))
    return Arg->getRegs();
  return nullptr;
}

// Bind a functional unit instantiation parameter to a register class.
void FuncUnitInstantiation::bindClassArg(ResourceRef *Arg) {
  getClassArgs()[Arg->getParameter()->getName()] = bindFuncUnitClass(Arg);
}

// Bind a functional unit instantiation parameter to a resource reference.
void FuncUnitInstantiation::bindResourceArg(ResourceRef *Arg) {
  getResourceArgs()[Arg->getParameter()->getName()] = bindFuncUnitResource(Arg);
}

// Map a functional unit instantiation parameter id to its bound class.
RegisterClassRef *FuncUnitInstantiation::getClassArg(int ParamId) {
  return getClassArgs()[(*getTemplate()->getParams())[ParamId]->getName()];
}

// Map a functional unit instantiation parameter id to its bound resource.
ResourceRef *FuncUnitInstantiation::getResourceArg(int ParamId) {
  return getResourceArgs()[(*getTemplate()->getParams())[ParamId]->getName()];
}

// Given a predicate for a subunit or latency instance, determine if it
// matches the instantiation context's cpu name, functional unit name, or
// functional unit template type.
bool FuncUnitInstantiation::isValidPredicate(IdList *Predicates) const {
  if (Predicates == nullptr)
    return true;
  for (auto *Id : *Predicates) {
    if (Id->getName() == Cpu->getName() ||
        Id->getName() == Instance->getName() ||
        Id->getName() == FuncType->getName() ||
        Spec->isValidInstructionPredicate(Id->getName()))
      return true;
    Spec->isValidPredicateName(Id);
  }
  return false;
}

//---------------------------------------------------------------------------
// For each subunit instance in a functional unit instantiation, create a
// subunit instantiation, bind its instance parameters, and instantiate
// all of its latency instances.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::instantiateSubunits() {
  for (auto *Instance : *getTemplate()->getSubunits())
    if (isValidPredicate(Instance->getPredicates())) {
      auto *Subunit = new SubUnitInstantiation(this, Instance);
      bindSubUnitParameters(Subunit);
      Subunit->instantiateLatencies();
      Spec->addSubUnitInstantiation(Subunit);
    }
}

//---------------------------------------------------------------------------
// Process connect statements. Find the connected resources and register
// classes, do some error checking.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::processConnects() {
  for (auto *Connect : *getTemplate()->getConnections()) {
    // First make sure the referenced port definition exists.
    auto *Port = FindItem(getPorts(), Connect->getName());
    if (Port == nullptr) {
      ErrorLog(Connect, "Port not found: {0}", Connect->getName());
      return;
    }

    // If a register class is specified, find it, either as an incoming
    // argument or globally defined.
    if (Connect->getRegClass()) {
      auto *Name = Connect->getRegClass();
      auto *RegClass = findRegClass(Name);
      if (RegClass == nullptr)
        RegClass = FindItem(getSpec()->getRegClasses(), Name->getName());
      if (RegClass == nullptr) {
        ErrorLog(Connect, "Register class not found: {0}", Name->getName());
        return;
      }
      Port->setRegClass(RegClass);
    }

    // If a resource reference was provided, verify it.
    if (auto *Resource = Connect->getResource()) {
      ResourceRef *Ref = nullptr;
      if (auto *Res = FindItem(getResourceArgs(), Resource->getName())) {
        ResourceRef Arg(*Res);
        Ref = mergeRefs(&Arg, Resource);
      } else if (auto *Def = FindItem(getResources(), Resource->getName())) {
        ResourceRef Arg(Def);
        Ref = mergeRefs(&Arg, Resource);
      }

      if (Ref == nullptr)
        ErrorLog(Connect, "Resource not found: {0}", Resource->getName());
      else
        Port->setPortResource(Ref);
    }
    if (ErrorsSeen())
      return;
  }
}

//---------------------------------------------------------------------------
// Bind a subunit instance port argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceDef *FuncUnitInstantiation::bindSubUnitPort(ResourceRef *Arg) {
  if (Arg->isNull())
    return NullPortDef;
  if (auto *PortArg = FindItem(getPorts(), Arg->getName()))
    return PortArg;

  ErrorLog(Arg, "Port argument not found: {0}", Arg->getName());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a subunit resource argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceRef *FuncUnitInstantiation::bindSubUnitResource(ResourceRef *Arg) {
  // If this is a "null" binding, just return null.
  if (Arg->isNull())
    return NullResourceRef;

  // Search for the resource definition in arguments and FU-level definitions.
  if (auto *Resource = FindItem(getResourceArgs(), Arg->getName())) {
    ResourceRef Ref(*Resource);
    return mergeRefs(&Ref, Arg);
  }
  if (auto *Resource = FindItem(getResources(), Arg->getName())) {
    ResourceRef Def(Resource);
    return mergeRefs(&Def, Arg);
  }

  ErrorLog(Arg, "Resource argument not found: {0}", Arg->getName());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a functional unit instance resource argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceRef *FuncUnitInstantiation::bindFuncUnitResource(ResourceRef *Arg) {
  // If this is a "null" binding, just return null.
  if (Arg->isNull())
    return NullResourceRef;

  // Search for resource definition in the cluster and CPU level.
  ResourceDef *Def;
  if ((Def = FindItem(*Cluster->getResources(), Arg->getName())) ||
      (Def = FindItem(*Cpu->getResources(), Arg->getName()))) {
    ResourceRef Ref(Def);
    return mergeRefs(&Ref, Arg);
  }

  ErrorLog(Arg, "Resource argument not found: {0}", Arg->getName());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a functional unit instance register class argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
RegisterClassRef *FuncUnitInstantiation::bindFuncUnitClass(ResourceRef *Arg) {
  // If this is a "null" binding, just return null.
  if (Arg->isNull())
    return new RegisterClassRef(NullRegisterClass);

  // Look up the register class in the global class table.
  if (auto *Item = FindItem(getSpec()->getRegClasses(), Arg->getName()))
    return new RegisterClassRef(Item);

  // If we don't find the class, but find a register definition, create a
  // custom class that contains just that single register (a common case).
  if (RegisterDef *Reg = FindItem(getSpec()->getRegisters(), Arg->getName())) {
    auto *Members = new RegisterDefList;
    Members->push_back(Reg);
    std::string NewName = "[" + Arg->getName() + "]";
    return new RegisterClassRef(
        new RegisterClass(*Arg, new Identifier(NewName), Members));
  }

  ErrorLog(Arg, "Register class argument not found: {0}", Arg->getName());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind functional unit instantiation parameters to resources and classes.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::bindFuncUnitParameters() {
  auto &InstanceArgs = *Instance->getArgs();
  int NumParams = InstanceArgs.size();

  // Iterate over the parameters, bind the parameters of the instance
  // to the objects (register classes or resources) they refer to.
  for (int ArgId = 0; ArgId < NumParams; ArgId++) {
    if (InstanceArgs[ArgId]->getParameter()->isResource()) {
      bindResourceArg(InstanceArgs[ArgId]);
    } else {
      bindClassArg(InstanceArgs[ArgId]);
    }
  }
}

//---------------------------------------------------------------------------
// Look up functional unit pinning resources.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::bindFuncUnitSlotResources() {
  IdList *SlotsAny = Instance->getPinAny();
  IdList *SlotsAll = Instance->getPinAll();
  IdList *Slots = SlotsAny ? SlotsAny : SlotsAll;
  IdList *ImplicitSlots = nullptr;

  // If the instance wasn't pinned to any slots, and slots have been
  // declared for this cpu/cluster, create an "any" set of resources.
  if (Slots == nullptr && (Cluster->getIssues() || Cpu->getIssues())) {
    auto *Issues = Cluster->getIssues() ?
                   Cluster->getIssues() : Cpu->getIssues();
    Slots = SlotsAny = ImplicitSlots = new IdList;
    for (auto *Res : *Issues)
      Slots->push_back(Res->getId());
  }

  // Find the definition of any pin reference.
  auto ResourceList = new ResourceRefList;
  ResourceDef *Res;
  for (auto *Slot : *Slots) {
    if ((Res = FindItem(*Cluster->getIssues(), Slot->getName())) ||
        (Res = FindItem(*Cpu->getIssues(), Slot->getName())))
      ResourceList->push_back(new ResourceRef(Res));
    else
      ErrorLog(Res, "Issue slot resource not found: {0}", Slot->getName());
  }

  // Add the Slot references to the functional unit instance.
  if (SlotsAny)
    Instance->setResourceSlotsAny(ResourceList);
  else
    Instance->setResourceSlotsAll(ResourceList);

  if (ImplicitSlots != nullptr)
    delete ImplicitSlots;
}

//---------------------------------------------------------------------------
// Bind subunit instantiation parameters to ports and resources.
//---------------------------------------------------------------------------
void FuncUnitInstantiation::bindSubUnitParameters(SubUnitInstantiation *Su) {
  auto &InstanceArgs = *Su->getSubunit()->getArgs();
  int NumParams = InstanceArgs.size();

  for (int ArgId = 0; ArgId < NumParams; ArgId++)
    if (InstanceArgs[ArgId]->getParameter()->isResource())
      Su->bindResourceArg(InstanceArgs[ArgId]);
    else
      Su->bindPortArg(InstanceArgs[ArgId]);
}

//---------------------------------------------------------------------------
// Implementation of SubUnitInstantiation methods.
//---------------------------------------------------------------------------
// Bind a port definition to the specified subunit instantiation parameter.
void SubUnitInstantiation::bindPortArg(ResourceRef *Arg) {
  PortArgs[Arg->getParameter()->getName()] = FuncUnit->bindSubUnitPort(Arg);
}

// Bind a resource definition to the specified subunit instantiation parameter.
void SubUnitInstantiation::bindResourceArg(ResourceRef *Arg) {
  ResourceArgs[Arg->getParameter()->getName()] =
                               FuncUnit->bindSubUnitResource(Arg);
}

// Map a subunit instantiation parameter id to its bound resource.
ResourceRef *SubUnitInstantiation::getResourceArg(int ParamId) {
  return ResourceArgs[(*getSuTemplate()->getParams())[ParamId]->getName()];
}

// Map a subunit instantiation parameter id to its bound port.
ResourceDef *SubUnitInstantiation::getPortArg(int ParamId) {
  return PortArgs[(*getSuTemplate()->getParams())[ParamId]->getName()];
}

// Given a predicate for a latency instance, determine if it matches the
// instantiation context's cpu name or functional unit name.
bool SubUnitInstantiation::isValidPredicate(IdList *Predicates) const {
  return FuncUnit->isValidPredicate(Predicates);
}

//---------------------------------------------------------------------------
// Bind a latency instance port argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceDef *SubUnitInstantiation::bindLatPort(ResourceRef *Arg) {
  if (Arg->isNull())
    return NullPortDef;
  if (auto *PortArg = FindItem(PortArgs, Arg->getName()))
    return PortArg;

  ErrorLog(Arg, "Port argument not found: {0}", Arg->getName());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind a latency resource argument to its definition.
// Return the definition if found, if any errors are found return nullptr.
//---------------------------------------------------------------------------
ResourceRef *SubUnitInstantiation::bindLatResource(ResourceRef *Arg) {
  // If this is a "null" binding, just return null.
  if (Arg->isNull())
    return NullResourceRef;

  // Search for the resource definition in arguments an SU-level definitions.
  if (auto *Resource = FindItem(ResourceArgs, Arg->getName())) {
    ResourceRef ref(*Resource);
    return FuncUnit->mergeRefs(&ref, Arg);
  }

  ErrorLog(Arg, "Resource argument not found: {0}", Arg->getName());
  return nullptr;
}

//---------------------------------------------------------------------------
// Bind latency instantiation parameters to ports and resources.
//---------------------------------------------------------------------------
void SubUnitInstantiation::bindLatencyParams(LatencyInstantiation *Lat) {
  auto &InstanceArgs = *Lat->getLatency()->getArgs();
  int NumParams = InstanceArgs.size();

  for (int ArgId = 0; ArgId < NumParams; ArgId++) {
    if (InstanceArgs[ArgId]->getParameter()->isResource()) {
      Lat->bindResourceArg(InstanceArgs[ArgId]);
    } else {
      Lat->bindPortArg(InstanceArgs[ArgId]);
    }
  }
}

//---------------------------------------------------------------------------
// Bind latency reference resources to template parameters.
//---------------------------------------------------------------------------
void SubUnitInstantiation::bindLatencyResources(LatencyInstantiation &Lat,
                                                Reference *LatReference,
                                                ResourceRefList *Resources) {
  for (auto *Res : *Resources) {
    ResourceRef *ResRef = nullptr;
    if (auto *FoundRes = FindItem(Lat.getResourceArgs(), Res->getName())) {
      ResRef = FuncUnit->mergeRefs(FoundRes, Res);
    } else if (auto *Port = FindItem(Lat.getPortArgs(), Res->getName())) {
      LatReference->addPort(Port);
      if (auto *PortRes = Port->getPortResource())
        ResRef = FuncUnit->mergeRefs(PortRes, Res);
    } else if (!Res->isNull()) {
      ErrorLog(Res, "Resource undefined: {0}", Res->getName());
    }
    // If we have a valid resource reference, add it to the reference.
    if (ResRef == nullptr)
      continue;
    LatReference->addResource(ResRef);

    // Check for unqualified pool/group references.  If it's a group
    // reference, either use all the members or just one, depending on
    // how the group was defined. If it's an array reference, print an
    // error message.
    if (ResRef->isPooledResourceRef() && !Res->useAllMembers()) {
      if (ResRef->isGroupRef()) {
        if (ResRef->getDefinition()->getGroupType() == GroupType::kUseAll)
          ResRef->setUseAllMembers();
        else
          ResRef->setPoolCount(1); // Set pool allocation for group to 1.
      } else {                    // It's an array reference.
        ErrorLog(LatReference,
                 "Unqualified pool - use :* to reference whole pool: {0}",
                 ResRef->ToString());
      }
    }
  }
}

//---------------------------------------------------------------------------
// Recursively copy conditional references and all their references and
// else clauses.
//---------------------------------------------------------------------------
ConditionalRef *
SubUnitInstantiation::copyLatencyCondReference(LatencyInstantiation &Lat,
                                               ConditionalRef *Cond) {
  // Copy the else clause, if there is one.
  if (Cond == nullptr)
    return nullptr;
  auto *ElseClause = copyLatencyCondReference(Lat, Cond->getElseClause());

  // Make a copy of the conditional reference (and the copied else clause),
  // and copy the references associated with this condition.
  auto *Copy = new ConditionalRef(Cond, ElseClause);
  for (auto *Ref : Cond->getRefs())
    copyLatencyReference(Lat, Copy->getRefs(), Ref);
  return Copy;
}

//---------------------------------------------------------------------------
// When instantiating a latency, copy each reference, bind resources to
// instance parameters, and do some error checking.
//---------------------------------------------------------------------------
void SubUnitInstantiation::copyLatencyReference(LatencyInstantiation &Lat,
                                                ReferenceList &LatReferences,
                                                Reference *Ref) {
  if (!isValidPredicate(Ref->getPredicates()))
    return;
  // Recursively copy conditional references.
  if (Ref->isConditionalRef()) {
    auto *Cond = copyLatencyCondReference(Lat, Ref->getConditionalRef());
    LatReferences.push_back(new Reference(Ref, Cond));
    return;
  }
  // If the reference doesn't have a phase expression, give it one.
  auto *Phase = Ref->getPhaseExpr();
  if (Phase == nullptr)
    Phase = new PhaseExpr(Spec->findFirstExecutePhase(getCpu()));
  else
    Phase = Phase->clone();

  // Copy the reference, and for each resource reference, bind the named
  // resource to the value passed into the subunit instance resource or
  // port parameter.
  auto *NewReference = new Reference(Ref, Phase);
  if (!Ref->isFuncUnitRef())
    bindLatencyResources(Lat, NewReference, Ref->getResources());

  // Currently we don't allow holds/reserves on pooled resources.
  if (NewReference->getRefType() & (RefTypes::kHold | RefTypes::kReserve))
    for (auto *Res : *NewReference->getResources())
      if (Res->hasCount())
        ErrorLog(Ref, "Hold/reserve not supported on pool references: {0}",
                 Res->ToString());

  LatReferences.push_back(NewReference);
}

//---------------------------------------------------------------------------
// Add references from a latency template to a subunit. lat_template is
// passed in explicitly so that we can instantiate parents and bases.
//---------------------------------------------------------------------------
void SubUnitInstantiation::instantiateLatency(LatencyInstantiation &Lat,
                                              LatencyTemplate *LatTemplate) {
  for (auto *Reference : *LatTemplate->getReferences())
    copyLatencyReference(Lat, References, Reference);
}

//---------------------------------------------------------------------------
// Instantiate a latency template and all of its bases, recursively.
//---------------------------------------------------------------------------
void SubUnitInstantiation::instantiateLatencyBases(LatencyInstantiation &Lat,
                                                   LatencyTemplate *Parent,
                                                   LatencyList &Bases) {
  // There's no need to instantiate a latency template in a particular
  // subunit more than once (which is possible if you have multiple bases,
  // or recursive bases).
  if (std::find(Bases.begin(), Bases.end(), Parent) != Bases.end())
    return;
  Bases.push_back(Parent);

  instantiateLatency(Lat, Parent);
  if (ErrorsSeen())
    return;

  for (auto *Base : Parent->getUnitBases()) {
    instantiateLatencyBases(Lat, Base, Bases);
    if (ErrorsSeen())
      return;
  }
}

//---------------------------------------------------------------------------
// Instantiate all the latencies (and latency bases) associated with a
// subunit instantiation.
//---------------------------------------------------------------------------
void SubUnitInstantiation::instantiateLatencies() {
  if (SuTemplate->getLatencies() == nullptr)
    return;

  for (auto *Instance : *SuTemplate->getLatencies())
    if (isValidPredicate(Instance->getPredicates())) {
      LatencyInstantiation Latency(this, Instance);
      bindLatencyParams(&Latency);
      if (ErrorsSeen())
        return;
      LatencyList Bases; // used to avoid duplicates and recursion.
      instantiateLatencyBases(Latency, Instance->getTemplate(), Bases);
      if (ErrorsSeen())
        return;
    }
}

//---------------------------------------------------------------------------
// Implementation of LatencyInstantiation methods.
//---------------------------------------------------------------------------

// Bind a port definition to the specified latency instantiation parameter.
void LatencyInstantiation::bindPortArg(ResourceRef *Arg) {
  PortArgs[Arg->getParameter()->getName()] = Subunit->bindLatPort(Arg);
}

// Bind a resource definition to the specified latency instantiation parameter.
void LatencyInstantiation::bindResourceArg(ResourceRef *Arg) {
  ResourceArgs[Arg->getParameter()->getName()] = Subunit->bindLatResource(Arg);
}

// Map a latency instantiation parameter id to its bound resource.
ResourceRef *LatencyInstantiation::getResourceArg(int ParamId) {
  return ResourceArgs[(*LatTemplate->getParams())[ParamId]->getName()];
}

// Map a latency instantiation parameter id to its bound port.
ResourceDef *LatencyInstantiation::getPortArg(int ParamId) {
  return PortArgs[(*LatTemplate->getParams())[ParamId]->getName()];
}

//----------------------------------------------------------------------------
// Implementations of MdlSpec methods.
//----------------------------------------------------------------------------

// Create and add a Functional Unit instantiation to the mdl spec table.
FuncUnitInstantiation *
MdlSpec::addFuncUnitInstantiation(CpuInstance *Cpu, ClusterInstance *Cluster,
                                  FuncUnitInstance *FuInst) {
  auto *Fu = new FuncUnitInstantiation(this, Cpu, Cluster, FuInst);
  Cluster->addFuncUnitInstantiation(Fu);
  return Fu;
}

// Create a base function unit instance object and add to mdl spec table.
FuncUnitInstantiation *
MdlSpec::addFuncUnitBaseInstantiation(FuncUnitInstantiation *Parent,
                                      FuncUnitTemplate *Base) {
  auto *Fu = new FuncUnitInstantiation(Parent, Base);
  Parent->getCluster()->addFuncUnitInstantiation(Fu);
  return Fu;
}

// Recursively add base functional units to instantiated parents.
void MdlSpec::addFunctionalUnitBases(FuncUnitInstantiation *Parent) {
  auto *Root = Parent->getTemplate();
  for (auto *Base : Root->getUnitBases()) {
    if (Base == Root) {
      WarningLog(Base, "Recursive functional unit derivation, ignored");
      continue;
    }
    if (!Base->getUnitBases().empty())
      WarningLog(Base, "Nested functional unit derivation, ignored");

    auto *Fu = addFuncUnitBaseInstantiation(Parent, Base);
    Fu->setResource();
    Fu->processConnects();
    Fu->instantiateSubunits();
  }
}

// Instantiate a single functional unit, and all of its base units.
void MdlSpec::instantiateFunctionalUnit(CpuInstance *Cpu,
                                        ClusterInstance *Cluster,
                                        FuncUnitInstance *Fu) {
  auto *FuTop = addFuncUnitInstantiation(Cpu, Cluster, Fu);
  FuTop->setResource();
  // Bind parameters to their associated definitions, check for errors.
  // If errors found, don't try to instantiate any subunits.
  FuTop->bindFuncUnitParameters();
  if (ErrorsSeen())
    return;
  // After processing parameters, promote groups.
  promoteResourceGroupMembers(&FuTop->getResources(), nullptr,
                              &FuTop->getResourceArgs());
  // Bind pinning resources.
  FuTop->bindFuncUnitSlotResources();
  // Process connect statements and instantiate subunits.
  FuTop->processConnects();
  FuTop->instantiateSubunits();
  // For each base unit, create a separate instantiation with the same
  // parameters as the parent, and instantiate its subunits.
  addFunctionalUnitBases(FuTop);
}

// Instantiate every functional unit instance (in every CPU and cluster).
// Simply abort if any errors are found.
void MdlSpec::instantiateFunctionalUnits() {
  for (auto *Cpu : Cpus)
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *FuInst : *Cluster->getFuncUnits())
        instantiateFunctionalUnit(Cpu, Cluster, FuInst);

  if (ErrorsSeen())
    Abort();
}

bool IsValidPoolCount(const ResourceRef *Resource, const Reference *Ref,
                      int Count, const SubUnitInstantiation *Subunit) {
  if (Count == 0)
    return false;
  if (Count < 0) {
    Subunit->ErrorLog(Ref, "Negative allocation size");
    return false;
  }
  if (Count > Resource->getPoolSize()) {
    Subunit->ErrorLog(Ref, "Allocation size exceeds resource pool size: {0}",
                      Resource->ToString());
    return false;
  }
  return true;
}

// Update the subpool reference table for this reference.
void ResourceDef::addReferenceSizeToPool(const ResourceRef *Resource,
                                         const Reference *Ref,
                                         const SubUnitInstantiation *Subunit) {
  SubPool Pool(Resource);
  auto &PoolInfo = getSubPool(Pool);

  // If the pool has a defined pool count, just use it.
  if (Resource->getPoolCount() != -1) {
    if (IsValidPoolCount(Resource, Ref, Resource->getPoolCount(), Subunit)) {
      PoolInfo.addCount(Resource->getPoolCount());
      return;
    }
  }

  // If we have no idea what a symbolic size attribute is, just record the
  // worst case number (the whole pool).
  if (Ref == nullptr || Ref->getOperand() == nullptr) {
    PoolInfo.addCount(Resource->getPoolSize());
    return;
  }

  // Find the whole derivation of the operand (if any).
  auto *OpndRef = Ref->getOperand();
  auto *OpndBase = OpndRef->getOperandDecl()->getOperand();
  auto *OpndDef = OpndRef->getOperand();
  OperandDefList Opnds;
  if (!findDerivation(OpndDef, OpndBase, Opnds))
    return; // This is a panic, which will have already been seen and reported.

  // Make sure we find at least one occurrence of the named attribute.
  // If it's not found, set a worst-case pool count.
  auto &CountName = Resource->getPoolCountName()->getName();
  OperandAttribute *Attr = nullptr;
  for (auto *Opnd : Opnds)
    if ((Attr = findAttribute(CountName, Opnd, Subunit)) != nullptr)
      break;
  if (Attr == nullptr) {
    PoolInfo.addCount(Resource->getPoolSize());
    return;
  }

  // Walk through all the operand derivations, and find values associated with
  // the attribute, and add them to the resource pools definition.
  for (auto *Opnd : Opnds)
    for (auto *OpAttr : *Opnd->getAttributes())
      if (OpAttr->getName() == CountName)
        if (Subunit->isValidPredicate(OpAttr->getPredicate()))
          if (IsValidPoolCount(Resource, Ref, OpAttr->getValues(0), Subunit))
            PoolInfo.addCount(OpAttr->getValues(0));
}

// Annotate a resource with the attributes of a reference to it.
void ResourceDef::recordReference(RefType Type, const PhaseExpr *Expr,
                                  const ResourceRef *Resource,
                                  const Reference *Ref,
                                  const SubUnitInstantiation *Subunit) {
  // For pools, record each pool size and the number of resources requested.
  if (Resource != nullptr && Resource->hasCount()) {
    addReferenceSizeToPool(Resource, Ref, Subunit);
    addAllocSize(Resource->getPoolCount());
  }

  Types |= Type;
  if (!Expr->isExpressionConstant()) {
    PhaseExprSeen = true;
    return;
  }
  auto Phase = Expr->evaluateConstantExpression();
  if (!Phase)
    return;
  int Cycles = Ref ? (Ref->getUseCycles() - 1) : 0;
  if (EarliestRef == -1 || *Phase < EarliestRef)
    EarliestRef = *Phase;
  if (LatestRef == -1 || *Phase + Cycles > LatestRef)
    LatestRef = *Phase + Cycles;
}

// Add a nice debug name to a resource definition.
void ResourceDef::setDebugName(std::string Type, const CpuInstance *Cpu,
                               const ClusterInstance *Cluster,
                               const FuncUnitInstantiation *Fu) {
  std::string Cpus = Cpu ? formatv("{0}.", Cpu->getName()) : "";
  std::string Cls = Cluster ? formatv("{0}.", Cluster->getName()) : "";
  std::string Fus = Fu ? formatv("{0}.", Fu->getName()) : "";
  DebugName = formatv("{0}.{1}{2}{3}{4}", Type, Cpus, Cls, Fus, getName());
}

// Assign a resource id to a resource definition.
// Note that we don't assign ids to groups or their members, they are subsumed
// by their promoted members.
static int AssignId(ResourceDef *def, int ResourceId) {
  def->setResourceId(ResourceId);
  if (def->getPoolSize() > 1)
    return ResourceId + def->getPoolSize();
  return ResourceId + 1;
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
void MdlSpec::assignResourceIds() {
  for (auto *Cpu : Cpus) {
    ResourceDef *LastFu = nullptr;

    // First add resources defined for each functional unit in each cluster.
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *Fu : Cluster->getFuInstantiations())
        if (!Fu->getInstance()->isCatchallUnit() &&
            Fu->getFuResource()->isUsed() && Fu->getParent() == nullptr)
          Cpu->addCpuResource(Fu->getFuResource(), "FuncUnit", Cpu, Cluster,
                                nullptr);

    // Note the resource id of the last functional unit.
    if (!Cpu->getAllResources().empty())
      LastFu = Cpu->getAllResources().back();

    // Add resources defined for CPU-level issue slots.
    for (auto *Res : *Cpu->getIssues())
      Cpu->addCpuResource(Res, "Issue", Cpu, nullptr, nullptr);

    // Add resources defined for cluster-level issue slots.
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *Res : *Cluster->getIssues())
        Cpu->addCpuResource(Res, "Issue", Cpu, Cluster, nullptr);

    // Add all resources defined at the CPU level.
    for (auto *Res : *Cpu->getResources())
      Cpu->addCpuResource(Res, "Resource", Cpu, nullptr, nullptr);

    // Add all other resources defined in clusters.
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *Res : *Cluster->getResources())
        Cpu->addCpuResource(Res, "Resource", Cpu, Cluster, nullptr);

    // Add resources defined in functional unit instantiations.
    for (auto *Cluster : *Cpu->getClusters()) {
      for (auto *Fu : Cluster->getFuInstantiations())
        for (auto *Resource : Fu->getResources())
          Cpu->addCpuResource(Resource, "FU Resource", Cpu, Cluster, Fu);
    }

    // Add one fake resource to mark the end of the list.
    Cpu->addCpuResource(new ResourceDef("end"), "fake", Cpu, nullptr, nullptr);

    // We've collected all the resources together, assign ids.
    // We skip resource groups, since their members were promoted to
    // regular resources.
    int ResourceId = 1;
    for (auto *Res : Cpu->getAllResources())
      if (!Res->isGroupDef())
        ResourceId = AssignId(Res, ResourceId);

    if (LastFu)
      Cpu->setMaxFuId(LastFu->getResourceId());
    Cpu->setMaxResourceId(ResourceId);
  }
}

// Assign pool ids (per CPU) to each resource that defines a pool.
void MdlSpec::assignPoolIds() {
  for (auto *Cpu : Cpus) {
    // First find and sort the pools by their size.  We prefer to allocate
    // the most constrained pools first.
    for (auto *Res : Cpu->getAllResources())
      if (Res->isPoolDef())
        Cpu->addPoolResource(Res);

    std::sort(Cpu->getPoolResources().begin(), Cpu->getPoolResources().end(),
              [](const ResourceDef *A, const ResourceDef *B) {
                return A->getSize() < B->getSize();
              });

    int PoolId = 0;
    for (auto *Res : Cpu->getPoolResources()) {
      Res->setPoolId(PoolId);
      for (auto &[Pool, PoolInfo] : Res->getSubPools()) {
        PoolInfo.setSubpoolId(PoolId);
        PoolId += *PoolInfo.getCounts().rbegin();
      }
    }
    Cpu->setPoolCount(PoolId);
  }
}

} // namespace mdl
} // namespace mpact

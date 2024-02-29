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

void MdlSpec::writeMessage(const MdlItem *Item, const std::string &Msg) {
  if (Item == nullptr) {
    llvm::errs() << formatv("Error: {0}\n", Msg);
    return;
  }
  std::string message = formatv("{0} {1}", Item->getLocation(), Msg);
  // If we've already see this exact message, don't print it out again!
  // This is fairly common, since latencies and subunits are potentially
  // instantiated many times.
  if (!ErrorMessages.count(message)) {
    ErrorMessages.insert(message);
    llvm::errs() << message << "\n";
  }
}

int SubUnitInstantiation::ErrorsSeen() const { return Spec->ErrorsSeen(); }
int FuncUnitInstantiation::ErrorsSeen() const { return Spec->ErrorsSeen(); }

//----------------------------------------------------------------------------
// Pipeline Phase expression methods.
//----------------------------------------------------------------------------
// Find the phase reference in an expression and return it.
PhaseName *PhaseExpr::getPhaseName() const {
  PhaseName *Name;
  if (getOperation() == kPhase)
    return Phase;
  if (getLeft() && (Name = getLeft()->getPhaseName()))
    return Name;
  if (getRight() && (Name = getRight()->getPhaseName()))
    return Name;
  return nullptr;
}

// Early check to see if there's at least one symbolic name in a phase expr.
bool PhaseExpr::hasPhaseName() const {
  if (getOperation() == kPhase)
    return true;
  if (getLeft() && getLeft()->hasPhaseName())
    return true;
  if (getRight() && getRight()->hasPhaseName())
    return true;
  return false;
}

// Return true if an expression doesn't contain any operand references.
bool PhaseExpr::isExpressionConstant() const {
  if (getLeft() && !getLeft()->isExpressionConstant())
    return false;
  if (getRight() && !getRight()->isExpressionConstant())
    return false;
  return getOperation() != kOpnd;
}

// Evaluate a constant expression.
llvm::ErrorOr<int> PhaseExpr::evaluateConstantExpression() const {
  // Recur on children, return error if either has errors.
  llvm::ErrorOr<int> LeftValue = 0, RightValue = 0;
  if (getLeft()) {
    LeftValue = getLeft()->evaluateConstantExpression();
    if (!LeftValue)
      return LeftValue;
  }
  if (getRight()) {
    RightValue = getRight()->evaluateConstantExpression();
    if (!RightValue)
      return RightValue;
  }

  if (getOperation() == kPlus)
    return *LeftValue + *RightValue;
  if (getOperation() == kMinus)
    return *LeftValue - *RightValue;
  if (getOperation() == kMult)
    return *LeftValue * *RightValue;
  // Return an error for division by zero.
  if (getOperation() == kDiv) {
    if (*RightValue == 0)
      return llvm::errc::invalid_argument;
    return *LeftValue / *RightValue;
  }
  if (getOperation() == kNeg)
    return -*LeftValue;
  if (getOperation() == kPositive)
    return std::max(0, *LeftValue);
  if (getOperation() == kInt)
    return getNumber();
  if (getOperation() == kPhase)
    return getPhaseId();
  return llvm::errc::invalid_argument;  // Cannot reach here
}

//----------------------------------------------------------------------------
// Make sure the overall specification has some basic components defined.
// We do this rather late in the semantic checking phase, because some of
// the components are auto-generated during semantic checking.
//----------------------------------------------------------------------------
void MdlSpec::checkInputStructure() {
  if (FamilyName.empty())
    ErrorLog(nullptr, "Specify a processor family name");
  if (Cpus.empty())
    ErrorLog(nullptr, "Specify at least one cpu definition");
  if (PipeDefs.empty())
    ErrorLog(nullptr, "Specify at least one pipeline definition.");
  if (FuncUnits.empty())
    ErrorLog(nullptr, "Specify at least one functional unit definition.");
  if (Subunits.empty())
    ErrorLog(nullptr, "Specify at least one subunit definition.");
  if (Latencies.empty())
    ErrorLog(nullptr, "Specify at least one latency definition.");
  if (Instructions.empty())
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
void MdlSpec::buildDictionaries() {
  for (auto *Fu : FuncUnits)
    if (!FuMap.emplace(Fu->getName(), Fu).second)
      ErrorLog(Fu, "Duplicate functional unit definition: {0}", Fu->getName());

  // Group names cannot conflict with themselves or functional unit names.
  for (auto *Group : FuncUnitGroups) {
    if (!FuGroupMap.emplace(Group->getName(), Group).second)
      ErrorLog(Group, "Duplicate functional unit group definition: {0}",
               Group->getName());
    if (FindItem(FuMap, Group->getName()) != nullptr)
      ErrorLog(Group, "Group name conflicts with functional unit name: {0}",
               Group->getName());
  }

  for (auto *Su : Subunits)
    if (!SuMap.emplace(Su->getName(), Su).second)
      ErrorLog(Su, "Duplicate subunit definition: {0}", Su->getName());

  for (auto *Lat : Latencies)
    if (!LatMap.emplace(Lat->getName(), Lat).second)
      ErrorLog(Lat, "Duplicate latency definition: {0}", Lat->getName());

  for (auto *Opnd : Operands)
    if (!OperandMap.emplace(Opnd->getName(), Opnd).second)
      ErrorLog(Opnd, "Duplicate operand definition: {0}", Opnd->getName());

  for (auto *Instr : Instructions)
    if (!InstructionMap.emplace(Instr->getName(), Instr).second)
      ErrorLog(Instr, "Duplicate instruction definition: {0}", Instr->getName());

  for (auto *Rclass : RegClasses)
    if (!RegisterClassMap.emplace(Rclass->getName(), Rclass).second)
      ErrorLog(Rclass, "Duplicate register class definition: {0}",
               Rclass->getName());

  // Initialize a vector of instantiations for every subunit template.
  for (auto *Su : Subunits)
    SuInstantiations[Su->getName()] = new std::vector<SubUnitInstantiation *>;

  // Create some "Null" resource definitions.
  NullResourceRef = new ResourceRef("__");
  NullRegisterClass = new RegisterClass("__");
  NullPortDef = new ResourceDef("__");

  // Scan over all CPU specs, collect feasible predicate names.
  findValidPredicateNames();
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
void MdlSpec::findImplicitFuncUnitTemplates() {
  // Check each functional unit instance in each CPU definition.
  for (auto *Cpu : Cpus)
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *Unit : *Cluster->getFuncUnits()) {
        auto *Type = Unit->getType();
        if (FindItem(FuMap, Type->getName()) == nullptr) {
          auto *Fu = new FuncUnitTemplate(Type);
          FuMap.emplace(Type->getName(), Fu);
          FuncUnits.push_back(Fu);
        }
        // If there's a group defined with this name, generate an error.
        if (auto *Group = FindItem(FuGroupMap, Type->getName()))
           ErrorLog(Group,
                    "Group name conflicts with functional unit name: {0}",
                    Group->getName());
      }

  // Check that each base of an implicitly defined functional unit template
  // is defined. If its not, then define it as an implicitly defined
  // functional unit.
  for (auto [Name, Unit] : FuMap)
    if (Unit->isImplicitlyDefined())
      for (auto *Base : *Unit->getBases())
        if (FindItem(FuMap, Base->getName()) == nullptr) {
          auto *Fu = new FuncUnitTemplate(Base);
          FuncUnits.push_back(Fu);
          FuMap.emplace(Base->getName(), Fu);
        }

  // Check all the names in an FU group.  They should either refer to a group,
  // or a functional unit.  If they are undefined, report an error.
  for (auto [Name, Group] : FuGroupMap)
    for (auto *Id : *Group->getMembers())
      if (FindItem(FuMap, Id->getName()) == nullptr  &&
          FindItem(FuGroupMap, Id->getName()) == nullptr)
        ErrorLog(Id, "Undefined functional unit reference: {0}", Id->getName());
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
void MdlSpec::findValidPredicateNames() {
  for (auto *Cpu : Cpus) {
    ValidPredicateNames.emplace(Cpu->getName());
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *FuInst : *Cluster->getFuncUnits())
        if (FuInst->getId())
          ValidPredicateNames.emplace(FuInst->getName());
  }

  for (auto *FuTemplate : FuncUnits)
    ValidPredicateNames.emplace(FuTemplate->getName());
}

//---------------------------------------------------------------------------
// Check that a predicate name is at least feasible.  If it is not, generate
// a warning.
//---------------------------------------------------------------------------
void MdlSpec::isValidPredicateName(const Identifier *Name) {
  if (!ValidPredicateNames.count(Name->getName()))
    WarningLog(Name, "Predicate is invalid: {0}", Name->getName());
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
void MdlSpec::checkForDuplicateDefs() {
  // Check that phase names groups and phase names are unique.
  checkPhaseDefinitions(&PipeDefs);

  // Check that globally defined resources, registers and classes are unique.
  findDuplicates(Resources);
  findDuplicates(Registers);
  findDuplicates(Registers, Resources);
  findDuplicates(Resources, RegClasses);
  findDuplicates(Registers, RegClasses);
  findDuplicateMembers(Resources);

  // Operand names cannot conflict with registers or register class names.
  findDuplicates(RegClasses, Operands);
  findDuplicates(Registers, Operands);

  // For functional unit templates, look for redefinitions of parameters,
  // registers, resources, or ports. We do allow locally defined resources and
  // ports to hide globally defined objects.
  for (auto *Fu : FuncUnits) {
    findDuplicates(*Fu->getParams());
    findDuplicates(*Fu->getResources());
    findDuplicates(*Fu->getPorts());
    findDuplicateMembers(*Fu->getResources());

    // Don't allow redefinitions across object types (ports, resources).
    findDuplicates(*Fu->getPorts(), *Fu->getResources());

    // Don't allow shadowing of template parameters.
    findDuplicates(*Fu->getResources(), *Fu->getParams());
    findDuplicates(*Fu->getPorts(), *Fu->getParams());
  }

  // For subunit and latency templates, check for unique parameters.
  for (auto *Su : Subunits)
    findDuplicates(*Su->getParams());
  for (auto *Lat : Latencies)
    findDuplicates(*Lat->getParams());

  // CPU's have a separate namespace, but redefinitions aren't allowed.
  // For each cpu definition, check for redefinitions of registers,
  // resources, and functional unit instance names.
  // For each cluster, check cluster names, register/resource names, and
  // functional unit instances.
  findDuplicates(Cpus);
  for (auto *Cpu : Cpus) {
    // Check that phase names groups and phase names are unique.
    // Note that these can mask globally defined phase definitions.
    checkPhaseDefinitions(Cpu->getPipePhases());

    // Make sure CPU definitions aren't masking globally defined things.
    findDuplicates(*Cpu->getResources());
    findDuplicates(*Cpu->getResources(), Resources);
    findDuplicates(*Cpu->getResources(), Registers);
    findDuplicates(*Cpu->getResources(), RegClasses);
    findDuplicates(*Cpu->getClusters());
    findDuplicateMembers(*Cpu->getResources());

    for (auto *Cluster : *Cpu->getClusters()) {
      findDuplicates(*Cluster->getResources());
      findDuplicates(*Cluster->getFuncUnits());
      findDuplicates(*Cluster->getIssues());
      findDuplicates(*Cluster->getIssues(), *Cluster->getFuncUnits());
      findDuplicates(*Cluster->getIssues(), *Cluster->getResources());
      findDuplicates(*Cluster->getResources(), *Cpu->getResources());
      findDuplicates(*Cluster->getResources(), Resources);
      findDuplicates(*Cluster->getResources(), Registers);
      findDuplicates(*Cluster->getResources(), RegClasses);
      findDuplicateMembers(*Cluster->getResources());

      // If this is a generated (promoted) cluster, we also check this
      // cluster's definitions against CPU-level definitions.
      if (Cluster->isNull()) {
        findDuplicates(*Cluster->getIssues(), *Cpu->getResources());
        findDuplicates(*Cluster->getIssues(), *Cpu->getClusters());
        findDuplicates(*Cluster->getFuncUnits(), *Cpu->getResources());
        findDuplicates(*Cluster->getFuncUnits(), *Cpu->getClusters());
      }
    }
  }

  // For each instruction definition, check for duplicate operand names.
  for (auto *Instr : Instructions)
    findDuplicates(*Instr->getOperands());

  // For each operand definition, check for duplicate (sub)operand names and
  // duplicate attribute definitions.
  for (auto *Opnd : Operands)
    findDuplicates(*Opnd->getOperands());
}

//---------------------------------------------------------------------------
// Look up a phase name in the pipeline definition set.  First look for
// CPU-specific phases, and if not found look for a global definition.
// Return null if it's not found anywhere.
//---------------------------------------------------------------------------
PhaseName *MdlSpec::searchPipeReference(Identifier *Phase, CpuInstance *Cpu) {
  if (Cpu)
    for (auto *PipeDef : *Cpu->getPipePhases())
      if (auto *Item = FindItem(*PipeDef->getPhaseNames(), Phase->getName()))
        return Item;

  for (auto *PipeDef : PipeDefs)
    if (auto *Item = FindItem(*PipeDef->getPhaseNames(), Phase->getName()))
      return Item;

  return nullptr;
}

//---------------------------------------------------------------------------
// Look up a phase name in the pipeline definition set.
// Print an error message if not found.
// Return a pointer to the object if found, or a fake object if not found.
//---------------------------------------------------------------------------
PhaseName *MdlSpec::findPipeReference(Identifier *PipeDef, CpuInstance *Cpu) {
  if (auto *Item = searchPipeReference(PipeDef, Cpu))
    return Item;

  // This is ultimately a fatal error.  Return a fake object.
  ErrorLog(PipeDef, "Pipeline phase \"{0}\" not found for cpu: {1}",
           PipeDef->getName(), Cpu->getName());
  return new PhaseName(*PipeDef, "", false, false);
}

//---------------------------------------------------------------------------
// Check that phase names groups and phase names are unique.
//---------------------------------------------------------------------------
void MdlSpec::checkPhaseDefinitions(PipeDefList *Pipes) {
  findDuplicates(*Pipes);

  for (auto *P1 : *Pipes) {
    findDuplicates(*P1->getPhaseNames());
    for (auto *P2 : *Pipes)
      if (P1 != P2)
        findDuplicates(*P1->getPhaseNames(), *P2->getPhaseNames());
  }
}

//---------------------------------------------------------------------------
// Specialize a phase expression for the context its instantiated in.
// Return true if at least one valid pipeline phase is found.
//---------------------------------------------------------------------------
void MdlSpec::specializePhaseExpr(PhaseExpr *Expr, CpuInstance *Cpu) {
  if (Expr->getOperation() == kPhase) {
    auto *Phase = findPipeReference(Expr->getPhase(), Cpu);
    Expr->setPhaseName(Phase);
  }
}

//---------------------------------------------------------------------------
// If a resource definition has a start_phase or end_phase specified,
// look them up in the pipe phase definitions.
//---------------------------------------------------------------------------
void MdlSpec::checkPipeReference(ResourceDef *Def, CpuInstance *Cpu) {
  if (Def->getStartPhase() != nullptr)
    findPipeReference(Def->getStartPhase(), Cpu);
  if (Def->getEndPhase() != nullptr)
    findPipeReference(Def->getEndPhase(), Cpu);
}

//---------------------------------------------------------------------------
// Quick check that reference phases contain at least one phase name.
//---------------------------------------------------------------------------
void MdlSpec::checkReferencePhases(ReferenceList *Refs) {
  if (Refs == nullptr)
    return;
  for (auto *Ref : *Refs) {
    // Functional unit refs don't always have an explicit phase expression.
    // We need to add one, but must do it later when we instantiate it.
    if (Ref->isFuncUnitRef() && Ref->getPhaseExpr() == nullptr)
      continue;
    // For conditional refs, recur on the then/else clauses.
    if (Ref->isConditionalRef()) {
      for (auto *Cr = Ref->getConditionalRef(); Cr; Cr = Cr->getElseClause())
        checkReferencePhases(&Cr->getRefs());
      continue;
    }
    // Normal case - make sure there's a phase name mentioned somewhere.
    if (!Ref->getPhaseExpr()->hasPhaseName())
      ErrorLog(Ref->getPhaseExpr(), "Invalid phase: missing phase name");
  }
}

//---------------------------------------------------------------------------
// Look up references to pipeline phases.
// These occur in resource definitions, issue definitions, and references.
// Any errors found here aren't immediately fatal, so we always return.
//---------------------------------------------------------------------------
void MdlSpec::checkPipeReferences() {
  // Check resources defined globally.
  for (auto *Res : Resources)
    checkPipeReference(Res, nullptr);

  // Check resources defined in CPUs and CPU clusters.
  for (auto *Cpu : Cpus) {
    for (auto *Issue : *Cpu->getIssues())
      checkPipeReference(Issue, Cpu);
    for (auto *Res : *Cpu->getResources())
      checkPipeReference(Res, Cpu);
    for (auto *Cluster : *Cpu->getClusters()) {
      for (auto *Issue : *Cluster->getIssues())
        checkPipeReference(Issue, Cpu);
      for (auto *Res : *Cluster->getResources())
        checkPipeReference(Res, Cpu);
    }
  }

  // We can't statically check phase references in latency template definitions.
  // Phase names are defined on a per-cpu basis, so we we need to check
  // templates' phase references when we instantiate the template. This happens
  // later in the compilation process.

  // That said, we -can- check that phase expressions contain at least ONE
  // phase name.  We do this to disallow references to phase indexes.
  for (const auto *Latency : Latencies)
    checkReferencePhases(Latency->getReferences());
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
void MdlSpec::sameParams(const ParamsList *TempParams,
                         const ParamsList *BaseParams, MdlItem *Item) {
  const bool BaseCanHaveExtraParams = false; // not supported downstream.
  const bool BaseCanHaveFewerParams = true;

  // Check that the number of parameters is compatible.
  if ((!BaseCanHaveExtraParams && (TempParams->size() < BaseParams->size())) ||
      (!BaseCanHaveFewerParams && (TempParams->size() > BaseParams->size()))) {
    ErrorLog(Item, "Incompatible parameters for template and base");
    return;
  }

  int MinParams = std::min(TempParams->size(), BaseParams->size());

  for (int Idx = 0; Idx < MinParams; Idx++) {
    if ((*TempParams)[Idx]->getType() != (*BaseParams)[Idx]->getType())
      ErrorLog(Item, "Unmatched parameter types for template and base");
    if ((*TempParams)[Idx]->getName() != (*BaseParams)[Idx]->getName())
      ErrorLog(Item, "Unmatched parameter names for template and base");
  }
}

//---------------------------------------------------------------------------
// Some helper template functions for finding cycles in MDL template
// definitions.  Recursive template definitions are not allowed.
//---------------------------------------------------------------------------
template <class T>
bool findCycle(T *Root, T *Unit, std::set<std::string> &Visited) {
  Visited.insert(Unit->getName());

  for (auto *Child : Unit->getUnitBases()) {
    if (Child == Root)
      return true;
    if (Visited.count(Child->getName()) == 0 &&
        findCycle(Root, Child, Visited))
      return true;
  }
  return false;
}

template <class T> void findCycles(T &Item, MdlSpec *md, std::string Type) {
  for (auto *Unit : Item) {
    std::set<std::string> Visited;
    if (findCycle(Unit, Unit, Visited))
      md->ErrorLog(Unit, "Recursively defined {0} template: {1}", Type,
                   Unit->getName());
  }
}

//---------------------------------------------------------------------------
// Some helper template functions for duplicate bases in MDL template
// definitions.  Duplicate bases in general are not allowed.  This is a
// pretty naive implementation - derived units aren't so common that this
// needs to be particularly efficient.
//---------------------------------------------------------------------------
template <class T>
void findAllBases(T *TopUnit, T *Unit, MdlSpec *Spec, std::string &Type,
                  std::set<std::string> &Seen) {
  for (auto *Base : Unit->getUnitBases()) {
    if (Seen.count(Base->getName()))
      Spec->ErrorLog(TopUnit, "{0} template {1} has duplicate bases: {2}", Type,
                   TopUnit->getName(), Base->getName());
    Seen.insert(Base->getName());
    findAllBases(TopUnit, Base, Spec, Type, Seen);
  }
}

// Ensure that a template doesn't have duplicate bases. Note that this
// assumes we've already checked for recursively defined templates.
template <class T>
void findDuplicateBases(T &Item, MdlSpec *Spec, std::string Type) {
  for (auto *Unit : Item) {
    std::set<std::string> Seen;
    findAllBases(Unit, Unit, Spec, Type, Seen);
  }
}

//---------------------------------------------------------------------------
// Check validity of template bases for functional units, subunits, latencies
// and functional unit groups, and link the template to its bases.
// Functional unit and latency template bases must have parameters which are
// compatible with the base. We also explicitly check for recursively
// defined templates. Any errors found here are considered fatal, so just abort.
//---------------------------------------------------------------------------
void MdlSpec::checkTemplateBases() {
  for (auto Fu : FuncUnits)
    if (auto *Bases = Fu->getBases()) {
      for (auto *Base : *Bases) {
        if (auto *FuBase = FindItem(FuMap, Base->getName())) {
          Fu->addBase(FuBase);
          sameParams(Fu->getParams(), FuBase->getParams(), Fu);
        } else {
          ErrorLog(Fu, "Undefined functional unit base: {0}", Base->getName());
        }
      }
    }

  // Check that subunit bases exist, and link the template to its bases. If
  // there are any string bases, use these to tie the subunit to a matched set
  // of instructions.
  // Unlike other template bases, subunit templates can have different
  // parameters than their bases (by design), so we don't check for parameter
  // compatibility.
  for (auto *Su : Subunits) {
    if (auto *Bases = Su->getBases()) {
      for (auto *Base : *Bases) {
        if (auto *SuBase = FindItem(SuMap, Base->getName())) {
          Su->addBase(SuBase);
          SuBase->addDerivedSubunit(Su);
        } else {
          ErrorLog(Su, "Undefined subunit base: {0}", Base->getName());
        }
      }
    }
    tieSubUnitToInstructions(Su, Su->getRegexBases());
  }

  for (auto *Latency : Latencies)
    if (auto *Bases = Latency->getBaseIds()) {
      for (auto *Base : *Bases)
        if (auto *LatBase = FindItem(LatMap, Base->getName())) {
          Latency->addBase(LatBase);
          sameParams(Latency->getParams(), LatBase->getParams(), Latency);
        } else {
          ErrorLog(Latency, "Undefined latency base: {0}", Base->getName());
        }
    }

  // If a functional unit group includes an FU group, expand that group into
  // the parent group.
  for (auto *Group : FuncUnitGroups)
    if (!expandGroup(Group, Group->getMembers(), 0))
      break;

  // Check that we don't have recursive derivations for templates.
  findCycles(FuncUnits, this, "functional unit");
  findCycles(Subunits, this, "subunit");
  findCycles(Latencies, this, "latency");

  if (ErrorsSeen())
    Abort();

  // Check for duplicate bases.  Functional units *can* have duplicate bases,
  // and duplicate bases for latencies are relatively common and harmless.
  // But subunits cannot have duplicate bases.
  findDuplicateBases(Subunits, this, "Subunit");

  if (ErrorsSeen())
    Abort(); // If any errors found, abort.
}

//---------------------------------------------------------------------------
// Link all the members of functional unit group to the group.  Subgroups
// are allowed, but we need to check for recursion.
// This function returns true if no errors were found, else returns false.
//---------------------------------------------------------------------------
bool MdlSpec::expandGroup(FuncUnitGroup *Group, IdList *Members,
                          unsigned Depth) {
  if (Depth >= FuGroupMap.size()) {
    ErrorLog(Group, "Group is recursively defined: {0}", Group->getName());
    return false;
  }

  for (auto *Member : *Members) {
    if (auto *Fu = FindItem(FuMap, Member->getName())) {
      Group->addUnit(Fu);
      continue;
    }
    if (auto *Subgroup = FindItem(FuGroupMap, Member->getName()))
      if (!expandGroup(Group, Subgroup->getMembers(), Depth + 1))
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
void MdlSpec::validateArgs(const ParamsList *Args,
                           const ResourceRefList *Instance, MdlItem *Item) {
  if (Args->size() != Instance->size()) {
    ErrorLog(Item, "Instance has wrong number of parameters");
    return;
  }
  for (unsigned Idx = 0; Idx < Args->size(); Idx++)
    (*Instance)[Idx]->setParameter((*Args)[Idx]);
}

//---------------------------------------------------------------------------
// For each functional unit, subunit, and latency instantiation, find the
// referenced template definition (if it exists) and make sure the parameters
// match. Link instances to their templates, and link instant arguments to
// the associated template parameter.
// Any errors found here are considered fatal, so just abort.
//---------------------------------------------------------------------------
void MdlSpec::checkInstantiations() {
  // For every CPU and cluster, find each functional unit instantiation and
  // check its parameters against its functional unit template definition.
  for (auto *Cpu : Cpus) {
    for (auto *Cluster : *Cpu->getClusters()) {
      for (auto *FuInst : *Cluster->getFuncUnits()) {
        if (auto *FuTemp = FindItem(FuMap, FuInst->getType()->getName())) {
          validateArgs(FuTemp->getParams(), FuInst->getArgs(), FuInst);
          FuInst->setTemplate(FuTemp);
        } else {
          ErrorLog(FuInst, "Undefined functional unit reference: {0}",
                   FuInst->getType()->getName());
        }
      }
    }
  }

  // For every functional unit template definition, find each subunit
  // instantiation and check its parameters against its subunit template
  // definition.
  for (auto *FuTemp : FuncUnits) {
    for (auto *Instance : *FuTemp->getSubunits()) {
      if (auto *SuTemp = FindItem(SuMap, Instance->getName())) {
        validateArgs(SuTemp->getParams(), Instance->getArgs(), Instance);
        Instance->setTemplate(SuTemp);
      } else {
        ErrorLog(Instance, "Undefined subunit reference: {0}",
                 Instance->getName());
      }
    }
  }

  // For every subunit template base definition, find each latency instantiation
  // and check its parameters against its latency template definition.
  for (auto *SuTemp : Subunits) {
    for (auto *LatInst : *SuTemp->getLatencies()) {
      if (auto *LatTemp = FindItem(LatMap, LatInst->getName())) {
        validateArgs(LatTemp->getParams(), LatInst->getArgs(), LatInst);
        LatInst->setTemplate(LatTemp);
      } else {
        ErrorLog(LatInst, "Undefined latency reference: {0}",
                 LatInst->getName());
      }
    }
  }

  if (ErrorsSeen())
    Abort(); // If any errors found, abort.
}

// For each CPU, determine if we need to explicitly manage issue slots.
// - If there's more than one cluster, we conservatively decide to manage them.
// - If any functional unit instance pins issue slots, we must manage them.
void MdlSpec::checkIssueSlots() {
  for (auto *Cpu : Cpus) {
    if (Cpu->getClusters()->size() > 1) {
      Cpu->setNeedsSlotResources(true);
      continue;
    }
    for (auto *Cluster : *Cpu->getClusters())
      for (auto *FuInst : *Cluster->getFuncUnits())
        if (FuInst->getPinAny() || FuInst->getPinAll()) {
          Cpu->setNeedsSlotResources(true);
          break;
        }
  }
}

//---------------------------------------------------------------------------
// Check instruction definitions for valid subunits.
// Any errors found here are considered fatal, so just abort.
//---------------------------------------------------------------------------
void MdlSpec::checkInstructions() {
  for (auto *Instruct : Instructions)
    for (auto *Subunit : *Instruct->getSubunits())
      if (!SuMap.count(Subunit->getName()))
        ErrorLog(Subunit, "Undefined subunit reference: {0}", Subunit->getName());
}

//---------------------------------------------------------------------------
// Flatten a single operand - push an operand for each component onto the
// operand list.
//---------------------------------------------------------------------------
void MdlSpec::flattenOperand(OperandDecl *Opnd, OperandDeclList *FlatOps) {
  // If this is not a reference to another operand, just add it to the list.
  if (Opnd->isImpliedRegister() || Opnd->getRegClass() ||
      Opnd->getOperand()->getOperands()->empty()) {
    FlatOps->push_back(Opnd);
    return;
  }

  // Recursively handle operands that reference other operands.
  for (auto *SubOpnd : *Opnd->getOperand()->getOperands()) {
    auto *NewSubOpnd = new OperandDecl(SubOpnd, Opnd);
    NewSubOpnd->addType(SubOpnd->getType());
    NewSubOpnd->addName(SubOpnd->getOpName());
    flattenOperand(NewSubOpnd, FlatOps);
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
void MdlSpec::flattenInstructionOperands() {
  for (auto *Instruct : Instructions)
    if (Instruct->getOperands()) {
      auto *FlatOps = new OperandDeclList();
      for (auto *Opnd : *Instruct->getOperands())
        flattenOperand(new OperandDecl(Opnd, Opnd), FlatOps);

      Instruct->setFlatOperands(FlatOps);
    }
}

// Determine if a derived operand definition is based on the specified operand.
// Return true if it does.
bool MdlSpec::findOperandDerivation(const OperandDef *Derived,
                                    const OperandDef *Operand) const {
  if (Derived == Operand)
    return true;

  for (auto *Base : *Derived->getBaseOperands())
    if (findOperandDerivation(Base, Operand))
      return true;
  return false;
}

// Check a qualified operand name list with an instruction's flattened
// operand list.
bool MdlSpec::compareOpndNames(const OperandDecl *Opnd, const IdList &Names) {
  int OpndSize = Opnd->getOpNames()->size();
  int NamesSize = Names.size();

  // If the operand reference isn't fully qualified, we allow you to skip
  // the last name if the underlying operand type is a register or register
  // class.  If you don't like this behavior, provide all the names!
  if (OpndSize != NamesSize) {
    if (OpndSize != NamesSize + 1)
      return false;

    // Make sure the missing operand type is a register or register class.
    std::string OpndType = Opnd->getTypes()->back()->getName();
    if (!FindItem(Registers, OpndType) &&
        !RegisterClassMap.count(OpndType))
      return false;
  }

  // All the leading names need to match.
  for (int Index = 0; Index < NamesSize; Index++)
    if ((*Opnd->getOpNames())[Index]->getName() != Names[Index]->getName())
      return false;
  return true;
}

// Look up qualified operand name in the instruction's flattened operand
// list, and return its index in the list, or -1 if not found.
// Implied operands may show up several times in the operand list, and
// we need to differentiate defs from uses.
int MdlSpec::findOperandName(const InstructionDef *Instruct,
                             const IdList &Names, RefType Type) {
  int Index = 0;
  int Itype = static_cast<int>(Type);
  for (auto *OpDecl : *Instruct->getFlatOperands()) {
    // Check for references to implied register operands.
    if (OpDecl->isImpliedRegister() && (Itype & RefTypes::kAnyUseDef)) {
      if (Names[0]->getName() == OpDecl->getType()->getName()) {
        if (OpDecl->isInput() && (Itype & RefTypes::kAnyUse))
          return Index;
        if (OpDecl->isOutput() && (Itype & RefTypes::kAnyDef))
          return Index;
      }
      continue;
    }
    // Handle "normal" operands.
    if (compareOpndNames(OpDecl, Names))
      return Index;
    Index++;
  }
  return -1;     // Not found
}

// Look up an operand by name and optional type, and return the index or
// -1 if not found.  If a non-empty type is provided, it must match the
// operand definition type.
int MdlSpec::findOperand(const InstructionDef *Instr, const IdList &Name,
                         const std::string &OpndType, RefType Type) {
  // First check to see if it's a variant operand ($$<number>). Note that these
  // never have a declared operand type.
  if (Name[0]->isVararg())
    return Name[0]->getVarargIndex() + Instr->getNumFlatOperands() - 1;

  // If the operand is simply an operand index ($<number>) use that as the
  // operand id, otherwise look up the operand name(s).
  int OpndId;
  if (Name[0]->isNumber()) {
    OpndId = Name[0]->getNumber();
    if (OpndId >= Instr->getNumFlatOperands())
      return -1;
  } else {
    OpndId = findOperandName(Instr, Name, Type);
  }
  if (OpndId == -1)
    return -1;

  // See if the operand types match.  If either is empty, we match.
  auto OpndDecl = Instr->getOperandDecl(OpndId);
  if (OpndDecl == nullptr)
    return OpndId;

  std::string decl_type = OpndDecl->getTypeName();
  if (OpndType.empty() || decl_type.empty() || decl_type == OpndType)
    return OpndId;

  // If the operand match type is a derived operand, check if its derived
  // from the declared operand type.
  if (!OpndType.empty() && !decl_type.empty()) {
    auto *InsOpndType = FindItem(Operands, decl_type);
    auto *RefOpndType = FindItem(Operands, OpndType);
    if (InsOpndType && RefOpndType && RefOpndType->isDerivedOperand())
      if (findOperandDerivation(RefOpndType, InsOpndType))
        return OpndId;
  }

  // If the operand names don't match, and they are both register classes,
  // and if the reference class is a strict superset of the declared class,
  // it's a match.
  auto *DeclClass = FindItem(RegClasses, decl_type);
  auto *RefClass = FindItem(RegClasses, OpndType);
  if (DeclClass && RefClass && RefClass->isSupersetOf(DeclClass))
    return OpndId;
  return -1; // Not found.
}

// Given an operand reference, determine its index in this instruction.
// Return the index, or -1 if not found.
int MdlSpec::getOperandIndex(const InstructionDef *Instr,
                             const OperandRef *Operand, RefType Type) {
  if (Operand == nullptr)
    return -1;
  std::string TypeName = Operand->getOpType() ?
                     Operand->getOpType()->getName() : "";
  return findOperand(Instr, *Operand->getOpNames(), TypeName, Type);
}

//---------------------------------------------------------------------------
// Make sure we don't have recursively defined operands.
// This can happen with suboperands or operand bases.
// Return true if recursion found.
//---------------------------------------------------------------------------
bool MdlSpec::checkRecursiveOperands(OperandDef *Opnd, OperandDefList &Seen) {
  Seen.push_back(Opnd);

  // Check suboperands.
  if (auto *SubOpnds = Opnd->getOperands()) {
    for (auto *OpndDecl : *SubOpnds) {
      if (OpndDecl->getOperand()) {
        if (FindItem(Seen, OpndDecl->getOperand()->getName())) {
          ErrorLog(Seen[0], "Recursively defined operand: {0}",
                   Seen[0]->getName());
          return true;
        }
        if (checkRecursiveOperands(OpndDecl->getOperand(), Seen))
          return true;
      }
    }
  }
  // Check base operands.
  for (auto *Base : *Opnd->getBaseOperands()) {
    if (FindItem(Seen, Base->getName())) {
      ErrorLog(Seen[0], "Recursively defined operand: {0}", Seen[0]->getName());
      return true;
    }
    if (checkRecursiveOperands(Base, Seen))
      return true;
  }
  Seen.pop_back();
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
static OperandDef *checkOperandDerivation(OperandDef *Opnd,
                                          OperandDefSet &Seen) {
  // If we've already seen this operand, its either recursive (already
  // checked) or ambiguously defined.  We've already checked for recursion,
  // so have to abort if we see it, but we don't want to report it as
  // an ambiguous derivation.
  if (Seen.count(Opnd))
    return (Opnd->isDerivedOperand()) ? nullptr : Opnd;

  Seen.insert(Opnd);
  for (auto *Base : *Opnd->getBaseOperands())
    if (auto *Item = checkOperandDerivation(Base, Seen))
      return Item;

  if (Opnd->isDerivedOperand())
    Seen.erase(Opnd);
  return nullptr;
}

//---------------------------------------------------------------------------
// Check that derived operands have only one derivation to any base operand.
//---------------------------------------------------------------------------
void MdlSpec::checkOperandDerivations(OperandDef *Opnd) {
  OperandDefSet Seen;
  if (auto *Base = checkOperandDerivation(Opnd, Seen))
    ErrorLog(Opnd, "Ambiguous operand derivation: {0}->{1}", Opnd->getName(),
             Base->getName());
}

//---------------------------------------------------------------------------
// Check that a single operand reference is either a reference to a defined
// operand, a register class, or a register name (an implied operand).
// Link valid declarations with their definitions.
// NOTE: We currently don't allow instruction definitions or operands to
// directly reference derived operands, so we explicitly check for this here.
// Derived operands exist to qualify regular operand types in reference rules.
//---------------------------------------------------------------------------
void MdlSpec::checkOperand(OperandDecl *OpndDecl) {
  const std::string &Name = OpndDecl->getType()->getName();
  if (OperandMap.count(Name)) {
    OpndDecl->setOperand(OperandMap[Name]);
    if (OpndDecl->getOperand()->getBases())
      ErrorLog(OpndDecl, "Invalid use of a derived operand: {0}", Name);
  } else if (RegisterClassMap.count(Name)) {
    OpndDecl->setRegclass(RegisterClassMap[Name]);
  } else if (FindItem(Registers, Name) != nullptr) {
    OpndDecl->setIsImpliedRegister();
  } else {
    ErrorLog(OpndDecl, "Undefined operand type: {0}", Name);
  }

  if (!OpndDecl->isImpliedRegister() && !OpndDecl->isEllipsis())
    if (OpndDecl->getName().empty())
      ErrorLog(OpndDecl, "Instruction operands must have names");
}

//---------------------------------------------------------------------------
// Check that operand references (in instruction definitions, operand
// definitions, and latency references) refer to valid operands.
// Link operand declarations to their definitions. Also link derived
// operands to their base operands.
// Check for recursively defined operands, or ambiguously derived operands.
//---------------------------------------------------------------------------
void MdlSpec::checkOperands() {
  // Check instruction definitions for valid operand types.  They can
  // be either operand definitions or register class definitions.
  for (auto *Instr : Instructions)
    for (auto *Opnd : *Instr->getOperands())
      checkOperand(Opnd);

  // Check operand definitions for valid operand types, and link declarations
  // to their definitions.  If an operand is derived, link it to its base.
  for (auto *OpndDef : Operands) {
    for (auto *Opnd : *OpndDef->getOperands())
      checkOperand(Opnd);
    if (auto *Bases = OpndDef->getBases()) {
      for (auto *Base : *Bases)
        if (OperandMap.count(Base->getName()))
          OpndDef->addBaseOperand(OperandMap[Base->getName()]);
        else
          ErrorLog(Base, "Undefined base operand: {0}", Base->getName());
    }
  }
  if (ErrorsSeen())
    Abort(); // If any errors found, abort.

  // Check for recursively defined operands.
  for (auto *OpndDef : Operands) {
    OperandDefList Seen;
    checkRecursiveOperands(OpndDef, Seen);
  }

  // Check for valid derivations for derived operands.
  for (auto *OpndDef : Operands)
    if (!OpndDef->getBaseOperands()->empty()) {
      checkOperandDerivations(OpndDef);
    }
  if (ErrorsSeen())
    Abort(); // If any errors found, abort.

  // Once we've checked all the operands, flatten the operand hierarchy to a
  // single level.
  flattenInstructionOperands();
}

//---------------------------------------------------------------------------
// Scan references in each latency rule and report references which were
// encountered, but never valid (in any instruction, in any subunit).
//---------------------------------------------------------------------------
void MdlSpec::checkReferenceUse() {
  for (auto *Lat : Latencies)
    for (auto *Ref : *Lat->getReferences())
      if (Ref->getSeen() && !Ref->getUsed())
        WarningLog(Ref, "Reference never used: {0}", Ref->ToString());
}

//---------------------------------------------------------------------------
// Print a warning for any subunit template that isn't used.
//---------------------------------------------------------------------------
void MdlSpec::checkSubunitUse() {
  for (auto *Subunit : Subunits)
    if (Subunit->getUseCount() == 0)
      WarningLog(Subunit, "Subunit never used: {0}", Subunit->getName());
}

//---------------------------------------------------------------------------
// Check a single resource definition for a pooled resource.
// Pooled resources with shared bits must specify a phase - there's no
// reasonable way to manage these across arbitrary pipeline phases.
//---------------------------------------------------------------------------
void MdlSpec::checkResourceDef(const ResourceDef *Def) {
  if (Def->isPoolDef() && Def->getStartPhase() == nullptr &&
      Def->hasSharedBits())
    ErrorLog(Def, "Shared resource pools must have a pipeline phase: {0}",
             Def->ToString());
}

//---------------------------------------------------------------------------
// Make sure shared resource pools have been declared with a pipe phase.
// Do this before functional unit instantiation and global/group resource
// promotion, so that we don't get duplicate error messages.
//---------------------------------------------------------------------------
void MdlSpec::checkResourceDefs() {
  // Check resources defined globally.
  for (auto Def : Resources)
    checkResourceDef(Def);

  // Check resources defined in functional units.
  for (auto *Fu : FuncUnits)
    for (auto Def : *Fu->getResources())
      checkResourceDef(Def);

  // Check resources defined in cpus (and clusters).
  findDuplicates(Cpus);
  for (auto *Cpu : Cpus) {
    for (auto Def : *Cpu->getResources())
      checkResourceDef(Def);
    for (auto *Cluster : *Cpu->getClusters())
      for (auto Def : *Cluster->getResources())
        checkResourceDef(Def);
  }
}

//---------------------------------------------------------------------------
// Print a warning for any inconsistent resource use.
//---------------------------------------------------------------------------
void MdlSpec::checkResourceUse() {
  for (auto *Cpu : Cpus)
    for (auto *Res : Cpu->getAllResources())
      if (Res != Cpu->getAllResources().back()) {  // skip last dummy resource
        if (!Res->isUsed())
          WarningLog(Res, "Resource never referenced: {0}",
                     Res->getDebugName());
        else if (Res->isOnlyHeld())
          WarningLog(Res, "Resource Held but never Reserved: {0}",
                     Res->getName());
        else if (Res->isOnlyReserved())
          WarningLog(Res, "Resource Reserved but never Held: {0}",
                     Res->getName());
      }
}

//---------------------------------------------------------------------------
// Check that conditional references have a valid predicate, and also check
// the predicated references for validity.
//---------------------------------------------------------------------------
void MdlSpec::checkConditionalReferences(ConditionalRef *CondRef) {
  if (CondRef == nullptr)
    return;
  if (auto *Pred = CondRef->getPredicate())
    if (PredicateTable.count(Pred->getName()) == 0)
      ErrorLog(Pred, "Undefined predicate name: {0}", Pred->getName());

  for (auto *Ref : CondRef->getRefs()) {
    if (Ref->getOperand() && Ref->getOperand()->getOpType() != nullptr)
      checkSubOperands(Ref->getOperand(), Ref->getOperand()->getOpType(), 1);
    if (Ref->isConditionalRef())
      checkConditionalReferences(Ref->getConditionalRef());
  }
  checkConditionalReferences(CondRef->getElseClause());
}

//---------------------------------------------------------------------------
// Check operand references in each rule. Note that, at this point in the
// compilation, we can only check references which explicitly specify an
// operand type. We look for references that will -always- fail for any
// instruction.
//---------------------------------------------------------------------------
void MdlSpec::checkReferences() {
  for (auto *Lat : Latencies)
    for (auto *Ref : *Lat->getReferences()) {
      if (Ref->getOperand() && Ref->getOperand()->getOpType() != nullptr)
        checkSubOperands(Ref->getOperand(), Ref->getOperand()->getOpType(), 1);
      if (Ref->isConditionalRef())
        checkConditionalReferences(Ref->getConditionalRef());
    }
}

//---------------------------------------------------------------------------
// Promote globally defined resources to be CPU-defined resources.  This
// gives each CPU a unique set of resources, so we can name them, renumber
// them, and track use of them separately for each CPU.
//---------------------------------------------------------------------------
void MdlSpec::promoteGlobalResources() {
  for (auto *Cpu : Cpus)
    for (auto *Res : Resources)
      Cpu->getResources()->push_back(new ResourceDef(*Res));
}

// If we promoted a member and the promoted resource already exists, check that
// they have compatible definitions.
void MdlSpec::checkPromotedMember(ResourceDef *Group, Identifier *Member,
                                  ResourceDef *Promoted) {
  // The promoted resource cannot be part of a group.
  if (Promoted->isPoolDef())
    ErrorLog(Member, "Invalid group member: {0}", Member->getName());
  // The group and the promoted resource must have the same attributes.
  if (Group->getBitSize() != Promoted->getBitSize() ||
      Group->getStartPhase() != Promoted->getStartPhase() ||
      Group->getEndPhase() != Promoted->getEndPhase())
    ErrorLog(Member, "Inconsistent group definition: {0}", Member->getName());
}

//---------------------------------------------------------------------------
// Given a list of resource definitions for a scope, find group definitions
// and promote each member to a regular resource definition, and annotate
// the resource group with the new resource definition. If the resource is
// already defined (either by the user or a previous promotion), make sure
// the definitions match.
//---------------------------------------------------------------------------
void MdlSpec::promoteResourceGroupMembers(ResourceDefList *Resources,
                                          ResourceDefList *OuterScope,
                                          ResourceRefDict *args) {
  ResourceDefList Promos;
  for (auto *Res : *Resources) {
    if (Res->isGroupDef()) {
      for (unsigned idx = 0; idx < Res->getMembers().size(); idx++) {
        auto *Mem = Res->getMembers()[idx];
        // See if this member is defined or has been previously promoted.
        ResourceDef *Def = FindItem(*Resources, Mem->getName());
        if (Def == nullptr)
          Def = FindItem(Promos, Mem->getName());
        if (Def == nullptr && OuterScope != nullptr)
          Def = FindItem(*OuterScope, Mem->getName());
        if (Def == nullptr && args != nullptr) {
          if (auto *Ref = FindItem(*args, Mem->getName())) {
            Def = Ref->getDefinition();
            Res->getMembers()[idx] =
                new Identifier(Def->getId(), Mem->getIndex());
          }
        }

        // If we didn't find the resource, create a new resource and add it to
        // the list of things to promote.
        if (Def == nullptr) {
          Def = new ResourceDef(*Mem, Mem, Res->getBitSize(), 0,
                                Res->getStartPhase(),
                                Res->getEndPhase());
          Promos.push_back(Def);
        }
        // Add the promoted resource to the def list for the group.
        checkPromotedMember(Res, Mem, Def);
        Res->addMemberDef(Def);
      }
      // After promoting all the members of a group, check that we didn't end
      // up with duplicate members in the group.
      findDuplicates(Res->getMembers());
    }
  }

  // Add all the new resources to the defined resources.
  Resources->insert(Resources->end(), Promos.begin(), Promos.end());
}

//---------------------------------------------------------------------------
// Scan arguments to functional unit instances, and promote implicit group
// definitions to cluster resources.
//---------------------------------------------------------------------------
void promoteFuncUnitGroupArgs(ClusterInstance *Cluster) {
  for (auto *Instance : *Cluster->getFuncUnits()) {
    for (auto *Arg : *Instance->getArgs()) {
      if (Arg->isGroupRef() && Arg->isImplicitGroup())
        Cluster->getResources()->push_back(Arg->getDefinition());
    }
  }
}

//---------------------------------------------------------------------------
// Promote group member definitions to regular resources for CPUs, Clusters.
// We promote functional unit templates' resources separately for each
// instance.
//---------------------------------------------------------------------------
void MdlSpec::promoteResourceGroups() {
  for (auto *Cpu : Cpus) {
    promoteResourceGroupMembers(Cpu->getResources(), nullptr, nullptr);
    for (auto *Cluster : *Cpu->getClusters()) {
      promoteFuncUnitGroupArgs(Cluster);
      promoteResourceGroupMembers(Cluster->getResources(), Cpu->getResources(),
                                  nullptr);
    }
  }
}

//---------------------------------------------------------------------------
// Return true if this is a valid operand reference.
//---------------------------------------------------------------------------
bool MdlSpec::checkSubOperands(OperandRef *Ref, const Identifier *Opnd,
                               int Idx) {
  int Size = Ref->getOpNames()->size();
  bool IsRegClass = RegisterClassMap.count(Opnd->getName());
  bool IsOperand = OperandMap.count(Opnd->getName());

  if (IsRegClass)
    Ref->setRegClass(RegisterClassMap[Opnd->getName()]);
  if (IsOperand)
    Ref->setOperand(OperandMap[Opnd->getName()]);

  if (IsRegClass && Idx == Size)
    return true;

  if (IsOperand && Idx < Size) {
    OperandDef *Type = OperandMap[Opnd->getName()];
    if (!Type->getOperands()->empty()) {
      auto *Item =
        FindItem(*Type->getOperands(), (*Ref->getOpNames())[Idx]->getName());
      if (Item != nullptr)
        return checkSubOperands(Ref, Item->getType(), Idx + 1);
      Opnd = nullptr; // Force an error message.
    }
  }
  if (Opnd == nullptr || (!IsRegClass && !IsOperand)) {
    ErrorLog(Ref, "Undefined operand type: {0}", Ref->ToString());
    return false;
  }
  if (Idx < Size) {
    ErrorLog(Ref, "Over-qualified operand reference: {0}", Ref->ToString());
    return false;
  }
  if (IsOperand && !OperandMap[Opnd->getName()]->getOperands()->empty()) {
    ErrorLog(Ref, "Under-qualified operand reference: {0}", Ref->ToString());
    return false;
  }
  return true;
}

} // namespace mdl
} // namespace mpact

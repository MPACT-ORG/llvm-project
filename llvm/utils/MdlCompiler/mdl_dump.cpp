//===- mdl_dump.cpp - Dump out internal MDL objects -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MDL method implementations that dump the internal representation of the
// input machine descriptions.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <map>
#include <string>

#include "mdl.h"

namespace mpact {
namespace mdl {

//----------------------------------------------------------------------------
// Definitions of functions that format objects for printing that represent
// the input machine description.
//----------------------------------------------------------------------------
// Stringify a reference to an Identifier.
std::string Identifier::ToString() const { return getName(); }

// Stringify a phase name definition.
std::string PhaseName::ToString() const { return getName(); }

// Stringify a register definition.
std::string RegisterDef::ToString() const { return getName(); }

// Stringify a register class definition.
std::string RegisterClass::ToString() const {
  std::string Hdr = formatv("\nRegister class: {0}", getName());
  std::string Regs = StringVec<RegisterDef *>(Members, " { ", ", ", " }");
  return Hdr + Regs;
}

// Stringify a reference to a register class.
std::string RegisterClassRef::ToString() const { return Id->ToString(); }

// Stringify a pipe phase set definition.
std::string PipePhases::ToString() const {
  std::string Type;
  if (IsHard)
    Type = "(hard):";
  else if (IsProtected)
    Type = "(protected):";
  else
    Type = "(unprotected):";
  return formatv("Pipe Phases {0}{1}", Id->ToString(),
                 StringVec<PhaseName *>(PhaseNames, Type, ", "));
}

// Stringify a resource definition.
std::string ResourceDef::ToString() const {
  std::string Out = formatv("Resource {0}", Id->ToString());
  if (getResourceId() > 0)
    Out += formatv("{{{0}}", getResourceId());
  if (StartPhase)
    Out += formatv("({0}", StartPhase->ToString());
  if (EndPhase)
    Out += formatv("..{0}", EndPhase->ToString());
  if (StartPhase)
    Out += ")";
  if (BitSize > 0)
    Out += formatv(":{0}", BitSize);
  if (PoolSize > 0)
    Out += formatv("[{0}]", PoolSize);
  if (!Members.empty()) {
    auto Sep = getGroupType() == GroupType::kUseAll ? " & " : " , ";
    Out += StringVec<Identifier *>(&Members, " {", Sep, "}");
  }
  if (RegClass)
    Out += formatv(" <{0}>", getRegClass()->getId()->ToString());
  if (getPortResource() != nullptr)
    Out += formatv("<{0}>", getPortResource()->ToString());
  return Out;
}

// Format a string that summarizes the refs we've seen to this resource.
std::string ResourceDef::getRefSummary() const {
  if (EarliestRef == -1 && LatestRef == -1 && !PhaseExprSeen)
    return "";
  return formatv(",\tcycles: [{0}..{1}]{2}{3}", EarliestRef, LatestRef,
                 PhaseExprSeen ? " (expr) " : " ",
                 formatReferenceTypes(Types));
}

// Stringify a reference to a resource.
std::string ResourceRef::ToString() const {
  std::string Out = Id->ToString();

  if (isGroupRef()) {
    std::string Ids;
    auto Sep = Definition->getGroupType() == GroupType::kUseAll ? "&" : ",";
    for (auto *Member : Definition->getMemberDefs()) {
      if (!Ids.empty())
        Ids += Sep;
      Ids += std::to_string(Member->getResourceId());
    }
    Out += "{" + Ids + "}";
  }

  if (getResourceId() > 0)
    Out += formatv("{{{0}}", getResourceId());

  if (Member)
    Out += formatv(".{0}{{{1}}", Member->ToString(), getMemberId());
  if (First != -1 && Last != -1) {
    if (Last != First)
      Out += formatv("[{0}..{1}]", First, Last);
    else
      Out += formatv("[{0}]", First);
  }
  if (PoolCount != -1)
    Out += formatv(":{0}", PoolCount);
  if (PoolCountName)
    Out += formatv(":{0}", PoolCountName->ToString());
  if (ValueName)
    Out += formatv(":{0}", ValueName->ToString());
  if (OperandIndex != -1)
    Out += formatv("-->{0}", OperandIndex);
  return Out;
}

static const char *Divider =
    "===========================================================\n";

// Stringify a CPU instance.
std::string CpuInstance::ToString() const {
  std::string Out =
      formatv("{0}Cpu Definition:{1}\n\n", Divider, Id->ToString());

  return Out + formatv("{0}{1}{2}{3}{4}", StringVec<PipePhases *>(Phases),
                       StringVec<ResourceDef *>(Issues, "Issue Slots:", ", "),
                       StringVec<ResourceDef *>(Resources),
                       StringVec<ClusterInstance *>(Clusters),
                       StringVec<ForwardStmt *>(ForwardStmts));
}

// Stringify a cluster instance.
std::string ClusterInstance::ToString() const {
  std::string Out = formatv("{0}Cluster: {1}\n\n", Divider, Id->ToString());

  return Out + formatv("{0}{1}{2}{3}",
                       StringVec<ResourceDef *>(Issues, "Issue Slots:", ", "),
                       StringVec<ResourceDef *>(Resources),
                       StringVec<FuncUnitInstance *>(FuncUnits),
                       StringVec<ForwardStmt *>(ForwardStmts));
}

// Stringify a single forwarding statement.
std::string ForwardStmt::ToString() const {
  std::string Out = formatv("Forward: {0} -> ", FromUnit->getName());
  for (auto [Unit, Cycles] : ToUnits)
    Out += formatv("{0}({1})", Unit->getName(), Cycles);
  return Out + "\n";
}

// Stringify a functional unit instance.
std::string FuncUnitInstance::ToString() const {
  std::string Out = formatv("Func Unit: {0}", Type->ToString());
  if (Id)
    Out += formatv(" {0}", Id->ToString());
  Out += StringVec<ResourceRef *>(Args, "(", ", ", ")");
  if (PinAny)
    Out += StringVec<Identifier *>(PinAny, " -> ", " | ", "");
  if (PinAll)
    Out += StringVec<Identifier *>(PinAll, " -> ", " & ", "");
  return Out;
}

// Stringify a subunit instance.
std::string SubUnitInstance::ToString() const {
  std::string Out = StringVec<Identifier *>(Predicates, "[", ",", "] : ");
  Out += formatv("Subunit: {0}", Id->ToString());
  if (Args == nullptr)
    return Out + "()\n";
  return Out + StringVec<ResourceRef *>(Args, "(", ", ", ")\n");
}

// Stringify a latency instance.
std::string LatencyInstance::ToString() const {
  std::string Out = StringVec<Identifier *>(Predicates, "[", ",", "] : ");
  Out += formatv("Latency {0}", Id->ToString());
  if (Args == nullptr)
    return Out + "()\n";
  return Out + StringVec<ResourceRef *>(Args, "(", ", ", ")\n");
}

// Stringify a parameter of a functional unit, subunit, or latency template.
std::string Params::ToString() const {
  const char *Kinds[] = {"p", "c", "r"};
  return formatv("{0}:{1}", Kinds[static_cast<int>(Type)], Id->ToString());
}

// Stringify a functional unit template definition.
std::string FuncUnitTemplate::ToString() const {
  std::string Out =
      formatv("{0}Func Unit Template: {1}\n\n", Divider, Id->ToString());

  if (Bases && !Bases->empty())
    Out +=
        StringVec<Identifier *>(Bases, "Base Functional Unit: ", ", ", "\n");

  Out += StringVec<Params *>(FuParams, "Template Parameters(", ", ", ")\n\n");
  if (Ports && !Ports->empty())
    Out += StringVec<Identifier *>(Ports, "Ports: ", ", ", "\n");
  Out += StringVec<ResourceDef *>(Resources);
  Out += StringVec<Connect *>(Connections, "", "", "");
  Out += StringVec<SubUnitInstance *>(Subunits, "", "", "");
  return Out;
}

// Stringify a connect statement in a functional unit template.
std::string Connect::ToString() const {
  std::string Out = formatv("Connect {0}", Id->ToString());
  if (RegClass)
    Out += formatv(" to {0}", RegClass->ToString());
  if (Resource)
    Out += formatv(" via {0}", Resource->ToString());
  return Out + "\n";
}

// Stringify a subunit template definition.
std::string SubUnitTemplate::ToString() const {
  std::string Out =
      formatv("{0}Sub Unit Template: {1}\n\n", Divider, Type->ToString());

  if (Bases && !Bases->empty())
    Out += StringVec<Identifier *>(Bases, "Base Subunits: ", ", ", "\n");

  Out += StringVec<Params *>(SuParams, "Template Parameters(", ", ", ")\n") +
         StringVec<LatencyInstance *>(Latencies);
  Out += "\n";
  return Out;
}

// Stringify a latency template definition.
std::string LatencyTemplate::ToString() const {
  auto Out = formatv(
      "{0}Latency Template: {1}\n\n{2}{3}{4}", Divider, Id->ToString(),
      StringVec<Identifier *>(BaseIds, "Bases: ", ", ", "\n"),
      StringVec<Params *>(LatParams, "Template Parameters(", ", ", ")\n"),
      StringVec<Reference *>(References, "   ", "\n   "));

  for (auto &[Predicate, Funits] : ReferencedFus->getFuncUnits())
    Out += formatv("FUs: {0} -> {1}\n", (Predicate == "" ? "<all>" : Predicate),
            StringSet<std::string>(&Funits, "", ", ", ""));
  return Out;
}

// Find an appropriate name for a operand reference type.
std::string OperandRef::getTypeName() const {
  if (Decl)
    return Decl->getTypeName();
  if (Operand)
    return Operand->getName();
  if (RegClass)
    return RegClass->getName();
  if (OpType)
    return OpType->ToString();
  return "";
}

// Stringify a single operand descriptor in a latency reference object.
std::string OperandRef::ToString() const {
  std::string Out = getTypeName();
  if (!Out.empty())
    Out += ":";
  Out += "$" + StringVec<Identifier *>(OpNames, "", ".", "");
  if (OperandIndex != -1)
    Out += formatv("[{0}]", OperandIndex);
  return Out;
}

// Stringify a latency expression for debug output.
std::string PhaseExpr::ToString() const {
  std::string Lstr = Left ? Left->ToString() : "";
  std::string Rstr = Right ? Right->ToString() : "";

  switch (Operation) {
  case kPlus:
    return formatv("({0}+{1})", Lstr, Rstr);
  case kMinus:
    return formatv("({0}-{1})", Lstr, Rstr);
  case kMult:
    return formatv("({0}*{1})", Lstr, Rstr);
  case kDiv:
    return formatv("({0}/{1})", Lstr, Rstr);
  case kNeg:
    return formatv("(-{0})", Lstr);
  case kPositive:
    return formatv("{{{0}}", Lstr);
  case kOpnd:
    return Operand->ToString();
  case kInt:
    return formatv("{0}", Number);
  case kPhase:
    if (Phase)
      return Phase->ToString();
    if (PhaseId)
      return PhaseId->ToString();
  }
  return "Unknown";
}

// Create a string that briefly represents the protection type of a phase.
std::string PhaseName::formatProtection() const {
  if (IsHard)
    return ".h";
  if (IsProtected)
    return ".p";
  return ".u";
}

// Stringify a reference argument in a latency template.
std::string Reference::ToString() const {
  std::string Out;
  if (isConditionalRef()) {
    Out = getConditionalRef()->ToString(false);
  } else {
    Out = convertRefTypeToString(getRefType());

    if (getRefType() == RefTypes::kFus) {
      Out += "(";
      ResourceRef *Res = nullptr;
      if (!Resources->empty()) {
        Res = (*Resources)[0];
        Out += Res->getName();
      }
      if (Phase != nullptr)
        Out += formatv("<{0}:{1}> ", Phase->ToString(), getUseCycles());
      else
        Out += formatv("<{0}> ", getUseCycles());

      if (getMicroOps() != 0)
        Out += formatv("Mops={0} ", getMicroOps());
      if (getBufferSize() != -1)
        Out += formatv("Buffersize={0} ", getBufferSize());
      if (RefFlags::isBeginGroup(getFuFlags()))
        Out += "BeginGroup ";
      if (RefFlags::isEndGroup(getFuFlags()))
        Out += "EndGroup ";
      if (RefFlags::isSingleIssue(getFuFlags()))
        Out += "SingleIssue ";
      if (RefFlags::isRetireOOO(getFuFlags()))
        Out += "RetireOOO ";
      Out += ")";
      if (Predicates)
         Out += StringVec<Identifier *>(Predicates, "  {", ",", "}");
      return Out;
    }
    Out += formatv("{0}({1}", Phase->formatProtection(),
                   Phase->ToString());
    if (getUseCycles() != 1)
      Out += formatv(":{0}", getUseCycles());
    if (getRepeat() > 1)
      Out += formatv("[{0},{1}]", getRepeat(), getDelay());
    if (Operand)
      Out += formatv(", {0}", Operand->ToString());
    if (getRefType() != RefTypes::kFus && !Resources->empty())
      Out += StringVec<ResourceRef *>(Resources, ", <", ", ", ">");
    if (Port && Port->getRegClass())
      Out += formatv(", port {0}<{1}>", Port->getName(),
                     Port->getRegClass()->getName());
    Out += ")";
  }
  if (Predicates)
    Out += StringVec<Identifier *>(Predicates, "  {", ",", "}");
  return Out;
}

// Stringify an if/then/else reference.
std::string ConditionalRef::ToString(bool IsElse) {
  std::string Pred = Predicate ? " if " + Predicate->getName() : " ";
  std::string Out = (IsElse ? " else " : "") + Pred + "\n";
  for (auto *Ref : getRefs())
    Out += "           " + Ref->ToString() + "\n";
  if (getElseClause())
    Out += getElseClause()->ToString(true);
  if (Out.back() == '\n')
    Out.pop_back();
  return Out + "\n";
}

// Stringify a single operand declaration for an instruction or operand.
std::string OperandDecl::ToString() const {
  if (PrintFullyQualifiedDeclaration && Types->size() > 1)
    return StringVec<Identifier *>(Types, "(", ".", ") ") +
           StringVec<Identifier *>(Names, "(", ".", ")");
  return formatv("{0} {1}", Types->back()->getName(), Names->back()->getName());
}

// Stringify a single operand definition.
std::string OperandDef::ToString() const {
  std::string Out = formatv("Operand: {0}", getName());
  std::string Tstr = Type ? formatv("type({0});", Type->ToString()) : "";

  // For LLVM operands, write Out a short form of the operand.
  if (Operands && Type && !Bases)
    return Out + StringVec<OperandDecl *>(Operands, "(", ", ", ")") +
           formatv(" {{ {0} }\n", Tstr);
  if (Bases)
    Out += StringVec<Identifier *>(Bases, ": ", ": ", "");
  Out += " {\n";
  if (Type)
    Out += formatv("    {0}\n", Tstr);
  return Out + StringVec<OperandAttribute *>(Attributes) + "}\n";
}

// Stringify an operand attribute.
std::string OperandAttribute::ToString() const {
  std::string Out = formatv("    attribute {0} = ", Name->ToString());
  if (getValues()->size() == 1) {
    Out += formatv("{0}", getValues(0));
  } else {
    Out += "[";
    for (auto Value : *getValues())
      Out += formatv("{0},", Value);
    Out += "]";
  }
  if (!PredicateValues->empty())
    Out += "\n      ";
  if (!getType().empty())
    Out += formatv(" if {0}", getType());
  if (!PredicateValues->empty())
    Out += StringVec<PredValue *>(PredicateValues, " [", ", ", "]");
  if (Predicate)
    Out += StringVec<Identifier *>(Predicate, "  {", ",", "}");
  return Out;
}

// Format an operand predicate value. Mostly we want to avoid printing
// out long decimal numbers.
std::string PredValue::formatValue(int64_t Value) const {
  constexpr int kMinValue = 0;
  constexpr int kMaxValue = 9999;
  if (Value >= kMinValue && Value <= kMaxValue)
    return std::to_string(Value);
  else
    return formatv("{0:X8}UL", static_cast<uint64_t>(Value));
}

// Stringify an operand attribute predicate value.
std::string PredValue::ToString() const {
  if (isValue())
    return formatValue(getValue());
  if (isRange())
    return formatv("{0}..{1}", formatValue(getLow()), formatValue(getHigh()));
  if (isMask())
    return formatv("{{ {0:X8}UL }", getMask());
  return "empty";
}

// Stringify a single instruction definition.
std::string InstructionDef::ToString() const {
  return formatv(
      "Instruction: {0}{1}{2}{3}{4}", getName(),
      StringVec<OperandDecl *>(Operands, "(", ", ", ")\n"),
      StringVec<OperandDecl *>(FlatOperands, "\t\tflat(", ", ", ")\n"),
      StringVec<Identifier *>(Subunits, "\t\t{ subunit(", ",", "); }\n"),
      StringVec<Identifier *>(Derived, "\t\t{ derived(", ",", "); }\n"));
}

// Stringify all the instruction definitions.
// We organize the list by subunit, so that instructions sharing a subunit are
// dumped next to each other.  The purpose of this is to help the user write
// and debug the machine description for similar instructions.
std::string DumpInstructionDefs(const InstructionList &Instructions) {
  // build a map of instruction lists indexed by the first subunit name.
  std::string Out;
  std::map<std::string, InstructionList> InstrMap;
  for (auto *Instr : Instructions)
    if (!Instr->getSubunits()->empty())
      InstrMap[(*Instr->getSubunits())[0]->getName()].push_back(Instr);

  for (auto &Entries : InstrMap) {
    auto [SuName, Instructions] = Entries;
    for (auto *Instr : Instructions)
      Out += Instr->ToString();
  }

  return Out;
}

// Stringify the entire machine description.
std::string MdlSpec::ToString() const {
  return formatv("{0}Machine Description\n\n", Divider) +
         StringVec<PipePhases *>(&PipeDefs) +
         StringVec<ResourceDef *>(&Resources) +
         StringVec<RegisterDef *>(&Registers, "", ", ", "\n") +
         StringVec<RegisterClass *>(&RegClasses) +
         StringVec<CpuInstance *>(&Cpus) +
         StringVec<FuncUnitTemplate *>(&FuncUnits) +
         StringVec<SubUnitTemplate *>(&Subunits) +
         StringVec<LatencyTemplate *>(&Latencies) +
         StringVec<OperandDef *>(&Operands) +
         DumpInstructionDefs(Instructions);
}

// Print details of a single functional unit instantiation.
void FuncUnitInstantiation::dumpFuncUnitInstantiation() {
  auto Out = formatv("{0}: {{{1}} {2} {3}(", Cpu->getName(),
                     Cluster->getName(), FuncType->getName(),
                     Instance->getName());
  if (ResourceRefList *Args = Instance->getArgs()) {
    int FuParams = std::min(FuncType->getParams()->size(), Args->size());
    for (int ArgId = 0; ArgId < FuParams; ArgId++) {
      if ((*FuncType->getParams())[ArgId]->isResource())
        Out += getResourceArg(ArgId)->ToString();
      else
        Out += getClassArg(ArgId)->ToString();
      if (ArgId < FuParams - 1)
        Out += ", ";
    }
  }
  std::cout << Out << ")\n";
}

// Print details of a single functional unit instance.
void ClusterInstance::dumpFuncUnitInstantiations() {
  for (auto *Fu : FuInstantiations) {
    std::cout << "\nFunc_unit: " << Fu->getFuncType()->getName()
              << "---------------------------------------\n";
    Fu->dumpFuncUnitInstantiation();
  }
}

// Print details of all functional unit instantiations.
void MdlSpec::dumpFuncUnitInstantiations() {
  for (const auto *Cpu : Cpus)
    for (auto *Cluster : *Cpu->getClusters())
      Cluster->dumpFuncUnitInstantiations();
}

// Print details of a single subunit instantiation.
void SubUnitInstantiation::dumpSubUnitInstantiation() {
  auto Out =
      formatv("{0}: {{{1}} {2} {3} <{4}>(", FuncUnit->getCpu()->getName(),
              FuncUnit->getCluster()->getName(),
              FuncUnit->getFuncType()->getName(),
              FuncUnit->getInstance()->getName(), getSubunit()->getName());
  if (ResourceRefList *Args = getSubunit()->getArgs()) {
    int NumParams = std::min(SuTemplate->getParams()->size(), Args->size());
    for (int ArgId = 0; ArgId < NumParams; ArgId++) {
      if ((*SuTemplate->getParams())[ArgId]->isResource())
        Out += getResourceArg(ArgId)->ToString();
      else
        Out += getPortArg(ArgId)->ToString();
      if (ArgId < NumParams - 1)
        Out += ", ";
    }
  }
  Out += ")\n";
  for (auto *Ref : getReferences())
    Out += formatv("    {0}\n", Ref->ToString());
  std::cout << Out;
}

// Print details of all subunit instantiations.
void MdlSpec::dumpSubUnitInstantiations() {
  // Dump out all instantiations for each subunit.
  for (const auto &Subunit : SuInstantiations) {
    auto [Name, Unit] = Subunit;
    std::cout << formatv(
        "\nSubunit: {0} ---------------------------------------\n", Name);
    for (auto *Su : *Unit)
      Su->dumpSubUnitInstantiation();
  }
}

// Print details of a single latency instantiation.
void LatencyInstantiation::dumpLatencyInstantiation() {
  auto *FuncUnit = Subunit->getFuncUnit();
  auto Out = formatv("{0}: {{{1}} {2} {3} <{4}>[{5}](",
                     FuncUnit->getCpu()->getName(),
                     FuncUnit->getCluster()->getName(),
                     FuncUnit->getFuncType()->getName(),
                     FuncUnit->getInstance()->getName(),
                     Subunit->getSubunit()->getName(), Latency->getName());
  if (ResourceRefList *Args = Latency->getArgs()) {
    int NumParams = std::min(LatTemplate->getParams()->size(), Args->size());
    for (int ArgId = 0; ArgId < NumParams; ArgId++) {
      if ((*LatTemplate->getParams())[ArgId]->isResource())
        Out += getResourceArg(ArgId)->ToString();
      else
        Out += getPortArg(ArgId)->ToString();
      if (ArgId < NumParams - 1)
        Out += ", ";
    }
  }
  std::cout << Out << ")\n";
}

void MdlSpec::dumpPredicates() {
  for (const auto &[Name, Expr] : PredicateTable)
    std::cout << formatv("Predicate {0} : {1}\n\n", Name, Expr->ToString(0));
}

// Format a string that represents the ids associated with a resource.
std::string ResourceDef::formatResource() {
  int Id = getResourceId();
  std::string Out = formatv("{0} : ", getDebugName());
  if (!isGroupDef() && getPoolSize() <= 1)
    Out += std::to_string(Id);
  if (isGroupDef()) {
    Out += formatv("(gid={0}) ", getPoolId());
    Out += "[";
    for (auto *Mem : getMemberDefs())
      Out += std::to_string(Mem->getResourceId()) + ",";
    Out.pop_back();
    Out += "]";
  }
  if (getPoolSize() > 1) {
    Out += formatv("(pid={0}) ", getPoolId());
    Out += formatv("[{0}..{1}]", Id, getPoolSize() + Id - 1);
  }

  return Out;
}

std::string SubPool::ToString() const {
  if (getFirst() == -1 && getLast() == -1)
    return "[group]";
  return formatv("subrange: [{0}..{1}]", getFirst(), getLast());
}

// Write out all allocation pools associate with a subpool.
std::string SubPoolInfo::ToString(std::string subpool) const {
  std::string Out;
  int PoolId = getSubpoolId();
  for (auto Rit = Counts.rbegin(); Rit != Counts.rend(); Rit++)
    Out += formatv("    Subpool:{0} size:{1} {2}\n", PoolId++, *Rit, subpool);
  return Out;
}

// Dump resource ids for each resource.
void MdlSpec::dumpResourceIds() {
  std::string Out;
  for (auto *Cpu : Cpus) {
    Out += formatv("\nResources defined for '{0}' "
                   "---------------------------------------\n", Cpu->getName());
    for (auto Res : Cpu->getAllResources())
      Out += formatv("{0}{1}\n", Res->formatResource(), Res->getRefSummary());
    Out += formatv("\nPooled resources defined for '{0}' "
                   "--------------------------------\n", Cpu->getName());
    for (auto *Res : Cpu->getPoolResources())
      if (!Res->getAllocSizes().empty()) {
        Out += formatv("{0}{1}\n", Res->formatResource(),
                       Res->getRefSummary());
        for (auto &[Subpool, Info] : Res->getSubPools())
          Out += Info.ToString(Subpool.ToString());
      }
  }
  std::cout << Out; // Write out the string!
}

} // namespace mdl
} // namespace mpact

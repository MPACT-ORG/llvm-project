//===- mdl_output.h - Definitions for writing out an MDL database ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions used to manage the creation of the C++ output
// database, and in particular automatically avoid generating duplicate
// information.
//
//===----------------------------------------------------------------------===//

#ifndef MDL_COMPILER_MDL_OUTPUT_H_
#define MDL_COMPILER_MDL_OUTPUT_H_

#include <algorithm>
#include <fstream>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "mdl.h"
#include "mdl_generate.h"

namespace mpact {
namespace mdl {

using OutputSet = std::map<std::string, int>;

class OutputState {
  bool GenerateLLVMDefs;      // generate defs for stand-alone tools
  std::set<int> ForwardPhases; // funcs which need forward decls
  std::set<std::string> ForwardOpndRefs;       // OperandRef forward refs
  std::set<std::string> ForwardResourceRefs;   // conditional resource refs
  std::set<std::string> ForwardPooledRefs;     // conditional pooled refs
  std::set<std::string> ForwardConstraintRefs; // conditional constraints

  std::set<std::string> ForwardCondOpndRefs;
  std::set<std::string> ForwardCondResRefs;
  std::set<std::string> ForwardCondPoolRefs;
  std::set<std::string> ForwardCondConstraintRefs;

  OutputSet Phases;                    // non-trivial pipeline phases
  OutputSet RegisterClasses;           // register classes
  OutputSet OperandRefs;               // operand reference lists
  OutputSet CondOperandRefs;           // conditional operand references
  OutputSet CondResourceRefs;          // conditional resource references
  OutputSet CondPooledResourceRefs;    // conditional resource references
  OutputSet CondConstraints;           // conditional constraint references
  OutputSet UsedResourceRefs;          // used resource reference lists
  OutputSet HeldResourceRefs;          // held resource reference lists
  OutputSet ReservedResourceRefs;      // reserved resource reference lists
  OutputSet ResourceGroups;            // all unique resource groups
  OutputSet PoolDescriptors;           // all unique pools/subpools
  OutputSet PooledResourceRefs;        // pooled resource references
  OutputSet PooledCountFunctions;      // pooled count functions
  OutputSet PoolMaskFunctions;         // pool mask functions
  OutputSet Constraints;               // constraint sets
  OutputSet Subunits;                  // subunit lists
  OutputSet CpuInstrSubunits;          // cpu/instruction mapping to subunits
  OutputSet ReferencePredicates;       // reference predicate functions
  OutputSet VirtualRefPredicates;      // virtualized reference predicates
  OutputSet ForwardSets;               // a set of forwarding edge weights

  std::string FileNameC;       // name of the database output file
  std::string FileNameT;       // name of the target library output file
  std::string FileNameH;       // name of the generated header output file

  std::fstream *OutputC;        // the database output file
  std::fstream *OutputT;        // the target library database output file
  std::fstream *OutputH;        // the generated header file stream
  InstructionDatabase *Database; // the thing we're writing out

public:
  explicit OutputState(InstructionDatabase *Database, bool GenerateLLVMDefs)
      : GenerateLLVMDefs(GenerateLLVMDefs), Database(Database) {
    openOutputFiles();
  }
  ~OutputState() {
    outputC().close();
    outputT().close();
    outputH().close();

    // If errors were discovered after we started creating the output files,
    // delete all the output files.
    if (getSpec().ErrorsSeen()) {
      std::remove(FileNameC.c_str());
      std::remove(FileNameH.c_str());
      std::remove(FileNameT.c_str());
      Abort();
    }
  }

  using FormatName = std::string (*)(int);

  // Function to add entries to OutputSet tables. The purpose of these tables
  // is to avoid duplication in generated output. When an output string is
  // added to a table, if it already exists return its unique index in the
  // table. If it's new, add it and allocate it a new id.
  static int AddEntry(OutputSet &Table, std::string &Entry) {
    return Table.emplace(Entry, Table.size()).first->second;
  }

  // Top level function to handle output of the database.
  void writeCpuTable();
  void writeExterns();
  void writeHeader();
  void writeTrailer();
  void writeSpecDefinitions();
  void writeLLVMDefinitions();
  void writeResourceDefinitions();

  MdlSpec &getSpec() const { return Database->getSpec(); }

  // Format a predicate function and add to the reference predicates table.
  int formatPredicate(PredExpr *pred);
  // Format a predicate for the output file.
  std::string formatPredicateFunc(PredExpr *expr);

  OutputSet &getReferencePredicates() { return ReferencePredicates; }
  OutputSet &getVirtualRefPredicates() { return VirtualRefPredicates; }
  bool generateLLVMDefs() const { return GenerateLLVMDefs; }

private:
  // Open the output files, abort if it cannot be opened.
  void openOutputFiles();

  // Format a function body to generate non-trivial phases.
  std::string formatPhaseExpr(const PhaseExpr *Expr) const;
  // Format a pipeline phase reference.
  std::string formatPhase(const PhaseExpr *Expr);

  // Format a conditional reference for a single operand.
  std::string formatSingleConditionalOperand(ConditionalRef *Cond);
  // Format a non-trivial conditional operand ref.
  std::string formatConditionalOperandRef(ConditionalRef *Cond);
  // Format a conditional operand else clause.
  std::string formatIfElseOperandRef(ConditionalRef *Cond);
  // Format an operand reference.
  std::string formatOperandReference(const Reference *Ref);
  // Format an operand reference list, which can be share between subunits.
  std::string formatOperandReferenceList(const ReferenceList *Refs);
  // Format a single resource reference.
  std::string formatResourceReference(const ResourceEvent &Ref);
  // Format an operand reference list, which can be shared between subunits.
  std::string formatResourceReferences(InstrInfo *Info, RefType Type,
                                       OutputSet &OutputList, FormatName Name);
  // Format a conditional resource reference.
  std::string formatIfElseResourceRef(SubUnitInstantiation *Subunit,
                                      ConditionalRef *Ref, RefType Type,
                                      OutputSet &OutputList, FormatName Name,
                                      bool FormatPooledRefs);
  std::string formatResourceReferenceList(SubUnitInstantiation *Subunit,
                                          ReferenceList &List, RefType Type,
                                          OutputSet &OutputList,
                                          FormatName Name,
                                          bool FormatPooledRefs);
  // Format an explicit functional unit reference.
  std::string formatFuncUnitReference(SubUnitInstantiation *Subunit,
                                      Reference *Ref, bool FormatPooledRef);
  // Format a table of resource ids for a group.
  std::string formatResourceGroup(const ResourceEvent &Ref);
  // Format a reference to a pooled count, which may include a function.
  std::string formatPooledCount(const ResourceEvent &Ref);
  // Format a reference to a pool values function.
  std::string formatPoolValues(const ResourceEvent &Ref);
  // Format a single pool descriptor.
  std::string formatPoolDescriptor(const ResourceEvent &Ref);
  // Format a single pooled resource reference.
  std::string formatPooledResourceReference(const ResourceEvent &Ref);
  // Format a pooled reference list, which can be share between subunits.
  std::string formatPooledResourceReferences(InstrInfo *Info,
                                             OutputSet &OutputList,
                                             FormatName Name);
  // Format a single constraint.  Return an empty string if no constraint found.
  std::string formatConstraint(const Reference *Ref);
  // Find and format a list of constraints. Not all operands have constraints,
  // so the resulting string could be empty.
  std::string formatConstraintList(ReferenceList *Refs);
  // Format a single conditional constraint.
  std::string formatIfElseConstraint(ConditionalRef *Cond);
  // Format all the conditional and unconditional constraints for an instr.
  std::string formatPortReferences(InstrInfo *Info);
  // Format a single subunit.  These are also shared between instructions.
  std::string formatSubunit(InstrInfo *Info);
  // Format a subunit set for an instruction on a single CPU.
  std::string formatSubunits(const std::string &Instr,
                             const InstrInfoList &InfoList,
                             const std::string &Cpuname);

  // Methods for writing out parts of the machine description.
  void writeTable(const OutputSet &Objects, const std::string &Type,
                  const std::string &Suffix, FormatName Name,
                  const std::string &Title, const std::string &Info = "");
  void writeVectorTable(const OutputSet &Objects, const std::string &Type,
                        FormatName Name, const std::string &Title,
                        const std::string &Info = "");
  void writePhases(const OutputSet &Phases, FormatName Name) const;
  void writePoolCountFunctions(const OutputSet &Funcs, FormatName Name) const;
  void writePoolValueFunctions(const OutputSet &Funcs, FormatName Name) const;
  void writePredicateFunctions(const OutputSet &Funcs, FormatName Name,
                               const std::string &Type,
                               std::fstream &Output) const;
  void writeVirtualPredicateTable(const OutputSet &Funcs) const;
  void writeClasses(const OutputSet &RegClasses, FormatName Name);
  void writeInstructionInfo() const;
  void writeInstructionTables() const;

  // Methods for generating and writing out forwarding networks.
  class FwdNetwork {
  public:
    explicit FwdNetwork(int Units) : Units(Units) {
      Matrix = new int8_t *[Units];
      for (int I = 0; I < Units; I++)
        Matrix[I] = new int8_t[Units]();
    }
    ~FwdNetwork() {
      for (unsigned I = 0; I < Units; I++)
        delete Matrix[I];
      delete Matrix;
    }
    void set(int From, int To, int Weight) { Matrix[From][To] = Weight; }
    int get(int From, int To) const { return Matrix[From][To]; }
    int getUnits() const { return Units; }

  private:
    int8_t **Matrix;
    unsigned Units; // number of functional units modeled
  };

  void expandForwardStmt(FwdNetwork &Network, const CpuInstance *Cpu,
                         const ClusterInstance *Cluster,
                         const ForwardStmt *Fwd) const;
  std::vector<int> findUnitIds(const CpuInstance *Cpu,
                               const ClusterInstance *Cluster,
                               const Identifier *Name) const;
  std::string formatForwardingInfo(const CpuInstance *Cpu, FwdNetwork &Network);
  void generateForwardingInfo();

  void writeCpuList() const;

  std::fstream &outputC() const { return *OutputC; }
  std::fstream &outputT() const { return *OutputT; }
  std::fstream &outputH() const { return *OutputH; }
};

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_OUTPUT_H_

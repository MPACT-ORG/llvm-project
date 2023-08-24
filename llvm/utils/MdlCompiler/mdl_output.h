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
public:
  using FormatName = std::string (*)(int);

  // Function to add entries to OutputSet tables. The purpose of these tables
  // is to avoid duplication in generated output. When an output string is
  // added to a table, if it already exists return its unique index in the
  // table. If it's new, add it and allocate it a new id.
  static int AddEntry(OutputSet &table, std::string &entry) {
    return table.emplace(entry, table.size()).first->second;
  }

  explicit OutputState(InstructionDatabase *database, bool generate_llvm_defs)
      : generate_llvm_defs_(generate_llvm_defs), database_(database) {
    OpenOutputFiles();
  }
  ~OutputState() {
    output_c().close();
    output_t().close();
    output_h().close();

    // If errors were discovered after we started creating the output files,
    // delete all the output files.
    if (spec().ErrorsSeen()) {
      std::remove(file_name_c_.c_str());
      std::remove(file_name_h_.c_str());
      std::remove(file_name_t_.c_str());
      Abort();
    }
  }

  // Top level function to handle output of the database.
  void WriteCpuTable();
  void WriteExterns();
  void WriteHeader();
  void WriteTrailer();
  void WriteSpecDefinitions();
  void WriteLLVMDefinitions();
  void WriteResourceDefinitions();

  MdlSpec &spec() const { return database_->spec(); }

  // Format a predicate function and add to the reference predicates table.
  int FormatPredicate(PredExpr *pred);
  // Format a predicate for the output file.
  std::string FormatPredicateFunc(PredExpr *expr);

  OutputSet &reference_predicates() { return reference_predicates_; }
  OutputSet &virtual_ref_predicates() { return virtual_ref_predicates_; }
  bool generate_llvm_defs() const { return generate_llvm_defs_; }

private:
  // Open the output files, abort if it cannot be opened.
  void OpenOutputFiles();

  // Format a function body to generate non-trivial phases.
  std::string FormatPhaseExpr(const PhaseExpr *expr) const;
  // Format a pipeline phase reference.
  std::string FormatPhase(const PhaseExpr *expr);

  // Format a conditional reference for a single operand.
  std::string FormatSingleConditionalOperand(ConditionalRef *cond);
  // Format a non-trivial conditional operand ref.
  std::string FormatConditionalOperandRef(ConditionalRef *cond);
  // Format a conditional operand else clause.
  std::string FormatIfElseOperandRef(ConditionalRef *cond);
  // Format an operand reference.
  std::string FormatOperandReference(const Reference *ref);
  // Format an operand reference list, which can be share between subunits.
  std::string FormatOperandReferenceList(const ReferenceList *refs);
  // Format a single resource reference.
  std::string FormatResourceReference(const ResourceEvent &ref);
  // Format an operand reference list, which can be shared between subunits.
  std::string FormatResourceReferences(InstrInfo *info, RefType type,
                                       OutputSet &output_list, FormatName name);
  // Format a conditional resource reference.
  std::string FormatIfElseResourceRef(SubUnitInstantiation *subunit,
                                      ConditionalRef *ref, RefType type,
                                      OutputSet &output_list, FormatName name,
                                      bool format_pooled_refs);
  std::string FormatResourceReferenceList(SubUnitInstantiation *subunit,
                                          ReferenceList &list, RefType type,
                                          OutputSet &output_list,
                                          FormatName name,
                                          bool format_pooled_refs);
  // Format an explicit functional unit reference.
  std::string FormatFuncUnitReference(SubUnitInstantiation *subunit,
                                      Reference *ref, bool format_pooled_ref);

  // Format a table of resource ids for a group.
  std::string FormatResourceGroup(const ResourceEvent &ref);
  // Format a reference to a pooled count, which may include a function.
  std::string FormatPooledCount(const ResourceEvent &ref);
  // Format a reference to a pool values function.
  std::string FormatPoolValues(const ResourceEvent &ref);
  // Format a single pool descriptor.
  std::string FormatPoolDescriptor(const ResourceEvent &ref);
  // Format a single pooled resource reference.
  std::string FormatPooledResourceReference(const ResourceEvent &ref);
  // Format a pooled reference list, which can be share between subunits.
  std::string FormatPooledResourceReferences(InstrInfo *info,
                                             OutputSet &output_list,
                                             FormatName name);
  // Format a single constraint.  Return an empty string if no constraint found.
  std::string FormatConstraint(const Reference *ref);
  // Find and format a list of constraints. Not all operands have constraints,
  // so the resulting string could be empty.
  std::string FormatConstraintList(ReferenceList *refs);
  // Format a single conditional constraint.
  std::string FormatIfElseConstraint(ConditionalRef *cond);
  // Format all the conditional and unconditional constraints for an instr.
  std::string FormatPortReferences(InstrInfo *info);
  // Format a single subunit.  These are also shared between instructions.
  std::string FormatSubunit(InstrInfo *info);
  // Format a subunit set for an instruction on a single CPU.
  std::string FormatSubunits(const std::string &instr,
                             const InstrInfoList &info_list,
                             const std::string &cpuname);

  // Methods for writing out parts of the machine description.
  void WriteTable(const OutputSet &objects, const std::string &type,
                  const std::string &suffix, FormatName name,
                  const std::string &title, const std::string &info = "");
  void WriteVectorTable(const OutputSet &objects, const std::string &type,
                        FormatName name, const std::string &title,
                        const std::string &info = "");
  void WritePhases(const OutputSet &phases, FormatName name) const;
  void WritePoolCountFunctions(const OutputSet &funcs, FormatName name) const;
  void WritePoolValueFunctions(const OutputSet &funcs, FormatName name) const;
  void WritePredicateFunctions(const OutputSet &funcs, FormatName name,
                               const std::string &type,
                               std::fstream &output) const;
  void WriteVirtualPredicateTable(const OutputSet &funcs) const;
  void WriteClasses(const OutputSet &reg_classes, FormatName name);
  void WriteInstructionInfo() const;
  void WriteInstructionTables() const;

  // Methods for generating and writing out forwarding networks.
  class FwdNetwork {
  public:
    explicit FwdNetwork(int units) : units_(units) {
      matrix_ = new int8_t *[units];
      for (int i = 0; i < units; i++)
        matrix_[i] = new int8_t[units]();
    }
    ~FwdNetwork() {
      for (unsigned i = 0; i < units_; i++)
        delete matrix_[i];
      delete matrix_;
    }
    void set(int from, int to, int weight) { matrix_[from][to] = weight; }
    int get(int from, int to) const { return matrix_[from][to]; }
    int units() const { return units_; }

  private:
    int8_t **matrix_;
    unsigned units_; // number of functional units modeled
  };

  void ExpandForwardStmt(FwdNetwork &network, const CpuInstance *cpu,
                         const ClusterInstance *cluster,
                         const ForwardStmt *fwd) const;
  std::vector<int> FindUnitIds(const CpuInstance *cpu,
                               const ClusterInstance *cluster,
                               const Identifier *name) const;
  std::string FormatForwardingInfo(const CpuInstance *cpu, FwdNetwork &network);
  void GenerateForwardingInfo();

  void WriteCpuList() const;

  std::fstream &output_c() const { return *output_c_; }
  std::fstream &output_t() const { return *output_t_; }
  std::fstream &output_h() const { return *output_h_; }

  bool generate_llvm_defs_;      // generate defs for stand-alone tools
  std::set<int> forward_phases_; // funcs which need forward decls
  std::set<std::string> forward_opnd_refs_;       // OperandRef forward refs
  std::set<std::string> forward_resource_refs_;   // conditional resource refs
  std::set<std::string> forward_pooled_refs_;     // conditional pooled refs
  std::set<std::string> forward_constraint_refs_; // conditional constraints

  std::set<std::string> forward_cond_opnd_refs_;
  std::set<std::string> forward_cond_res_refs_;
  std::set<std::string> forward_cond_pool_refs_;
  std::set<std::string> forward_cond_constraint_refs_;

  OutputSet phases_;                    // non-trivial pipeline phases
  OutputSet register_classes_;          // register classes
  OutputSet operand_refs_;              // operand reference lists
  OutputSet cond_operand_refs_;         // conditional operand references
  OutputSet cond_resource_refs_;        // conditional resource references
  OutputSet cond_pooled_resource_refs_; // conditional resource references
  OutputSet cond_constraints_;          // conditional constraint references
  OutputSet used_resource_refs_;        // used resource reference lists
  OutputSet held_resource_refs_;        // held resource reference lists
  OutputSet reserved_resource_refs_;    // reserved resource reference lists
  OutputSet resource_groups_;           // all unique resource groups
  OutputSet pool_descriptors_;          // all unique pools/subpools
  OutputSet pooled_resource_refs_;      // pooled resource references
  OutputSet pooled_count_functions_;    // pooled count functions
  OutputSet pool_mask_functions_;       // pool mask functions
  OutputSet constraints_;               // constraint sets
  OutputSet subunits_;                  // subunit lists
  OutputSet cpu_instr_subunits_;        // cpu/instruction mapping to subunits
  OutputSet reference_predicates_;      // reference predicate functions
  OutputSet virtual_ref_predicates_;    // virtualized reference predicates
  OutputSet forward_sets_;              // a set of forwarding edge weights

  std::string file_name_c_;       // name of the database output file
  std::string file_name_t_;       // name of the target library output file
  std::string file_name_h_;       // name of the generated header output file

  std::fstream *output_c_;        // the database output file
  std::fstream *output_t_;        // the target library database output file
  std::fstream *output_h_;        // the generated header file stream
  InstructionDatabase *database_; // the thing we're writing out
};

} // namespace mdl
} // namespace mpact

#endif // MDL_COMPILER_MDL_OUTPUT_H_

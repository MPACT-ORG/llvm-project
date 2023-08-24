

## Using TdScan and the MDL compiler

Reid Tatge 	  tatge@google.com


[TOC]



#### **Overview of the process**

This document describes the steps to building the MDL compiler and Tablegen scraper (tdscan) so that you can create and debug MDL instruction descriptions for LLVM.

The “normal” process of using an MDL machine description for a target is to write the overall architecture description by hand, and generate an instruction description by scraping information from the tablegen description of the target.  The generated instruction description is explicitly imported by the MDL compiler to tie the hand-written architecture description to the instruction descriptions in the target’s tablegen files.

To keep the architecture in sync with the LLVM description, we extract and scrape the tablegen information as part of the compiler build process.  The extraction process uses tablegen to write out all the target information, and the scraper scans this file and produces an MDL-based description of instructions, operands, registers, and register classes.  This is imported by the architecture description so that the two descriptions are compiled together.  This produces .cc and .h files that can be included in the LLVM build.



#### **Scraping Information from Tablegen**

To synchronize the MDL for a target with LLVM, we need to extract all of the instruction, operand and register definitions from the tablegen description. The first step in this process is to get tablegen to dump its internal representation of the target description to a plain text file.


##### **Create tablegen information for a target:**

This step uses the normal tablegen program to produce a dump of all the tablegen information for any LLVM target.



*   <code>export LLVM=&lt;<em>path to llvm</em>></code>
*   <code>export TARGET=&lt;<em>family-name</em>></code> \
Where family-name is one of AArch64, AMDGPU, ARC, ARM, AVR, BPF, CSKY, Hexagon, Lanai, M68k, Mips, MSP430, NVPTX, PPC, RISCV, Sparc, SystemZ, VE, WebAssembly, X86, XCore
*   <code>.../clang-tblgen -print-records \
  -I $LLVM/llvm-project/llvm/include/ \
  -I $LLVM/llvm/include/llvm/IR/ \
  -I $LLVM/llvm-project/llvm/lib/Target/$TARGET/ \
  $LLVM/llvm-project/llvm/lib/Target/$TARGET/$(TARGET).td > ~/$(TARGET).txt</code>

This creates the file &lt;family\_name>.txt, which can be processed by “tdscan” to produce an MDL file that describes the ISA of the processor family.


##### **Scraping the tablegen file to produce ISA information**

In this step we use “tdscan” to process the tablegen output file, which produces an MDL language description of the target architecture.  \




*   <code>export TARGET=&lt;<em>family-name</em>></code>

    Where family-name is one of: AArch64, AMDGPU, ARC, ARM, AVR, BPF, CSKY, Hexagon, Lanai, LoongArch, M68k, Mips, MSP430, NVPTX, PPC, RISCV, Sparc, SystemZ, VE, WebAssembly, X86, XCore

*   `…/tdscan -–family_name=$TARGET $(TARGET).txt`

 \
This produces the file $(TARGET)\_instructions.mdl, which contains MDL descriptions for all instructions, operands, registers, and register classes defined in the td files for that target.

Anomalies: 



*   For Sparc, the family name is actually “SP”, while the file name is “Sparc.txt”.  
*   For PowerPC, the name of the td file is PPC.td  which resides at //third\_party/llvm/llvm-project/llvm/lib/Target/**PowerPC**

Note: Without the –family\_name argument, tdscan uses the input file name as the target name (minus the filename extension).  As noted above, in general the family name matches the input file name.

**Scraping the tablegen file to produce a full architecture spec**

If the tablegen description contains Schedules and/or Itinerary descriptions, you can also have tdscan produce an MDL architecture spec for a processor.  Currently, this applies to the following targets: AArch64, AMDGPU, ARM, Hexagon, Lanai, MIPS, PPC, RISCV, Sparc (SP), SystemZ, and X86.



*   <code>export TARGET=&lt;<em>family-name</em>></code>

    Where family-name is one of: AArch64, AMDGPU, ARM, Hexagon, Lanai, Mips, PPC, RISCV, Sparc, SystemZ, X86 (same family name caveat for Sparc)

*   `…/tdscan --gen_arch_spec -–family_name=$TARGET $(TARGET).txt`

This will produce both the instructions file ($(TARGET)\_instructions.mdl) and the architecture spec file ($(TARGET).mdl).  The generated architecture spec will explicitly import the instruction description file.  Compiling $(TARGET).mdl with the MDL compiler will produce an instruction database for the processor family.


#### **Compiling a Machine Description**

Generally, we separate the instruction descriptions from the architecture spec into separate .mdl files, and the architecture spec explicitly imports the instruction descriptions. So to compile a full machine description, we invoke the compiler on the architecture spec:


```
.../mdl CPU.mdl
```


This will create three files: CPUGenMdlInfo.inc,  CPUGenMdlTarget.inc, and CPUGenInfo.h, which contain the database of architecture and instruction information that is imported into LLVM.


##### **Command line options**

You can invoke the compiler with “--help” to get a brief description of the command line options. The following options are supported, and discussed in more detail below:


```
    --check_all_operands (Check references to all operands - not just registers);
      default: false;
    --check_usage (Check subunit, reference, and resource usage);
      default: false;

    --dump_fus (Dump functional unit instantiations); default: false;
    --dump_instr (Dump instruction information); default: false;
    --dump_llvm_defs (Dump LLVM definitions); default: false;
    --dump_preds (Dump user-define predicates); default: false;
    --dump_resources (Dump resource ids); default: false;
    --dump_spec (Dump entire mdl specification); default: false;
    --dump_sus (Dump subunit instantiations); default: false;

    --fatal_warnings (Treat warnings as errors); default: false;
    --import_dir (import file dir); default: "";
    --output_dir (output file dir); default: "";
    --warnings (Print warnings); default: true;
```



##### **Options that help debug a machine description under development**


###### **–check\_usage:**

This option checks for possible errors in the description:



*   It checks that every register operand is explicitly referred to any any latency rules that apply to that instruction.
*   It warns for any latency template reference (use, def, etc) that never appear to apply to any instruction. 
*   It warns for any unused subunit template (never referred to in any instruction.
*   It warns for any resource that is never referenced anywhere.

These are not errors, but could indicate that something is incorrectly modeled.


###### **–check\_all\_operands:**

This option does the same checks that –check\_usage performs, but also checks that every single operand - even non-register operands - always is referenced.  This is also not an error, but simply a diagnostic tool.


###### **–dump\_instr:**

This option dumps comprehensive information (to std::out) about every behavior of every instruction on every subtarget.  

**NOTE: **There are a LOT of instruction descriptions - each instruction has entries for the cross product of each processor, functional unit, and issue slot it can run on.  You'll notice that the entries are often almost identical except for where they run.  Internally, identical aspects of the description are shared - across different instructions, functional units, and processors - so this isn't as bad as it might seem.  If you look through the resource references, you should see EXACTLY what each instruction does in each context it can run in.  A few thoughts:



*   If you ignore functional unit and issue slot resources, many of the instances of an instruction are going to be identical (from the perspective of a simulator, for example). 
*   There are quite a few instructions that have different operand and resource latencies based on which functional unit they run on.  So the only difference between their descriptions will be a single latency (operand or resource). The good news is that the representation of all of this is pretty compact.
*   All of this information is encoded in the output file (&lt;family>.mdl.cc).  

So there is a massive amount of information here - not to worry: the compiler deduplicates everything, so there is very little redundancy in the generated database.  This is just the “raw” information the compiler generates internally.

Here's what the output of --dump\_instr looks like:  \



```
Instruction: MOV16rm(GR16 dst, i16mem src)
                flat(GR16 dst, (i16mem.ptr_rc) (src.0), (i16mem.i8imm) (src.1), 
                    (i16mem.ptr_rc_nosp) (src.2), (i16mem.i32imm) (src.3), 
                    (i16mem.SEGMENT_REG) (src.4)) {    
    subunit(sub579,sub1976,sub1977,sub1978,sub1979,sub1980,sub1981,sub1982,sub1983,
            sub1984,sub2767,sub2768,sub2769,sub2770); }
    Subunit: AlderlakeP.U11
      Operand references:
      ===>  def.p(E6, GR16:$dst[0])
      Resources:
            use.p(F1,U11{12})
      Pool Resources:
      Architectural Register Constraints:



Instruction: MOV16rm(GR16 dst, i16mem src)
                flat(GR16 dst, (i16mem.ptr_rc) (src.0), (i16mem.i8imm) (src.1),      
                    (i16mem.ptr_rc_nosp) (src.2), (i16mem.i32imm) (src.3),
                    (i16mem.SEGMENT_REG) (src.4)) { subunit(sub579,sub1976,sub1977,sub1978,sub1979,sub1980,sub1981,sub1982,sub1983,
        sub1984,sub2767,sub2768,sub2769,sub2770); }
    Subunit: Znver1.U0
      Operand references: 
      ===>  def.p(E5, GR16:$dst[0])
      ===>  use.p(E5, i16mem:$src.0[1])
      Resources:
            use.p(F1,U0{1})
      Pool Resources:
      Architectural Register Constraints:
```


**_How to Interpret MDL Debug Output_**

Each instruction record describes a single behavior of an instruction on a particular processor and functional unit.  For each instruction, we write out:



*   The instruction name (per LLVM) and the operand types/names as declared in llvm.   Some of these operands are composites of other operands.
*   The "flat" operand list: each composite operand is expanded to its components as discrete operands.  This is the "real" operand list.
*   The "Subunit": the processor and functional unit names for this instance of the instruction.
*   All of this instruction's operand references, and the name of the pipeline phase they happen in (E1, etc).  This includes operand-related resource references, if any.
*   All of this instruction's resource references, and when they happen
*   All of this instruction's pooled resource references, and when they happen.
*   Any architectural register constraints imposed on the instruction by this functional unit (most CPU’s don’t have these)

**Operands: \
**The operand references have the syntax (in the output):

      ` &lt;opcode> (&lt;protection>)? '(' &lt;pipeline_phase> ','  \
                                 &lt;operand_specification> ','             \
                                 (&lt;resource_references>)? ')'  \
`

where the opcodes are "use", "def", “predicate”.  &lt;Protection> is what kind of pipeline protection is used for this reference (protected, unprotected, hard), one of “.p”, “.u”, or “.h”.

**Resources:**

The resource references are the same, without the operand reference component.

      ` &lt;opcode> (&lt;protection>)? '(' &lt;pipeline_phase> ',' &lt;resource_references> ')' `

An operand specification has the syntax:


```
    <operand_type> ':' '$' <operand_name> '[' <operand_index> ']'
```


An example: GPR:$x[2]  refers to operand number 2 (in the flat operand list), called "x", which has operand type GPR. 

The resource references have the syntax:


```
    <name> '{' <resource_id> '}'
```


An example: alu1{2}    refers to a resource "alu1" which has a resource id of 1.

**Pooled Resources:**

Pooled resources have a slightly more complex syntax:


```
    <name> '{' <resource_id> '}' '[' <range> ']'  
                                   (':' <attribute>)* '-->' <resource_count>
```


An example: imm{26}[0..3]:size:bits-->2 refers to the "imm" resource, resource id 26, a subrange of members 0..3 with "size" and "bits" attributes, associated with operand 2.

Pooled resources also have a “subpool id” and “size requests” information. 


###### **--dump\_resources:**

Write descriptions of all defined resources to std::out. 

For each subtarget, we print a set of resource definitions, followed by a list of _pooled _resource definitions (if the description includes any resource pools).  

**_Example Resource Dump:_**


```
Resources defined for 'RISCV' ---------------------------------------
fake.RISCV.end : 1

Pooled resources defined for 'RISCV' --------------------------------

Resources defined for 'Rocket' ---------------------------------------
Funcunit.Rocket.__.U0 : 1,      cycles: [0..0] <Use>
Funcunit.Rocket.__.U1 : 2,      cycles: [0..0] <Use>
Funcunit.Rocket.__.U2 : 3,      cycles: [0..0] <Use>
Funcunit.Rocket.__.U3 : 4,      cycles: [0..0] <Use>
Funcunit.Rocket.__.U4 : 5,      cycles: [0..0] <Use>
Funcunit.Rocket.__.U5 : 6,      cycles: [0..0] <Use>
Funcunit.Rocket.__.U6 : 7,      cycles: [0..0] <Use>
Funcunit.Rocket.__._default_ : 8,       cycles: [0..0] <Use>
Issue.Rocket.__.s0 : 9
fake.Rocket.end : 10

Pooled resources defined for 'Rocket' --------------------------------

Resources defined for 'SiFive7' ---------------------------------------
Funcunit.SiFive7.__.U0 : 1,     cycles: [0..0] <Use>
Funcunit.SiFive7.__.U1 : 2,     cycles: [0..0] <Use>
Funcunit.SiFive7.__.U1 : 3,     cycles: [0..0] <Use>
Funcunit.SiFive7.__.U1 : 4,     cycles: [0..0] <Use>
Funcunit.SiFive7.__._default_ : 5,      cycles: [0..0] <Use>
Issue.SiFive7.__.s0 : 6
Issue.SiFive7.__.s1 : 7
fake.SiFive7.end : 8

Pooled resources defined for 'SiFive7' --------------------------------
```


**Interpreting Resource Dumps**

For each subtarget, we print a set of resource definitions, and a second list of _pooled _resource definitions.

**Resource definitions:**

In the resource definition list, each line describes a single resource or pooled resource on a single processor, including its fully-qualified name. There are three classes of resources reflected in their names: functional units, issue slots, and every explicitly defined resource.  Each line has the syntax:


```
   <resource_name> ':' <resource_id(s)>  
	'cycles:' '[' <earliest_latest_cycle> ']' '<' <reference_types> '>'
```


The resource name reflects the type and context of how a resource is defined: 



*   “Funcunit” describes the instance of a functional unit (in a CPU definition).
*   “Issue” is a resource defined as an issue slot.
*   “Resource” is any other explicitly defined resource.
*   After the initial identifier, the rest of the name indicates the context of the definition: the CPU, the optional cluster name, the optional functional unit name, followed by the actual defined resource name.

Following its name, we list its resource id (or resource ids if its a pool), its earliest and latest reference cycles, and the type of references seen.

A few examples:


    A functional unit, id=2, "used" in cycle 0:


```
      Funcunit.Rocket.U1 : 2,      cycles: [0..0] <Use>
```



    An issue slot, id=6 (no explicit references)


```
      Issue.SiFive7.s0 : 6
```



    A resource pool, ids 38-40, "used" in cycle 0:


```
      Resource.CPU.pool : [38..40], cycles: [0..0] <Use>
```



    A resource pool, ids 41-42, earliest Hold/Reserve cycle 2, latest Hold/Reserve cycle 11:


```
      Resource.CPU.my_pool : [41..42],  cycles: [2..11] <Hold Reserve>
```



      A single resource, id=56, earliest cycle=2, latest cycle=9:


```
      Resource.CPU.my_res : 56, cycles: [2..9] <Hold Reserve>
```


**Pooled resource definitions:**

The resource pool descriptions describe each allocation subpool.  Subpools are automatically created by the MDL compiler for every resource pool (all of which are listed in the resource definitions output). 

The order of subpools is important.  For each resource, the compiler allocates a subpool for every allocation request of a particular subrange of resources, and for a particular number of resources.  The subpools (for each pool) are then ordered such that the most restricted subranges are allocated first, and for each subrange the biggest allocations are done first. 

By parsing all allocations into these subpools, we can use a greedy algorithm and produce the best possible allocation.

**Note:** Pool allocation is actually moderately limited in its ability to deal with allocation across pipeline phases.  In general, you want resource pools that are allocated to be defined with a specific pipeline phase (ie: resource(F1) res[5];).  The MDL compiler doesn’t check this (although it could, and probably should).


###### **–fatal\_warnings:**

Treat warning messages as fatal errors.


###### **–warnings, –nowarnings:**

Print warnings (by default this is on.)** **


##### **Options that help debug the compiler**


###### **–dump\_preds:**

Dumps all predicate definitions scraped form LLVM to std::out (in a format similar to the way they appear in tablegen files.)


###### **–dump\_spec:**

Dumps the internal representation of the entire MDL specification to std::out. \



###### **–dump\_sus:**

Dump every instantiated subunit to std::out.  This shows exactly which resources are passed down to the subunit, and subsequently to the latency template and all the associated latency rules (defs, uses, etc).


#### **MDL Compiler Output**

The MDL compiler generates three C++ files (two .inc files and one .h file) that encapsulate the information derived from the input machine description file. Generally, the names of MDL input files are of the form _<family>.mdl_, where _<family>_ is the llvm target name.  The output file names are correspondingly _<family>GenMdlInfo.inc, &lt;family>GenMdlTarget.inc,_ and _<family>GenMdlInfo.h_.

The types of these objects used in the database files are declared in 

llvm-project/llvm/include/llvm/MC/MDLInstrInfo.h


    llvm-project/llvm/include/llvm/MC/MDLInfo.h


##### **Generated header file: &lt;family>MDLGenMdlInfo.h**

This generated file contains a few constant definitions that describe overall parameters of the architecture family, and constants that associate defined resources with ids. All of these definitions are wrapped in the “llvm” namespace. 


###### Global Maximums

There are a few constants defined that apply to all CPUs (subtargets) defined in a single machine description.  These are the overall maximum values versus the values that are defined on a per-subtarget basis (in the generated C++ CpuInfo object, described later). The following constants are defined:


    **kMaxResourceId:**


        The maximum resource id allocated over all subtargets.


    **kMaxUsedResourceId:**


        The maximum resource id that appears in “use” statements.  These are generally defined for pipeline hazards, functional units, and issue slots, If the compiler uses a dense set representation for resources associated with these things, this constant indicates the size needed to represent them all.


    **kMaxPipePhase:**


        The latest pipe phase in which resources are “used”.  Paired with kMaxUsedResourceId, this constant can be used to implement dense sets for used resources.


    **kMaxIssue:**


        Indicates the maximum number of instructions that can be issued in parallel. This is primarily used for VLIW architectures, but applies to any architecture that can do parallel issue of instructions (any superscalar processor)


    **kMaxPools:**


        Number of resource pools managed by the compiler


    **kMaxPoolCount**


        Maximum number of resources allocated out of any pool.

There’s honestly not much use for these, since all of these values are stored in the CpuInfo object associated with each subtarget.  


###### Resource Definitions

Every resource defined in a machine description is given a distinct C++ constant int definition that allows compiler code to access its value.  Each defined resource is wrapped in a namespace for the family, CPU, and (optional) cluster that it is defined in. Structured resource members are additionally wrapped in the enclosing resource definition. Globally defined resources are inherited by all CPU’s in the family.  Using this methodology, each defined resource has a unique name in its namespace hierarchy.

We don’t currently provide names for resources defined in functional units - primarily because there is no current utility for providing access to them from C++.  

This is best illustrated with an example:


```
    family FAM;

    resource global_res;

    cpu CPU {
    resource cpu_res;
    	func_unit alu cpu_alu();
    cluster CLU {
    		resource cluster_res;
    	      resource struct { f1, f2 };
    		issue slot0, slot1;
    		func_unit alu alu0();
    		func_unit alu alu1();
    	}
    }
```


This generates the following definitions in the output header file:


```
    namespace llvm {
      namespace FAM {
        namespace CPU {
          const int global_res = 1;           // FAM::CPU::global_res
          const int cpu_res = 2;              // FAM::CPU::cpu_res
    	const int cpu_alu = 3;              // FAM::CPU::cpu_alu (func unit)
          namespace CLU {
            const int slot0 = 4;              // FAM::CPU::CLU::slot0 (issue slot)
            const int slot1 = 5;              // FAM::CPU::CLU::slot1 (issue slot) 
            const int alu0 = 6;               // FAM::CPU::CLU::alu0 (func unit)
            const int alu1 = 7;               // FAM::CPU::CLU::alu1 (func unit)
            const int cluster_res = 8;        // FAM::CPU::CLU::cluster_res
            namespace struct {
              const int f1 = 9;               // FAM::CPU::CLU::struct::f1
              const int f2 = 10;              // FAM::CPU::CLU::struct::f2
            }
          }
        }
      }
    }
```



##### **Generated C++ file: &lt;family>MdlInstrInfo.inc**

The generated C++ file contains initializations of the database that contains all of the architecture information for all defined subtargets.  These objects are all defined in “llvm\_instruction\_info.h”.

The top level object is a CpuTable, that contains a dictionary which maps llvm subtarget names to CpuInfo objects.  CpuTable has a single method:

	CpuInfo \*cpu(std::string subtarget) \
Which returns a pointer to the allocated and initialized CpuInfo object for the specified subtarget. A compiler can use as many subtargets as needed, although typically only one is selected, allocated, and initialized.

Each CpuInfo object contains the following information for a single subtarget:



*   The number of resources
*   The number of “used” resources
*   The number of pipeline phases that use resources
*   The maximum number of instructions that can issue in parallel
*   The number of resource pools that must be allocated for instructions
*   The earliest pipeline phase of operand uses
*   A function point to initialize the subtargets subunit table.
*   A memoized pointer to the initialized subunit table.

The first 6 integers are subtarget-specific values related to the globally maximal values defined in the generated header file (described earlier), and are (or can be) used to size various objects used in scheduling.  The subunit table is automatically allocated and initialized and contains information about each instruction’s behaviors for the selected subtarget.

The subunit table contains, for each instruction, a set of instruction behaviors.   Each behavior is described as a single Subunit, which contains pointers to:



*   A set of operand references
*   A set of resource uses
*   A set of resource holds
*   A set of resource reservations
*   A set of pooled resource references
*   A set of context-specific register constraints

All of these sets are shared between subunits, and subunits are shared between instructions with identical behaviors. 





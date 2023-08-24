

## Virtual Predicate Functions

Reid Tatge           tatge@google.com


[TOC]



### Background

In LLVM, latencies of instructions are modeled by associating Read- or Write-resources with instruction operands.  Briefly, the “latency” of an operand is represented by the “latency” of the resource.  Resources are primarily associated with “output” operands - operands which write results to registers, but can also be associated with “input” operands.  

To support variable latencies on reads and writes, Tablegen uses ReadVariant and WriteVariant records which each associate a set of explicitly predicated resources with a single read or write resource.  At compile time, the predicates are evaluated to determine which predicated resource is to be used for a particular read or write. 

LLVM has a predicate language which can be used to check the number, type, and contents of an instruction’s operands.  The language also includes the ability to call a C++ function to decide whether the predicate applies to a particular instruction instance. 

LLVM has two distinct instruction internal representations (IRs) that the predicates can be applied to: MachineInstr and MCInst, which are packaged in separate libraries.  MCInst is a low-level instruction representation, suitable for writing assemblers and object-code analysers.  MachineInstr is the instruction representation used in the LLVM code generator, and is a much higher-level representation. Predicates can apply to either representation, with the caveat that since MachineInstr carries more semantic information, predicates operating on that can do more detailed tests in some cases. 

While most of the predicate language applies equally to either representation (such as “number of operands”, or “type of operand”), you can specify a call to MCInst- or MachineInstr-specific functions. Therefore, a predicate function can reference different functions in either (or both) libraries. 

This isn’t a problem when both the Target and MC libraries are included in an application, which is the typical use case.  However, a few applications include _only_ MC, which leads to link problems when a predicate function references functions in the Target library.  

The TableGen solution produces two separate schedule-class resolution functions for MCInst and MachineInstr, and they inline all the specialized predicate functions.  These two functions are quite large (in some cases over 4000 LOC), and are mostly logically identical functions, which only do different things when a predicate specifies a representation-specific function call. All the predicate evaluation is folded into these two functions (resolveScheduleClass() for MachineInstr, and resolveVariantSchedClassImpl() for MCInst). 

In the MDL infrastructure, each distinct predicate is placed in it a separate C++ function, and called from representation-independent functions that handle both instruction representations.  

This creates a problem in the uncommon situation where the Target library isn’t included in an application, but a Target function was called in an MachineInstr predicate function, leading to undefined function references at link time. In the MDL generated code, its not exactly feasible to refactor the higher-level functions, since they are called from representation-independent tables, and/or from functions referenced in the representation-independent database. Note that this is only a problem for ARM and AArch64 processor families.

To clarify the problem:



*   TableGen generates two functions, which look roughly like this: (many details omitted, and in these examples the string &lt;MIpred_n_(MI)>_ _or &lt;MCpred_n_(MI)>_ _represent the inlined body of a predicate function, not necessarily a function call)

	

	<code>unsigned resolveScheduleClass(unsigned SchedClass, <strong>MachineInstr</strong> *MI) {</code>


```
		switch (SchedClass) {
			case 1:
            if (<MIpred(MI)>) return 1000;
				else if (<MIpred2(MI)>) return 1001;
				break;
			case 2:
            if (<MIpred1(MI)>) return 1002;
				else if (<MIpred4(MI)>) return 1003;
				…
				break;
			…
			Case 400:
            If (<MIPred123(MI)>) return 4321;
            if (<MIpred333(MI)>) return 4322;
            break;
    }
    return 0;
    }

unsigned resolveVariantScheduleClassImpl(unsigned SchedClass, MCInst *MI) {
		switch (SchedClass) {
			case 1:
            if (<MCpred1(MI)>) return 1000;
				else if (<MCpred2(MI)>) return 1001;
				break;
			case 2:
            if (<MCpred1(MI)>) return 1002;
				else if (<MCpred4(MI)>) return 1003;
				…
				break;
			…
			Case 400:
            If (<MCpred123(MI)>) return 4321;
            if (<MCpred333(MI)>) return 4322;
            break;
    }
    return 0;
	}
```



    The first function (resolveScheduleClass) is called by Target library functions, and operates on MachineInstr objects.  The second function is called by MC library functions, and operates solely on MCInst objects.


    Note that if a particular schedule class doesn’t have an appropriate MachineInst or MCInst predicate, that case is simply not implemented in the switch statements for the function, and the function returns a value of 0 (ie invalid).  TBH, this is kind of odd.



*   In contrast, the MDL compiler currently produces this:

	

	`bool Pred1(Instr *MI) {`


```
		if (MI->is_MC()) return <MCpred1>(MI);
		if (MI->is_MI()) return <MIpred1>(MI);
		return false;
}
bool Pred2(Instr *MI) {
	…
}
…
bool Pred123(Instr *MI) { … }
bool Pred333(Instr *MI) { … }
```



    In the MDL infrastructure, Instr objects have separate constructors for MCInst and MachineInstr objects, so that the client functions (including predicate functions) can support both representations.


    The problem is that the MI\_Pred() functions are (for ARM and AArch64) sometimes defined in the Target library, so that library must be included to avoid link errors.


### Solutions

Typically, there are only a handful of predicate functions (for either representation).  We could do a few things to hack this solution:



1. Always include the Target library if the MC library is included.  This seems heavy handed from an LLVM perspective.  Its easy but won’t be accepted by the community.  It only affects a handful of LLVM tools, mostly tools for manipulating object code.
2. Move the function definitions to a separate file, and include the file associated with the linked libraries. There’s not an automated way to do this.
3. Automatically generate fake stubs for each of the referenced MachineInst functions, and include the stubs when the Target library isn’t included.   If we could guarantee the order of library inclusion, perhaps this approach could work.
4. Related to approach 3: Automatically generate “weak-attributed” empty stubs of each referenced MachineInst predicate function and include them in the MCInst library.  This is great but there doesn’t appear to be a standard way to create weak references.
5. Simulate virtual functions: generate two tables - one for MCInst functions and one for MachineInstr functions, and use common indexes to reference the appropriate table.  Add the table definitions to the appropriate library, and add them to the CpuTable object separately (in the constructors.)  You can then access the tables via the CpuTable object, which is accessible anywhere.

Method 5 is the most involved, and for most targets isn’t even necessary (and the tables will be empty).  But its also completely transparent to the build process.  

I’d prefer to use weak function references, if I can figure out how to generate them portably.

In the absence of weak function support (option 4), we could implement approach 3 if we could  guarantee that the library containing MachineInstr is _always _included before MCInst.

Update: can’t find a reasonable way to do weak references portably, so that option is out.  Option 3 doesn’t really work, since … make.   So option 5 is the only viable option, and thats whats implemented.  Implementation described below.


##### **Simulating virtual functions**

The Target library explicitly includes the MC library.  So we only need to virtualize MachineInstr predicate functions.  We’ll add a pointer to the Target’s predicate table to CpuTableDef.

In this method, we would (optionally, as needed) create an vector of function pointers in the CPU table to contain pointers to MachineInstr predicate functions.


```
	std::vector<PredFunc> InstrPredicates { MI_Pred1, …, MI_Predn };
```


In the CpuTable object, we would include a pointer to this array:


```
	std::vector<PredFunc> *instr_predicates;
```


The MDL compiler would generate an auto-initialization of this array in a file that can be included by the Subtarget module, and the Subtarget constructor would register the generated definition to the CpuTable object.

So, for example, if we currently have a predicate function that looks like this:


```
	static bool PRED_3(const Instr *MI) {
    return ((static_cast<const ARMInstrInfo*>(MI->tii())
                ->getNumLDMAddresses(*MI->mi())+1)/2==1);
```


We would get this:

          `  // generated in an inc file included by Subtarget`


```
	static bool MI_Pred_3(const Instr *MI) {
    return ((static_cast<const ARMBaseInstrInfo*>(MI->tii())
                ->getNumLDMAddresses(*MI->mi())+1)/2==1);
```


	…


```
	std::vector<PredFunc> InstrPredicates { MI_Pred_1, …, MI_Pred_3, …, MI_Pred_n };

	// generated in the MDL output inc file
static bool PRED_3(const Instr *MI) { 
    return MI->isMI() && MI->evaluate_predicate(3); }
```


Here’s a more complex example that includes MCInst and MachineInstr predicates:


```
    static bool PRED_36(const Instr *MI) {
      return ((MI->isMC() ? ARM_MC::isCPSRDefined(*MI->mc(), MI->mcii())
                          : static_cast<const ARMBaseInstrInfo *>(MI->tii())
                                ->ARMBaseInstrInfo::isCPSRDefined(*MI->mi())) &&
              (MI->isMC()
                   ? ARM_MC::isPredicated(*MI->mc(), MI->mcii())
                   : static_cast<const ARMBaseInstrInfo *>(MI->tii())->isPredicated(
                         *MI->mi())));
    }
```


We would get this:


```
// generated in an inc file included by Subtarget
    static bool MI_Pred_5(const Instr *MI) { 
    	return ((static_cast<const ARMInstrInfo*>(MI->tii())
                                ->ARMBaseInstrInfo::isCPSRDefined(*MI->mi()))
    }
    static bool MI_Pred_6(const Instr *MI) {
    	return static_cast<const ARMBaseInstrInfo *>(MI->tii())->isPredicated(
                         *MI->mi())));
    }

std::vector<PredFunc> InstrPredicates { MI_Pred_1, …, MI_Pred_n };

	// generated in the MDL output inc file
    static bool PRED_36(const Instr *MI) {
      return ((MI->isMC()
        ? ARM_MC::isCPSRDefined(*MI->mc(), MI->mcii())
            : MI()->evaluate_predicate(5)) &&
              (MI->isMC()
        ? ARM_MC::isPredicated(*MI->mc(), MI->mcii())
        : MI()->evaluate_predicate(6)));
    }
```



##### Library-based solution

In this solution, we create stubs in the MCInst library for MachineInstr functions which are referenced in predicate functions.  These stubs are put in a separate translation unit and added to the MC library.  In this approach, we need to ensure that the MachineInstr library is ALWAYS included first so that its predicate functions are found if the library is included.

Since we can’t really enforce the order of linking, this probably isn’t really viable.


##### Side Note

As mentioned, only two targets specify predicates with Target library functions - ARM and AArch64.  And only a handful of tools include MC but not Target.  (Note that Target explicitly includes MC).  A rough stab at those tools/utilities are:



*   Objcopy
*   DwarfLinker
*   Interface Stub
*   llvm-mca 
*   llvm-mc
*   llvm-nm
*   llvm-ml
*   llvm-libtool-darwin
*   sancov
*   llvm-cfi-verify 
*   llvm-objdump 
*   llvm-jitlink
*   llvm-profgen 
*   llvm-rtdyld 
*   llvm-dwarfdump 
*   llvm-ar 

 

So, unfortunately, this is a small problem for a small number of architectures, for a small number of tools.  It seems the virtual function table is kind of overkill for this little problem. It would be nice if we could just hack this for the two architectures. 

But the good news is that there are so few instances of this, the cost incurred by the virtualizing of the functions is insignificant.


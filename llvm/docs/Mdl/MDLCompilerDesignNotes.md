

## Machine Description Compiler Design Notes

Reid Tatge         tatge@google.com


[TOC]



### Introduction and Overview

The purpose of this document is to describe the overall design and flow of the machine description compiler. (See llvm/docs/Mdl/MachineDescriptionNotes.md for a description of the language.)

The compiler compiles the input machine description into C++ code that describes the behavior of every target instruction in enough detail to generate low-level instruction scheduling, register allocation, and bundle-packing infrastructure. It accomplishes this in phases:



*   Parsing the input
*   Error checking, consistency checking
*   Template instantiation
*   Generation of an instruction database
*   Generation of the C++ code

Each of these will be discussed separately.


### Parsing the Input File

The compiler uses an Antlr4-generated parser. We use the antlr visitor model to walk through the generated parse tree and generate an internal representation of the entire description in an object “MdlSpec”.

An MdlSpec represents all the objects defined at the top level of the input machine description:



*   A processor family name.
*   The set of processor pipeline specifications.
*   The set of globally defined resources.
*   The set of defined registers.
*   The set of defined register classes.
*   The set of CPU definitions.
*   The set of function unit template definitions.
*   The set of subunit template definitions.
*   The set of latency template definitions.
*   The set of target instruction definitions.
*   The set of target operand definitions.

The parsing phase does essentially no semantic checking.  This implies that definitions of MDL objects can appear in any order, and namespaces between functional units, subunits, latencies, clusters, etc can overlap. 


#### CPU Definitions

A CPU definition (CpuInstance) at this point contains:



*   A set of “issue” resource definitions.
*   A set of general resource definitions.
*   A set of cluster definitions.

Note that while the syntax allows it, we don’t describe functional unit instantiations at this level.  The parser collects functional unit instances in a CPU definition, and creates a cluster to contain them.

A cluster instantiation (ClusterInstantiation) at this point contains:



*   A set of “issue” resource definitions.
*   A set of general resource definitions.
*   A set of function unit instantiations. 

 

A functional unit instance (FuncUnitInstance) contains:



*   The type and name of the instance.
*   A set of arguments to this instantiation of the functional unit template.
*   A set of issue slot pinning resource references


#### Template Definitions

There are three types of templates that are defined in a machine description.  

_Functional unit template_ definitions contain the following information (In FuncUnitTemplate objects():



*   A base functional unit template reference.  A base functional unit is instantantiated any time a derived functional unit is instantiated.  
*   A list of template parameters, used to parameterize each instantiation of the template.  Parameters can refer to either resources or register classes. 
*   A list of “port” resources.  Ports are resources which we can associate register constraints and resources that are referenced when the port is referenced.
*   A list of functional unit resource definitions.
*   A list of “connect” statements.  Connects are used to associate register classes and resources with a port definition.
*   A list of subunit instantiations specific to this functional unit. Each subunit instance has a set of parameters which are a combination of local and parameter definitions (ports or resources).

Functional unit templates are instantiated in CPU clusters, and are parameterized by the parameters passed to the instantiation.  Each instance gets a unique set of declared resources and ports.

_Subunit template_ definitions contain the following information (in SubUnitTemplate objects): 



*   A base subunit template reference. The derived subunit contains all the rules from the base template.
*   A list of template parameters, used to parameterize each instantiation of the template.  Subunit template parameters are reference expressions that refer to template parameters (ports or resources).
*   A list of latency instantiations. Each latency instance has a set of parameters which are references to the subunit template parameters.

_Latency template_ definitions contain the following information (in LatencyTemplate objects):



*   A (set of) base latency template(s).   When a latency template is instantiated in a subunit instance, the contents of all the base latency templates are also instantiated.  Note that these can be recursive or diamond shaped inheritance, since we only instantiate each base class once.
*   A list of template parameters, used to parameterize each instantiation of the template.  Latenchy template parameters are reference expressions that refer to template parameters (ports or resources).


### Error and Consistency Checking

After we import the Antlr parse tree into our internal representation, we need to perform semantic error checking on the specification.  The primary purpose of this stage is to ensure that there are no glaring semantic errors in the input description, so that the template instantiation and database generate stages have relatively little correctness checking to do.


#### Building dictionaries

The first task is to build lookup tables for all the different objects in the description. While we do this, we do an initial check for duplicate entries in any single table.   We build dictionaries for: \




*   Functional unit templates
*   Subunit templates
*   Latency templates
*   Operand definitions
*   Instruction definitions
*   Register class definitions

The dictionaries provide a way to rapidly look up globally defined objects.


#### Checking for duplicate definitions

Once we’ve built dictionaries for the major globally defined objects, we check for duplicate definitions across all objects. This one-time check ensures that we don’t have namespace conflicts later in the compilation. In general, we want to make sure that lists of similar objects don’t contain duplicates.  



*   For pipeline definitions, we
    *   Check that each pipe class definition has a different name.
    *   Check that each phase name in each pipe class is unique across all pipeline definitions.
*   For globally defined registers, resources, and register classes
    *   We make sure all of these names are different from each other.
*   For any globally defined structured resource,
    *   Check that its member names are not duplicates.
*   For register classes and operands.
    *   Check that the operand names and register class names are not duplicates.
*   For each template (functional unit, subunit, and latency)
    *   Check the parameter list for duplicate names. 
*   For functional unit templates,
    *   Check that locally defined resources and ports have unique names.
    *   Check that locally defined structured resources have unique member names.
*   Check that all CPU names are unique.
    *   All locally defined resources are unique, and different from globally defined resources, registers, and register classes.
    *   Check that cluster names are all different.
    *   Check that locally defined structured resources have unique names.
    *   For each cluster, 
        *   Check that locally defined resources and ports have unique names, and don’t hide CPU and globally defined resources, registers, or register classes.
        *   Check that each functional unit instance has a different name.
        *   Check that locally defined structured resources have unique names.
*   Check that each instruction and operand definition has a unique name
*   For each instruction and operand definition, check that each operand (or suboperand) has a different name.


#### Instruction and operand checks



*   We check that each instruction refers to a defined subunit template definition.
*   We check that instruction operands refer to defined operand template definitions.
*   We check that operands’ suboperands refer to defined operand template definitions.
*   We check that derived operands have valid base operand definitions.
*   We check that operands are not recursively defined (either through base operands or suboperands).
*   We check operand base classes to ensure that there are no diamond-shaped derivations.


#### Template and instance checking



*   We check that all template bases (for functional units, subunits, and latencies) refer to valid base templates, and have compatible parameter types.
*   Check that all template instantiations (in CPUs, clusters, functional units, and subunits) refer to appropriately defined templates, and have the right number of template parameters.
*   In this phase, we link each functional unit and subunit instance to their associated template definitions.  We also link templates (of all types) to their declared bases.


#### Other semantic checks and miscellany 



*   We need to ensure that each pooled resource definition that supports sharing is defined with an explicit pipeline phase.  
*   We check that all pipe phase references (in resource definitions and latency rules) all refer to valid pipeline phase definitions.
*   We check each reference rule in each latency template:
    *   that references to operand’s suboperands refer to valid members
    *   that the phase expression is legal

The last thing we do at this phase is to promote all globally defined resources into each CPU definition.  This provides each CPU definition with a completely unique set of resources.


### Template Instantiation

The goal of this phase is to create, for each subunit type, a table of specialized subunit template instantiations, each of which contains the specialized instantiations of its latency instances. Each of these subunit instantiations is in turn a partially specialized template definition used in generation of sets of behaviors for each client instruction definition.

This phase of compilation iterates over all CPUs and all clusters, and instantiates each function unit instance found.  It then instantiates each subunit found in each expanded functional unit instance.  The critical output of this process is the subunit instantiation table, which contains a vector of specialized subunit instantiations for each subunit name.  This table forms the basis for the next phase of compilation.

The pseudocode for this pass is:


```
    for each CPU definition
       for each cluster in the CPU
          for each functional unit instance in the cluster
             instantiate the functional unit;
```


The pseudocode for instantiating a functional unit:


```
    Bind incoming parameters and connect statements;
    for each subunit instance
       instantiate the subunit and add to the subunit table;
    instantiate all base functional units;
```


The pseudocode for instantiating a subunit instance:


```
    Bind incoming parameters;
    For each latency instance
       instantiate the latency instance;
```


The pseudocode for instantiating a latency instance:


```
    Bind incoming parameters;
    For each reference
       Specialize the reference and append to the subunit instance;
    Instantiate all base latency templates;
```


This is best illustrated with a simple machine description example: \



```
    phases xxx { E1, E2, E3 };            // define 3 pipeline phases
    register r[0..31];                    // define 31 registers
    register_class LOW { r[0..15]; }      // define a register class
    register_class HIGH { r[16..31]; }    // define a register class
    register_class GPR { r[0..31]; }      // define a register class

    cpu cpu1 {                            // define a cpu 
       resource res1, res2, res3, res4;
       cluster a {
           func_unit alu alu1(res1, res2, LOW);   // instance of FU alu
           func_unit alu alu2(res3, res4, HIGH);  // instance of FU alu
       }
    }
    func_unit alu(resource a, b; class hilow) {
        port regs;
        connect regs to hilow via b;
        subunit alu(a, regs);
    }
    subunit alu(resource x; port y) {
        latency alu(x, y);
    }
    latency alu(resource x, port y) {
        use(E1, $src, y, x);
        def(E2, $dst, y, x);
    }
    instruction add(GPR dst, GPR src) { subunit(alu); }
```


Template instantiation works similarly to C++ template instantiation - instances are effectively replaced by specialized instantiations of the referenced template.  Functional unit instantiation replaces the two functional unit instances (ie, “alu1” and “alu2” in cpu1, cluster a) with the two specialized versions of the underlying template:


```
    cpu cpu1 { 
       resource res1, res2, res3, res4;
       cluster a {
           func_unit alu alu1(res1, res2, LOW) {
              port regs<LOW>(res2);
              subunit alu(res1, regs);
           }
           func_unit alu alu2(res3, res4, HIGH) {
              port regs<HIGH>(res4);
              subunit alu(res3, regs);
           }
    }
```


 

We then instantiate the subunits to create 2 instances for subunit “alu”, and instantiate their latency instances, which are specialized for the cpu, cluster, and functional unit they were instantiated in. This produces:


```
    subunit cpu1.a.alu1.alu(resource res1; port regs[LOW] via res3) {
        use(E1, $src, regs[LOW] via res3, res1);
        def(E2, $dst, regs[LOW] via res3, res1);
    }
    subunit cpu1.a.alu2.alu(resource res2; port regs[HIGH] via res4) {
        use(E1, $src, regs[HIGH] via res4, res2);
        def(E2, $dst, regs[HIGH] via res4, res2);
    }
```


In this example, we have two subunit instantiations which can be used to generate behaviors for instructions that use the “alu” subunit.  We now know that the “add” instruction has two distinct behaviors, in terms of functional units it can run on, resources used, and function-unit-specific operand constraints.


### Generation of the Instruction Database

<coming soon!>


### Generation of C++ Code

<coming soon!>


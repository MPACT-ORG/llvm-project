
## A Microarchitecture Description Language for Modeling Accelerators in LLVM


[TOC]



### Introduction and Motivation

General–purpose processor designs and accelerator designs have many fundamental things in common.  Both have instruction sets, functional units, registers, memory systems, and potentially complex execution pipelines.  Both can typically issue more than one operation per cycle. Both have hardware resources that must be managed to some extent by the compiler. Both may have “complex” instructions that perform non-trivial operations on vectors or matrices, and have very long latencies or complex pipeline behaviors.  

But beyond these basic concepts the fundamental microarchitectural plumbing of GPPs and accelerators, and the task of managing those details, diverge.

LLVM’s processor model is primarily motivated by modern general-purpose CPUs (IA32/64, ARM, RiscV, PowerPC, etc).  These processor families share a number of common attributes:



*   Out-of-order parallel issue with large reorder buffers,
*   Dynamic decomposition of instructions to (largely undocumented) micro-operations,
*   Dynamic dispatch of micro-operations to functional units’ issue queues,
*   Fully protected pipelines,
*   Register renaming, forwarding,
*   Code and data caches,
*   Branch prediction.

The explicitly _dynamic_ nature of these execution pipelines make it fundamentally impossible to precisely model the exact execution behavior of a particular sequence of instructions, even on a specific instance of a CPU. Consequently, rather than provide a precise cycle-by-cycle model, TableGen’s processor model provides input to a sophisticated heuristic which uses imprecise resource usage estimates and precise latency information to drive instruction scheduling.

Itineraries offer a more precise model of how and when instructions use functional units, and how instructions can be explicitly bundled for parallel execution.  

But schedules and itineraries do not _directly _support all of the architectural features needed to support many modern accelerators. In these cases, we don’t have a tablegen-driven way of calculating instruction latencies, allocating and managing resources, comprehending constraints, determining what instructions can be issued together, etc. This falls on the shoulders of the compiler writer, and often the writing of 10Ks of lines of code to model a family of accelerators. This is painful, brittle, and error-prone.  (Note: itineraries do provide some of this support, but is not generally sufficient to model behaviors of many VLIW processors.)

We propose a declarative language that provides a no-code methodology for the creation of back-end microarchitecture modeling information for a very broad class of processors and accelerators, including all upstreamed LLVM targets.  While this model coexists with the current methodologies, we believe that our language can provide a significant superset of those capabilities, in an easy-to-write and understandable form.


### Attributes of accelerators that we want to support

While our machine description language can model a very large class of processors - including all currently upstreamed targets - it is specifically designed to support things that tablegen cannot directly support:

Statically scheduled, explicitly parallel instruction issue (ie VLIW processors)



*   With “unprotected” or “protected” pipeline modeling
*   Exposed hazards that the compiler must manage
*   Complex rules for instruction issue

	

Multiple functional unit clusters



*   Distributed/partitioned register files
*   Minimal connections between clusters
*   Cluster-specific issue rules

Per-CPU and Per-functional-unit instruction behaviors



*   Different latencies
*   Different register constraints
*   Different resource usage
*   Different encoding restrictions

Explicit management of more general resources



*   Allocation of resource “pools” (issue slots, register ports, shared encoding bits, etc)
*   Shared resources (across clusters and functional units)
*   Complex resource reservation patterns


### Goals

The overall goal of the language is to eliminate the need to write _any _target-specific (C++) code to describe how instructions execute on a family of processors, and to do this with a purpose-built, simple language and a generated database of information about instructions.


#### High-level goals of the language design:



1. Provide direct support for a broad range of accelerator architectures, including all architectures currently supported by LLVM.
2. Easily support related families of processors.
3. Simple, intuitive, declarative language that reflects the hardware description
4. Straightforward support of families of processors
5. Minimize cut/paste duplication of information (single source of truth)
6. Easy integration with all existing compiler components.


#### Non-goals of the language design



1. Replace tablegen
2. Replace SchedModels for general purpose architectures (although it could)
3. New/different/better instruction/operand/register descriptions
4. Replace existing schedulers, register allocators, etc

 The generated database is used in the back-end to:



1. Determine precise instruction latencies associated with data dependence and/or pipeline hazards,
2. Determine if a set of instructions can be issued together,
3. Manage resource reservation tables (for things like instruction scheduling),
4. Manage resource allocation and assignment.
5. Manage functional-unit-specific register constraints.


### Language Approach

The machine description language provides an alternative to using Schedules and Itineraries to specify processors, functional units, and latencies (things normally defined in \*Sched\*.td files).  We leverage tablegen definitions for instructions, operands, registers, and register classes. The MDL language provides all the capabilities of Schedules and Itineraries, in a more precise and concise manner.

The language uses a hierarchical structure to describe a family of processors in a single specification.  A family is a related set of processors that share dna: functional units, instructions, operands, registers, overall pipeline structure, etc. This corresponds to a single collection of td files for a processor family.

At the top level, we can define a number of things:



*   An overall pipeline model, enumerating each pipeline’s stages and behavior.
*   A set of CPU definitions, each corresponding to a processor in the family, which specifies the top-level functional unit architecture of the CPUs.
*   A set of “functional unit” template definitions. These templates generally describe hardware functional units in the traditional sense, and define what operations a functional unit can do, and how it should be specialized for each instance.
*   A set of “subunit” template definitions.  A subunit template represents a set of abstract operations that have similar execution behaviors.
*   A set of “latency” template definitions.  Latency definitions are similar in concept to schedules, in that they describe the pipeline behavior of a set of instructions.

We go into more detail about each of these below.

In the MDL language, we “name” things that are components of the hardware: pipeline phases, functional units, issue slots, and processor resources.  The MDL compiler assigns no semantic meaning to names defined in a specification.  CPU names, instruction names, operand names, register names, and register classes correspond to the associated items defined in TD files. All of these definitions are automatically scraped from TD files and imported into the description. In general, we use names derived directly from the hardware spec for functional units, resources, etc; and use names defined in tablegen for instructions, operands, and registers.

To test our approach, we developed a tool (llvm-tdscan) that reads tablegen files for all processor families and produces the equivalent MDL files. We then compile the generated descriptions, and include the generated MDL database(s) into LLVM. 


### 


### Brief Introduction to the MDL language

In this section we provide a brief description of each of the major components of the machine description language.  These are not complete specifications of each language component, but just an overview of typical simple definitions.  We’ll describe components in a tops-down manner, covering the major components first.  Note that the order of declarations in a description is completely arbitrary. This leaves the author with the freedom to organize components however they wish.


#### CPU Definitions

Each CPU definition within a family describes a specific implementation of that processor.  A top-level description describes the high-level architecture of the processor: its global resources to be managed, its functional units, or clusters of functional units, and its issue slots. At this level of the specification, while we do know precisely how many functional units are implemented, the details of the pipelines are explicitly not visible. Further, at this level of abstraction we don’t know which instructions execute on which functional units, or what the latencies or pipeline hazards are.

A few examples:

A single-issue, single pipeline processor:


```
    cpu scalar {
    	func_unit alu my_alu();       // instantiate an "alu" FU, named "my_alu"
    }
```


A 4-way VLIW with 6 functional units and 4 issue slots:


```
    cpu vliw {
    	issue slot1, slot2, slot3, slot4;    // explicitly name the issue slots
    func_unit alu alu1();                // an "alu" named "alu1"
    	func_unit alu alu2();                // an "alu" named "alu2"
    	func_unit mul mul1();                // etc
    	func_unit mul mul2();
    	func_unit load load_mem();
    	func_unit store store_mem();
    }
```


A 4-way VLIW with two clusters of 4 functional units, each with 2 issue slots, with some functional units “pinned” to specific issue slots:


```
    cpu vliw {
    	cluster a {
			issue slot1, slot2;
    		func_unit alu alu1();
    		func_unit mul mul1() -> slot1;          // must issue on slot1
    		func_unit load load_mem1();
    		func_unit store store_mem1() -> slot2;  // must issue on slot2
    	}
    	cluster b {
    		issue slot1, slot2;
    		func_unit alu alu2();
    		func_unit mul mul2() -> slot1;
    		func_unit load load_mem2();
    		func_unit store store_mem2() -> slot2;
    	}
    }
```


Instances of functional units can also be parameterized so that each instance is specialized. In this example, the same instruction running on the two functional units could have different register constraints, and use different resources (in a manner that isn’t specified at this level). In this context, a defined “resource” can represent any object that could be shared between functional units - register ports, ALUs, internal busses, etc.


```
	cpu my_cpu {
    register_class GPRa { R0..R15 };
		register_class GPRb { R16..R31 };
    resource x, y, z;       // shared resources
		…
    func_unit xyzzy my_alu_0(GPRa, x, z);
    …
    func_unit xyzzy my_alu_1(GPRb, x, y);
}
```



#### Functional unit template definitions

A functional unit instance (in a CPU definition) corresponds explicitly to an execution pipeline.  A functional unit template definition specifies what kinds of abstract operations can be performed on any instance of that hardware. Each instance can be specialized with a set of register constraints (register class-based) and resources.

Just as CPUs are collections of specialized functional unit instances, functional unit templates are collections of specialized subunit instantiations.  Each subunit instance is associated with a set of instruction definitions that can run on that subunit, and subsequently any functional unit that instantiates that subunit.

A simple functional unit template definition that supports 3 classes of instructions might look like this (again, the names have no implied semantic meaning):


```
	func_unit alu() {
		subunit my_adder();
		subunit my_shifter();
		subunit my_logical();
	}
```


A parameterized functional unit template can have specialized instances that specify register constraints and resources for each instance.  Using the CPU example above, this looks like this:


```
	func_unit xyzzy(class regs; resource res1, res2) { … }
```


The template parameters can be used to specialize the subunit instances specified in the definition.  Register class parameters are used to define “ports”, a locally defined resource which ties a resource to a register constraint.  Ports can then be used to specialize subunits:


```
	func_unit xyzzy(class regs; resource res1, res2, res3) {
		port my_port<regs>;
		subunit adder(my_port);
		subunit shifter(my_port, res1);
		subunit logical(res1, res2);
	subunit logical(res2, res3);      // note: duplicate subunit
	}
```


Any subunit can be instantiated more than once in a single functional unit template - typically with different parameters.  Each instance represents how its associated set of instructions executes with different constraints and resources - on the same functional unit. 

Functional unit templates can also declare “private” resources, which they can use to specialize subunit instances.  Each instance of a functional unit template has its own unique set of these resource definitions:


```
	func_unit xyzzy(class regs; resource res1, res2, res3) {
	resource my_res;
	port my_port<regs>;
		subunit adder(my_port, my_res);
		subunit shifter(my_port, res1, my_res);
		subunit logical(res1, res2, my_res);
	}
```


Subunit instances can be predicated, so that subunits can optionally be included in the functional unit instance based on the instantiating CPU or functional unit:


```
	cpu cpu_0 {
    func_unit xyzzy my_alu_0(GPRa, x, z);
	func_unit xyzzy my_alu_1(GPRa, y, z);
}
	cpu cpu_1 {
    func_unit xyzzy my_alu_0(GPRa, x, z);
	func_unit xyzzy my_alu_1(GPRa, y, z);
}

func_unit xyzzy(class regs; resource a, b, c) {
	cpu0:     subunit alu(regs, a);          // Case 1
	my_alu_0: subunit alu(regs, c);          // Case 2
}

```



*   In case 1, the “alu” subunit is instantiated if the instantiating functional unit was instantiated in cpu0.
*   In case 2, the “alu” subunit is instantiated (with different parameters from the previous case) if the instantiating functional unit is named “my\_alu\_0”.


#### Subunit template definitions

A subunit template definition defines an abstract class of instructions that have similar execution attributes.  The associated instructions can run on the same functional units with the same resource usage and latencies.  Currently, an instruction definition (in tablegen) specifies which subunits it can use by specifying the “SubUnits” attribute on an instruction definition:


```
	def my_add_instruction : <attribute>, <attribute>, SubUnits<[SU_add, SU_sub]>;
```


This indicates that the “my\_add\_instruction” can run on any functional unit (on any CPU) that instantiates an “add” or “sub” subunit. 

Since each instance of a subunit template can be specialized (across processors and functional units), the behaviors can also be specialized.  A basic subunit template definition has the following form:


```
	subunit adder(<template parameters>) {
		latency adder_latency(<instance parameters>);
	}
```


The parameters to a subunit template are resources and ports (defined in functional units).  Typically, a subunit specifies a single parameterized latency instance:


```
	subunit adder(port regs; resource a, b) {
		latency adder_latency(regs, a, b);
	}
```


Similarly to functional unit templates, subunits can conditionally specify which latencies they use, based on the instantiating CPU and functional unit:


```
	subunit adder(port regs; resource a, b) {
		cpu_0: latency adder_latency_0(regs, a, b);
		cpu_1: latency adder_latency_1(regs, a, b);
	}
```



#### Latency template definitions

Latency templates are used to describe detailed, cycle-by-cycle behavior of instructions.  They describe reads and writes of instruction operands, and allocation and usage of arbitrary processor resources. Latency templates are specialized with resource and port parameters. They have the following general form:


```
	latency <name>(<parameters>) {
		<reference>;    <reference>; …
}
```


The primary purpose of a reference is to tie an instruction’s access of operands to specific pipeline phases.  Given a tablegen instruction definition with the following ins and outs:


```
(outs GPR:$rd) and (ins GPR:$rs1, GPR:$rs2)
```


we would use the following three reference statements to declare when the operands are read and written (here E1 and E5 are MDL-defined pipeline phase names):


```
	use(E1, GPR:$rs1);
	use(E1, GPR:$rs2);
	def(E5, GPR:$rd);
```


Note that the pipeline phase can be an arbitrary expression: E1 + 3. It can also refer directly to an instruction immediate operand: E1 + $length - 2.

Since latencies are meant to represent the behavior of classes of instructions, the MDL compiler allows you to refer to operands of any instruction in the class.  These references only apply to instructions which have the specified operand definition, and are ignored for other instructions.

References can be used to indicate the use of a resource, or a set of resources, and optionally a number of cycles its used:


```
	use(E4, my_resource1, my_resource2);    // use 2 resources at E4 for one cycle
	use(E6:3, my_resource3);                // use a resource at E6 for 3 cycles
```


Resource pools: if a resource is defined to be a resource “pool”, we can indicate that some number of resources in the pool must be allocated for a reference:


```
	use(E1, read_port:1);
```


Or we can specify a particular member of a named resource group:


```
	use(E1, read_port.xyzzy);
```


If the resource is an array of resources, we can specify a subrange of resources to be used, or an allocation from a subrange, or a specific element:


```
	use(E1, read_port[2..4]);               // uses all of these resources
	use(E1, read_port[2..4]:1);             // allocates one of a subrange
	use(E1, read_port[3]);                  // allocates a specific member
```


References can also tie resources to specific accesses of an operand:


```
	use(E1, GPR:$rs1, read_port);
```


This indicates that the “read\_port” resource is reserved for one cycle when the $rs1 operand is read.  Any kind of resource references and allocations are allowed, for example:


```
	use(E1, GPR:$rs1, read_port:1, random.first, other_read_port[3..9]:2);
```


The use of a “port” parameter in a reference can be used to tie a specific register constraint to an instruction’s use of an operand. This is critical for support of partitioned register file machines.  Consider the following snippet:


```
	cpu my_cpu {
		func_unit alu alu0(some_register_class);
		func_unit alu alu1(another_register_class);
	}
	func_unit alu(class regs) {
		port my_port<regs>;
		subunit adder(regs);
	}
	subunit adder(port regs) {
		latency adder(regs);
	}
	latency adder(port regs) {
		use(E1, GPR:$rs1, port);  // Tie an additional constraint to an operand
	}
```


In this example, alu0 and alu1 impose different (possibly intersecting) constraints on the $rs1 operand of instructions that run on those units.  A postRA scheduler would see instructions possibly constrained to one of alu0 or alu1 depending on the register assigned to operand rs1. A preRA scheduler could proactively choose which constraints to honor for a register based on functional-unit-based constraints. 

Likewise, a post scheduling allocator would also see constraints imposed by scheduling.  And a pre-scheduling allocator can directly query all possible constraints for an instruction in order to make non-local allocation decisions.

While “def” and “use” are the most common reference types, there are 7 distinct types:



*   **def** - specify when an operand is written (defed)
*   **use** - specify when an operand is read (used)
*   **kill** - specify when an operand is killed (contents overwritten with a non-value)
*   **usedef** - specify when an operand is read, then written atomically
*   **hold** - specify when a resource reservation begins
*   **res** - specify when a resource reservation ends
*   **predicate** - specify when a predicate operand is read

Hold and reserve deserve some explanation.  A latency template could contain the following pair of references:


```
	hold(E1, some_resource);       // wait for some_resource to become available
	res(E10, some_resource);       // reserve some_resource until E10
```


At first glance, this seems indistinguishable from:


```
	use(E1:10, some_resource);
```


In this case, either approach reserves the resource for 10 cycles, therefore modeling a 10-cycle pipeline hazard that the scheduler must honor.  But in the more general use case, these are treated more like a “use” and a “def” in how they define interactions between instructions.  

In particular, instructions may specify only a “hold” or only a “res” of a particular resource.  In other words, an instruction may wait for a resource to be available, but not reserve it for its own use.  Likewise, an instruction may reserve a resource without ever waiting for it to be available.  This is important for modeling complex hardware resource usage where a set of instructions can share the resource in a fully pipelined manner, but instructions outside of that set would encounter some level of pipeline hazards. This is, in fact, not uncommon.

References can be predicated much like subunit instances in functional unit templates.  This is useful for differentiating pipeline models between family members:


```
	use(E1, GPR:$rs1);
	use(E1, GPR:$rs2);
	CPU0: def(E5, GPR:$rd);
	CPU1: def(E8, GPR:$rd);
```


Note: they can also use tablegen-derived predicate expressions:

	<code>my_predicate: <strong>use</strong>(E1, GPR:$rs1);</code>

Putting this all together, you can write references that look like:


```
	CPU5: use(E5+$width:13, GPR:$r4, read_port[2..7]:2);
```


Finally: latency templates can be based on other latency templates. This allows us to describe common behaviors in a single place and use them in multiple latency templates.  The only restriction on latency bases is that they must have the same leading parameters as any latencies that refer to them:


```
	latency base_0(resource a, b, c) {...}
	latency base_1(resource a) { … }
	latency base_2 : base_0 : base_1(resource a, b, c) { … }
	latency my_latency : base_2(resource a, b, c, d, e) {...}
```


A typical example of this is when every instruction has a shared predicate operand.  Rather than specify the behavior of that operand in every latency template, you can describe it once, then include that latency as a base in every other latency definition.


```
	latency pred() { predicate(E1, $pred); }
	latency general_alu : pred(<parameters>) { … }
```



#### Resource Definitions and Use

Resources are used to model hardware components that instructions use in their execution pipeline.  They are the basis of hardware hazard management.  They can represent:



*   functional units, 
*   issue slots, 
*   register ports, 
*   shared encoding bits, 
*   or can name any hardware resource an instruction uses when it executes that could impact the instruction’s behavior (such as pipeline hazards).

Its important to note that different instances of an instruction can use completely different resources depending on which functional unit, and which subtarget, it's issued on. The MDL has an explicit way to model this.

There are a few ways that resources are defined:



*   **Functional Units:** A resource is _implicitly_ defined for every functional unit instance in a CPU definition. An instruction that executes on a particular instance will reserve that resource implicitly. 
*   **Ports:** Ports are functional unit resources that model a register class constraint and a set of associated resources. These are intended to model register file ports that are shared between functional units, and impose constraints running on those functional units.
*   **Issue Slots: **Each CPU, or cluster of functional units in a CPU, can explicitly define a set of issue slots.  For a VLIW, these resources directly correspond to instruction encoding slots in the machine instruction word, and can be used to control which instruction slots can issue to which functional units.  For dynamically scheduled CPUs, these correspond to the width of the dynamic instruction issue. 
*   **Named Resources** can be explicitly defined in several contexts, described below.


##### Issue Resource Definitions

Issue resources are used to explicitly model issue slots in a multi-issue architecture.  They are defined in cpu and cluster definitions.  For VLIW architectures, they loosely refer to the encoding bits that specify independent instructions.   For dynamically scheduled machines, they refer to the number of issue pipelines available.  An example:


```
	issue slot1, slot2, slot3, slot4;
```


Issue resources are used in cpu and cluster definitions to “pin” functional units to specific issue slots.  Currently, a functional unit can:



1. Be pinned to one slot:

    ```
    func_unit alu() -> slot_a;
    ```


2. Be pinned to one of several slots: \
	**<code>func_unit alu() -> slot_a | slot_b | slot_c;</code></strong>
3. Be pinned to all of a set of slots (it uses them all) \
<code>	<strong>func_unit</strong> alu() -> slot_a & slot_b & slot_c;</code>
4. Be unpinned - it can run on any slot (the default behavior)

    ```
    func_unit alu();

    ```


In general, there’s no need for more complex pinning.  A conjunction of |’s and &’s can be done with separate functional unit instances:

	**<code>func_unit alu() -> slot_a & slot_b</code></strong>


```
    func_unit alu() -> slot_b & slot_c
```


Is equivalent to writing:

	**<code>func_unit alu() -> (slot_a & slot_b) | (slot_b & slot_c)</code></strong>


##### Port Resource Definitions

Port resources are defined in functional unit templates to associate a resource expression and register constraints with a single “port” resource which can be used to specialize subunit instances.  An example:


```
	func_unit xyzzy(class regs, resource a, b) {
		port my_port<regs>(a);
		subunit my_subunit(my_port);
	}
	subunit my_subunit(port my_port) {
		latency my_latency(my_port);
```


	}

In this example, any reference in my\_latency that uses my\_port uses both the register constraint and the associated resource.


##### General Resource Definitions


##### Simple resource definitions

The simplest resource definition is simply a comma-separated list of names:

	**<code>resource name1, name2, name3;</code></strong>

A resource can also have an explicit pipeline stage associated with it, indicating that the defined resources are always used in the specified pipeline phase:

	**<code>resource(E4) name1, name2;    // define resources that are always used in E4</code></strong>

A resource can have a set of bits associated with it. This defines a resource that can be shared between two references if the bits in an associated operand reference are identical.

	**<code>resource immediate:8;         // define a resource with 8 bits of data</code></strong>


##### Grouped resource definitions

We can declare a set of named, related resources:

	**<code>resource bits     { bits_1, bits_2, bits_3 };</code></strong>

A resource group typically represents a pool of resources that are shared between instructions executing in parallel, where an instruction may require one or all of the resources. This is a common attribute of VLIW architectures, and used to model things like immediate pools and register ports.

Any defined resource can be included in a group, and the order of the members of a group is significant when members are allocated.  If a group mentions an undefined resource (in either the current or enclosing scope), the member is declared as a resource in the current scope.  In the case above, if the members (bits\_1, etc) are not declared, the compiler would internally create the definition:

	**<code>resource bits_1, bits_2, bits_3;</code></strong>

and the group members would refer to these definitions. (Note: we don’t support nested groups).

The resource group can be referenced by name, referring to the entire pool, or by individual members, such as “bits.bits\_2” to specify the use of a specific pooled resource.  Consider the following example:


```
	resource bits_1, bits_2, bits_3;
```


	**<code>resource bits_x { bits_1, bits_2, bits_3 };</code></strong>

	**<code>resource bits_y { bits_3, bits_1, bits_2 };</code></strong>

“bits\_x” and “bits\_y” are distinct groups that reference the same members, but members are allocated in a different order.  Groups can also be defined with syntax that indicates how its members are allocated by default.

	**<code>resource bits_or  { bits_1 | bits_2 | bits_3 };       // allocate one of these</code></strong>

	**<code>resource bits_and { bits_1 & bits_2 & bits_3 };       // allocate all of these</code></strong>

Groups can also be implicitly defined in functional unit and subunit template instantiations as a resource parameter.

	**<code>func_unit func my_fu(bits_1 | bits_2 | bits_3);</code></strong>

This implicitly defines a resource group with three members, and passes that group as a parameter of the instance.


##### Pooled resource definitions

We can also declare a set of “unnamed” pooled resources:


```
	resource shared_bits[0..5];
```


This describes a resource pool with 6 members.  The entire pool can be referenced by name (ie “shared\_bits”), or each member can be referenced by index (“shared\_bits[3]”), or a subrange of members (“shared\_bits[2..3]). A resource reference can also indicate that it needs some number of resources allocated with the syntax: shared\_bits:<number>.  

Resource pools can also have data associated with them, each member has its own set of bits:


```
resource bits:20 { bits_1, bits_2, bits_3 };
resource shared_bits:5[6];
```


Resource pools, like resource groups, are used to model things like shared encoding bits and shared register ports, where instructions need one or more members of a set of pooled resources.

Finally, resource definitions can pin a resource to a particular pipeline phase. All references to that resource will be automatically modeled only at that pipeline stage. This is particularly useful for modeling shared encoding bits (typically for resource pools).  The syntax for that looks like:


```
    resource(E1) my_pool { res1, res2, res3 };
```


where E1 is the name of a pipeline phase.  The resource “my\_pool” (and each of its elements) is always modeled to be reserved in pipeline phase E1.


#### **Using Resources**

Resource references appear in several contexts.  They are used in all template instantiations to specialize architecture templates (functional units, subunit, or latency templates) and are ultimately used in latency rules to describe pipeline behaviors. These will be described later in the document.

When used to specialize template instances, resource references have the following grammar:


```
    resource_ref            : IDENT ('[' range ']')?
                            | IDENT '.' IDENT
                            | IDENT '[' number ']'
                            | IDENT ('|' IDENT)+
                            | IDENT ('&' IDENT)+ ;
```


Some examples of resource uses in functional unit instantiations, subunit instantiations, latency instantiations, and latency reference rules:


```
some_resource           // reference a single resource or an entire group/pool    
some_resource_pool[1]   // use a specific member from an unnamed pool.
register_ports[6..9]    // select a subset of unnamed pooled resources.
group.xyzzy             // select a single named item from a group.
res1 | res2 | res3      // select one of these resources
res6 & res7 & res8      // select all of these resources
```


References in latency reference rules have additional syntax to support the allocation of resources from groups and pools:


```
    latency_resource_ref    : resource_ref ':' number (':' IDENT)?
                            | resource_ref ':' IDENT (':' IDENT)?
                            | resource_ref ':' ':' IDENT
                            | resource_ref ':' '*'
                            | resource_ref ;
```



##### **Allocating Grouped and Pooled Resources**

Latency references allow you to optionally manage allocation of pooled resources, as well as specifying the significant bits of operands whose values can be shared with other instructions.

A reference of the form:


```
	some_resource_pool:1
```


indicates that a reference needs one element from a group/pooled resource associated with a latency reference. A reference of the form:


```
	some_resource_pool:2
```


indicates that the reference needs 2 (or more) _adjacent_ elements from a pooled resource associated with a latency reference.  A reference of the form:


```
	some_resource_pool:*
```


indicates that a reference needs _all _elements from a resource group or pool. Note that grouped resources can only use :1 and :\*.

A reference of the form:

	`some_resource_pool:size`

indicates an operand reference that requires some number of resources from the resource pool.   The number of resources needed is specified in the “size” attribute of the associated operand type. This enables us to decide at compile time how many resources to allocate for an instruction’s operand based on its actual value.  For example, large operand constant values may require more resources than small constants, while some operand values may not require any resources. There’s a specific syntax for describing these attributes in derived operand definitions (described earlier).

In the examples above, if the resource has shared bits associated with it (it’s shareable by more than one instruction), the entire contents of the operand are shared. In some cases, only part of the operand’s representation is shared, and we can can specify that with the following reference form:

	`some_resource_pool:size:mask`

This indicates that the associated operand’s “mask” attribute indicates which of the operand bits are sharable.  Finally, we can use a share-bits mask without allocation:

	`some_resource_pool::mask`

This reference utilizes the resource - or an entire pool - and uses the operand’s “mask” attribute to determine which bits are shared with other references.


#### Pipeline Definitions

We don’t explicitly define instruction “latencies” in the MDL. Instead, we specify when instructions’ reads and writes happen in terms of pipeline phases.  From this, we can calculate actual latencies. Rather than specify pipeline phases with numbers, we provide a way of naming pipeline stages, and refer to those stages strictly by name. The declaration looks much like a C enumeration definition, such as:


```
phases my_pipeline { fetch, decode, read1, read2, ex1, ex2, write1, write2 };
```


We typically define these in a global phase namespace, and they are shared between CPU definitions. All globally defined phase names must be unique from all other phase names. However, each CPU definition can have private pipeline definitions, and names defined locally override globally defined names.

You can define more than one pipeline, and each pipeline can have the attribute “protected”, “unprotected”, or “hard”.  “Protected” is the default if none is specified.


```
protected phases alu { fetch, decode, ex1, ex2 };
unprotected phases vector { vfetch, vdecode, vex1, vex2 };
hard phases branch { bfetch, bdecode, branch };
```


A “protected” latency describes a machine where the hardware manages latencies between register writes and reads by injecting stalls into a pipeline when reads are issued earlier than their inputs are available, or resources are oversubscribed (pipeline hazards). Most modern general purpose CPUs have protected pipelines, and in the MDL language this is the default behavior.

An “unprotected” pipeline never stalls for read-after-writes or pipeline hazards. In this type of pipeline, reads fetch whatever value is in the register (in the appropriate pipeline phase).  A resource conflict (hazard) results in undefined behavior (ie, the compiler must avoid hazards!). In this model, if an instruction stalls for some reason, the entire pipeline stalls. This kind of pipeline is used in several DSP architectures.

A “hard” latency typically describes the behavior of branch and call instructions, whose side effect occurs at a particular pipeline phase.  The occurrence of the branch or call always happens at that pipeline phase, and the compiler must accommodate that (by inserting code in the “delay slots” of the branch/call, for example).

You can define multiple stages as a group - the following rule is equivalent to the first example above.

**<code>phases my_pipeline { fetch, decode, read[1..2], ex[1..2], write[1..2] };</code></strong> \


Like C enumerated values, each defined name is implicitly assigned an integer value, starting at zero and increasing sequentially, that represents its integer stage id.  You can explicitly assign values to pipeline phases, as in C, with the syntax  “`phase=value`”. You can also explicitly assign sequential values to a range, by using the syntax `"name[2..5]=value`”. 

Finally, there is specific syntax to annotate the first “execute” phase in a pipeline spec, using the ‘#’ syntax:


```
phases my_pipeline { fetch, decode, #read1, read2, ex1, ex2, write1, write2 };
```


This indicates that “read1” is the first execute stage in the pipeline, which serves as the “default” phase for any operand that isn’t explicitly described in a latency rule.


### APPENDIX A: Full Language Grammar

The following is the full Antlr4 grammar for the MDL language.


```
grammar mdl;

//---------------------------------------------------------------------------
// Top level production for entire file.
//---------------------------------------------------------------------------
architecture_spec       : architecture_item* EOF
                        ;
architecture_item       : family_name
                        | cpu_def
                        | register_def
                        | register_class
                        | resource_def
                        | pipe_def
                        | func_unit_template
                        | func_unit_group
                        | subunit_template
                        | latency_template
                        | instruction_def
                        | operand_def
                        | derived_operand_def
                        | import_file
                        | predicate_def
                        ;

//---------------------------------------------------------------------------
// We support import files at the top-level in the grammar. These will be
// handled by the visitor, so the containing file is completely parsed
// before handling any imported files.
//---------------------------------------------------------------------------
import_file             : IMPORT STRING_LITERAL
                        ;

//---------------------------------------------------------------------------
// Define a processor family name, used to interact with target compiler.
//---------------------------------------------------------------------------
family_name             : FAMILY ident ';'
                        ;

//---------------------------------------------------------------------------
// Top-level CPU instantiation.
//---------------------------------------------------------------------------
cpu_def                 : (CPU | CORE) ident
                             ('(' STRING_LITERAL (',' STRING_LITERAL)* ')')?
                             '{' cpu_stmt* '}' ';'?
                        ;
cpu_stmt                : pipe_def
                        | resource_def
                        | reorder_buffer_def
                        | issue_statement
                        | cluster_instantiation
                        | func_unit_instantiation
                        | forward_stmt
                        ;

reorder_buffer_def      : REORDER_BUFFER '<' size=number '>' ';'
                        ;

//---------------------------------------------------------------------------
// Cluster specification.
//---------------------------------------------------------------------------
cluster_instantiation   : CLUSTER cluster_name=ident '{' cluster_stmt+ '}' ';'?
                        ;
cluster_stmt            : resource_def
                        | issue_statement
                        | func_unit_instantiation
                        | forward_stmt
                        ;
issue_statement         : ISSUE '(' start=ident ('..' end=ident)? ')'
                          name_list ';'
                        ;

//---------------------------------------------------------------------------
// Functional unit instantiation (in CPUs and Clusters).
//---------------------------------------------------------------------------
func_unit_instantiation : FUNCUNIT type=func_unit_instance
                          bases=func_unit_bases*
                          name=ident '(' resource_refs? ')'
                          ('->' (one=pin_one | any=pin_any | all=pin_all))?  ';'
                        ;
pin_one                 : ident              // avoid ambiguity with any/all.
                        ;
pin_any                 : ident ('|' ident)+
                        ;
pin_all                 : ident ('&' ident)+
                        ;
func_unit_instance      : ident (unreserved='<>' | ('<' buffered=number '>'))?
                        ;
func_unit_bases         : ':' func_unit_instance
                        ;

//---------------------------------------------------------------------------
// A single forwarding specification (in CPUs and Clusters).
//---------------------------------------------------------------------------
forward_stmt            : FORWARD from_unit=ident '->'
                                      forward_to_unit (',' forward_to_unit)* ';'
                        ;
forward_to_unit         : ident ('(' cycles=snumber ')')?
                        ;

//---------------------------------------------------------------------------
// Functional unit template definition.
//---------------------------------------------------------------------------
func_unit_template      : FUNCUNIT type=ident base=base_list
                                '(' func_unit_params? ')'
                                '{' func_unit_template_stmt* '}' ';'?
                        ;
func_unit_params        : fu_decl_item (';' fu_decl_item)*
                        ;
fu_decl_item            : RESOURCE  name_list
                        | CLASS     name_list
                        ;
func_unit_template_stmt : resource_def
                        | port_def
                        | connect_stmt
                        | subunit_instantiation
                        ;
port_def                : PORT port_decl (',' port_decl)* ';'
                        ;
port_decl               : name=ident ('<' reg_class=ident '>')?
                             ('(' ref=resource_ref ')')?
                        ;

connect_stmt            : CONNECT port=ident
                             (TO reg_class=ident)? (VIA resource_ref)? ';'
                        ;

//---------------------------------------------------------------------------
// Functional unit group definition.
//---------------------------------------------------------------------------
func_unit_group         : FUNCGROUP name=ident ('<' buffered=number '>')?
                                  ':' members=name_list ';'
                        ;

//---------------------------------------------------------------------------
// Other FU statements, we may not need these.
//---------------------------------------------------------------------------
// local_connect <port> TO <regclass_name>.

//---------------------------------------------------------------------------
// Definition of subunit template instantiation.
//---------------------------------------------------------------------------
subunit_instantiation   : (predicate=name_list ':')? subunit_statement
                        | predicate=name_list ':'
                             '{' subunit_statement* '}' ';'?
                        ;

subunit_statement       : SUBUNIT subunit_instance (',' subunit_instance)* ';'
                        ;

subunit_instance        : ident  '(' resource_refs? ')'
                        ;

//---------------------------------------------------------------------------
// Definition of subunit template definition.
//---------------------------------------------------------------------------
subunit_template        : SUBUNIT name=ident base=su_base_list
                             '(' su_decl_items? ')'
                             (('{' body = subunit_body* '}' ';'?) |
                              ('{{' latency_items* '}}' ';'? ))
                        ;
su_decl_items           : su_decl_item (';' su_decl_item)*
                        ;
su_decl_item            : RESOURCE name_list
                        | PORT     name_list
                        ;
su_base_list            : (':' (unit=ident | regex=STRING_LITERAL))*
                        ;

//---------------------------------------------------------------------------
// Subunit template statements.
//---------------------------------------------------------------------------
subunit_body            : latency_instance
                        ;
latency_instance        : (predicate=name_list ':')? latency_statement
                        | predicate=name_list ':'
                             '{' latency_statement* '}' ';'?
                        ;
latency_statement       : LATENCY ident '(' resource_refs? ')' ';'
                        ;

//---------------------------------------------------------------------------
// Latency template definition.
//---------------------------------------------------------------------------
latency_template        : LATENCY name=ident base=base_list
                             '(' su_decl_items? ')'
                             '{' latency_items* '}' ';'?
                        ;
latency_items           : (predicate=name_list ':')?
                               (latency_item | ('{' latency_item* '}' ';'?))
                        ;
latency_item            : latency_ref
                        | conditional_ref
                        | fus_statement
                        ;

//---------------------------------------------------------------------------
// Conditional references
//---------------------------------------------------------------------------
conditional_ref         : 'if' ident '{' latency_item* '}'
                               (conditional_elseif | conditional_else)?
                        ;
conditional_elseif      : 'else' 'if' ident '{' latency_item* '}'
                               (conditional_elseif | conditional_else)?
                        ;
conditional_else        : 'else' '{' latency_item* '}'
                        ;

//---------------------------------------------------------------------------
// Basic references
//---------------------------------------------------------------------------
latency_ref             : ref_type '(' latency_spec ')' ';'
                        ;
ref_type                : (USE | DEF | USEDEF | KILL | HOLD | RES | PREDICATE)
                        ;
latency_spec            : expr (':' cycles=number)? ',' latency_resource_refs
                        | expr ('[' repeat=number (',' delay=number)? ']')?
                               ',' operand
                        | expr ',' operand ',' latency_resource_refs
                        ;
expr                    : '-' negate=expr
                        | left=expr mop=('*' | '/') right=expr
                        | left=expr aop=('+' | '-') right=expr
                        | '{' posexpr=expr '}'
                        | '(' subexpr=expr ')'
                        | phase_name=ident
                        | num=number
                        | opnd=operand
                        ;

//---------------------------------------------------------------------------
// Shorthand for a reference that uses functional units.
//---------------------------------------------------------------------------
fus_statement           : FUS '(' (fus_item ('&' fus_item)* ',')?
                                  micro_ops=snumber (',' fus_attribute)* ')' ';'
                        ;
fus_item                : name=ident ('<' (expr ':')? number '>')?
                        ;
fus_attribute           : BEGINGROUP | ENDGROUP | SINGLEISSUE | RETIREOOO
                        ;

//---------------------------------------------------------------------------
// Latency resource references allow resource allocation and value masking.
// Member references and index referencing don't allow allocation, but do
// allow masking.  This is checked semantically in the visitor, not here.
//---------------------------------------------------------------------------
latency_resource_refs   : latency_resource_ref (',' latency_resource_ref)*
                        ;
latency_resource_ref    : resource_ref ':' count=number    (':' value=ident)?
                        | resource_ref ':' countname=ident (':' value=ident)?
                        | resource_ref ':' ':' value=ident   // no allocation
                        | resource_ref ':' all='*'
                        | resource_ref
                        ;
operand                 : (type=ident ':')? '$' opnd=ident ('.' operand_ref)*
                        | (type=ident ':')? '$' opnd_id=number
                        | (type=ident ':')? '$$' var_opnd_id=number
                        ;
operand_ref             : ident | number
                        ;

//---------------------------------------------------------------------------
// Pipeline phase names definitions.
//---------------------------------------------------------------------------
pipe_def                : protection? PIPE_PHASES ident '{' pipe_phases '}' ';'?
                        ;
protection              : PROTECTED | UNPROTECTED | HARD
                        ;
pipe_phases             : phase_id (',' phase_id)*
                        ;
phase_id                : (first_exe='#')? ident ('[' range ']')? ('=' number)?
                        ;

//---------------------------------------------------------------------------
// Resource definitions: global in scope, CPU- or Cluster- or FU-level.
//---------------------------------------------------------------------------
resource_def            : RESOURCE ( '(' start=ident ('..' end=ident)? ')' )?
                              resource_decl (',' resource_decl)*  ';'
                        ;
resource_decl           : name=ident (':' bits=number)? ('[' count=number ']')?
                        | name=ident (':' bits=number)? '{' name_list '}'
                        | name=ident (':' bits=number)? '{' group_list '}'
                        ;
resource_refs           : resource_ref (',' resource_ref)*
                        ;
resource_ref            : name=ident ('[' range ']')?
                        | name=ident '.' member=ident
                        | name=ident '[' index=number ']'
                        | group_or=ident ('|' ident)+
                        | group_and=ident ('&' ident)+
                        ;

//---------------------------------------------------------------------------
// List of identifiers.
//---------------------------------------------------------------------------
name_list               : ident (',' ident)*
                        ;
group_list              : group_or=ident ('|' ident)+
                        | group_and=ident ('&' ident)+
                        ;
//---------------------------------------------------------------------------
// List of template bases
//---------------------------------------------------------------------------
base_list               : (':' ident)*
                        ;

//---------------------------------------------------------------------------
// Register definitions.
//---------------------------------------------------------------------------
register_def            : REGISTER register_decl (',' register_decl)* ';'
                        ;
register_decl           : name=ident ('[' range ']')?
                        ;
register_class          : REGCLASS ident
                            '{' register_decl (',' register_decl)* '}' ';'?
                        | REGCLASS ident '{' '}' ';'?
                        ;

//---------------------------------------------------------------------------
// Instruction definition.
//---------------------------------------------------------------------------
instruction_def         : INSTRUCTION name=ident
                             '(' (operand_decl (',' operand_decl)*)? ')'
                             '{'
                                 (SUBUNIT '(' subunit=name_list ')' ';' )?
                                 (DERIVED '(' derived=name_list ')' ';' )?
                             '}' ';'?
                        ;

//---------------------------------------------------------------------------
// Operand definition.
//---------------------------------------------------------------------------
operand_def             : OPERAND name=ident
                             '(' (operand_decl (',' operand_decl)*)? ')'
                             '{' (operand_type | operand_attribute)* '}' ';'?
                        ;
operand_decl            : ((type=ident (name=ident)?) | ellipsis='...')
                              (input='(I)' | output='(O)')?
                        ;

operand_type            : TYPE '(' type=ident ')' ';'
                        ;
operand_attribute       : (predicate=name_list ':')? operand_attribute_stmt
                        | predicate=name_list ':'
                              '{' operand_attribute_stmt* '}' ';'?
                        ;
operand_attribute_stmt  : ATTRIBUTE name=ident '='
                          (value=snumber | values=tuple)
                           (IF type=ident
                                   ('[' pred_value (',' pred_value)* ']' )? )?
                            ';'
                        ;
pred_value              : value=snumber
                        | low=snumber '..' high=snumber
                        | '{' mask=number '}'
                        ;

//---------------------------------------------------------------------------
// Derived Operand definition.
//---------------------------------------------------------------------------
derived_operand_def     : OPERAND name=ident base_list  ('(' ')')?
                              '{' (operand_type | operand_attribute)* '}' ';'?
                        ;

//---------------------------------------------------------------------------
// Predicate definition.
//---------------------------------------------------------------------------
predicate_def           : PREDICATE ident ':' predicate_op? ';'
                        ;

predicate_op            : pred_opcode '<' pred_opnd (',' pred_opnd)* ','? '>'
                        | pred_opcode (('<' '>') | '<>')
                        | code=code_escape
                        | ident
                        ;
code_escape             : '[' '{' .*? '}' ']'
                        ;

pred_opnd               : name=ident
                        | snumber
                        | string=STRING_LITERAL
                        | '[' opcode_list=ident (',' ident)* ']'
                        | pred=predicate_op
                        | operand
                        ;

pred_opcode             : 'CheckAny' | 'CheckAll' | 'CheckNot'
                        | 'CheckOpcode'
                        | 'CheckIsRegOperand' | 'CheckRegOperand'
                        | 'CheckSameRegOperand' | 'CheckNumOperands'
                        | 'CheckIsImmOperand' | 'CheckImmOperand'
                        | 'CheckZeroOperand' | 'CheckInvalidRegOperand'
                        | 'CheckFunctionPredicate'
                        | 'CheckFunctionPredicateWithTII'
                        | 'TIIPredicate'
                        | 'OpcodeSwitchStatement' | 'OpcodeSwitchCase'
                        | 'ReturnStatement'
                        | 'MCSchedPredicate'
                        ;

//---------------------------------------------------------------------------
// ANTLR hack to allow some identifiers to override some keywords in some
// circumstances (wherever "ident" is used).  For the most part, we just
// allow overriding "short" keywords for resources, registers, operands,
// pipeline names, and ports.
//---------------------------------------------------------------------------
ident                   : 'use' | 'def' | 'kill' | 'usedef' | 'hold' | 'res'
                        | 'port' | 'to' | 'via' | 'core' | 'cpu' | 'issue'
                        | 'class' | 'type' | 'hard' | 'if' | 'family'
                        | 'fus' | 'BeginGroup' | 'EndGroup' | 'SingleIssue'
                        | 'RetireOOO' | 'register' | IDENT
                        ;

//---------------------------------------------------------------------------
// Match and convert a number.
//---------------------------------------------------------------------------
number returns [int64_t value]
                         : NUMBER { $value = std::stoul($NUMBER.text, 0, 0); }
                         ;
snumber returns [int64_t value]
                         : NUMBER { $value = std::stoul($NUMBER.text, 0, 0); }
                         | '-' NUMBER
                                { $value = -std::stoul($NUMBER.text, 0, 0); }
                         ;

//---------------------------------------------------------------------------
// Match a set of numbers.
//---------------------------------------------------------------------------
tuple                    : '[' snumber (',' snumber)* ']'
                         ;

//---------------------------------------------------------------------------
// A constrained range - both must be non-negative numbers.
//---------------------------------------------------------------------------
range                   : first=number '..' last=number
                        ;

//---------------------------------------------------------------------------
// Token definitions.
//---------------------------------------------------------------------------
FAMILY                  : 'family';
CPU                     : 'cpu';
CORE                    : 'core';
CLUSTER                 : 'cluster';
REORDER_BUFFER          : 'reorder_buffer';
ISSUE                   : 'issue';
FUNCUNIT                : 'func_unit';
FORWARD                 : 'forward';
FUNCGROUP               : 'func_group';
CONNECT                 : 'connect';
SUBUNIT                 : 'subunit';
FUS                     : 'fus';
BEGINGROUP              : 'BeginGroup';
ENDGROUP                : 'EndGroup';
SINGLEISSUE             : 'SingleIssue';
RETIREOOO               : 'RetireOOO';
MICROOPS                : 'micro_ops';
DERIVED                 : 'derived';
LATENCY                 : 'latency';
PIPE_PHASES             : 'phases';
PROTECTED               : 'protected';
UNPROTECTED             : 'unprotected';
HARD                    : 'hard';
RESOURCE                : 'resource';
PORT                    : 'port';
TO                      : 'to';
VIA                     : 'via';
REGISTER                : 'register';
REGCLASS                : 'register_class';
CLASS                   : 'class';
IMPORT                  : 'import';
INSTRUCTION             : 'instruction';
OPERAND                 : 'operand';
TYPE                    : 'type';
ATTRIBUTE               : 'attribute';
IF                      : 'if';
USE                     : 'use';
DEF                     : 'def';
USEDEF                  : 'usedef';
KILL                    : 'kill';
HOLD                    : 'hold';
RES                     : 'res';
PREDICATE               : 'predicate';

IDENT                   : [_a-zA-Z][_a-zA-Z0-9]*;

NUMBER                  : HEX_NUMBER | OCT_NUMBER | BIN_NUMBER | DEC_NUMBER;
DEC_NUMBER              : '0' | [1-9][0-9]*;
HEX_NUMBER              : '0x' HEX_DIGIT (HEX_DIGIT | '\'')*;
HEX_DIGIT               : [0-9a-fA-F];
OCT_NUMBER              : '0' OCT_DIGIT (OCT_DIGIT | '\'')*;
OCT_DIGIT               : [0-7];
BIN_NUMBER              : '0b' [0-1] ([0-1] | '\'')*;

STRING_LITERAL          : UNTERMINATED_STRING_LITERAL '"';
UNTERMINATED_STRING_LITERAL : '"' (~["\\\r\n] | '\\' (. | EOF))*;

BLOCK_COMMENT           : '/*' .*? '*/' -> channel(HIDDEN);
LINE_COMMENT            : '//' .*?[\n\r] -> channel(HIDDEN);
WS                      : [ \t\r\n]     -> channel(HIDDEN);


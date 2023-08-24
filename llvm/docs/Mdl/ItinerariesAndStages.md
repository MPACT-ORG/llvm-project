

## Itineraries and Stages

Reid Tatge         tatge@google.com


[TOC]



#### Introduction

Tablegen “Itineraries” provide a way to describe how each instruction uses resources in its pipelined execution.  Each instruction itinerary object (InstrItinData) can have a set of “InstrStages” associated with it.  Each stage specifies:



*   A set of alternate resources, one of which can be reserved in that cycle
*   The number of cycles they are reserved
*   The number of elapsed cycles to the next stage (which could be 0)

A trivial example: the following tuple describes the use of a single resource in a single stage:


```
	InstrStage: cycles=1, func_units=[RES1], timeinc=1
```


This indicates that the resource RES1 is reserved for 1 cycle, and the next stage starts 1 cycle later.  If multiple resources are used in a single cycle, you can use more than one stage to specify that conjunction:


```
	InstrStage: cycles=1, func_units=[RES1], timeinc=0
	InstrStage: cycles=1, func_units=[RES2], timeinc=1
```


This could be written as (RES1 & RES2), since both resources are reserved for 1 cycle.

You can also express alternative resources in a stage (a disjunction of resources):


```
	InstrStage: cycles=1, func_units=[RES1, RES2, RES3], timeinc=1
```


This could be written as (RES1 | RES2 | RES3). You can also specify conjunctions of disjunctions:


```
InstrStage: cycles=1, func_units=[RES1, RES2], timeinc=0
	InstrStage: cycles=1, func_units=[RES3, RES4], timeinc=1
```


This indicates that one of [RES1, RES2] AND one of [RES3, RES4] are allocated in the same execution cycle. We write this as (RES1 | RES2) & (RES3 | RES4).

In tablegen, disjunctions of conjunctions are implemented via ComboFuncUnit objects - a resource that includes 2 or more resources. Consider this example, where COM1 and COM2 are ComboFuncUnits.


```
InstrStage: cycles=1, func_units=[COM1, COM2], timeinc=0
```


If COM1 is [RES1, RES2] and COM2 is [RES3, RES4], this implements:

	(RES1 | RES2) & (RES3 | RES4)

In summary, a single InstrStage can represent disjunction of resources, conjunctions of resources, and disjunctions of conjunctions:

	(A | B | C)          (A & B & C)          ((A & B) | (C & D))

Using sets of InstrStages, you can implement conjunctions of any of those.

In managing resources, the MDL language directly supports conjunctions and disjunctions of resources.

	(A | B | C)            (A & B & C)

The conjunction of these is accomplished by simply using more than one resource specification in a template instance. For ((A | B) & (C | D)), you’d write:

	`	func_unit Adder my_adder1(A | B, C | D);`

The disjunction of these is accomplished by using several templates: (A & B) | C & D)

	`	func_unit Adder my_adder1(A & B);`

	`	func_unit Adder my_adder2(C & D);`

   

Note that conjunctions of disjunctions can trivially be rewritten as their cross-product, or a disjunction of conjunctions:

(A | B) & (C | D) —> (A & C) | (A & D) | (B & C) | (B & D)

Issue slots can only have a single resource-specification, so we use this technique when complex conjunctions are used as issue slot specs.


#### Issue Slot Resource Entries

In the MDL, we like to separate issue resources (like issue slots) from other resources, such as functional units.  There isn’t a first-class difference between these in InstrStages in TableGen. However, targets like Hexagon use a naming convention which we can key off of to identify issue resources (ie “SLOTS”.)  

Consequently, we want to identify InstrStages that reference issue slot resources and treat them as separate from functional unit resources. 


#### MDL Resource Management

In the machine description language, a functional unit “instance” is the primary vehicle for specifying which resources are used by instructions running on that unit.  It has the following general form:


```
	func_unit <unit-type> <unit-name> (<resource-list>) → <slot-specification>
```


The resource list is a comma separated list of resource specifications.  Each resource specification can be a single resource reference (with several forms), a named resource group, a conjunction of individual resource names, or a disjunction of individual resource names.  Here’s an example which passes four resource specifications to a functional unit instance:


```
	func_unit Adder my_adder(foo, bar, foo | bar, foo & bar);
```


Note that _how and when_ those resources are used is not determined by the functional unit instance, but in the subunit and/or latency templates associated with the functional unit. Also note that since all of the resources are passed to the functional unit, each functional unit can make conjunctive or disjunctive use of each of them.  Multiple instances of the same functional unit with different resource specifications represent disjunctive uses of the resource combinations.  For example:


```
	func_unit Adder my_adder1(a | b | c, x & y & z);
	func_unit Adder my_adder2(a & b, x | y);
```


Specify that instructions that run on an Adder functional unit have two different, independent resource usage patterns.

Similarly, the slot specification can be a resource, or a conjunction or disjunction of separate resources:


```
	func_unit Adder adder0(foo, bar, foo | bar, foo & bar) → slot0;
	func_unit Adder adder1(foo, bar, foo | bar, foo & bar) → slot2 | slot3;
	func_unit Adder adder2(foo, bar, foo | bar, foo & bar) → slot0 & slot1 & slot3;
```


In this example, there are three alternative sets of issue slots an “Adder” instruction can use.

The reservation of these resources is done in latency rules, where you explicitly reference each resource.  For the above example, a reasonable latency rule might look like:


```
	latency adder(resource a, b, c, d) {
		use(E1, a, b);
		use(E2:3, c);
		use(E5, d)
}
```


This models “a” and “b” (or in this case “foo” and “bar”) as reserved in cycle E1 for 1 cycle, and “c” (“foo” or “bar”) as used in cycle E2 for 3 cycles, and “d” (“foo” and “bar”) as reserved in cycle E5.


##### Representing InstrStages in MDL

Some basic facts about InstrStages:



*   A single InstrStage represents either a single resource, or a disjunction of alternative resources.
*   A set of associated InstrStages represents a conjunction of the individual stages - in other words an instruction uses _all_ of the stages.
*   A stage can indicate whether the next stage occurs in the same cycle or a later cycle (this is the “timeinc” attribute)
*   Each stage specifies how long the resource(s) are reserved for - independent of when the next stage occurs (the “cycles” attribute)


##### Handling Issue Stages

Issue stage disjunctions of single resources are directly supported:


```
InstrStage: cycles=1, func_units=[SLOT0, SLOT1], timeinc=0
```


	`func_unit Adder add1() → SLOT0 | SLOT2;`

Issue stage conjunctions of single resources are directly supported:


```
InstrStage: cycles=1, func_units=[SLOT0], timeinc=0
InstrStage: cycles=1, func_units=[SLOT1], timeinc=0
func_unit <type> <name>() → SLOT0 & SLOT1;
```


Issue stage conjunctions with disjunctions are converted to disjunctions of conjunctions and modeled as separate functional unit instances:


```
InstrStage: cycles=1, func_units=[SLOT0 | SLOT1], timeinc=0
InstrStage: cycles=1, func_units=[SLOT2], timeinc=0
```


	`func_unit Adder add1() → SLOT0 & SLOT2;`


```
func_unit Adder add2() → SLOT1 & SLOT2;
```



##### Handling Resource Reservation Specifications

As with issue slots, resource disjunctions and conjunctions can be represented directly:


```
InstrStage: cycles=1, func_units=[CVI_ST, CVI_XLANE], timeinc=0
```


	`func_unit Adder add1(CVI_ST | CVI_XLANE);`

Conjunctions with the same phase and cycles can also be combined into a single reference spec:


```
InstrStage: cycles=1, func_units=[CVI_ST], timeinc=0
InstrStage: cycles=1, func_units=[CVI_XLANE], timeinc=0
```


	`func_unit Adder add1(CVI_ST & CVI_XLANE);`

Resource conjunctions of disjunctions are simply split into separate resource specifications.  Unlike with issue slots, there’s no need to generate separate functional units:


```
InstrStage: cycles=1, func_units=[CVI_ST | CVI_LD], timeinc=0
InstrStage: cycles=1, func_units=[CVI_XLANE], timeinc=-1
```


	`func_unit Adder add1(CVI_ST | CVI_LD, CVI_XLANE);`

Finally, resources that are used in different cycles are passed as separate resource specifications:


```
InstrStage: cycles=1, func_units=[CVI_ST], timeinc=1
InstrStage: cycles=1, func_units=[CVI_XLANE], timeinc=1
InstrStage: cycles=1, func_units=[CVI_XLANE | CVI_ST], timeinc=-1
```


	`func_unit Adder add1(CVI_ST, CVI_XLANE, CVI_ST | VI_XLANE);`

Note that while disjunctions always share cycles and timeinc parameters, conjunctions can have distinct timing for each member.  

Resource disjunctions of conjunctions can’t be represented directly in itineraries.  However, they can be modeled like any disjunction in MDL with separately specialized functional unit instances.


#### MDL Language Implementation Note

As mentioned previously, the MDL language syntactically only supports disjunctions and conjunctions of resources.  We do not currently allow resource groups to be used in conjunction or disjunction expressions, _even though they can represent conjunctions or disjunctions, and used in place of one_. 

Nor do we support more complex expressions of resources and/or resource groups. We could support this, and transparently perform the decomposition of these expressions (described earlier in the doc), but this adds complexity that we felt was, in general, unnecessary.  One of the important goals of the language is to mirror a low-level interface to the actual hardware, and to not do “surprising” transformations to the description.


#### Implementation

The first step is to identify issue stages. These are the initial stages that only reference “SLOT” resources.  All the rest of the stages will be associated with functional unit instance resource specs.

Next we split the resource specs into expressions that will be treated as separate arguments to the functional unit template.  There are two rules:



*   All the stages associated with a single argument must be in the same pipeline phase (timeinc=0) with the same number of cycles.
*   Any stage that is a disjunction is a separate argument

Next, for the issue stages and each argument, create an object that represents the phase, cycle, and resource expression.  Expand ComboFuncUnits into & expressions, and simplify if necessary.



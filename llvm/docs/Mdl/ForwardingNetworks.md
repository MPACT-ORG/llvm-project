
## Modeling Forwarding Networks

Reid Tatge         tatge@google.com


[TOC]



### Introduction

To reduce the apparent latency between instructions that write and read register values, CPUs use “forwarding” networks: a set of connections between processor functional units which move data directly between the functional units, rather than routing them through a register file. This network reduces the latency between instructions running on connected units.

Forwarding networks are usually not “complete”, where all functional units are connected to all other functional units.  Instead, the network typically only connects units which are physically close to each other, and in particular each unit to itself.  In practice, the network can be quite sparse. However, its worth noting that a fully connected, uniform network doesn’t have to be explicitly modeled at all.


### Background

If we want to accurately model latencies between instructions, we need to comprehend forwarding networks.  The need to accurately model latencies varies by the class of architecture we’re modeling:



1. Dynamic-issue machines,
2. Statically scheduled machines with protected pipelines,
3. Statically scheduled machines with unprotected pipelines.

We’d like to model each of these classes effectively. 

The primary issue common to all three architecture classes is the need to model the behavior of instructions that can issue on more than one functional unit.  If the forwarding network is non-uniform, the effective latency can potentially be different on each candidate unit. Further, if we’re compiling for a dynamic-issue machine, we cannot statically determine which functional unit it will be issued on.  On a statically scheduled machine, we cannot determine its “final” behavior until we’ve assigned a functional unit to the instruction instance. 

Here’s a more concrete example: we have two instructions _A_ and _B_.  _A_ can run on functional units _A<sub>1</sub>_ and _A<sub>2</sub>_, while _B_ can run on _B<sub>1</sub>_ or _B<sub>2</sub>_.  We have a forwarding path only between _A<sub>1</sub>_ and _B<sub>1</sub>._

On a dynamic-issue machine, we _cannot_ know which unit the instructions issue on, so we can’t, with certainty, determine the latency between _A_ and _B_, so we must use a heuristic, perhaps best-case, worst-case, or some other heuristic.

On a statically scheduled machine, if the instructions haven’t been assigned functional units, we similarly cannot know for certain what the latency will be. Again we must use a heuristic, but for an unprotected pipeline we must be conservative.  Another approach for this class of machine is to prune or restrict the functional unit candidates for the instructions so that we can make more accurate latency calculations.


#### Dynamic-Issue Machines

This class of machine describes most modern general-purpose microprocessors: ARM, X86, PPC, Mips, etc.  They are superscalar, multi-issue, out-of-order machines which dynamically issue and aggressively reorder the execution of instructions to avoid pipeline stalls. They typically have comprehensive forwarding networks, including full register renaming support.  Further, several of these processors decompose the exposed ISA instructions into undocumented micro-ops which are separately issued into one of several parallel pipelines. 

For this class of architecture, precise modeling of the latency between pairs of instructions is at best a heuristic that is useful for motivating the order of instructions in memory, and grouping of instructions that can be issued in parallel.

Note that this is not an excuse to provide an inadequate compiler model for these architectures, just an observation that it is generally impossible to provide a perfect model. We are largely just trying to help the processor do a better job. 


#### Statically scheduled machines with protected pipelines

In this machine class, instructions are issued and executed in-order, but the processor inserts stalls when the input values of an instruction are not available yet.  This includes many VLIW’s and embedded microcontrollers (including Google TPUs).

For these processors, the goal is to generate code which avoids predictable stalls, and therefore we need more accurate latency information than in a dynamic-issue machine. Non-uniform forwarding networks can complicate this, so the compiler may use heuristics or a functional-unit allocation mechanism to improve the accuracy of latency calculations.


#### Statically scheduled machines with unprotected pipelines

This is a smaller class of machine where the processor has few or no hardware assists for managing latency, typically found in embedded accelerators (TI’s C6x VLIW processors, for example).

For these processors, the compiler is fully responsible for ordering instructions such that all of an instruction’s inputs are available when needed by the instruction.  Although ambiguous cases are less common for this class of architecture, in those cases the compiler must make conservative assumptions about required latencies.


### LLVM SchedModel Approach

LLVM uses “Read Resources” and “Write Resources” to model each instruction’s register reads and writes.  In general, resources associated with an instruction’s operands model when the reads or writes takes place in the execution pipeline.  In the simple case, the latency between two dependent instructions can be calculated by using the difference between the write resource (in the writing instruction) and the read resource (in the reading instruction):

	Latency (Instr<sub>def</sub> → Instr<sub>use</sub>) = WriteResourceLatency<sub>def</sub> - ReadAdvanceLatency<sub>use</sub> + 1

The ReadAdvance latency provides an adjustment to the latency between a pair of instructions. There are generally two kinds of adjustments:



1. A reduction in latency due to the presence of a forwarding network, which delivers input operands to a functional unit earlier than instruction pipeline would indicate.
2. A reduction in latency due to an instruction reading its input operand(s) later in the pipeline than normal.

We refer to these as a forwarded-read and a late-operand-read, respectively. The primary difference between these reads is that a late-operand-read latency always applies to an instruction, but a forwarded-read latency is only applied if the def instruction has a particular set of write-resource ids - ie, the value is forwarded to the read instruction.

When calculating latencies between two instructions, LLVM first calculates the latency for the writing instruction, and notes the instruction’s Write Resource id. It uses the Write Resource Id to lookup the read latency adjustment in the ReadAdvance table.  The latency is then calculated as, effectively:

        Latency (Inst<sub>def</sub> → Inst<sub>use</sub>) = WriteResourceLatency<sub>def</sub> - ReadAdvance<sub>use</sub>[WriteResource<sub>def</sub>]

A ReadAdvance resource can optionally specify a set of write resources (ValidWrites) which indicate that the adjustment is associated with a forwarding network.  An empty ValidWrites attribute indicates a late-operand-read.  So tablegen can represent both adjustment types (described above), but it _cannot_ represent a combination of the two for a single instruction.

This approach conflates the behavior of an instruction (when it reads its operands) with the latency adjustment of the forwarding network that delivers values earlier or later to an instruction.  In the MDL approach, we want to decouple these behaviors.

A few more observations about the tablegen approach to latency adjustment:



1. Only three targets use SchedModel forwarding (AArch64, ARM, and PPC), and only three targets (ARM, PPC, and Hexagon) use Itinerary forwarding,
2. X86 _only _implements late-reads of operands (and has a few bugs in that logic), and no forwarding,
3. The SchedModel forwarding implementations are very sparse.  It appears that its generally been used to “cherry-pick” a few “important” cases, and in the few CPUs that model forwarding, most instructions don’t have forwarding information associated with them,
4.  In the few CPUs that model forwarding, the vast majority of instructions don’t have ReadAdvance entries associated with input operands.  Consequently, forwarding cannot be modeled for these instructions.

Or, more generally:



1. Modeling forwarding networks in tablegen is tedious, so existing implementations are sparse, resulting in uneven support for forwarding across instructions.
2. Thats ok, since latency calculations are simply a non-critical heuristic for general-purpose processors.
3. Trying to replicate this in the MDL language - in an automatic way (tdscan) - is difficult.


### LLVM Itinerary Approach

We don’t currently support Itinerary-based modeling.

Forwarding using Itineraries is only used for ARM (A9 only), PPC (for three CPUs), and Hexagon.


### MDL Approach

The MDL approach to forwarding networks is based on a several principles:



*   Each instruction (or instruction class) describes its own behavior, and may have different specified behaviors on different functional units.
*   A forwarding network impacts the behavior of a functional unit, _not_ instructions that run on it.  Therefore it is orthogonal to the specification of instruction behaviors.

This is subtly different from the LLVM approach, which conflates forwarding with the behavior of the instruction, rather than a function of the datapath.

In effect, we want a latency calculation that explicitly separates the instruction behavior from the forwarding network behavior:

        Latency (Inst<sub>def</sub> → Inst<sub>use</sub>) = WritePhase<sub>def</sub> - ReadPhase<sub>use</sub> + 1 - 

                                                                                            ForwardingCycles[FU<sub>def</sub>, FU<sub>use</sub>]

In this model, there are three factors that affect the latency between a pair of instructions:



*   The pipeline phase in which the write occurs
*   The pipeline phase in which the read occurs
*   An adjustment made for the forwarding network between the two functional units

In other words: ideally, we’d like to use instruction information (SchedWrites and ReadAdvance resources) to calculate the latency between two instructions, and then independently adjust this latency based on the presence of a forwarding path between the functional units that the two instructions execute on.

From a language definition perspective, we use an approach that is tied to the description of a forwarding network as a feature of the processor datapath.  Its not an attribute of instructions or even functional units, but rather a relationship between functional units.

There are several types of situations that we want to handle cleanly:



1. All functional units are forwarded to all other functional units, or no forwarding is implemented.  These are essentially equivalent, and shouldn’t require any explicit modeling.
2. The forwarding network is _nearly _uniform, or extremely sparse.  In either case, we’d like a minimum description of the network, either describing connections that exist, or describing which connections are missing.
3. Arbitrary networks - neither sparse nor uniform.
4. (Optional) Handle the common case where units are only forwarded to themselves.

To describe a forwarding network, we need to describe each path in the network based on a CPU’s functional units.  For example:


```
	forward FU1 → FU1, FU2, FU3;
```


This asserts that results produced in FU1 are forwarded to units FU1 (itself, in this case), FU2, and FU3.The functional unit names could be functional unit template names, functional unit group names, or instance names - specifying a template or group name would include all functional units of that type (or group) in the CPU or cluster. To specify a particular instance of a functional unit, use its instance name instead:


```
	func_unit LOAD my_load1();
func_unit LOAD my_load2();
	func_unit ADD my_add1();
	func_unit ADD my_add2();
	forward my_load1 → my_load1, my_add1;
	forward my_load2 → my_load2, my_add2;
	…
```


This defines the network, and we’d like to be able to specify the latency adjustment for each edge in the network, which can be used, optionally, by instructions. Here’s how to do that:

	`forward my_load1 → my_load1(1), my_add1(2);`

This is interpreted as _my\_load1_ saving 1 cycle when forwarding to itself, and saving 2 cycles when forwarding to _my\_add1_.  Note that the adjustment could be negative numbers as well, indicating that the forwarding path is missing, resulting in longer latencies.

Here’s a simple example of a forwarding network.  Each “forward” statement specifies a functional unit, and a list of units it forwards to. 

	`cpu my_cpu {`


```
		func_unit MUL my_mul();
		func_unit ADD my_add();
		func_unit LOAD my_load();
		func_unit STORE my_store();
		func_unit BRANCH my_branch();


	forward MUL → MUL(1), ADD(2);
    forward ADD → ADD(1), LOAD(3);
    forward STORE → ADD(1), MUL(2);
}
```


In general, we don’t expect instruction-specific forwarding behavior on a functional unit connected to the forwarding network, so for typical cases, we expect this description to generally be sufficient to describe most architectures. However, there can be exceptions.  A relatively common case would be instruction operands that are _not_ connected to the forwarding network.  We need a reasonable way to model these exceptions. 


#### Representation of the network

Each CPU has a specific number of functional units it implements.  The basic representation is simply a 2-dimensional adjacency matrix, with the edges annotated with the latency adjustment. Although we expect the network to be sparse (or empty), we need this to be fast, so we’ll use a “dense” representation, since the sizes involved are rather small.  A typical processor has fewer than  a dozen functional units, so we can use a simple 2-dimensional array of signed chars to represent the graph, and positive or negative latency adjustments.


#### Extracting the forwarding network from SchedModels with TdScan

Since SchedModel descriptions tend to conflate forwarding with instructions that read operands in later pipeline phases, we want to separate these two concerns when we generate an MDL description.  Our fundamental assumption is that forwarding to a functional unit has the same behavior for all instructions that run on that functional unit: the value is delivered to the unit some number of cycles - typically 1 - earlier than if the forwarding path wasn’t implemented. 

So our first step is to extract the forwarding network using the ReadAdvance records associated with forwarding information (the ones that have ValidWrites).  Using this information, we can generate a graph of forwarding latencies between every pair of instructions, and derive the overall forwarding graph.

So the approach we use in TdScan is that we use the forwarding information to find the minimal forwarding path between every pair of units, then use that information to inform the forwarding benefit for all instructions that execute on the receiving unit.   In very rare cases in current targets, the forwarding latency specified in tablegen is negative. Which we could interpret as an “early operand read” for the involved instructions. 

So given our desired formula for calculating latency:

        Latency (Inst<sub>def</sub> → Inst<sub>use</sub>) = WriteLatency<sub>def</sub> - ReadLatency<sub>use</sub> + 1 - 

                                                                                            ForwardingCycles[FU<sub>def</sub>, FU<sub>use</sub>]

We first calculate ForwardingCycles for each pair of functional units described in tablegen.  When generating latency records for ReadAdvance resources which have ValidUnits, we decrement the ReadAdvance amount by the calculated ForwardingCycles for that forwarding path. This can be a little complicated, so lets start with a simple case.

If we have an instruction ABC running on “UnitA” which has a ReadAdvance with a cycle count of 1, and has forwarding paths from “UnitB”, “UnitC”, and “UnitD”, each with a forwarding latency to “UnitA” of 1.  In this case, we don’t need to generate the explicit “use” reference of 1, since it is subsumed by the forwarding adjustment. 

For a more complicated case, say we have an instruction FMA running on “UnitA” which has a ReadAdvance with a cycle count of 4, and has forwarding paths from “UnitB” and “UnitC”, and we have the following forwarding graph:

		UnitB forwards to UnitA with cycles= &lt;1, 4>

		UnitC forwards to UnitA with cycles= &lt;2, 4>

The &lt;1, 4> notation implies that some instructions asserted that the forwarding latency between B and A was 1, and some instructions (such as FMA) asserted it was 4.  Our first observation is that this doesn’t necessarily make sense - the forwarding path either exists or it doesn’t, and it shouldn’t be different for different instructions.  This is (I believe) an artifact of the LLVM approach of conflating “late operand reads” with “forwarding cycles”.  In this case, (for UnitB to UnitA), we split the 4 cycle latency between forwarding (1 cycle) and late operand reads (3 cycles), and generate a “use” record with a 3-cycle latency.  

Our second observation is that since UnitC has a different forwarding latency than UnitB, we don’t have a single convenient way of describing the latency of the FMA instruction wrt both forwarding paths.

The last observation is that most instructions do not have any LLVM forwarding information associated with them.  We also need to differentiate between ReadAdvances used for late-operand reads, and those used to represent forwarding.   Differentiating late-operand-reads from forwarding reads requires some explicit way of annotating which is which in the generated description.  There are three cases here (no forwarding info, late-operand-read, and forwarding read), and we need a way to communicate the three cases.

In the case there is no forwarding information provided for an instruction, we need to _not_ model forwarding information for these instructions, assuming we want to match LLVM’s behavior.

There’s an easy solution: instructions which don’t have input operand read resources also don’t use forwarding information.  This is perhaps inaccurate, but we can match llvm’s behavior.

There are a few approaches to differentiating between late-operand-reads and forwarding reads, none of them are ideal.  If we want to match LLVM’s behavior exactly, we would also have to annotate references that _don’t _use forwarding, but _do _have explicit references.

The current approach that we’ve implemented is to treat all ReadAdvances as late-operand-reads.  This produces a “best-case” latency calculation, since we assume the operand is always read late.  Note that a number of architectures (like X86, RISCV) don’t model _any_ forwarding, but only model late-read-operands. In other words, their heuristic is to assume a best-case latency. This isn’t an unreasonable approach, particularly if the target machine has a fully-connected, uniform forwarding network.

Bottom line, we have forwarding support in the MDL language, but because of the incomplete and/or conflated operand reads in tablegen, its not easy to translate an LLVM description into MDL in a reasonable way.  


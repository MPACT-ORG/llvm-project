

## MDL-Based Bundle Packing and Pool Allocation

Reid Tatge         tatge@google.com


[TOC]



### Introduction

The MDL language enables the compiler writer to describe the target machine microarchitecture in a first-class language. From that description, we create a database of information with which we can implement low-level components of a compiler backend, including latency management, pipeline modeling, and bundle packing. Normally these passes require carefully written target-specific code. With the MDL infrastructure, we can write efficient, fast and target-independent implementations of these components. This document discusses some of the design tradeoffs that we are trying to address in:



*   **Bundle packing:** For VLIW architectures, we need to determine which instructions can be issued in parallel. 
*   **Resource pool allocation:** VLIW processors commonly have instructions which may reserve some number of resources from a common, possibly shared, resource pool.  


### Bundle Packing

A VLIW instruction scheduler needs to know whether a particular set of instructions can be issued in parallel.  We refer to this as “bundle packing”. 

Typically a scheduler maintains a list of instructions which are “ready” to be scheduled in the context of an execution cycle, ie a “ready list”.  The candidate instructions typically have different “priorities” - it may be more urgent to schedule some instructions at a particular time, such as those on a critical path, vs other instructions.  This is a key part of the scheduling algorithm, and largely machine independent.  However the scheduler needs a way of determining if a particular set of instructions can be \*issued\* in parallel, and this requires significant machine knowledge. In the low-level bundling infrastructure, we don’t want to address the priority problem - that is a higher-level concern that is the domain of the scheduler. Instead the bundler is focused primarily on resource management.

In addition to knowing whether a set of instructions can be bundled, we need to know whether the bundle can be issued in the context of the current partially generated schedule.  We need to comprehend any structural pipeline hazards and/or reserved resources that occur in nearby cycles.  This implies that we need a way for the scheduler to maintain a temporal reservation table of resources and hazards while scheduling a section of code.  (In the absence of structural hazards, we want to be able to skip or minimize this step.)

Additionally, for loop scheduling, we also want a way of defining and using a cyclical or modulo resource reservation table.

So we need to provide a few services to the scheduler:



*   A resource reservation table that models resources and hazards over some portion of a schedule. This could be null if the architecture doesn’t have structural hazards.
*   An object that represents a bundle which includes a list of instructions and the associated functional units and resources it consumes (over time).
*   A method for determining whether an instruction can be added to a bundle.

This approach allows a client scheduler to manage the priority of instructions and the order in which they are added to the schedule.  A cycle-by-cycle list scheduler can incrementally build up the instructions for a particular cycle, or for a range of cycles; a loop scheduler can incrementally build up the schedule for an entire loop.


#### Resource management

The primary task of the bundle packer is to assign subunits to candidate instructions such that the resources used by each instruction don’t conflict with the resources used by any other instruction in the bundle, or any resources reserved by other bundles over the execution lifetime of this bundle.  Recall that resources are used to model:



*   Functional unit assignment
*   Issue slot assignment
*   Arbitrary encoding bits
*   Pipeline hazards in general
*   Pools of resources

Each subunit candidate (for each instruction) models a particular behavior of an instruction - on a specific functional unit, in a specific issue slot, using specific resources.   A subunit precisely describes which set of resources are used by the instruction, and in what pipeline phase. 

To illustrate this, if we have an instruction ADD that can run on either functional unit A1 or A2, and it can be issued in one of three issue slots (I1, I2, and I3), it will have at least 6 candidate subunits to which it can be assigned: A1/I1, A1/I2, A1/I3, A2/I1, A2/I2, A2/I3.

Identical functional unit candidates and identical issue slots are essentially implicitly defined resource pools for which we enumerate all possible permutations in distinct subunits. Consequently, they don’t have to be explicitly assigned as with user-defined resource pools.

Note that the MDL compiler can in fact treat all resource pools similarly to functional unit and issue slot resources, and it does so, conditionally, for _small_ pools. However, when an instruction has several pool requests, or has requests for larger pools, it can lead to a combinatorial explosion of subunit candidates, and in general can be managed more efficiently with explicit pool allocation.


#### Interface to the bundle packer

The primary interface to the bundle packer allows the client to incrementally add instructions to a bundle, given a current bundle and a current reservation table:


```
    bool AddToBundle(SlotSet &bundle, SlotDesc &candidate, Reservations &res);
```


 \
A candidate is created with one of the the following constructors:


```
    SlotDesc(MCInst *instruction, MCSubtargetInfo *STI, MCInstrInfo *MCII);
    SlotDesc(MachineInstr *instruction, TargetSubtargetInfo *STI);
```


AddToBundle\*() attempts to add the candidate instruction to the input bundle, using the input Reservations object as an _initial _reservation table.  If the instruction can be scheduled, it adds the instruction to the bundle, and returns true. Note that it does _not_ update the reservation table, this is currently done as a separate step when the bundle is “complete”: 


```
    bool AddBundleToReservation(SlotSet &bundle, Reservations &res);
```


This two-step approach allows a scheduler to use the current state of the reservation table as an input to the bundling process, but not update it until it is ready to commit to a particular fully formed bundle. It also allows the bundle packer itself to be stateless - the state is contained in the bundle itself, and the reservation table that it maintains.

AddToBundle never ejects instructions from a bundle in order to allow the addition of a new instruction. This is based on the current assumption that instructions are passed to the bundle packer in priority order, so it would not make sense to eject a higher-priority instruction for a lower-priority instruction. 

This does not preclude us from designing an API to find an instruction in a bundle to eject to make it possible to schedule a new candidate.  This is in fact trivial to write given the MDL infrastructure.

This approach treats resource pool allocation as a separate problem.  Once we determine that a set of instructions can be bundled at a particular cycle in a schedule, we can independently attempt to find a pool allocation for that bundle. 


### Resource Pool Allocation

A resource pool is a set of resources that can be shared between different instructions.  Rather than statically reserving a particular resource, an instruction can “request” a resource from a shared pool of resources.   Examples of this are a common feature of VLIW architectures, and include:



*   A pool of identical (or at least similar) functional units,
*   A pool of issue slots for a cluster of functional units,
*   Encoding bits for immediate operands,
*   Register file ports (which may be limited to a maximum number of references per cycle.)

This is a general allocation problem where a subunit asserts that an instruction needs _m_ of _n_ resources from a pool which it shares with other instructions running in parallel with it. The compiler must find an allocation which satisfies all the pool requests for a bundle of instructions.

There are a few architectural aspects that complicate the allocation algorithm:



1. Pool entries can be exclusively reserved or shared between instructions. An example of sharing is immediate encoding bits that can be used by instructions that use the same immediate.  An exclusive resource example is opcode encoding bits, or a functional unit assignment.
2. There are three ways an instruction can reference a pool:
    *   A specific member of a pool  (pool[2]),
    *   A subset of elements of a pool (pool[3..4]),
    *   A number of elements of a pool (pool:3) 
3. An instruction instance may request zero, one or more members of a pool. (Clearly, a request for zero resources is trivial to accommodate.)
4. While pools are often associated with encoding bits and issue slots, they can be generalized to reserving resources over any range of pipeline phases.  However, we explicitly require shared-value resources to be declared with a specific pipeline phase.
5. Instructions may have different pool requirements depending on which functional unit they are issued on.

There are two ways a pooled resource can be allocated to an instruction:



1. The subunit can state a specific member of a pool be allocated to that instruction. The resource is effectively “preallocated” to instructions assigned to that subunit.
2. A subunit can request a number of resources from a specific pool.

Note: in cases where the MDL compiler chooses to decompose pools into distinct resources associated with different subunits (discussed above in resource management), there is no need to explicitly allocate those resources as a pool.  Ideally, we could do that for all resource pools, in which case we can skip explicit pool allocation. In the current MDL compiler, this is always done for functional units and issue slots, and for very small pools (2-3 members) in some circumstances. 


#### Compile-Time Complexity Considerations

When the bundle packer finds a subunit allocation for a given set of instructions, it collects all the resource pool requests for all assigned subunits and attempts to allocate all of them.  If it succeeds, the bundle is legal.

In the current implementation, if the allocation fails, the current bundle fails - in particular, the most recently added instruction is rejected. This is based on several observations:



*   Resource pools are a relatively uncommon architectural feature, even on VLIW processors.  This is particularly true if we ignore functional unit and issue slot pools.
*   When pools do exist, requests for a pooled resource are typically tied to the instruction, rather than the subunit or functional unit they are assigned. Put differently, all the subunit candidates for a particular instruction will, in general, all have the same set of pool requests.

For processors where this isn’t the case (easily checked in the MDL compiler), we would want to add code to make the bundle packer backtrack over the subunits when an allocation isn’t found. This is a significant reduction of complexity for the bundle packer.  We don’t anticipate this being a problem for a real processor.  

One common scenario we do anticipate is resource pools where some instructions tie specific members of a pool to a particular functional unit, while other instructions may use any member of the pool. An example of this when a VLIW has shared register ports accessible by several functional units.  The resources tied to a particular functional unit become “preallocated” resources, which are assigned by the initial bundle packer. The un-tied resources are then allocated in the pool allocator. If all the requests are of the same size (which is typical), we can always find allocations, assuming one is feasible. But in the case where preallocated resources are smaller than allocated resources, the bundle packer could create pre-allocations which would make it infeasible to find a legal allocation.  (Example: a pool has 3 members (a, b, c).  Instruction X can use “a”, “b”, or “c”, and instruction Y can use “ab” or “bc”.  If “b” is preallocated to X, we cannot allocate Y’s request)  Again, we don’t anticipate this being a problem in real processors.

Given that significant simplification, allocation of all of the pool requests associated with a bundle is still a complex task, which has the following parameters:



*   We have a list of instructions each of which may contain pool requests for various pools.
*   We have a reservation table which may indicate that some members of the pools may already be allocated to instructions (in the bundle).
*   Each pool request includes:
    *   A pool identifier,
    *   The number of resources needed from the pool (0 through pool size),
    *   The set of suitable resources in the pool,
    *   Whether or not the resource can be shared with other requests (and a value to share),
    *   What pipeline phase its needed in. 

Generally we’d like to sort the pool requests so that we allocate the largest requests in the most constrained pools first.  This allows a simple, greedy allocation algorithm to find a minimal allocation quickly with a single linear pass over the allocation request list.  However, this is expensive, since we do this for each instruction as it is added to a bundle. 


#### Allocation Algorithm

There are a number of things we can do to reduce the O(n log n) behavior of sorting the list:



1. Allocate a separate list of requests for each pool
2. Or: Allocate a separate list for each subpool, if any (and allocate the smallest first)
3. Or: Allocate separate lists for each allocation size, for each pool/subpool (and allocate the largest requests first)

Organizing by pool id divides “n” by the number of unique pools. However, since we can easily identify all the subpools encountered in the machine description (at machine-description compile time), we can further simplify the sorting. By further separating out pool-size requests (also statically discoverable from the machine description), we can greedily allocate the largest requests first, and further minimize the sorting effort. Rather than sorting, we’re simply collating requests into lists of similar requests _that can be allocated in any order_.

One complexity of this approach is that pool requests sizes are not always statically known at MDL build time, so a particular request may have to calculate the number of resources needed based on an operand’s value. This implies that any particular pool request can’t necessarily have its final pool id calculated by the MDL compiler. However, the MDL compiler can statically determine a maximum pool size request (worst case this is the entire pool, best case this is discoverable in the machine description). 

An few examples: 



1. if we have 4 pools, each with only single-resource requests, we would have 4 unique pool ids (1 through 4).  
2. However, if pool 3 had requests for 1 or 2 resources, we could add a subpool for pool 3, and renumber the pool ids as 1, 2, 3, 4, and 5.  
3. If each pool could have 1 or 2 entries allocated, we would introduce a subpool for each pool, and would produce the pool ids 1-8.

In any case, the “final” pool id would be calculated as the “base” pool id, plus the overall pool size, minus a specific size request.  (Note per example 2: there’s no reason to have each pool have the same number of size-based lists. In general, we would expect most pools would only have one allocation request size.)

If we only have one pool, or only one allocation size for any pool/subpool, the algorithm is the same.  In any case, the MDL compiler creates the pool ids for each subpool/request-size, and  provides a trivial mapping function from an arbitrary pool request to the appropriate allocation set.

Once requests are collated into the appropriate allocation set, the allocation algorithm is trivial:  for each request in a set, step through the reservation table entries for the pool to find the first resource(s) that aren’t already reserved, and allocate them to the current instruction (mark them allocated).

This algorithm is orthogonal to whether or not the resources are shared. Each allocated pooled resource must have a value that represents the value needed by its current clients. Sharing allocated resources is a simple table lookup. 

There are a few refinements to this algorithm:



1. For pools which never have preallocated members, there’s no reason to check the reservation table.
2. If all requests for a pool are the same size, it is be sufficient to simply add up the requests, and ensure they are fewer than the total available pool size (also see item 1).

Note: there may be processors where resource sharing is non-trivial, since the equivalence check is non-obvious or non-trivial.  We don’t have a great solution for this at this point.  We can do “simple” checks for equivalence, and believe that may be sufficient. In the initial implementation of the allocator, we don’t expect to implement sharing, even though we have the information to do it. Its more time consuming, and we don’t believe its particularly important.


#### Sharing Resources

Resources used for encoding are sometimes shared between instructions in a bundle. Two examples of this are register ports and immediate operands. If two instructions have operands that use identical encoding for the constant, the underlying architecture may allow them to share encoding bits, even if the decoded values are interpreted differently.

Encoding of immediate operands is particularly important, since processors often don’t have many encoding bits to dedicate to immediate values.  The values are shared and/or compressed. In general, in LLVM we don’t want to “expose” compression methods in the instruction description, since it complicates semantic analysis of instructions - so immediates are typically encode in the IR in their unadulterated form.

In the general case, instructions may use more than one resource to encode an immediate, encoding different parts of the immediate in different resources.  In this case, we want to be able to share the partial values with other instructions.

An example where two instructions encode different immediate operands identically

     mov\_i16                 8,r1

     load\_scaled\_word  32(r2),r3          // instruction encodes the unscaled value (as 8)

An example where one of a pair of resources could be shared:

     mov\_i32    0x12345678,r1             // uses 2 slots to encode 0x1234 and 0x5678

     mov\_i16   0x1234,r2                      // uses 1 slot to encode 0x1234

     mov\_i16   0x5678,r3                      // uses 1 slot to encode 0x5678

An example where the bits are interpreted differently:

     addf          1.0,r1                            // adds 0x3F800000 (1.0) to r1

     addi          1065353216,r2             // adds integer constant (0x3F80000) to r2

     addi          16256,r3                       // adds the high half of 0x3F800000 to a register

The compiler needs to know how the bits of an immediate operand will be represented in resources (typically encoding bits) in order to share them.  In the general case, this could be arbitrarily complex, but we want a reasonably simple mechanism that handles common cases. We do not need to check for legality of the immediate value - it is safely assumed that the construction of the instruction and operand performed the legalization.

An additional complexity is that immediate operands can be overloaded so that different ranges of values may be encoded, and we only actually encode a subset of all the bits, but a different subset depending on the value.  For example, we may have an operand type that can represent either of the two values 0x00001234 or 0x12340000 in a single resource, but only encoding the significant bits. While it might be better to define two different operands, that can lead to an explosion of instruction definitions. So we need a methodology that identifies both the type of encoding, and which bits to encode.

 \
We need to introduce an operand attribute that characterizes the encoding type and decomposes an operand’s value (as represented in the compiler representation) into one or more constituent values that will be directly used to encode the representation of the value in the resource(s).  Note that this doesn’t need to identify the exact encoding bits, only a canonical representation of those bits that can be compared for equality with different operand values.  In general, we want to strip off prefixes and suffixes to identify the significant bits of an operand.

The default case is that the immediate value is encoded exactly as-is in a single resource. For this case, we don’t need any additional information about the operand - we simply use the operand value directly.

If the value is encoded using more than one resource, we need a way of decomposing the value to identify the parts that will be encoded in different resources.  We may also want to have several different ways of encoding values - in one or more resources.

A simple approach would be to have a syntax that identifies which bits of a value are prefixes (leading 1s or 0s), the value itself, and suffixes (typically trailing 0’s).  

 


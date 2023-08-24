


## Modeling Resource Groups

Reid Tatge        tatge@google.com


[TOC]



### Introduction

The MDL language supports the specification and use of “resource groups”, which is a set of related resources that can be allocated like a pool:


```
	resource group { a, b, c, d, e, f };
```


Resource groups have CPU, Cluster, or Functional Unit Template scope, and can be passed as parameters to functional unit, subunit, or latency templates.  You can pass an entire group to a template as a parameter:


```
	subunit yyy(group);      // reference the entire group
```


Or you can pass references to a member with a C++ “struct” like syntax:


```
	subunit xxx(group.a);    // reference a single member of a group
```


When a group is passed to a template, you can allocate a single member of a group:


```
	def(E3, group:1);        // allocate a single resource from the group
```


Or reference a named item of the group:


```
	def(E3, group.d);        // use a named member of the group
```


Or reference the entire group:


```
	def(E3, group);          // use all the resources in a group
```


However, you cannot cleanly reference a subset of a group (or an arbitrary set of resources). 


### Former interpretation of groups

Currently, members of a resource group have the scope of the context they are defined in (CPU, Cluster, or Functional Unit Template).  Resource groups defined in the same scope may define members with the same name, and these names can shadow other resource names defined in the same scope.  So for example, the following is legal:


```
	resource fun;
resource group g1 { happy, fun, ball };        // Don't tease this
	resource group g2 { programming, is, fun };
```


In this case, we have defined 9 distinct resources in the same scope (including the group resources):


```
	fun, g1, g1.happy, g1.fun, g1.ball, g2, g2.programming, g2.is, g2.fun
```


The previous compiler allowed you to specify group members by name as long as they are unique in the current context, and they don’t shadow other defined resources.  In this case, “fun” is defined three times, so any use of those must qualify the reference:


```
	func_unit mu_fu fu1(fun, g1.fun, g2.fun);     // passes 3 different resources


```


Grouped resources with unique names can simply be referenced by their name:


```
	subunit yyy(programming, is, happy);
```



### New model: Arbitrary grouping of resources

There is a fairly common need to specify different subsets of a set of defined resources. The MDL has a methodology to support aspects of this, but in the general case we didn’t have a direct syntax for making this easy to specify.  This is particularly common with itineraries, where each stage specifies a different set of resources which can be used by each stage.  For this use case, we’d like to be able to use groups to define subsets of defined resources, for example:


```
	resource res1, res2, res3, res4, res5, res6, res7, res8;
	resource lows { res1, res2, res3, res4 };
	resource highs { res5, res6, res7, res8 };
	resource odds { res1, res3, res5, res7 };
	resource evens { res2, res4, res6, res8 };
	resource arbitrary { res1, res4, res5 };
```


In this case, all the group members with the same name refer to the same defined resource (in the current scope). This allows us to use groups to define arbitrary sets of defined resources, rather than defining distinct resources for each member.

In the “fun” example from the previous section, rather than creating nine distinct resources, we would generate only seven: g1, g2, happy, fun, ball, programming, is - ie, all the “fun” members refer to the same “fun” resource.

This is a very minor change in the language interpretation, and would obsolete the feature that two resource groups, defined in the same scope, could have members with the same name. This is of relatively little utility versus being able to define arbitrary subsets of defined resources.


### Semantic and Syntax Changes

Since this is primarily a change of the interpretation of resource groups, syntax changes are _required_.  However, we would like to introduce a syntax for shortcutting the specification of a resource group as a template parameter. Consider the following example:


```
	resource group1 { res1, res2, res3 };
	resource group2 { res3, res4, res5 };
	resource group3 { res5, res6 };
	subunit xyzzy(group1, group2, group3);
```


With the new syntax, this defines 3 resource groups and (only) 6 resources (res1..res6).

We introduce a syntax that allows you to define these groups implicitly as part of the instance, so that the explicit group definitions are unnecessary.  We’ll also add syntax to set the default allocation for a resource group - either “one of” or “all of”. 


```
	subunit xyzzy(res1|res2|res3, res3|res4|res5, res5|res6);
	subunit plugh(res1&res2&res3, res3&res4&res5, res5&res6);
```


Normally defined groups can also be defined with this syntax. Note that all the “operators” (‘,’ ‘&’, and ‘|’) must be identical in a single definition:


```
	resource group1 { res1 | res2 | res3 };
	resource group2 { res3 & res4 & res5 };
	resource group3 { res5, res6 };            // equivalent to |
```


When a group declared with “&” is “used” without an explicit allocation (ala x.y), all of its members are used.  When a group declared with “,” or “|” is used, only 1 is allocated (ala x.1). We now have a syntax x.\* which allocates all of a group’s members, regardless of how it is declared.

Implicitly declared groups can be used/declared in functional unit instances and subunit instances only.  They cannot be used in latency instances (ie, in subunit templates), since resources can only be declared in CPUs, clusters, and functional unit templates.  We may add this capability in the future.

As with the current syntax, note that defined group members are promoted to the scope that the group is defined in, so there’s no need to explicitly define the members of the group as normally defined resources. This change would formalize that promotion.

There are a few minor aspects of this new capability that we need to error check.  A resource group definition can have shared bits (“resource x:3”), and/or a phase specification (“resource(E1) x”) and we assume all items in the resource group have the same definition.  If we allow a group to reference already defined resources, we _may_ want to ensure all the resources are the same as the group resource definition (which might be an implicit definition…).  Or not - there may be some value in allowing different members of a resource group to have different phases, for example.


### General Design

An important part of this design change is that for descriptions that don’t have groups with identically named members, the behavior doesn’t change, and this change should be transparent.  (None of the existing descriptions have this issue.)

In general, this design simplifies the compiler design of resources quite a bit. It complicates the bundle packing code a bit, since we must provide an explicit list of resource ids to allocate. We may want to handle reference groups and reference arrays the same.


#### Parser Changes

We will modify the parser to recognize implicitly defined groups in template instance parameters, and create groups for each of those occurrences.  


```
	subunit xxx(res1 | res2 | res3, res3 & res4);
```


produces (internally):


```
resource anon1 { res1 | res2 | res3 };
resource anon2 { res3 & res4 };	
subunit xxx(anon1, anon2);
```


which in turn produces (internally):

	`resource res1, res2, res3, res4;`


```
	resource anon1 { res1 | res2 | res3 };
	resource anon2 { res3 & res4 };
	subunit xxx(anon1, anon2);
```


We maintain a table of  groups so that we share definitions across explicit and implicit definitions.


#### Promoting Members

In the front-end of the compiler, we preprocess resource group definitions in CPUs, Clusters, and Functional Unit Templates to promote members to the scope they are defined in. While doing this promotion, if the resource already exists, we want to ensure that any phase or shared-bits attribute are the same. As we promote the members of a group, we create a vector of ResourceDef’s for the group definition to link each member to their promoted, defined resources.  Each member contains an index into that list of ResourceDefs.


#### Name Lookup

In general, member name lookup is easier.  For unqualified references (like “member”), we can eliminate the separate member-name lookups, since the member would have been promoted to a top-level reference.  For qualified members (like “group.member”) the code can remain the way it is.  We could also simplify it to simply reduce to a pointer to the promoted resource.


#### Resource Id Assignment

We no longer need to assign resource ids to either a group or to its declared members.  A group is now simply a set of defined resources, and their associated ids. 


#### Accessing Member Ids

Currently, a member’s id is the sum of its group id and its index in the group.  In the new approach, a member’s index in the group is used to index into the group’s vector of ResourceDefs, and we use that resource’s id.


#### Writing out Member Id Name Definitions

When we write out definitions for resources, we no longer need to write out ids for resource groups, or their members.  We can simply skip them.


#### Building Resource Sets

When we create permutations of pooled resource assignments, we must use a set of resource ids, rather than a simple range.  We should do this the same for arrays and groups. \



#### Output of the Database For Resource Groups and Arrays

Rather than simply write out an initial resource id and a number of resources, for groups we need to write out a vector of the resource ids in the group.  We may want to create a table of these, since there will be many duplicates.  We will probably want to use the same mechanism for both Arrays and Groups, so that these can be treated the same way in the database and the bundle packer - even though an Array is guaranteed to have consecutive ids.

We modify the PooledResourceRef definition - rather than provide a base resource id for the pool, we instead provide an array of resources associated with the pool.  For example, today a PooledResourceRef looks like this:


```
	static std::vector<PooledResourceRef> PRES_101 
                {{RefUse,1,0,nullptr,47,2,&POOL_11}};
```


Currently, we only provide the base id of the pool/group, in this example 47.  To implement the new methodology, we instead provide a pointer to an array of ids associated with the pool:


```
	static ResourceId MEMBERS_47 { 23, 43, 39, 35 };
	static std::vector<PooledResourceRef> PRES_101 
                {{RefUse,1,0,nullptr,&MEMBERS_47,2,&POOL_11}};
```



#### Bundle Packing

As in the database, a “Pool” is no longer a base plus a number of members.  It is now a vector of explicit resource ids as part of the PooledResourceRef object.  Rather than “compute” the resource id’s in a pool, we just use the explicitly enumerated resource ids. (This is a one or two line change in the pool allocation code.)


#### TdScan Changes

Currently each stage of an itinerary can specify a set of resources to use in that stage, specifying either all of the resources or just one.  Functional unit templates and subunit templates are defined to have a resource template argument for each stage.  For each CPU, for each functional unit, TdScan generates a separate instance of the functional unit for each permutation of the stage resources.  For example, given the set of InstrStages:


```
InstrStage: cycles=1, units=[ADD1, ADD2], timeinc=-1
InstrStage: cycles=1, units=[UNIT1, UNIT2], timeinc=-1
InstrStage: cycles=1, units=[STORE1, STORE2], timeinc=-1
```


We previously generated the following functional unit definitions:


```
	func_unit type name(ADD1, UNIT1, STORE1);
	func_unit type name(ADD2, UNIT1, STORE1);
	func_unit type name(ADD1, UNIT2, STORE1);
	func_unit type name(ADD2, UNIT2, STORE1);
	func_unit type name(ADD1, UNIT1, STORE2);
	func_unit type name(ADD2, UNIT1, STORE2);
	func_unit type name(ADD1, UNIT2, STORE2);
	func_unit type name(ADD2, UNIT2, STORE2);
```


This is all the permutations of the resource sets associated with the three stages. With the new syntax, we generate the following:

	`func_unit type name(ADD1|ADD2, UNIT1|UNIT2, STORE1|STORE2);`

and let the MDL compiler create the allocation pools to implement the permutations automatically.


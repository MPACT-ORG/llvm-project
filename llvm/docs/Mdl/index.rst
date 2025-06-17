==========================================
Machine Description Language Documentation
==========================================

.. contents::
   :local:

Overview
========

These documents describe the design and usage of the Machine Description
Language and its use in LLVM.

.. toctree::
   :hidden:

   RFC.md
   MDLSpec.md
   MDLIntro.md
   UsingTheMDLCompiler.md

:doc:`RFC`
  Current RFC regarding integration of this work into LLVM.

:doc:`MDLSpec.md`
  A detailed users guide for the MDL language, including detailed language spec.

:doc:`MDLIntro.md`
  A condensed tutorial to describing accelerator architectures in MDL.

:doc:`UsingTheMDLCompiler.md`
  A user's guide to the MDL Compiler.

Internal Design Docs
====================
These are various docs that describe the design of the MDL compiler and its
integration with LLVM

.. toctree::
   :hidden:

   MDLCompilerDesign.md
   ForwardingNetworks.md
   ItinerariesAndStages.md
   BundlePacking.md
   CmakeChanges.md

:doc:'MDLCompilerDesign.md`
  An overview of the design of the MDL compiler.

:doc:`ForwardingNetworks.md`
  Describes the modeling of forwarding networks in MDL.

:doc:`ItinerariesAndStages.md`
  Describes how Itineraries and Schedules are modeled in MDL.

:doc:`ItinerariesAndStages.md`
  Describes how Itineraries and Schedules are modeled in MDL.

:doc:`BundlePacking.md`
  Describes the design of MDL-based bundle packing.

:doc:`CmakeChanges`
  Describes changes made to CMake to integrate the use of MDL in LLVM.


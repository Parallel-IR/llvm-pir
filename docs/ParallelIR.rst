================
Parallel LLVM IR
================

.. contents::
   :local:

Introduction
============

LLVM's parallel IR offers a low-level way to express fork-join parallelism with
three `parallel instructions <LangRef.html#parallelinsts>`_. These instructions
form `parallel tasks <#parallel-tasks>`_ and `parallel regions
<#parallel-regions>`_ that are identified and maintained by the `parallel
region info <#parallel-region-info>`_. Most common high-level constructs can be
lowered to fork-join parallelism, including parallel loops, parallel
divide-and-conquer algorithms, parallel maps/reduction/scan operations
(`details
<https://github.com/Parallel-IR/llvm-pir/wiki/%5BRFC%5D%5BPIR%5D-Parallel-IR,-Stage-0:-IR-extensions#low-level-representation-of-parallelism>`_).


.. _parallel-regions:

Parallel Regions
================

Parallel regions are a single-entry-multi-exit sub-graphs of the CFG only
entered via a '``fork``' terminator and only left via '``join``' terminators.
A parallel region itself is divided into two single-entry-multi-exit sub-graphs
that start at the successors of the '``fork``', the so called `parallel tasks
<#parallel-tasks>`_. The two tasks are the *forked* task and the *continuation*
task and they can be executed in parallel. The two tasks are called sibling
tasks.

It is possible to sequentialize these tasks, thus to execute the *forked* task
first and the *continuation* task second. If sequentialized, all `parallel
terminators <LangRef.html#parallelinsts>`_ of this parallel region are replaced
by unconditional branch instructions (see `sequentialization
<#sequentialization>`_).

Parallel regions can be nested, thus there can be parallel regions embedded in
the *forked* as well as *continuation* task. As these nested parallel regions
have to be completely enclosed by the containing parallel task of the outer
parallel region they form a *parallel region forest* similar to the loop forest
maintained by the LoopInfo pass.


Well-formedness
---------------

A parallel region is well-formed if:

  * The '``fork``' instruction dominates the whole parallel region.

  * The two `parallel tasks <#parallel-tasks>`_  started by the '``fork``' are
    well-formed.

  * There is no "early exit" from a parallel region, e.g., via a '``return``'
    or '``unreachable``' terminator.

  * Exception handling is not allowed in parallel regions.

Well-formedness implies that there exists a perfect nesting of parallel
regions, thus that two parallel regions are either disjunct or one is
completely enclosed by one parallel task of the other. It also means that each
path in the CFG contains as many '``fork``' instructions as '``join``'
instructions.


.. _parallel-tasks:

Parallel Tasks
==============

A parallel task can either be a *forked* task or a *continuation* task. Each
starts with a successor block of a '``fork``' terminator, where the *forked*
task is started by the first successor and the *continuation* task by the
second.


Forked Task
-----------

The *forked* task contains the CFG sub-graph that might be executed in parallel
to the *continuation* task. If so, it is executed by a different thread, thus
not the one that reaches the '``fork``' instruction of the parallel region.

Well-formedness
^^^^^^^^^^^^^^^

The *forked* task is well-formed if:

  * The first '``fork``' successor (the entry point of the *forked* sub-graph),
    dominates the whole *forked* task.

  * The sub-graph of the *forked* task ends with '``halt``' instructions that
    have the entry block of the sibling *continuation* task as target operand.
    This means the *continuation* task entry block has to post-dominate all
    blocks in the sub-graph *forked* task.


Continuation Task
-----------------

The *continuation* task contains the CFG sub-graph that might be executed in
parallel to the *forked* task. If so, it is executed by the same thread that
reached the '``fork``' instruction of the parallel region.

Well-formedness
^^^^^^^^^^^^^^^

The *continuation* task is well-formed if:

  * The second '``fork``' successor (the entry point of the *continuation*
    sub-graph), dominates the whole *continuation* task.

  * The sub-graph of the *continuation* task ends with '``join``' instructions.


.. _parallel-region-info:

Parallel Region Info
====================

The parallel region info pass is an analysis that identifies all parallel
regions in a CFG and maintains the parallel region forest. It offers an API
similar to LoopInfo or RegionInfo to query and modify parallel regions.

Currently the parallel region info is "lazy" in the sense that it does
only need to be updated if new parallel regions are created (or
deleted). As this should not happen very often (and only in very few
places) it allows transformation passes to preserve the parallel
region info without modifications. Additionally, it makes the analysis
very lightweight in the absence of parallel regions (which should be
the majority of functions).

The drawback for passes that need to deal with parallel regions
explicitly is the absence of a mapping from basic blocks to parallel
regions. For now these passes can use the createMapping() function to
generate such a mapping on-demand. After integration of parallel
regions a separate function pass could be introduced to maintain this
mapping and recompute it if it was not preserved by a transformation.
However, at the moment there are only a small number of places that
require it but a lot of transformations that would need to be modified
to preserve it.


.. _lowering-of-parallel-ir:

Lowering of Parallel IR
=======================

Parallel IR can either be lowered to runtime calls of a parallel runtime
library (libGOMP, libCilk++, libpthread, ...) or sequentialized to regular IR.

Lowering to Runtime Calls
-------------------------

In the simplest setting parallel regions can be lowered to a runtime call that
spawns the *forked* task, and "joines" this task after the *continuation* task.
However, parallel loops can be lowered to specialized runtime calls.

*The work on lowering is still in process and more details will follow.*

Sequentialization
-----------------

To sequentialize a well-formed parallel region, `parallel instructions
<LangRef.html#parallelinsts>`_ are replaced as follows:

  * '``fork``' is replaced with a branch to the *forked* block (first
    '``fork``' successor).

  * '``halt``' is replaced with a branch to the sibling *continuation* block
    (second '``fork``' successor, and '``halt``' successor).

  * '``join``' is replaced with a branch to its destination block.

The sequentialization is linear in the size of the CFG without `parallel region
information <#parallel-region-info>`_ and constant otherwise.


Analysis & Transformation Impact
================================

* Analysis and transformations shall assume both, the *forked* and the
  *continuation* task can be both live at the same time. While the common
  assumptions that the *forked* task could be executed prior to the
  *continuation* task should suffice for most analysis and transformation.
  However, most notably it is not allowed to merge '``alloca``' instructions
  from different parallel tasks.


.. _limitations:
.. _extensions:

Limitations & Extensions
========================

Limitations due to missing extensions include:

* Parallel tasks can only end in '``halt``' (*forked* task) or '``join``'
  (*continuation* task) instructions. Thus '``unreachable``' or '``return``'
  terminators are not allowed.

* Exception handling is not allowed in parallel tasks.

* As there is only a "blocking" '``join``' so far, *fire and forget*
  (or "nowait") parallelism is not possible.

Note that these limitations will be tackled in the future and are consequently
subject to change.


Conceptual limitations mainly stem from the fact that there has to be a start
and end block in the CFG that defines the range of parallelism. Hence,
"dynamically terminated" parallelism is excluded, e.g., pthreads.

# Topology

Graph topology is represented explicitly through GraphNodeDescriptor and
GraphEdgeDescriptor. Nodes are typed (Kernel, MemoryCopy, MemorySet,
HostOperation, Synchronization, ChildGraph, EventPrimitive, BackendOpaque).
Edges represent execution dependency.

Topology validation enforces node identity uniqueness, edge endpoint validity,
acyclicity (Kahn), no dangling dependencies, no duplicate or contradictory edges,
and no self-dependency. GraphDescriptor::finalize canonicalizes (sorts) nodes and
edges by id and computes a deterministic topology digest and a semantic digest.
The digest is part of the graph's semantic identity.
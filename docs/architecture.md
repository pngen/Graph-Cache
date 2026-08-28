# Architecture

Graph Cache is a C++20 runtime library (the GraphCache target) plus a small set
of binaries. The top-level components are:

- The vendor-neutral graphcache static library.
- gc-coordinator and gc-worker distributed control-plane binaries.
- The gc CLI.
- Tests, examples, and benchmarks.

## Data model

Graph Cache models executable execution graphs with strong typed identities:
GraphId, GraphArtifactId, GraphGeneration, CacheGeneration, CaptureAttemptId,
ReplayAttemptId, and GraphNodeId / GraphEdgeId. A GraphDescriptor carries nodes
and edges. This is the object model; the graph topology semantics are described
in docs/topology.md.

The runtime models backend identity, runtime/graph/kernel ABI, device
architecture and compute capability, tensor shape/datatype/layout,
scalar/quantization specialization, binding and alignment, stream/capture
semantics, and executable dependency authority.

## Correctness core

The central invariant is that a graph cache hit is a correctness decision.
GraphCompatibilityKey (see docs/compatibility.md) is a deterministic SHA-256
digest over a canonical typed encoding of the request. Candidate graphs are
compared field-by-field by decide_compatibility, which yields a structured
compatible/incompatible decision plus the set of reasons.

## Engine

GraphCache implements the lookup pipeline, single-flight capture, guarded
lifecycle, generation authority, leases, eviction, residency tiers,
invalidation, persistence, recovery, indexing, namespaces, and observability.
See docs/lifecycle.md, docs/residency.md, docs/invalidation.md, and
docs/persistence.md.

## Backends

GraphBackend defines capture, validation, load/instantiate, replay, rebind,
unload, and backend-resident accounting. create_backend returns the CPU DAG
backend by default and the CUDA Graph backend when CUDA is available. See
docs/capture.md.

## Distributed control plane

The protocol frames messages over TCP using a 4-byte little-endian length
prefix, a hard max frame size, a protocol version, a message type, and strict
decode validation. The coordinator owns authority (coordinator epoch, cache
generation, graph generation, worker boot id) and delegates capture/replay to
worker processes. See docs/protocol.md.

CUDA Graphs are one proven backend; they do not define the abstract semantics
of Graph Cache.

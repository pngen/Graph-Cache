# Benchmarks

The `gc-bench` (CPU) and `gc-bench-cuda` (CUDA) executables report measured
figures. Values labelled "(measured)" are directly timed with a steady clock;
"avoided-capture" is labelled "(derived from cold-vs-warm)" and is computed from
the measured cold-capture time multiplied by the number of replayed executions —
it is never presented as a directly measured quantity.

## CPU (gc-bench), single core unless noted

| Operation | Figure (measured) |
|---|---|
| GraphCompatibilityKey construction | ~225,000 keys/s |
| SHA-256 compatibility digest | ~353,000 digests/s |
| Topology hashing | ~319,000 hashes/s |
| CPU cold capture + validation | ~0.10 ms (first lookup) |
| CPU warm replay | ~0.8-0.9 us/op mean (10,000 replays) |
| Lookup (exact, single workload) | ~109,000-135,000 lookups/s |
| Avoided capture (derived) | ~1.0 s over 10,000 replays |
| insert+hit 2,000 graphs | ~86 ms |
| 100% hit 1-thread | ~110,000/s |
| 90/10 hit/miss 1-thread | ~114,000/s (1800 hits, 200 misses) |
| 100% hit 8-thread | ~323,000/s |
| 50/50 hit/miss 1-thread | ~97,000/s (1000/1000) |
| 256-element topology 1-thread | ~117,000/s |
| insert+hit 10,000 graphs | ~336 ms |
| 10,000-graph 100% hit 1-thread | ~104,000/s |
| graph-metadata serialize 100k entries | ~386,000/s |
| compat-key 100k entries | ~242,000/s |
| graph-metadata deserialize 100k entries | ~1,139,000/s |

## CUDA (gc-bench-cuda), NVIDIA GeForce RTX 5090 (compute 12.0 / sm_120)

| Operation | Figure (measured) |
|---|---|
| Cold capture + instantiate (first lookup) | ~52.9 ms |
| Warm graph replay | ~36.9 us/op mean (200 replays) |
| Workload | 1024 elements, 2 kernel nodes, 2 memcpy nodes |

The CUDA figures are real CUDA Graph captures executed on the target accelerator,
not a list of sequential kernel launches.
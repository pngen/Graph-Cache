# Benchmarks

Benchmarks measure canonical GraphCompatibilityKey construction, SHA-256
compatibility digest, topology hashing, exact and compatibility lookup, lookup
miss, lease acquire/release, single-flight contention, metadata
serialization/deserialization, snapshot generation, persistence write, recovery,
and CPU graph construction and replay. Where CUDA is available, the CUDA
benchmark records cold capture, instantiate, warm replay, and uncaptured
baseline for a representative element count. Avoided capture cost is reported as
derived from measured cold-vs-warm values, never as directly measured.
# Capture

GraphCapturer / GraphBackend::capture converts a finalized GraphDescriptor into a
backend-executable artifact. The CPU backend builds a real operation DAG; the
CUDA backend builds a real CUDA Graph (cudaGraphCreate / cudaGraphAdd, then
cudaGraphInstantiate).

Validation runs after capture: topology integrity, artifact integrity, backend
compatibility, and an execution smoke (launch + synchronize + deterministic
reference comparison where possible). A captured graph that cannot replay
correctly is not valid cache state.

Single-flight capture ensures N concurrent misses for one compatibility key cause
one capture; other requests wait and share the same result. Capture failures
propagate consistently, and a stale capture completion cannot publish.
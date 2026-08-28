# Replay

A graph is replayed under a GraphLease that pins the eligible generation. Replay
checks the lease validity, the expected generation, and backend residency, then
executes the graph over caller-supplied buffers and returns a structured
GraphReplayResult (ok, replayed node count, output digest, latency). A stale
lease cannot be replayed after release.
# Invalidation

Invalidation is supported by GraphId, GraphArtifactId, compatibility key,
workload identity, topology identity, backend/runtime generation, architecture,
dependency generation, model/operator revision, namespace, and predicate. It
immediately prevents new leases, preserves currently safe active replay leases
until drain (unless strong invalidation requires authoritative revocation),
prevents stale lookup races, retires backend-resident state at a safe point,
prevents stale capture completion from republishing, persists where required,
and increments generation authority.
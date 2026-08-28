# Persistence

Persistence writes a versioned, checksummed envelope atomically (temp-write then
rename), using SHA-256 over the canonical payload. The store rejects corruption,
truncation, unknown version, trailing garbage, and orphan temp artifacts. Opaque
backend handles are never claimed to survive restart. On recovery, canonical
metadata and indices are rebuilt, invalidated graphs are reconciled, backend
residency is marked absent, and graph execution state is reconstructed or
recaptured as semantics permit.
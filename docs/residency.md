# Residency

Residency tiers are MetadataOnly, PersistentStorage, HostResident, and
BackendResident (a live instantiated backend graph). Backend handles are tracked
separately from the canonical persistent artifact identity. Residency policy
considers artifact size, recapture cost, instantiate/load cost, replay frequency,
last access, expected reuse, backend resource cost, active lease count, pin
state, dependency criticality, namespace, workload priority, and device
affinity. Backend-resident state is never destroyed while an active replay lease
requires it.
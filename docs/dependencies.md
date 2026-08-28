# Executable Dependency Authority

Graph Cache references executable dependencies (such as kernels) abstractly via
KernelIdentityRef: stable id, generation, ABI, provenance, and content digest.
A graph is stale when a correctness-contributing dependency changes
incompatibly. Ignorable (non-contributing) metadata does not invalidate.
The explanation identifies exactly which dependency invalidated eligibility.
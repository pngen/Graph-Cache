# Rebinding

Bindings are typed: ImmutableBinding, ReplayMutableBinding,
RecaptureRequiredBinding, BackendValidatedBinding. Graph Cache never assumes
pointer values may always change nor that node parameters may always be patched;
backend rules decide what is legal.

For CUDA, where supported, cudaGraphExecMemcpyNodeSetParams1D / cudaGraphExec*NodeSetParams
update node parameters at replay. Legal rebinding is proven correct against a CPU
reference; incompatible shape/ABI rebinding is rejected; illegal topology change
requires recapture; and rebinding never silently mutates canonical graph identity
unless the compatibility policy defines it as replay-safe.
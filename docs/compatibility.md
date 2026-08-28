# Compatibility

A graph cache hit is a correctness decision. Compatibility is never inferred from
a name, topology hash, operator list, graph size, or workload label.

GraphCompatibilityKey is built from a fully-typed CompatibilityFacts set. The
facts are encoded into a canonical length-delimited typed stream (big-endian
integers, fixed schema order) and digested with SHA-256. The canonical form is
retained alongside the digest for explainability. Falsely-encoded or reordered
metadata is rejected on decode.

decide_compatibility compares a request against a candidate's captured facts and
returns a decision class: ExactCompatible, CompatibleWithRebinding,
CompatibleWithDynamicShapeConstraint, CompatibleWithRuntimeValidation, or one of
the Incompatible* / InvalidGraph / StaleGraph / CorruptGraph / PolicyRejected
classes, plus fine-grained CompatibilityReason records. Incompatible graphs are
never silently coerced into eligibility. 
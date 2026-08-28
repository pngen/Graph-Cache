# Contributing

Graph Cache accepts contributions from individuals and organizations on the terms
of the Apache License 2.0 without requiring a Contributor License Agreement (CLA).

Guidance:

- Open an issue or pull request on the repository.
- Keep the public build clean under `/W4` and `/WX` (Release and Debug).
- Add a test (unit, adversarial, concurrency, persistence, property) for any
  behavior you change; distribute tests under no timeouts.
- Preserve the documented compatibility semantics; never silently coerce an
  incompatible graph into eligibility.
- Keep the architecture vendor-neutral: CUDA is one proven backend, not the
  definition of Graph Cache semantics.
- Do not add telemetry or outbound network behavior.

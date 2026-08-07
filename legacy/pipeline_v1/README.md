# Legacy pipeline v1

This directory archives the original low-level pipeline implementation and its oracle test.

It is intentionally outside the main `include/`, `src/`, and `tests/` trees and is not part of the default CMake build.

Current direction:

```text
- Do not use low-level pipeline wiring for apparent position, solar terms, eclipses, or other astronomy kernels.
- Keep low-level astronomy as typed functions with options/profile and local scratch.
- Future pipeline design, if needed, should be chart-level workflow only.
```

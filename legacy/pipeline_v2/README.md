# Legacy pipeline_v2 prototype

This directory archives the previous `pipeline_v2` experiment.

It is intentionally outside the main `include/`, `src/`, and `tests/` trees and is not part of the default CMake build.

Current direction:

```text
- Low-level astronomy calculations should use typed functions, options/profile, local scratch, and direct control flow.
- Pipeline/dataflow, if reintroduced, belongs to high-level chart/workflow composition such as houses, aspects, shensha, BaZi, Ziwei, and chart assembly.
- This prototype should not be used as the main calculation path.
```

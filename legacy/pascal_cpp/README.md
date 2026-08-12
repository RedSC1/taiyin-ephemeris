# Legacy Pascal/C++ bridge

This directory preserves the Pascal implementation and C++ FFI bridge that were replaced by the native C++ Ganzhi and BaZi rule cores.

It is reference-only: no active CMake target includes these files, and it is not a supported build path. The original relative layout is retained below this directory to make code history and rule comparisons straightforward.

## Differential QiYun verification

The active build never compiles this legacy tree. If a pre-migration
`qiyun_records_10000` executable is available, compare all 10,000 records with
the native executable using:

```sh
python3 bazi_astrology/tests/compare_qiyun_records.py \
  --reference /path/to/pascal/qiyun_records_10000 \
  --candidate build/bazi_astrology/qiyun_records_10000 \
  --data-root data
```

The comparison checks direction, time model, Jie index, traditional calendar
offset components, split Julian dates, and civil start time field by field. A
one-microsecond tolerance applies only to floating-point time fields.

The same comparison can be registered with CTest by configuring
`TAIYIN_BAZI_QIYUN_REFERENCE_EXECUTABLE` with the absolute path to the
pre-migration executable. This optional verification path does not restore an
FPC dependency to the active build.

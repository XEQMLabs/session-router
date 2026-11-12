# Regression tests

Security regression cases for session-router, under `regress/`. They link the real `srouter::`
component libraries and are meant to run under AddressSanitizer/UBSan so that memory-safety and DoS
fixes stay fixed.

Build and run:

```
cmake -B build -G Ninja -DSROUTER_TESTS=ON -DSROUTER_XSAN=address,undefined
ninja -C build srouter-regress
./build/test/srouter-regress        # or: ninja -C build check
```

`srouter-regress` exits non-zero if any case fails. Each case self-registers via the `REGRESS_CASE`
macro in `regress/test_harness.hpp`; `regress/main.cpp` runs them all.

To add a case, drop a `regress/test_<area>.cpp` file and list it in `CMakeLists.txt`.

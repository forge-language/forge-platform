# Package-manager build comparison

`build-cache.py` uses real Forge/C compilers and checks the produced executable
prints 42 after every build. It copies one supplied toolchain into a disposable
fixture, imports a Forge helper, warms both managers, then alternates before/after
order over seven repetitions. No native external modules are present.

```
python3 benchmarks/build-cache.py --toolchain /path/to/toolchain-root \
  --before /path/to/baseline-manager --after /path/to/current-manager \
  --output /tmp/forge-build-cache.json
```

The toolchain root has `build/bin/forge`, `build/lib/*.a` and `include/`.
Manager paths must be executable binaries; configure their dynamic library paths
externally if needed. The fixture never changes HOME or production data.

The measured unchanged-build median was 127.946 ms before and 33.337 ms after
(3.84×). These are seven alternating runs on the shared host, including process
startup, tool probes and content hashing. Large inputs and unsupported filesystem
entries can eliminate cache benefits. Native external modules always run CMake
build dependency checks and the linker; only CMake configuration is cached.
Browser caching has separate end-to-end checks in `tests/installed-cache.py`.

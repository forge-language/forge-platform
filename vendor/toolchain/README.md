# Pinned Forge toolchain

Compiler/runtime/include/stdlib sources from Helloworld0822/forge commit
9a6109c. Packaging uses a minimal CMake file and disables optional OpenCL.
The stage0 compiler is C, the runtime is C, and application/server/package policy
is written in Forge. This snapshot includes imported extern/module prototypes,
string escapes, runtime lifetime fixes and experimental JavaScript output.

Upstream: https://github.com/Helloworld0822/forge
License: MIT, see LICENSE. Supported distribution: Linux x86_64 glibc 2.35+.

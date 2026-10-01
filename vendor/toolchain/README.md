# Pinned Forge toolchain

Compiler/runtime/include/stdlib sources from forge-language/forge-preview commit
ba36611. Packaging uses a minimal CMake file and disables optional OpenCL.
The stage0 compiler is C, the runtime is C, and application/server/package policy
is written in Forge. This snapshot includes imported extern/module prototypes,
string escapes, scoped byte views/builders, idle scheduler fixes, bounded queue reuse
and experimental JavaScript output.

Upstream: https://github.com/forge-language/forge-preview
License: MIT, see LICENSE. Supported distribution: Linux x86_64 glibc 2.35+.

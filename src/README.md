# Blaze interpreter sources

Layout (each folder is an include root; sources use `#include "header.h"` without a path prefix):

| Directory | Contents |
|-----------|----------|
| `base/` | `common`, `memory`, `value`, `chunk` |
| `runtime/` | `object`, `vm` (split across `vm.c`, `vm_core.c`, `vm_debugger.c`, `vm_natives.c`, `vm_call.c`, `vm_execute.c`), `vm_internal.h`, `debug` |
| `syntax/` | `scanner`, `parser` (`.c` includes `parser_*`.inc fragments), `ast` |
| `semantics/` | `types`, `typechecker` (`.c` includes `typechecker_*`.inc fragments), `generic` |
| `codegen/` | `compiler` (`.c` includes `compiler_*`.inc fragments), `compiler_internal.h` |
| `module/` | `module` (imports / load pipeline) |
| `support/` | `colors` |

Build: paths are listed in the root `CMakeLists.txt`. To regenerate `.inc` fragments from a monolithic backup, use `tools/split_sources.py` (compiler, typechecker, parser only).

# Blaze interpreter sources

Layout (each folder is an include root; sources use `#include "header.h"` without a path prefix):

| Directory | Contents |
|-----------|----------|
| `base/` | `common`, `memory`, `value`, `chunk` |
| `runtime/` | `object`, `vm`, `debug` |
| `syntax/` | `scanner`, `parser`, `ast` |
| `semantics/` | `types`, `typechecker`, `generic` |
| `codegen/` | `compiler` |
| `module/` | `module` (imports / load pipeline) |
| `support/` | `colors` |

Build: paths are listed in the root `CMakeLists.txt`.

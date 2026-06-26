# CMake Build Instructions

Modern CMake (3.20+) build system for C++23 Lua implementation.

## Quick Start

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Install (optional)
cmake --install build --prefix /usr/local
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | - | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |
| `LUA_BUILD_TESTS` | `ON` | Enable test mode with ltests.h |
| `LUA_BUILD_SHARED` | `OFF` | Build shared library |
| `LUA_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `LUA_ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |
| `LUA_ENABLE_COVERAGE` | `OFF` | Enable code coverage reporting (gcov/lcov) |
| `LUA_ENABLE_LTO` | `OFF` | Enable Link Time Optimization |

## Examples

**Debug with sanitizers:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DLUA_ENABLE_ASAN=ON -DLUA_ENABLE_UBSAN=ON
cmake --build build
```

**Code coverage:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DLUA_ENABLE_COVERAGE=ON
cmake --build build
cd testes && ../build/lua all.lua
lcov --capture --directory ../build --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

**Release with LTO:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLUA_ENABLE_LTO=ON
cmake --build build
```

> **LTO note:** Works on both GCC and clang. GCC 15's whole-program IPA
> miscompiles the GC liveness/marking path (the registry root fails
> `checkliveness` on any full collection); clang+LTO and UBSan are both clean, so
> it's a GCC LTO bug rather than portable UB. The build therefore compiles the GC
> translation units (`src/memory/lgc.cpp` + `src/memory/gc/*.cpp`) **without
> interprocedural optimization on GCC** (`-fno-lto`), keeping LTO for the rest of
> the codebase. Excluding only a subset of the GC files does not fix it.

**Production:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLUA_BUILD_TESTS=OFF -DLUA_BUILD_SHARED=ON
cmake --build build
```

## Targets

- **liblua_static**: Static library `liblua.a`
- **liblua_shared**: Shared library `liblua.so` (if enabled)
- **lua**: Lua interpreter executable

## Testing

```bash
cd build && ctest                # Run all tests
cd build && ctest --output-on-failure  # Verbose
```

## Parallel Builds

```bash
cmake --build build --parallel
cmake --build build -- -j$(nproc)
```

## Cleaning

```bash
cmake --build build --target clean  # Clean artifacts
rm -rf build                        # Full clean
```

# Mnemosyne Artificial Intelligence: - In Greek mythology, Mnemosyne (/nɪˈmɒzɪni/; Ancient Greek: Μνημοσύνη) was one of the Titans, who are the children of Uranus (Sky) and Gaia (Earth). She was the goddess of memory and the mother of the nine Muses by Zeus. The name Mnemosyne is derived from the Greek word mnēmē ('remembrance, memory').

> `main()` lives inside a **static library**, not in the executable.

The executable links the library and contains **no entrypoint of its own**.  
At link time, the linker pulls the object file containing `main()` out of the static library,  
so the program effectively *runs the library*.

This example uses **standard C++23 modules** with MSVC and stays close to the metal
(batch build, VS projects, and CMake).

---

## Layout

### Static library (`mylib.lib`)

The static library contains both functionality *and* the process entrypoint.

Folders:

- `modules/` — module interface units (`.ixx`)
- `src/` — module implementation units and normal translation units (`.cpp`)
- `include/` — headers used by non-module translation units

Modules and sources:

- `mylib.ixx`  
  Module interface for the example library API

- `mylib.cpp`  
  Module implementation

- `core.log.ixx` / `core.log.cpp`  
  Logging system using C++23 `<print>` with console, file, and debugger sinks

- `core.time.ixx` / `core.time.cpp`  
  Monotonic timing utilities and a simple frame clock

- `core.assert.ixx` / `core.assert.cpp`  
  Fail-fast assertions (always-on + debug-only)

- `core.error.ixx` / `core.error.cpp`  
  Minimal error / result helpers

- `core.env.ixx` / `core.env.cpp`  
  Environment variable helpers

- `core.path.ixx` / `core.path.cpp`  
  Executable path and path utilities

- `core.string.ixx` / `core.string.cpp`  
  Small string helpers (trim / split / join)

- `core.format.ixx` / `core.format.cpp`  
  Formatting helpers (wraps `<format>`)

- `core.id.ixx` / `core.id.cpp`  
  Strongly-typed IDs

- `core.math.ixx` / `core.math.cpp`  
  Small math helpers (clamp / lerp / align)

- `core.memory.ixx` / `core.memory.cpp`  
  In-process memory records, staging, and commit operations

- `core.memory.store.ixx` / `core.memory.store.cpp`  
  Persistent snapshot I/O for memory records

- `core.memory.index.ixx` / `core.memory.index.cpp`  
  Query indexing and scoring for memory search

- `core.memory.stage.ixx` / `core.memory.stage.cpp`  
  Staging journal (add/edit/commit) for memory records

- `runtime.ixx` / `runtime.cpp`  
  Runtime module that owns initialization, memory loading, and the main loop

- `lib_main.cpp`  
  Defines `main()` and forwards execution to `runtime::run()`

- `tests_main.cpp`  
  Standalone test executable (only built when tests are enabled)

### Executable (`app.exe`)

- `app.cpp`  
  A translation unit with **no `main()`**

The executable exists purely to link against the static library and host
application-side callbacks (currently defined, but not yet invoked).

Shared headers:

- `include/app_api.h`  
  C-compatible callbacks exposed by the app, consumable by the library

---

## How it works

1. All modules and `lib_main.cpp` are compiled into `mylib.lib`
2. The executable links against `mylib.lib`
3. The linker resolves `main()` from the library because the executable has none
4. When the program starts, execution begins inside the library runtime

This pattern is useful for:

- engine-style runtimes
- test harnesses
- tool and launcher architectures
- codebases where “the library *is* the program”

---

## Build options

### A) Command line (Developer Command Prompt)

``` bat
.\build.bat
``` 

The script:

- compiles module interfaces (`.ixx`) to `.ifc`
- compiles module implementations with explicit `/reference`
- archives everything (including `main`) into `mylib.lib`
- links `app.exe` without defining an entrypoint

### B) Visual Studio

- Open `StaticLibEntryPoint.sln`
- Right-click **App** and select **Set as Startup Project**
- Build and run

Notes:

- The **App** project intentionally does not define `main()`
- Module dependency scanning is enabled
- No custom build steps are required

### C) CMake (MSVC with C++23 modules)

``` bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Optional tests target:

``` bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMNEMOSYNE_BUILD_TESTS=ON
cmake --build build --config Release --target tests
```

---

## Runtime behavior

When run, the program:

- logs startup via `core.log`
- loads the memory store from `data/memory` (relative to the executable)
- calls `mylib::entry()` and logs its return value
- enters a loop driven by `core.time::frame_clock`
- prints one line per second

The loop is intentionally simple and exists only to prove:

- the runtime lives in the library
- systems initialize in a controlled order
- the process lifetime is owned by the library
- memory staging and query plumbing is working

---

## Smoke / tests

### Smoke run (finite, CI-friendly)

Runs exactly **3 frames** and exits cleanly.

``` bat
.\build.bat smoke
``` 

Smoke mode runs a short memory staging flow (stage → commit → query) and then
exits after 3 frames.

### Tests

Builds and runs a small `tests.exe` linked against the same library.

``` bat
.\build.bat tests
``` 

---

## Scope

This is **not** a framework and **not** a build system replacement. It is a
minimal, correct, reproducible example of an **internal-entrypoint architecture**
using pure C++23 modules, suitable as a foundation for adding additional systems.

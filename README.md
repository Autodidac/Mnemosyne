# Static library with an Internal Entrypoint (in C++23 and Modules)

This repository demonstrates an **internal entrypoint** pattern in modern C++23:

> `main()` lives inside a **static library**, not in the executable.

The executable links the library and contains **no entrypoint of its own**.  
At link time, the linker pulls the object file containing `main()` out of the static library,  
so the program effectively *runs the library*.

This example is intentionally minimal and uses **standard C++23 modules** with MSVC.

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

- `runtime.ixx` / `runtime.cpp`  
  Runtime module that owns initialization and the main loop

- `lib_main.cpp`  
  Defines `main()` and forwards execution to `runtime::run()`

### Executable (`app.exe`)

- `app.cpp`  
  A translation unit with **no `main()`**

The executable exists purely to link against the static library and provide
application-side callbacks.

---

## How it works

1. All modules and `lib_main.cpp` are compiled into `mylib.lib`
2. The executable links against `mylib.lib`
3. An anchor symbol forces the linker to pull in the object that defines `main()`
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

---

## Runtime behavior

When run, the program:

- logs startup via `core.log`
- calls `mylib::entry()` and logs its return value
- enters a loop driven by `core.time::frame_clock`
- prints one line per second

The loop is intentionally simple and exists only to prove:

- the runtime lives in the library
- systems initialize in a controlled order
- the process lifetime is owned by the library

---

## Smoke / tests

### Smoke run (finite, CI-friendly)

Runs exactly **3 frames** and exits cleanly.

``` bat

.\build.bat smoke

``` 

### Tests

Builds and runs a small `tests.exe` linked against the same library.

``` bat

.\build.bat tests


``` 

---

## Scope

This is **not** a framework and **not** a build system replacement.

It is a:

- minimal
- correct
- reproducible

- example of an **internal-entrypoint architecture** using pure C++23 and modules,
suitable as a foundation for adding additional systems.


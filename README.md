# stdex

A header-only C++ extension library: hashing, fast modular arithmetic, flat hash tables, inlined bitsets, stable radix sort, and small utilities.

## Build

`cmake-scaffold` is vendored as a git submodule and supplies the CMake modules and tool configuration. Initialize it before configuring:

```powershell
git submodule update --init --recursive
```

CMake presets are provided in `CMakePresets.json` and `CMakeUserPresets.json`:

```powershell
cmake --preset msvc_x64_debug
cmake --build out/build/msvc_x64_debug
ctest --test-dir out/build/msvc_x64_debug --output-on-failure
```

Replace `msvc_x64_debug` with another configured preset as needed.

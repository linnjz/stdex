# CI: build & run all tests across compilers

These scripts configure (CMake + Ninja), build **every** test target registered
in `tests/CMakeLists.txt`, and run them via `ctest`. The C++ standard and extra
preprocessor macros are configurable.

## Compilers

| Platform | `-Compiler` / `-c` | Notes |
|---|---|---|
| Windows | `msvc`, `clang-cl` | run inside a VS developer environment (INCLUDE/LIB/PATH) |
| Linux / WSL | `gcc`, `clang` | `cmake`, `ninja`, compiler on PATH |

## Windows (PowerShell 7)

```powershell
# msvc, C++23, all tests
./ci/run-tests.ps1 -Compiler msvc -CxxStandard 23

# clang-cl, C++26, with two macros, Release, only tests matching "inplace"
./ci/run-tests.ps1 -Compiler clang-cl -CxxStandard 26 `
    -Defines "FOO=1;BAR" -Config Release -Filter inplace

# just configure (no build), or build-only (no run)
./ci/run-tests.ps1 -Compiler msvc -ConfigureOnly
./ci/run-tests.ps1 -Compiler msvc -SkipRun
```

Parameters: `-Compiler msvc|clang-cl`, `-CxxStandard`, `-Defines`,
`-ExtraCxxFlags`, `-Config Debug|Release|RelWithDebInfo|MinSizeRel`,
`-BuildDir`, `-Jobs`, `-Filter`, `-ConfigureOnly`, `-SkipRun`.

## Linux / WSL (bash)

```bash
# gcc, C++23
ci/run-tests.sh -c gcc -s 23

# clang, C++26, macros, Release, filter
ci/run-tests.sh -c clang -s 26 -d "FOO=1;BAR" -t Release -f inplace

# configure-only / skip-run
ci/run-tests.sh -c gcc -C
ci/run-tests.sh -c gcc -n
```

Options: `-c gcc|clang`, `-s <std>`, `-d "NAME[=VAL];..."`, `-x "extra flags"`,
`-t <build type>`, `-b <builddir>`, `-f <ctest regex>`, `-j <jobs>`, `-C`, `-n`,
`-r` (relaxed: add `-w`, i.e. suppress warnings). The suite is now warning-clean
under the scaffold's strict flags (`-Wall -Wextra -Wshadow -Wconversion -Werror`),
so strict is the default; `-r` is only a fallback.

## Notes

- C++ standard is passed as `CMAKE_CXX_STANDARD`. The `stdex` target asks for
  `cxx_std_23`, so usable values are **23 and above** (e.g. `26` on toolchains
  that support it).
- Preprocessor macros are translated per driver: `/D` for `msvc`/`clang-cl`,
  `-D` for `gcc`/`clang`.
- Build directory defaults to `out/ci/<compiler>-cxx<std>-<config>`.
- Some targets are conditionally built by CMake itself (e.g. `test_field_variant`
  only when the compiler supports P2996 reflection; `test_property` Qt off by
  default) — that is expected and handled by the existing `tests/CMakeLists.txt`.
- GitHub Actions: `.github/workflows/ci.yml` runs the full matrix
  (Windows msvc + clang-cl, Linux gcc + clang); `cxx_version` and `defines` are
  `workflow_dispatch` inputs.

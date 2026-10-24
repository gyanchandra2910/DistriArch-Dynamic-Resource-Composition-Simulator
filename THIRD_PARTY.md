# Native dependencies

The simulator, CLI, experiment runner, test harness and local HTTP server are C++20. There is no Python runtime dependency. The React dashboard uses its existing pinned pnpm lockfile.

| Dependency | Version | Use | License |
|---|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json/tree/v3.11.3) | 3.11.3 | Strict configuration and result JSON | MIT, `third_party/licenses/json-MIT.txt` |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib/tree/v0.58.0) | 0.58.0 | Native localhost HTTP API and static dashboard | MIT, `third_party/licenses/httplib-MIT.txt` |
| [PicoSHA2](https://github.com/okdshin/PicoSHA2/tree/v1.0.1) | 1.0.1 | Input SHA-256 fingerprints | MIT, `third_party/licenses/picosha2-MIT.txt` |
| [zlib](https://zlib.net/) | 1.3.2 tested | Native gzip trace I/O | zlib license, `third_party/licenses/zlib-LICENSE.txt` |

The three headers are vendored. Their SHA-256 values and the tested compiler are in `dependency-lock.json`. The build does not download C++ dependencies. `build.ps1` uses the installed GCC and zlib and statically links the Windows executable, including GCC runtime libraries and zlib. Windows system DLLs remain required. Windows 10 or later is required by this HTTP library. CMake uses the platform zlib package and does not enforce static linkage.

To reproduce the tested Windows toolchain, install MSYS2's `mingw-w64-x86_64-gcc` and `mingw-w64-x86_64-zlib`, with `C:\msys64\mingw64\bin` in PATH. The supplied binaries can run without a compiler, Node or Python; rebuilding the React dashboard needs Node/pnpm.


The redistribution includes installed dashboard production dependency and peer license notices under [third_party/licenses/frontend/INDEX.md](third_party/licenses/frontend/INDEX.md).


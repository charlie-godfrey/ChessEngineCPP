# Aurora Chess Engine (C++20)

Performance-first C++20 scaffold for a chess engine with a modular engine library
and multiple CLI entry points.

## Build (Linux: clang/gcc)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Optional toggles:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DAURORA_ENABLE_LTO=ON \
  -DAURORA_MARCH_NATIVE=ON
cmake --build build -j
```

Run:

```bash
./build/aurora_cli
./build/perft
./build/bench
./build/uci
```

## Use with a UCI-compatible GUI (Linux/macOS)

Set the GUI's engine command to the absolute path of `run_uci.sh` (for example,
`/path/to/ChessEngineCPP/run_uci.sh`) and select the UCI protocol. The script
builds the `uci` target if it is missing, then replaces itself with the engine
process so the GUI communicates with the engine over standard input/output.
Build output is sent to standard error, not the UCI protocol stream.

On Windows, select `build\Release\uci.exe` as the engine executable after
building with CMake.

## Build (Windows: MSVC)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Optional toggles (set once during configure):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DAURORA_ENABLE_LTO=ON
cmake --build build --config Release
```

Run (PowerShell):

```powershell
.\build\Release\aurora_cli.exe
.\build\Release\perft.exe
.\build\Release\bench.exe
.\build\Release\uci.exe
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

---
name: coca-toolchain
description: >-
  Activate and use the COCA C++ toolchain for building miki. Use when the user
  mentions coca, toolchain, build preset, Emscripten, wasm-emscripten, cross-compile,
  or needs to configure/activate the build environment.
---

# COCA Toolchain for miki

## Toolchain Location

```
C:\Users\nekom\Downloads\22\toolchains\coca-toolchain
```

## Quick Reference

### 1. Activate toolchain environment (PowerShell)

```powershell
python "C:\Users\nekom\Downloads\22\toolchains\coca-toolchain\setup.py" | Invoke-Expression
```

Sets env vars: `COCA_TOOLCHAIN`, `COCA_TOOLCHAIN_BIN`, `COCA_TOOLCHAIN_CMAKE`,
`EMSDK`, `EMSDK_NODE`, `EMSDK_PYTHON`, `EM_CONFIG`, and prepends tools to `PATH`.

### 2. Verify activation

```powershell
echo $env:COCA_TOOLCHAIN_CMAKE   # should print toolchain.cmake path
clang++ --version                 # should resolve from toolchain bin
cmake --version                   # should resolve from toolchain tools
```

### 3. Configure and build (miki presets)

| Preset | Profile | Build Dir | Command |
|--------|---------|-----------|---------|
| `debug` | `win-x64-clang` | `build/debug` | `cmake --preset debug` |
| `debug-vulkan` | `win-x64-clang` | `build/debug-vulkan` | `cmake --preset debug-vulkan` |
| `debug-d3d12` | `win-x64-clang` | `build/debug-d3d12` | `cmake --preset debug-d3d12` |
| `wasm-emscripten` | `wasm-emscripten` | `build/wasm` | `cmake --preset wasm-emscripten` |
| `release` | `win-x64-clang` | `build/release` | `cmake --preset release` |

After configure: `cmake --build <build_dir> [--target <target>]`

### 4. Emscripten / WebGPU specifics

The wasm-emscripten profile delegates to Emscripten.cmake from the bundled emsdk.

The miki toolchain wrapper (`cmake/toolchain/miki-coca.cmake`) includes the real COCA
toolchain via `$ENV{COCA_TOOLCHAIN_CMAKE}` and adds WinRT SDK paths for D3D12.

**Important**: Disable C++ module scanning for Emscripten builds:

```powershell
cmake --preset wasm-emscripten -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
```

### 5. Available target profiles

| Profile | Description |
|---------|-------------|
| `win-x64` | Windows x64, clang-cl driver |
| `win-x64-clang` | Windows x64, clang++ GNU driver (miki default) |
| `wasm-emscripten` | WebAssembly + WebGPU (browser) |
| `wasm-wasi` | WebAssembly WASI (standalone) |
| `linux-x64` | Linux x86_64 cross-compile |
| `linux-arm64` | Linux AArch64 cross-compile |

### 6. Troubleshooting

| Symptom | Fix |
|---------|-----|
| `COCA_TOOLCHAIN_CMAKE` empty | Re-run `setup.py` pipe `Invoke-Expression` |
| `'""' is not recognized` in wasm build | Add `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF` |
| Emscripten python path wrong | Check `$env:EMSDK_PYTHON` points to existing exe |
| `wrl/client.h` not found (D3D12) | miki-coca.cmake adds WinRT include; verify `COCA_TOOLCHAIN_ROOT` |
| CRT mismatch crash | miki-coca.cmake forces `/MD` (release CRT) for all build types |

### 7. coca_tools CLI (optional)

miki uses its own CMakePresets.json and does NOT use `coca_tools workspace build`.
Always use `cmake --preset <name>` followed by `cmake --build` for miki.

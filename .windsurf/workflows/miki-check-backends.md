---
description: Check different miki backends (WASM/WebGPU, Vulkan, D3D12, OpenGL) compile, run tests, and demos work correctly
---

# miki Backend Verification Workflow

> Verifies that miki builds, tests pass, and demos run on specified backends.
> Default: WASM/Emscripten + WebGPU. User can specify other backends.
>
> **Supported backends**:
> - `wasm-emscripten` — WebAssembly + WebGPU (Emscripten)
> - `debug` — Native (win-x64-clang, default backend)
> - `debug-vulkan` — Native + Vulkan
> - `debug-d3d12` — Native + D3D12 (Windows only)
> - `release` — Native Release build

---

## 1. Environment Setup

// turbo
1.1. Set Emscripten environment variables (required for WASM builds):
```powershell
$emsdkRoot = "C:\Users\nekom\Downloads\22\toolchains\coca-toolchain\tools\emsdk"
$env:EMSDK_PYTHON = "$emsdkRoot\python\3.13.3_64bit\python.exe"
$env:EM_CONFIG = "$emsdkRoot\.emscripten"
$env:EMSDK = $emsdkRoot
$env:EMSDK_NODE = "$emsdkRoot\node\22.16.0_64bit\bin\node.exe"
```

1.2. Determine target backend(s):
     - If user specified backend(s): use those
     - Default: `wasm-emscripten`
     - Multiple backends can be checked in sequence

---

## 2. WASM/Emscripten Backend

### 2.1 Configure

// turbo
```powershell
cmake --preset wasm-emscripten 2>&1
```

Expected: `-- Configuring done` with no errors.

### 2.2 Build

// turbo
```powershell
cmake --build build/wasm 2>&1
```

Expected: All targets compile. Warnings are acceptable, errors are not.

### 2.3 Verify Output Files

// turbo
```powershell
Get-ChildItem build/wasm -Recurse -Include "*.js","*.wasm" | Select-Object FullName, Length
```

Expected: `.js` and `.wasm` files generated for each executable target.

### 2.4 Node.js Test (CLI demos only)

> Note: Demos using browser APIs (tapioca terminal, WebGPU canvas) will fail in Node.js.
> This is expected. Only pure computation demos can run in Node.js.

// turbo
```powershell
& "$env:EMSDK_NODE" build/wasm/<demo>.js 2>&1
```

### 2.5 Browser Test

2.5.1. Create HTML wrapper if not exists:
```powershell
# Check for index.html in build/wasm/bin/
Test-Path build/wasm/bin/index.html
```

2.5.2. Start HTTP server:
```powershell
& "$env:EMSDK_PYTHON" -m http.server 8080
```
     Run from `build/wasm/bin/` directory.

2.5.3. Open browser preview at `http://localhost:8080`
     - Verify demo loads without console errors
     - Verify visual output (if applicable)

### 2.6 WebGPU Verification (Phase 3a+)

> Requires WebGPU-capable browser (Chrome 113+, Firefox 121+, Edge 113+).

- Check browser console for WebGPU initialization
- Verify `navigator.gpu` is available
- Verify adapter/device creation succeeds

---

## 3. Native Backend (debug / debug-vulkan / debug-d3d12)

### 3.1 Configure

// turbo
```powershell
cmake --preset <backend> 2>&1
```

### 3.2 Build

// turbo
```powershell
cmake --build build/<backend> 2>&1
```

### 3.3 Run Tests

// turbo
```powershell
ctest --test-dir build/<backend> --output-on-failure 2>&1
```

Expected: All tests pass (0 failures).

### 3.4 Run Demo

// turbo
```powershell
./build/<backend>/bin/<demo>.exe
```

Expected: Demo launches, runs for 5 seconds without crash.

---

## 4. Cross-Backend Comparison

> For Phase 3b+ with visual regression testing.

4.1. Capture reference image from native backend
4.2. Capture test image from WASM/WebGPU backend
4.3. Compare using golden image diff (threshold: 1% pixel difference)

---

## 5. Issue Resolution

If build or test failures occur:

5.1. **Compilation errors**:
     - Check for missing `#ifdef EMSCRIPTEN` guards
     - Check for platform-specific code paths
     - Verify third-party dependencies support WASM

5.2. **Runtime errors**:
     - Check for `std::filesystem` usage (limited in WASM)
     - Check for threading issues (WASM threading requires SharedArrayBuffer)
     - Check for browser API dependencies

5.3. **WebGPU errors**:
     - Verify emdawnwebgpu port is correctly linked
     - Check for Dawn vs emdawnwebgpu API differences
     - Verify shader compilation (WGSL)

5.4. **Fix and re-verify**:
     - Apply minimal fix
     - Re-run build + test
     - Confirm fix doesn't break other backends

---

## 6. Summary Report

Output verification summary table:

| Backend | Configure | Build | Tests | Demo | Notes |
|---------|-----------|-------|-------|------|-------|
| wasm-emscripten | PASS/FAIL | PASS/FAIL | N/A | PASS/FAIL | Browser-only demos |
| debug | PASS/FAIL | PASS/FAIL | PASS/FAIL | PASS/FAIL | — |
| debug-vulkan | PASS/FAIL | PASS/FAIL | PASS/FAIL | PASS/FAIL | — |
| debug-d3d12 | PASS/FAIL | PASS/FAIL | PASS/FAIL | PASS/FAIL | Windows only |

---

## 7. Quick Commands Reference

### WASM Full Check
```powershell
$emsdkRoot = "C:\Users\nekom\Downloads\22\toolchains\coca-toolchain\tools\emsdk"
$env:EMSDK_PYTHON = "$emsdkRoot\python\3.13.3_64bit\python.exe"
$env:EM_CONFIG = "$emsdkRoot\.emscripten"
$env:EMSDK = $emsdkRoot
$env:EMSDK_NODE = "$emsdkRoot\node\22.16.0_64bit\bin\node.exe"

cmake --preset wasm-emscripten
cmake --build build/wasm
```

### Native Full Check
```powershell
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

### Vulkan Full Check
```powershell
cmake --preset debug-vulkan
cmake --build build/debug-vulkan
ctest --test-dir build/debug-vulkan --output-on-failure
```

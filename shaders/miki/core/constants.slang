// core/constants.slang — Global shader constants (implementing miki_core).
//
// All engine-wide limits and tuning parameters live here.
// Link-time specialization constants use `extern static const` for vendor adaptation.

implementing miki_core;

// --- Engine limits ---
public static const uint kMaxInstances        = 10000000;  // 10M
public static const uint kMaxLights           = 4096;
public static const uint kMaxBindlessTextures = 1048576;   // 1M
public static const uint kMaxBindlessBuffers  = 262144;    // 256K
public static const uint kMaxBindlessSamplers = 2048;
public static const uint kMaxMeshlets         = 16777216;  // 16M

// --- Vendor-adaptive meshlet sizing via link-time specialization ---
// Default: NVidia-optimal (64V/126T). Override at link time for AMD (128V/256T).
//
// Rationale (2025 industry consensus):
//   NVidia: shader export allocates primitive indices in 128-byte groups.
//     126*3 = 378 bytes fits well. 64 vertices = 1:1 thread mapping at numthreads(64).
//   AMD RDNA3+: exp instruction supports wave-wide offset, 128 threads can export
//     256 primitives in 2 strides. 128V/256T maximizes vertex reuse and occupancy.
//   Intel Arc: same limits as VK_EXT_mesh_shader minimum (256/256), 64V/126T works.
//
// SlangCompiler generates override module:
//   export static const uint kMeshletMaxVertices = 128;   // AMD path
//   export static const uint kMeshletMaxPrimitives = 256; // AMD path
extern static const uint kMeshletMaxVertices      = 64;    // NVidia default
extern static const uint kMeshletMaxPrimitives    = 126;   // NVidia default (126*3=378B aligned)
extern static const uint kMeshShaderWorkgroupSize = 64;    // must == kMeshletMaxVertices for 1:1 mapping

// Task shader workgroup size: number of meshlets processed per task invocation.
// AMD recommends >= 32 to amortize DispatchMesh command processor latency.
extern static const uint kTaskShaderMeshletCount = 32;     // meshlets per task workgroup

// --- Misc constants ---
public static const uint  kClusterTileSize = 16;           // 16x16x24 light cluster grid
public static const float kPI              = 3.14159265358979323846;
public static const float kInvPI           = 0.31830988618379067154;
public static const float kEpsilon         = 1e-6;

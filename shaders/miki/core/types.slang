// core/types.slang — GPU data contract types (implementing miki_core).
//
// These structs have mirrored C++ definitions validated at build time
// via SlangCompiler::Reflect() -> StructLayout comparison with offsetof().
// Any layout mismatch is a hard error.

implementing miki_core;

/// GPU instance data — matches C++ GpuInstance exactly.
/// Layout validated by SlangCompiler::Reflect() in debug builds.
public struct GpuInstance {
    float4x4 modelMatrix;       // offset 0, size 64
    float4x4 normalMatrix;      // offset 64, size 64
    uint     materialIndex;     // offset 128, size 4
    uint     flags;             // offset 132, size 4
    float2   _pad0;             // offset 136, size 8  (explicit padding)
};                              // total: 144 bytes, align 16

/// GPU light data — matches C++ GpuLight.
public struct GpuLight {
    float3   position;          // offset 0
    float    range;             // offset 12
    float3   direction;         // offset 16
    float    spotAngle;         // offset 28
    float3   color;             // offset 32
    float    intensity;         // offset 44
    uint     type;              // offset 48 (0=point, 1=spot, 2=directional, 3=area)
    uint     shadowIndex;       // offset 52
    float2   _pad0;             // offset 56
};                              // total: 64 bytes, align 16

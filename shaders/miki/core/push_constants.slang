// core/push_constants.slang — Push constant abstraction (implementing miki_core).
//
// Cross-target push constants via __target_switch (§15.5.3).
// Each backend uses its native mechanism:
//   SPIRV  -> [[vk::push_constant]] OpVariable PushConstant (std430)
//   HLSL   -> root constants (cbuffer register(b0, space0))
//   Metal  -> [[buffer(0)]] argument buffer
//   WGSL   -> uniform at group(0) binding(0)
//   GLSL   -> uniform block at binding(0)

implementing miki_core;

public struct PushConstants {
    public float4x4 model;
    public uint materialIndex;
    public uint instanceId;
};

// Per-target global declarations.
// Slang's __target_switch only works inside function bodies, so we declare
// separate global resources for each target and the loadPushConstants()
// accessor abstracts the access pattern.

[[vk::push_constant]]
ConstantBuffer<PushConstants> _pushConstants_spirv;

ConstantBuffer<PushConstants> _pushConstants_hlsl;

ConstantBuffer<PushConstants> _pushConstants_ubo;

/// Load push constants from the backend-appropriate source.
/// All call sites use this function — never access globals directly.
[ForceInline]
public PushConstants loadPushConstants() {
    __target_switch {
    case spirv:
        // Native Vulkan push constants (VkPushConstantRange, std430 layout)
        return _pushConstants_spirv;
    case hlsl:
        // D3D12 root constants (cbuffer at register(b0, space0))
        return _pushConstants_hlsl;
    case metal:
        // Metal argument buffer at [[buffer(0)]]
        return _pushConstants_ubo;
    case wgsl:
        // WebGPU: emulated via UBO at group(0) binding(0), max 256B
        return _pushConstants_ubo;
    case glsl:
        // OpenGL: emulated via UBO at binding(0), max 128B
        return _pushConstants_ubo;
    }
}

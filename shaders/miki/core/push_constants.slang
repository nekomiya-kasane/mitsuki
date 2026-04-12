// core/push_constants.slang — Push constant abstraction (implementing miki_core).
//
// Slang natively handles push constants via [[vk::push_constant]] attribute.
// For SPIR-V: emitted as OpVariable with StorageClass PushConstant.
// For HLSL/DXIL: emitted as root constant (cbuffer at register(bN, spaceN)).
// For GLSL/WGSL/MSL: Phase 1b adds UBO / argument buffer fallback.

implementing miki_core;

public struct PushConstantsBlock {
    float4x4 model;
    uint materialIndex;
    uint instanceId;
};

// Global push constant declaration.
// Slang maps this to the correct mechanism per target:
//   SPIRV  -> OpVariable PushConstant (std430 layout)
//   HLSL   -> root constants / cbuffer
//   GLSL   -> uniform block (Phase 1b)
//   WGSL   -> uniform (Phase 1b)
//   MSL    -> argument buffer (Phase 1b)
[[vk::push_constant]]
public ConstantBuffer<PushConstantsBlock> pushConstants;

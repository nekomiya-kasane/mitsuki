// core/bindless.slang — Bindless resource access helpers (implementing miki_core).
//
// Set 3 is the global bindless table — fixed layout, never changes.
// Matches kBindlessLayout in C++ (RHI descriptor set layout).
//
// Tier1-only: requires descriptor indexing (NonUniformResourceIndex).
// Slang's capability system automatically infers requirements from
// NonUniformResourceIndex — no explicit [require] needed.
// Compat (GLSL/WGSL/MSL) passes that accidentally call these functions
// will get a Slang capability error at compile time (§15.4.3).

implementing miki_core;

// Bindless descriptor arrays at set 3
[[vk::binding(0, 3)]] Texture2D<float4>     globalTextures[];
[[vk::binding(1, 3)]] StructuredBuffer<uint> globalBuffers[];
[[vk::binding(2, 3)]] SamplerState           globalSamplers[];

/// Type-safe bindless texture fetch.
/// Tier1-only: Slang infers descriptor indexing capability from NonUniformResourceIndex.
public float4 sampleBindlessTexture(uint textureIndex, uint samplerIndex, float2 uv) {
    return globalTextures[NonUniformResourceIndex(textureIndex)]
        .Sample(globalSamplers[NonUniformResourceIndex(samplerIndex)], uv);
}

/// Type-safe bindless buffer load.
/// Tier1-only: Slang infers descriptor indexing capability from NonUniformResourceIndex.
public uint loadBindlessBufferUint(uint bufferIndex, uint elementIndex) {
    return globalBuffers[NonUniformResourceIndex(bufferIndex)][elementIndex];
}

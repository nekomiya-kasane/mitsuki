// core/bindless.slang — Bindless resource access helpers (implementing miki_core).
//
// Set 3 is the global bindless table — fixed layout, never changes.
// Matches kBindlessLayout in C++ (RHI descriptor set layout).
//
// Phase 1a: declarations + stub implementations.
// Phase 3b: full implementation with NonUniformResourceIndex.

implementing miki_core;

// Bindless descriptor arrays at set 3
[[vk::binding(0, 3)]] Texture2D<float4>     globalTextures[];
[[vk::binding(1, 3)]] StructuredBuffer<uint> globalBuffers[];
[[vk::binding(2, 3)]] SamplerState           globalSamplers[];

/// Type-safe bindless texture fetch.
public float4 sampleBindlessTexture(uint textureIndex, uint samplerIndex, float2 uv) {
    return globalTextures[NonUniformResourceIndex(textureIndex)]
        .Sample(globalSamplers[NonUniformResourceIndex(samplerIndex)], uv);
}

/// Type-safe bindless buffer load.
public uint loadBindlessBufferUint(uint bufferIndex, uint elementIndex) {
    return globalBuffers[NonUniformResourceIndex(bufferIndex)][elementIndex];
}

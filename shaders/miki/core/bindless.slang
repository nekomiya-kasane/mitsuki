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

/// Type-safe bindless buffer load (uint).
/// Tier1-only: Slang infers descriptor indexing capability from NonUniformResourceIndex.
public uint loadBindlessBufferUint(uint bufferIndex, uint elementIndex) {
    return globalBuffers[NonUniformResourceIndex(bufferIndex)][elementIndex];
}

/// Bindless texture sample with explicit LOD (mip level).
public float4 sampleBindlessTextureLod(uint textureIndex, uint samplerIndex, float2 uv, float lod) {
    return globalTextures[NonUniformResourceIndex(textureIndex)]
        .SampleLevel(globalSamplers[NonUniformResourceIndex(samplerIndex)], uv, lod);
}

/// Bindless texture load at integer coordinates (no filtering).
public float4 loadBindlessTexture(uint textureIndex, int2 coord, int mipLevel) {
    return globalTextures[NonUniformResourceIndex(textureIndex)].Load(int3(coord, mipLevel));
}

/// Bindless buffer load returning 4 consecutive uint values (for structured data).
public uint4 loadBindlessBufferUint4(uint bufferIndex, uint elementIndex) {
    uint idx = NonUniformResourceIndex(bufferIndex);
    return uint4(
        globalBuffers[idx][elementIndex],
        globalBuffers[idx][elementIndex + 1u],
        globalBuffers[idx][elementIndex + 2u],
        globalBuffers[idx][elementIndex + 3u]
    );
}

/// Bindless buffer load returning float (reinterpret uint bits as float).
public float loadBindlessBufferFloat(uint bufferIndex, uint elementIndex) {
    return asfloat(globalBuffers[NonUniformResourceIndex(bufferIndex)][elementIndex]);
}

/// Bindless buffer load returning float4 (4 consecutive floats).
public float4 loadBindlessBufferFloat4(uint bufferIndex, uint elementIndex) {
    uint idx = NonUniformResourceIndex(bufferIndex);
    return float4(
        asfloat(globalBuffers[idx][elementIndex]),
        asfloat(globalBuffers[idx][elementIndex + 1u]),
        asfloat(globalBuffers[idx][elementIndex + 2u]),
        asfloat(globalBuffers[idx][elementIndex + 3u])
    );
}

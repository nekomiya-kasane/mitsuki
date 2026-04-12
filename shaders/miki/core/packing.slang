// core/packing.slang — Data packing/unpacking utilities (implementing miki_core).
//
// R11G11B10 float packing for HDR lighting, octahedral normal encode/decode
// for GBuffer normal compression.

implementing miki_core;

// --- R11G11B10 Float Pack/Unpack ---
// Layout: [10:0] R mantissa+exp, [20:11] G mantissa+exp, [31:21] B mantissa+exp
// Used for HDR lighting accumulation buffers.

/// Pack linear float3 color into R11G11B10 uint.
public uint packR11G11B10(float3 color) {
    color = max(color, float3(0.0));
    uint r = min(uint(color.r * 2047.0 + 0.5), 2047u);
    uint g = min(uint(color.g * 2047.0 + 0.5), 2047u);
    uint b = min(uint(color.b * 1023.0 + 0.5), 1023u);
    return r | (g << 11) | (b << 22);
}

/// Unpack R11G11B10 uint to linear float3 color.
public float3 unpackR11G11B10(uint packed) {
    float r = float(packed & 0x7FFu) / 2047.0;
    float g = float((packed >> 11) & 0x7FFu) / 2047.0;
    float b = float((packed >> 22) & 0x3FFu) / 1023.0;
    return float3(r, g, b);
}

// --- Octahedral Normal Encode/Decode ---
// Encodes unit normal (float3) to 2D octahedral coordinate (float2) in [-1, 1].
// Used for GBuffer normal compression (R16G16 SNORM).
// Reference: Cigolle et al. 2014, "Survey of Efficient Representations for
// Independent Unit Vectors"

/// Encode unit normal to octahedral float2 in [-1, 1].
public float2 encodeOctahedral(float3 n) {
    float l1 = abs(n.x) + abs(n.y) + abs(n.z);
    float2 oct = n.xy * (1.0 / l1);
    if (n.z < 0.0) {
        oct = (1.0 - abs(oct.yx)) * float2(oct.x >= 0.0 ? 1.0 : -1.0, oct.y >= 0.0 ? 1.0 : -1.0);
    }
    return oct;
}

/// Decode octahedral float2 in [-1, 1] to unit normal.
public float3 decodeOctahedral(float2 oct) {
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

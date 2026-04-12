// math/noise.slang — Procedural noise functions (implementing miki_math).
//
// Provides GPU-friendly noise for procedural textures, terrain, effects.
// All functions are pure and deterministic.

implementing miki_math;

// Hash helper: Wang hash for integer noise
[ForceInline]
public uint wangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

// Convert hash to [0, 1) float
[ForceInline]
public float hashToFloat(uint h) {
    return float(h) * (1.0f / 4294967296.0f);
}

// 3D gradient table (12 edges of a cube)
static const float3 kGrad3[12] = {
    float3(1, 1, 0), float3(-1, 1, 0), float3(1, -1, 0), float3(-1, -1, 0),
    float3(1, 0, 1), float3(-1, 0, 1), float3(1, 0, -1), float3(-1, 0, -1),
    float3(0, 1, 1), float3(0, -1, 1), float3(0, 1, -1), float3(0, -1, -1)
};

// Fade curve: 6t^5 - 15t^4 + 10t^3 (Perlin improved)
[ForceInline]
public float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/// 3D Perlin noise, range approximately [-1, 1].
/// Uses improved gradient noise with quintic interpolation.
public float perlinNoise3D(float3 p) {
    float3 i = floor(p);
    float3 f = p - i;
    int3 ip = int3(i) & 255;

    float u = fade(f.x);
    float v = fade(f.y);
    float w = fade(f.z);

    // Hash corners
    uint h000 = wangHash(uint(ip.x) + wangHash(uint(ip.y) + wangHash(uint(ip.z))));
    uint h100 = wangHash(uint(ip.x + 1) + wangHash(uint(ip.y) + wangHash(uint(ip.z))));
    uint h010 = wangHash(uint(ip.x) + wangHash(uint(ip.y + 1) + wangHash(uint(ip.z))));
    uint h110 = wangHash(uint(ip.x + 1) + wangHash(uint(ip.y + 1) + wangHash(uint(ip.z))));
    uint h001 = wangHash(uint(ip.x) + wangHash(uint(ip.y) + wangHash(uint(ip.z + 1))));
    uint h101 = wangHash(uint(ip.x + 1) + wangHash(uint(ip.y) + wangHash(uint(ip.z + 1))));
    uint h011 = wangHash(uint(ip.x) + wangHash(uint(ip.y + 1) + wangHash(uint(ip.z + 1))));
    uint h111 = wangHash(uint(ip.x + 1) + wangHash(uint(ip.y + 1) + wangHash(uint(ip.z + 1))));

    // Gradient dot products
    float g000 = dot(kGrad3[h000 % 12u], f - float3(0, 0, 0));
    float g100 = dot(kGrad3[h100 % 12u], f - float3(1, 0, 0));
    float g010 = dot(kGrad3[h010 % 12u], f - float3(0, 1, 0));
    float g110 = dot(kGrad3[h110 % 12u], f - float3(1, 1, 0));
    float g001 = dot(kGrad3[h001 % 12u], f - float3(0, 0, 1));
    float g101 = dot(kGrad3[h101 % 12u], f - float3(1, 0, 1));
    float g011 = dot(kGrad3[h011 % 12u], f - float3(0, 1, 1));
    float g111 = dot(kGrad3[h111 % 12u], f - float3(1, 1, 1));

    // Trilinear interpolation
    float x00 = lerp(g000, g100, u);
    float x10 = lerp(g010, g110, u);
    float x01 = lerp(g001, g101, u);
    float x11 = lerp(g011, g111, u);
    float y0 = lerp(x00, x10, v);
    float y1 = lerp(x01, x11, v);
    return lerp(y0, y1, w);
}

/// Fractal Brownian Motion (fBm) using Perlin noise.
/// @param octaves Number of noise octaves (1-8 recommended).
/// @param lacunarity Frequency multiplier per octave (typically 2.0).
/// @param gain Amplitude multiplier per octave (typically 0.5).
public float fbm(float3 p, int octaves, float lacunarity, float gain) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        value += amplitude * perlinNoise3D(p * frequency);
        maxAmplitude += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return value / maxAmplitude;
}

/// Blue noise dithering value from screen-space coords and frame index.
/// Uses interleaved gradient noise (Jimenez 2014, "Next Generation Post Processing in Call of Duty").
[ForceInline]
public float interleavedGradientNoise(float2 screenPos, uint frameIndex) {
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    float2 pos = screenPos + float2(frameIndex) * 5.588238f;
    return frac(magic.z * frac(dot(pos, magic.xy)));
}

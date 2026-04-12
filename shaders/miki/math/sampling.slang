// math/sampling.slang — Low-discrepancy sampling sequences (implementing miki_math).
//
// Hammersley, Halton, cosine-weighted hemisphere, GGX importance sampling.
// Used by IBL prefiltering, BRDF integration, and AO.
// Reference: Holger Dammertz, "Hammersley Points on the Hemisphere" (2012).

implementing miki_math;

static const float kPI = 3.14159265358979323846f;
static const float kTwoPI = 6.28318530717958647693f;
static const float kInvPI = 0.31830988618379067154f;

/// Radical inverse (Van der Corput) in base 2, using bit manipulation.
[ForceInline]
public float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // 1 / 2^32
}

/// Hammersley 2D point set. i in [0, N), N = total sample count.
[ForceInline]
public float2 hammersley(uint i, uint N) {
    return float2(float(i) / float(N), radicalInverseVdC(i));
}

/// Halton sequence value for given index and base.
public float halton(uint index, uint base) {
    float result = 0.0f;
    float f = 1.0f / float(base);
    uint i = index;
    while (i > 0u) {
        result += f * float(i % base);
        i /= base;
        f /= float(base);
    }
    return result;
}

/// Halton 2D: base-2 and base-3 for well-distributed 2D points.
[ForceInline]
public float2 halton2D(uint index) {
    return float2(radicalInverseVdC(index), halton(index, 3u));
}

/// Cosine-weighted hemisphere sampling from uniform 2D input.
/// Returns direction in tangent space (z-up).
[ForceInline]
public float3 sampleCosineHemisphere(float2 xi) {
    float phi = kTwoPI * xi.x;
    float cosTheta = sqrt(1.0f - xi.y);
    float sinTheta = sqrt(xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

/// Uniform hemisphere sampling.
[ForceInline]
public float3 sampleUniformHemisphere(float2 xi) {
    float phi = kTwoPI * xi.x;
    float cosTheta = xi.y;
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

/// Uniform sphere sampling.
[ForceInline]
public float3 sampleUniformSphere(float2 xi) {
    float phi = kTwoPI * xi.x;
    float cosTheta = 1.0f - 2.0f * xi.y;
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

/// GGX (Trowbridge-Reitz) importance sampling in tangent space.
/// Input: xi = uniform 2D, roughness = perceptual roughness.
/// Returns: half-vector H in tangent space.
[ForceInline]
public float3 importanceSampleGGX(float2 xi, float roughness) {
    float a = roughness * roughness;
    float phi = kTwoPI * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

/// Build orthonormal basis from a normal vector (Frisvad 2012, revised Duff et al. 2017).
[ForceInline]
public void buildOrthonormalBasis(float3 n, out float3 tangent, out float3 bitangent) {
    float s = (n.z >= 0.0f) ? 1.0f : -1.0f;
    float a = -1.0f / (s + n.z);
    float b = n.x * n.y * a;
    tangent = float3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
    bitangent = float3(b, s + n.y * n.y * a, -n.y);
}

/// Transform tangent-space direction to world space given a normal.
[ForceInline]
public float3 tangentToWorld(float3 localDir, float3 N) {
    float3 T, B;
    buildOrthonormalBasis(N, T, B);
    return T * localDir.x + B * localDir.y + N * localDir.z;
}

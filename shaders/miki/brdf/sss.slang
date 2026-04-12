// brdf/sss.slang — Separable subsurface scattering (SSS).
//
// Burley's normalized diffusion profile for skin, wax, marble, etc.
// Uses separable convolution in screen space for real-time performance.
//
// Reference: Burley 2015 "Extending the Disney BRDF to a BSDF with
//            Integrated Subsurface Scattering"
//            Jimenez et al. 2015 "Separable Subsurface Scattering"

implementing miki_brdf;

/// Burley normalized diffusion profile R(r).
/// scatterDistance: mean free path length (controls SSS radius).
/// r: distance from light entry point.
public float3 burleyDiffusionProfile(float3 scatterDistance, float r) {
    // R(r) = A * exp(-r/d) + B * exp(-r/(3d))
    // where d = scatterDistance, A = 0.25/pi/d, B = 0.25/pi/3d
    float3 d = max(scatterDistance, float3(0.001));
    float3 rOverD = float3(r) / d;
    float3 falloffNear = exp(-rOverD);
    float3 falloffFar  = exp(-rOverD / 3.0) / 3.0;
    return (falloffNear + falloffFar) / (8.0 * 3.14159265 * d);
}

/// Sample weight for separable SSS kernel (precomputed LUT approach).
/// sampleIndex: 0..kMaxSSSamples-1.
/// totalSamples: number of samples in the kernel.
public float sssSampleWeight(int sampleIndex, int totalSamples) {
    // Gaussian-like falloff for the kernel weights
    float t = float(sampleIndex) / float(max(totalSamples - 1, 1));
    float sigma = 0.3;
    return exp(-t * t / (2.0 * sigma * sigma));
}

/// Transmittance approximation for thin objects.
/// thickness: local thickness (from thickness map or SDF).
/// scatterDistance: mean free path.
public float3 thinTransmittance(float thickness, float3 scatterDistance) {
    float3 d = max(scatterDistance, float3(0.001));
    return exp(-float3(thickness) / d);
}

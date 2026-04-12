// rt/denoiser.slang — Temporal + spatial denoiser for RT output.
//
// Implements SVGF-family denoising:
// 1. Temporal accumulation with reprojection and variance tracking.
// 2. Spatial a-trous wavelet filter guided by normals + depth.
// 3. Variance-driven adaptive filter radius.
//
// Designed for 1spp RT input (shadows, reflections, GI), producing
// temporally stable output suitable for real-time display.
//
// Reference: Schied et al. (2017) "SVGF", Christensen (2018) "BMFR",
//            AMD FidelityFX Denoiser, NVIDIA NRD.
//
// Performance target: < 1.5ms at 1080p for combined temporal + 5-iteration spatial.

implementing miki_rt;

import miki_core;

/// Denoiser parameters.
public struct DenoiserParams {
    public float temporalAlpha;     // Base temporal blend factor (default: 0.05)
    public float depthThreshold;    // Depth similarity threshold for reprojection
    public float normalThreshold;   // Normal similarity threshold (dot product)
    public float phiColor;          // Color edge-stopping sensitivity
    public float phiNormal;         // Normal edge-stopping sensitivity
    public float phiDepth;          // Depth edge-stopping sensitivity
    public uint  atrousIterations;  // Number of a-trous filter passes (default: 5)
};

/// Compute temporal reprojection UV from motion vector.
/// currentUv: current frame UV [0,1]^2.
/// motionVector: screen-space motion vector (in UV space).
public float2 reprojectUv(float2 currentUv, float2 motionVector) {
    return currentUv - motionVector;
}

/// Validate temporal reprojection: check if history sample is still valid.
/// Returns true if the history sample can be used.
public bool validateReprojection(
    float currentDepth,
    float historyDepth,
    float3 currentNormal,
    float3 historyNormal,
    float depthThreshold,
    float normalThreshold
) {
    float depthDiff = abs(currentDepth - historyDepth) / max(currentDepth, 1e-6);
    float normalDot = dot(currentNormal, historyNormal);
    return depthDiff < depthThreshold && normalDot > normalThreshold;
}

/// Temporal accumulation with variance tracking.
/// currentColor: current frame noisy input.
/// historyColor: previous frame accumulated result.
/// historyMoments: float2(mean, meanSquared) from previous frame.
/// alpha: temporal blend factor (adjusted by reprojection confidence).
/// Returns: float4(accumulatedColor.rgb, variance).
public float4 temporalFilter(
    float3 currentColor,
    float3 historyColor,
    float2 historyMoments,
    float alpha
) {
    float3 blended = lerp(historyColor, currentColor, alpha);
    float lum = dot(currentColor, float3(0.2126, 0.7152, 0.0722));
    float mean = lerp(historyMoments.x, lum, alpha);
    float meanSq = lerp(historyMoments.y, lum * lum, alpha);
    float variance = max(meanSq - mean * mean, 0.0);
    return float4(blended, variance);
}

/// Edge-stopping weight for a-trous spatial filter.
/// Combines depth, normal, and color similarity.
public float edgeStoppingWeight(
    float3 centerNormal,
    float3 sampleNormal,
    float centerDepth,
    float sampleDepth,
    float3 centerColor,
    float3 sampleColor,
    float phiNormal,
    float phiDepth,
    float phiColor
) {
    // Normal weight
    float normalW = pow(max(dot(centerNormal, sampleNormal), 0.0), phiNormal);

    // Depth weight
    float depthDiff = abs(centerDepth - sampleDepth);
    float depthW = exp(-depthDiff / max(phiDepth * centerDepth, 1e-6));

    // Color / luminance weight
    float3 colorDiff = centerColor - sampleColor;
    float colorDist = dot(colorDiff, colorDiff);
    float colorW = exp(-colorDist / max(phiColor, 1e-6));

    return normalW * depthW * colorW;
}

/// A-trous wavelet filter kernel weights (5x5 base pattern).
/// Returns the 1D kernel weight for a given offset.
/// The 2D weight is kernel1D[dx] * kernel1D[dy].
public float atrousKernel1D(int offset) {
    // [1/16, 4/16, 6/16, 4/16, 1/16] for 5-tap
    switch (abs(offset)) {
        case 0: return 6.0 / 16.0;
        case 1: return 4.0 / 16.0;
        case 2: return 1.0 / 16.0;
        default: return 0.0;
    }
}

/// Compute the step size for a-trous iteration level.
/// iteration: 0-based iteration index.
/// Returns pixel step size (1, 2, 4, 8, 16, ...).
public int atrousStepSize(uint iteration) {
    return 1 << iteration;
}

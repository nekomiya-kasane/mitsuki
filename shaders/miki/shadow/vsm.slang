// shadow/vsm.slang — Variance Shadow Maps (implementing miki_shadow).
//
// VSM stores depth moments (mean, variance) in a 2-channel texture.
// Chebyshev's inequality provides a smooth probabilistic shadow factor.
// Better filtering than PCF (bilinear/trilinear safe), but light bleeding
// on overlapping occluders requires moment clamping or EVSM.

implementing miki_shadow;

import miki_core;

/// Store depth moments for VSM. xy = (depth, depth^2).
[ForceInline]
public float2 computeVSMMoments(float depth) {
    return float2(depth, depth * depth);
}

/// Evaluate VSM shadow factor using Chebyshev's inequality.
/// moments.x = E[z], moments.y = E[z^2]. receiverDepth = fragment depth.
/// Returns [0=shadow, 1=lit].
[ForceInline]
public float evaluateVSM(float2 moments, float receiverDepth, float minVariance) {
    float mean = moments.x;
    float variance = max(moments.y - mean * mean, minVariance);

    // Chebyshev: P(z >= t) <= variance / (variance + (t - mean)^2)
    float d = receiverDepth - mean;
    if (d <= 0.0f) return 1.0f;  // Fully lit (receiver is closer than mean occluder)

    float pMax = variance / (variance + d * d);
    return pMax;
}

/// Exponential VSM for reduced light bleeding.
/// warpedDepth = exp(c * depth). Use c ∈ [40, 80] typically.
[ForceInline]
public float2 computeEVSMMoments(float depth, float exponent) {
    float warped = exp(exponent * depth);
    return float2(warped, warped * warped);
}

/// Evaluate EVSM shadow factor.
[ForceInline]
public float evaluateEVSM(float2 moments, float receiverDepth, float exponent, float minVariance) {
    float warpedReceiver = exp(exponent * receiverDepth);
    return evaluateVSM(moments, warpedReceiver, minVariance);
}

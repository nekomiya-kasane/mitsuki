// lighting/restir.slang — ReSTIR Direct/Global Illumination (implementing miki_lighting).
//
// Reservoir-based Spatiotemporal Importance Resampling (Bitterli et al. 2020).
// Key insight: maintain per-pixel reservoirs with weighted samples,
// resampling spatially and temporally for high-quality many-light sampling
// with just 1 shadow ray per pixel.

implementing miki_lighting;

import miki_core;
import miki_math;

/// Reservoir for importance resampling.
/// Stores the selected sample, its weight sum, and sample count.
public struct Reservoir {
    public uint selectedLightIndex;  // index of the selected light
    public float weightSum;          // sum of all considered weights
    public uint sampleCount;         // M: number of samples seen so far
    public float selectedTargetPdf;  // p_hat(y) of the selected sample
};

/// Initialize an empty reservoir.
[ForceInline]
public Reservoir createEmptyReservoir() {
    Reservoir r;
    r.selectedLightIndex = 0xFFFFFFFFu;
    r.weightSum = 0.0f;
    r.sampleCount = 0u;
    r.selectedTargetPdf = 0.0f;
    return r;
}

/// Update a reservoir with a new candidate sample.
/// Returns true if the new sample was selected (reservoir updated).
[ForceInline]
public bool updateReservoir(inout Reservoir r, uint lightIndex, float weight, float targetPdf, float randomValue) {
    r.weightSum += weight;
    r.sampleCount += 1u;
    if (randomValue * r.weightSum < weight) {
        r.selectedLightIndex = lightIndex;
        r.selectedTargetPdf = targetPdf;
        return true;
    }
    return false;
}

/// Compute the unbiased contribution weight (1/p) for the reservoir's selected sample.
[ForceInline]
public float computeReservoirWeight(Reservoir r) {
    if (r.sampleCount == 0u || r.selectedTargetPdf <= 0.0f) return 0.0f;
    return r.weightSum / (float(r.sampleCount) * r.selectedTargetPdf);
}

/// Combine two reservoirs (spatial or temporal resampling).
/// Merges reservoir b into reservoir a.
[ForceInline]
public void combineReservoirs(inout Reservoir a, Reservoir b, float targetPdfB, float randomValue) {
    float weight = targetPdfB * computeReservoirWeight(b) * float(b.sampleCount);
    bool updated = updateReservoir(a, b.selectedLightIndex, weight, targetPdfB, randomValue);
    // M is additive
    a.sampleCount = a.sampleCount + b.sampleCount - 1u;  // -1 because updateReservoir already incremented
}

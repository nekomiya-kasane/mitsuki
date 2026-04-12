// cae/scalar_field.slang — Scalar and vector field visualization.
//
// Renders scalar fields as colormapped surfaces or volume slices.
// Renders vector fields as arrow glyphs, hedgehog plots, or LIC
// (Line Integral Convolution) textures.
//
// Reference: ParaView Glyph filter, VTK vtkArrowSource, VTK LIC2D.

implementing miki_cae;

import miki_core;

/// Arrow glyph parameters for vector field rendering.
public struct ArrowGlyphParams {
    public float shaftLength;    // Arrow shaft length scale
    public float shaftRadius;    // Arrow shaft radius
    public float headLength;     // Arrow head (cone) length as fraction of total
    public float headRadius;     // Arrow head radius
    public float minMagnitude;   // Minimum magnitude to show arrow
    public float maxMagnitude;   // Maximum magnitude for scaling
    public bool  colorByMagnitude; // If true, color by vector magnitude; else direction
};

/// Compute arrow transformation matrix for a vector glyph.
/// position: glyph center in world space.
/// direction: vector direction (not necessarily normalized).
/// scale: overall scale factor.
/// Returns a 4x4 matrix that transforms a unit arrow [0,0,0]->[0,0,1] to world space.
public float4x4 arrowGlyphTransform(float3 position, float3 direction, float scale) {
    float mag = length(direction);
    if (mag < 1e-10) return float4x4(0.0);

    float3 forward = direction / mag;
    float3 up = abs(forward.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(up, forward));
    up = cross(forward, right);

    float s = scale * mag;
    return float4x4(
        float4(right * s, 0.0),
        float4(up * s, 0.0),
        float4(forward * s, 0.0),
        float4(position, 1.0)
    );
}

/// Color a vector by its direction (normalize to RGB).
public float3 directionToColor(float3 direction) {
    return normalize(direction) * 0.5 + 0.5;
}

/// Color a scalar field value using the viridis colormap approximation.
/// More perceptually uniform than turbo for scientific data.
public float3 colormapViridis(float t) {
    t = saturate(t);
    float3 c0 = float3(0.267004, 0.004874, 0.329415);
    float3 c1 = float3(0.282327, 0.140926, 0.457517);
    float3 c2 = float3(0.253935, 0.265254, 0.529983);
    float3 c3 = float3(0.206756, 0.371758, 0.553117);
    float3 c4 = float3(0.163625, 0.471133, 0.558148);
    float3 c5 = float3(0.127568, 0.566949, 0.550556);
    float3 c6 = float3(0.134692, 0.658636, 0.517649);
    float3 c7 = float3(0.266941, 0.748751, 0.440573);
    float3 c8 = float3(0.477504, 0.821444, 0.318195);
    float3 c9 = float3(0.741388, 0.873449, 0.149561);
    float3 c10 = float3(0.993248, 0.906157, 0.143936);

    float s = t * 10.0;
    uint i = uint(min(s, 9.0));
    float f = frac(s);

    float3 colors[11] = { c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10 };
    return lerp(colors[i], colors[min(i + 1u, 10u)], f);
}

/// Compute 2D LIC (Line Integral Convolution) for vector field on a surface.
/// Simplified version: traces streamline forward/backward N steps, averages noise.
/// uv: surface parameterization.
/// velocity: 2D velocity at this point (in UV space).
/// noiseTex: white noise texture.
/// noiseSampler: sampler for noise.
/// stepSize: integration step size in UV space.
/// numSteps: number of integration steps in each direction.
public float licIntensity(
    float2 uv,
    float2 velocity,
    Texture2D<float> noiseTex,
    SamplerState noiseSampler,
    float stepSize,
    uint numSteps
) {
    float sum = noiseTex.SampleLevel(noiseSampler, uv, 0).r;
    float totalWeight = 1.0;

    float2 pos = uv;
    for (uint i = 0; i < numSteps; i++) {
        pos += normalize(velocity) * stepSize;
        float w = 1.0 - float(i) / float(numSteps); // Distance falloff
        sum += noiseTex.SampleLevel(noiseSampler, pos, 0).r * w;
        totalWeight += w;
    }

    pos = uv;
    for (uint i = 0; i < numSteps; i++) {
        pos -= normalize(velocity) * stepSize;
        float w = 1.0 - float(i) / float(numSteps);
        sum += noiseTex.SampleLevel(noiseSampler, pos, 0).r * w;
        totalWeight += w;
    }

    return sum / max(totalWeight, 1e-6);
}

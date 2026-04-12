// cae/streamline.slang — Streamline and pathline tracing for CFD visualization.
//
// GPU-side streamline integration using RK4 (Runge-Kutta 4th order).
// Generates ribbon/tube geometry from seed points through a velocity field.
//
// Supports:
// - Streamlines (steady-state flow)
// - Pathlines (time-varying flow with temporal interpolation)
// - Streaklines (all particles passing through a seed point)
//
// Reference: ParaView StreamTracer, VTK vtkStreamTracer, Runge-Kutta integration.

implementing miki_cae;

import miki_core;

/// Streamline integration parameters.
public struct StreamlineParams {
    public float stepSize;           // Integration step size in world units
    public uint  maxSteps;           // Maximum integration steps per seed
    public float terminationSpeed;   // Stop if velocity magnitude below this threshold
    public float maxLength;          // Maximum streamline arc length
};

/// Sample a 3D velocity field from a structured grid stored in a 3D texture.
/// velocityField: Texture3D<float4> where .xyz = velocity, .w = magnitude.
/// fieldSampler: trilinear sampler.
/// worldPos: sample position in world space.
/// fieldOrigin: world-space origin of the velocity field grid.
/// fieldScale: world-space extent of the grid (inverse maps world -> [0,1]^3).
public float3 sampleVelocityField(
    Texture3D<float4> velocityField,
    SamplerState fieldSampler,
    float3 worldPos,
    float3 fieldOrigin,
    float3 fieldScale
) {
    float3 uvw = (worldPos - fieldOrigin) / max(fieldScale, float3(1e-10));
    if (any(uvw < 0.0) || any(uvw > 1.0)) return float3(0.0); // Out of bounds
    return velocityField.SampleLevel(fieldSampler, uvw, 0).xyz;
}

/// Single RK4 integration step.
/// Returns the new position after one step.
public float3 rk4Step(
    float3 pos,
    Texture3D<float4> velocityField,
    SamplerState fieldSampler,
    float3 fieldOrigin,
    float3 fieldScale,
    float dt
) {
    float3 k1 = sampleVelocityField(velocityField, fieldSampler, pos, fieldOrigin, fieldScale);
    float3 k2 = sampleVelocityField(velocityField, fieldSampler, pos + k1 * (dt * 0.5), fieldOrigin, fieldScale);
    float3 k3 = sampleVelocityField(velocityField, fieldSampler, pos + k2 * (dt * 0.5), fieldOrigin, fieldScale);
    float3 k4 = sampleVelocityField(velocityField, fieldSampler, pos + k3 * dt, fieldOrigin, fieldScale);
    return pos + (k1 + 2.0 * k2 + 2.0 * k3 + k4) * (dt / 6.0);
}

/// Compute tangent direction for ribbon/tube expansion.
/// prevPos, curPos, nextPos: three consecutive streamline points.
/// Returns the normalized tangent at curPos.
public float3 streamlineTangent(float3 prevPos, float3 curPos, float3 nextPos) {
    float3 t0 = curPos - prevPos;
    float3 t1 = nextPos - curPos;
    return normalize(t0 + t1);
}

/// Compute ribbon normal (perpendicular to tangent and a reference "up" direction).
/// tangent: streamline tangent direction.
/// upHint: approximate up direction (e.g. world Y).
public float3 ribbonNormal(float3 tangent, float3 upHint) {
    float3 binormal = normalize(cross(tangent, upHint));
    return normalize(cross(binormal, tangent));
}

/// Color a streamline point by local velocity magnitude.
/// speed: length of velocity vector.
/// minSpeed, maxSpeed: colormap range.
public float3 streamlineSpeedColor(float speed, float minSpeed, float maxSpeed) {
    float t = saturate((speed - minSpeed) / max(maxSpeed - minSpeed, 1e-6));
    // Cool-warm diverging colormap (blue -> white -> red)
    float3 cool = float3(0.23, 0.30, 0.75);
    float3 warm = float3(0.70, 0.02, 0.15);
    float3 mid = float3(0.87, 0.87, 0.87);
    return t < 0.5 ? lerp(cool, mid, t * 2.0) : lerp(mid, warm, (t - 0.5) * 2.0);
}

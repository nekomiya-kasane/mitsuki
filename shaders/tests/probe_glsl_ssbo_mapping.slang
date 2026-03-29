// Probe: GLSL SSBO mapping.
// Validates that StructuredBuffer maps to SSBO in GLSL 4.30 output.
// BDA (buffer device address) is NOT available in GLSL 4.30 — must use SSBO.

struct Particle {
    float3 position;
    float  mass;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Particle> particles;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    Particle p = particles[tid.x];
    output[tid.x] = length(p.position) * p.mass;
}

// Probe: image atomics (RWTexture2D atomic operations).
// Validates InterlockedAdd on RWTexture2D<uint>.

[[vk::binding(0, 0)]]
RWTexture2D<uint> atomicImage;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint original;
    InterlockedAdd(atomicImage[tid.xy], 1u, original);
}

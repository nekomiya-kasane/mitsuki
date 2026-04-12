// depth_prepass.slang — Depth pre-pass (vertex + fragment).
// Renders geometry to the depth buffer only, for HiZ generation and early-Z.
import miki_geometry;

struct VsInput {
    float3 position : POSITION;
    uint instanceId : SV_InstanceID;
};

struct VsOutput {
    float4 clipPos : SV_Position;
};

[shader("vertex")]
VsOutput vsMain(VsInput input) {
    VsOutput output;
    output.clipPos = float4(input.position, 1.0);
    return output;
}

[shader("fragment")]
void fsMain(VsOutput input) {
    // Depth-only: no color output.
}

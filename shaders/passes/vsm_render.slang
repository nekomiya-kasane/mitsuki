// vsm_render.slang — Virtual Shadow Map render pass.
// Renders geometry from light perspective for VSM page allocation.
import miki_shadow;

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
    // Shadow-map depth-only rendering.
}

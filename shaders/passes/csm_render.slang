// csm_render.slang — Cascaded Shadow Map render pass (compat vertex pipeline).
// Renders geometry for CSM cascade splits.
import miki_shadow;

struct VsInput {
    float3 position : POSITION;
    uint instanceId : SV_InstanceID;
};

struct VsOutput {
    float4 clipPos : SV_Position;
    uint cascadeIndex : TEXCOORD0;
};

[shader("vertex")]
VsOutput vsMain(VsInput input) {
    VsOutput output;
    output.clipPos = float4(input.position, 1.0);
    output.cascadeIndex = 0u;
    return output;
}

[shader("fragment")]
void fsMain(VsOutput input) {
    // CSM depth-only rendering.
}

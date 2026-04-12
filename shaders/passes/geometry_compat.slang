// geometry_compat.slang — Compat geometry pass (Tier2+ vertex pipeline).
// Traditional vertex + fragment entry points for non-mesh-shader hardware.
import miki_geometry;

struct VsInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    uint instanceId : SV_InstanceID;
};

struct VsOutput {
    float4 clipPos : SV_Position;
    float3 worldNormal : NORMAL;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint instanceId : TEXCOORD1;
};

[shader("vertex")]
VsOutput vsMain(VsInput input) {
    VsOutput output;
    output.clipPos = float4(input.position, 1.0);
    output.worldNormal = input.normal;
    output.texCoord = input.texCoord;
    output.instanceId = input.instanceId;
    return output;
}

[shader("fragment")]
float4 fsMain(VsOutput input) : SV_Target {
    return float4(input.worldNormal * 0.5 + 0.5, 1.0);
}

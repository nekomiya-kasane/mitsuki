// Pipeline library demo shader — vertex + fragment with configurable material color.
// Push constants provide MVP, model matrix, and material color for split-compilation demo.

struct PushConst {
    float4x4 mvp;
    float4x4 model;
    float4   materialColor;  // rgb = diffuse, a = specular power
};

[[vk::push_constant]] ConstantBuffer<PushConst> pc;

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
};

struct VSOutput {
    float4 clipPos   : SV_Position;
    float3 worldNorm : NORMAL;
    float3 worldPos  : TEXCOORD0;
};

[shader("vertex")]
VSOutput vs_main(VSInput input) {
    VSOutput o;
    o.clipPos   = mul(pc.mvp, float4(input.position, 1.0));
    float4 wp   = mul(pc.model, float4(input.position, 1.0));
    o.worldPos  = wp.xyz / wp.w;
    o.worldNorm = normalize(mul((float3x3)pc.model, input.normal));
    return o;
}

[shader("fragment")]
float4 fs_main(VSOutput input) : SV_Target {
    float3 N = normalize(input.worldNorm);
    float3 lightDir = normalize(float3(0.5, 1.0, 0.8));
    float3 viewDir  = normalize(float3(0.0, 0.0, 3.0) - input.worldPos);
    float3 H = normalize(lightDir + viewDir);

    float NdotL = max(dot(N, lightDir), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float spec  = pow(NdotH, pc.materialColor.a);

    float3 ambient  = pc.materialColor.rgb * 0.1;
    float3 diffuse  = pc.materialColor.rgb * NdotL;
    float3 specular = float3(1.0, 1.0, 1.0) * spec * 0.4;

    return float4(ambient + diffuse + specular, 1.0);
}

// postfx/bloom.slang — Bloom (implementing miki_postfx).
//
// Dual Kawase downsample/upsample (Jimenez 2014) for physically-based bloom.
// 13-tap downsampling filter eliminates firefly artifacts without pre-filter.

implementing miki_postfx;

import miki_core;

/// 13-tap downsample (Jimenez 2014). Reduces fireflies without clamping HDR values.
[ForceInline]
public float3 bloomDownsample13Tap(Texture2D<float4> src, SamplerState samp, float2 uv, float2 texelSize) {
    float3 a = src.SampleLevel(samp, uv + float2(-1, -1) * texelSize, 0).rgb;
    float3 b = src.SampleLevel(samp, uv + float2(0, -1) * texelSize, 0).rgb;
    float3 c = src.SampleLevel(samp, uv + float2(1, -1) * texelSize, 0).rgb;
    float3 d = src.SampleLevel(samp, uv + float2(-1, 0) * texelSize, 0).rgb;
    float3 e = src.SampleLevel(samp, uv, 0).rgb;
    float3 f = src.SampleLevel(samp, uv + float2(1, 0) * texelSize, 0).rgb;
    float3 g = src.SampleLevel(samp, uv + float2(-1, 1) * texelSize, 0).rgb;
    float3 h = src.SampleLevel(samp, uv + float2(0, 1) * texelSize, 0).rgb;
    float3 i = src.SampleLevel(samp, uv + float2(1, 1) * texelSize, 0).rgb;

    float3 j = src.SampleLevel(samp, uv + float2(-0.5, -0.5) * texelSize, 0).rgb;
    float3 k = src.SampleLevel(samp, uv + float2(0.5, -0.5) * texelSize, 0).rgb;
    float3 l = src.SampleLevel(samp, uv + float2(-0.5, 0.5) * texelSize, 0).rgb;
    float3 m = src.SampleLevel(samp, uv + float2(0.5, 0.5) * texelSize, 0).rgb;

    float3 result = e * 0.125f;
    result += (a + c + g + i) * 0.03125f;
    result += (b + d + f + h) * 0.0625f;
    result += (j + k + l + m) * 0.125f;
    return result;
}

/// Tent-filter upsample (3x3 bilinear).
[ForceInline]
public float3 bloomUpsampleTent(Texture2D<float4> src, SamplerState samp, float2 uv, float2 texelSize) {
    float3 result = float3(0.0f);
    result += src.SampleLevel(samp, uv + float2(-1, -1) * texelSize, 0).rgb * 1.0f;
    result += src.SampleLevel(samp, uv + float2(0, -1) * texelSize, 0).rgb * 2.0f;
    result += src.SampleLevel(samp, uv + float2(1, -1) * texelSize, 0).rgb * 1.0f;
    result += src.SampleLevel(samp, uv + float2(-1, 0) * texelSize, 0).rgb * 2.0f;
    result += src.SampleLevel(samp, uv, 0).rgb * 4.0f;
    result += src.SampleLevel(samp, uv + float2(1, 0) * texelSize, 0).rgb * 2.0f;
    result += src.SampleLevel(samp, uv + float2(-1, 1) * texelSize, 0).rgb * 1.0f;
    result += src.SampleLevel(samp, uv + float2(0, 1) * texelSize, 0).rgb * 2.0f;
    result += src.SampleLevel(samp, uv + float2(1, 1) * texelSize, 0).rgb * 1.0f;
    return result / 16.0f;
}

// xr/stereo.slang — Single-pass stereo rendering using VK_KHR_multiview.
//
// Renders both eye views in a single draw call by using gl_ViewIndex
// to select per-eye view/projection matrices. This halves the CPU draw
// call overhead compared to multi-pass stereo.
//
// Technique: Vertex shader indexes into a per-eye matrix array using
// gl_ViewIndex. Fragment shader is shared. Output goes to a texture
// array (layer 0 = left eye, layer 1 = right eye).
//
// Requires: VK_KHR_multiview (Vulkan), D3D12 SV_ViewID.
//
// Reference: Oculus multiview extension, ARM Mali multiview best practices.

implementing miki_xr;

import miki_core;

/// Maximum number of views (2 for standard stereo, up to 6 for cubemap VR).
public static const uint kMaxViews = 2u;

/// Per-eye stereo data (uploaded as a uniform buffer).
public struct StereoViewData {
    public float4x4 viewMatrix[kMaxViews];
    public float4x4 projMatrix[kMaxViews];
    public float4x4 viewProjMatrix[kMaxViews];
    public float4x4 invViewProjMatrix[kMaxViews];
    public float3   eyePosition[kMaxViews];
    public float    ipd; // Inter-pupillary distance
};

/// Get the view-projection matrix for the current eye.
/// viewIndex: gl_ViewIndex or SV_ViewID.
public float4x4 getViewProjForEye(StereoViewData stereo, uint viewIndex) {
    return stereo.viewProjMatrix[min(viewIndex, kMaxViews - 1u)];
}

/// Get the eye position for the current view.
public float3 getEyePositionForView(StereoViewData stereo, uint viewIndex) {
    return stereo.eyePosition[min(viewIndex, kMaxViews - 1u)];
}

/// Transform a vertex position for stereo rendering.
/// worldPos: world-space vertex position.
/// viewIndex: current eye index.
public float4 transformStereoVertex(float3 worldPos, StereoViewData stereo, uint viewIndex) {
    return mul(getViewProjForEye(stereo, viewIndex), float4(worldPos, 1.0));
}

/// Compute stereo parallax correction for screen-space effects.
/// Needed when applying screen-space effects (SSR, SSAO) to stereo renders.
/// uv: screen UV in the current eye's render target.
/// depth: depth at the pixel.
/// viewIndex: current eye.
/// Returns the corrected UV for the other eye's G-buffer lookup.
public float2 stereoParallaxCorrection(
    float2 uv,
    float depth,
    StereoViewData stereo,
    uint viewIndex
) {
    // Reconstruct world position from current eye
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 worldPos = mul(stereo.invViewProjMatrix[viewIndex], float4(ndc, depth, 1.0));
    worldPos.xyz /= worldPos.w;

    // Project to other eye
    uint otherEye = 1u - viewIndex;
    float4 otherClip = mul(stereo.viewProjMatrix[otherEye], worldPos);
    float2 otherNdc = otherClip.xy / otherClip.w;
    return otherNdc * 0.5 + 0.5;
}

// miki-rt.slang — Ray tracing shader module.
//
// Provides RT reflections, RT shadows, RT global illumination,
// progressive path tracer, and temporal + spatial denoiser.
//
// Tier1-only: requires VK_KHR_ray_tracing_pipeline / DXR 1.1.
// All RT functions use [require] annotations for capability gating.
//
// Dependencies: miki-core, miki-lighting, miki-brdf
// Usage: import miki_rt;
//
// Reference: AMD FidelityFX RT, NVIDIA RTXGI/RTXDI, UE5 Lumen RT,
//            SVGF denoiser (Schied 2017), SHaRC (NVIDIA 2024).

module miki_rt;

__include "rt/rt_common.slang";
__include "rt/rt_reflections.slang";
__include "rt/rt_shadows.slang";
__include "rt/rt_gi.slang";
__include "rt/path_tracer.slang";
__include "rt/denoiser.slang";

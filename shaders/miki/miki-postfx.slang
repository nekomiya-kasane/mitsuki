// miki-postfx.slang — Post-processing effects module (Phase 3b §12.5 Step 4).
//
// Full-screen post-processing: bloom, DoF, motion blur, tonemapping, TAA,
// FXAA, CAS, color grading, SSR, outline, GTAO/SSAO/RTAO ambient occlusion.
//
// Dependencies: miki-core (types, packing), miki-math (sampling, noise)

module miki_postfx;

__include "postfx/bloom.slang";
__include "postfx/dof.slang";
__include "postfx/motion_blur.slang";
__include "postfx/tonemap.slang";
__include "postfx/taa.slang";
__include "postfx/fxaa.slang";
__include "postfx/cas.slang";
__include "postfx/color_grade.slang";
__include "postfx/ssr.slang";
__include "postfx/outline.slang";
__include "postfx/gtao.slang";
__include "postfx/ssao.slang";

// miki-cad.slang — CAD domain shader module.
//
// Provides GPU-accelerated CAD visualization features:
// hidden line removal (HLR), section planes/volumes, ray/lasso picking,
// GPU measurement, boolean operation preview, draft angle analysis,
// and explode-view transforms.
//
// Dependencies: miki-core, miki-geometry
// Usage: import miki_cad;

module miki_cad;

__include "cad/hlr.slang";
__include "cad/section.slang";
__include "cad/pick.slang";
__include "cad/measure.slang";
__include "cad/boolean_preview.slang";
__include "cad/draft_angle.slang";
__include "cad/explode.slang";

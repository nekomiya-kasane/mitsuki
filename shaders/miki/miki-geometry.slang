// miki-geometry.slang — Geometry processing module (Phase 3a §12.4 Step 5).
//
// Meshlet-based geometry pipeline: task/mesh shaders, GPU culling, HiZ,
// LOD selection, visibility buffer, macro binning, SW rasterizer fallback.
//
// Dependencies: miki-core (types, constants, push constants)

module miki_geometry;

__include "geometry/geometry_pipeline_interface.slang";
__include "geometry/meshlet.slang";
__include "geometry/culling.slang";
__include "geometry/hiz.slang";
__include "geometry/lod.slang";
__include "geometry/visibility_buffer.slang";
__include "geometry/macro_binning.slang";
__include "geometry/sw_rasterizer.slang";
__include "geometry/vertex_pipeline.slang";

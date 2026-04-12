// miki-debug.slang — GPU debug visualization module.
//
// Provides wireframe overlay, attribute visualization (normals, UVs, IDs),
// Nanite-style LOD/meshlet coloring, overdraw heatmap, mip level coloring,
// and GPU-driven debug line rendering.
//
// Dependencies: miki-core
// Usage: import miki_debug;

module miki_debug;

__include "debug/wireframe.slang";
__include "debug/attribute_viz.slang";
__include "debug/lod_overlay.slang";
__include "debug/overdraw.slang";
__include "debug/mip_viz.slang";
__include "debug/gpu_lines.slang";

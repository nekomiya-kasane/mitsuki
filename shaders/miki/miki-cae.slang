// miki-cae.slang — CAE (Computer-Aided Engineering) visualization module.
//
// Provides GPU-accelerated scientific visualization features:
// FEM mesh rendering with contour plots, scalar/vector field visualization,
// streamline/pathline tracing, GPU marching cubes isosurface extraction,
// tensor glyph rendering, and point cloud splatting with EDL shading.
//
// Dependencies: miki-core, miki-geometry, miki-lighting
// Usage: import miki_cae;
//
// Reference: ParaView/VTK GPU rendering, Kitware isosurface extraction (2024).

module miki_cae;

__include "cae/fem.slang";
__include "cae/scalar_field.slang";
__include "cae/streamline.slang";
__include "cae/isosurface.slang";
__include "cae/tensor_glyph.slang";
__include "cae/point_cloud.slang";

// miki-brdf.slang — Material and BRDF shader module for the miki renderer.
//
// Provides the IMaterial interface, DSPBR (Disney Standard PBR), GGX, diffuse,
// sheen, clearcoat, iridescence, SSS, and anisotropic BRDF implementations.
// Link-time type specialization selects the concrete material at compile time.
//
// Usage: import miki_brdf;

module miki_brdf;

// Implementation files
__include "brdf/material_interface.slang";
__include "brdf/ggx.slang";
__include "brdf/diffuse.slang";
__include "brdf/sheen.slang";
__include "brdf/clearcoat.slang";
__include "brdf/iridescence.slang";
__include "brdf/sss.slang";
__include "brdf/aniso.slang";
__include "brdf/dspbr.slang";

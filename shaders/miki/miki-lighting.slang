// miki-lighting.slang — Lighting module (Phase 3a §12.4 Step 5).
//
// ILight interface, clustered forward lighting, IBL, area lights (LTC),
// ReSTIR direct/global illumination, DDGI probes.
//
// Dependencies: miki-core (types, constants), miki-math (SH, sampling)

module miki_lighting;

__include "lighting/light_interface.slang";
__include "lighting/clustered.slang";
__include "lighting/ibl.slang";
__include "lighting/area_light.slang";
__include "lighting/ltc_lut.slang";
__include "lighting/restir.slang";
__include "lighting/ddgi.slang";

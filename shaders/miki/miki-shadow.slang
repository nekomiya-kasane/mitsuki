// miki-shadow.slang — Shadow mapping module (Phase 3b §12.5 Step 4).
//
// Variance Shadow Maps (VSM), Cascaded Shadow Maps (CSM),
// and shadow atlas management.
//
// Dependencies: miki-core (types, constants), miki-math (sampling)

module miki_shadow;

__include "shadow/vsm.slang";
__include "shadow/csm.slang";
__include "shadow/shadow_atlas.slang";

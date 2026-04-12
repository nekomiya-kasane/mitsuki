// miki-xr.slang — XR/VR shader module.
//
// Provides single-pass stereo rendering (VK_KHR_multiview), foveated
// rendering via Variable Rate Shading (VRS), and late-latch reprojection
// for low-latency head tracking.
//
// Dependencies: miki-core, miki-geometry
// Usage: import miki_xr;
//
// Reference: Oculus VR SDK, SteamVR, OpenXR Toolkit, ARM Foveated Rendering.

module miki_xr;

__include "xr/stereo.slang";
__include "xr/foveated.slang";
__include "xr/reprojection.slang";

// miki-math — Core math utility module.
//
// Provides: spherical harmonics, noise, sampling, quaternion, matrix utilities.
// All functions are pure (no side effects, no GPU resources), compilable to
// host-callable for CPU golden reference tests (§13.2).
//
// Usage: import miki_math;

module miki_math;

__include "math/sh.slang";
__include "math/noise.slang";
__include "math/sampling.slang";
__include "math/quaternion.slang";
__include "math/matrix_utils.slang";

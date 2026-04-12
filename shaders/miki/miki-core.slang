// miki-core.slang — Foundation shader module for the miki renderer.
//
// Provides core types, constants, push constant abstraction, color space
// utilities, data packing helpers, and bindless resource access stubs.
//
// Usage: import miki_core;

module miki_core;

// Implementation files
__include "core/types.slang";
__include "core/constants.slang";
__include "core/bindless.slang";
__include "core/push_constants.slang";
__include "core/color_space.slang";
__include "core/packing.slang";

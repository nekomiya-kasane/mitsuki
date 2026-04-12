// miki-neural.slang — Neural shader inference module (Phase 17+).
//
// GPU-side neural network inference using Cooperative Vectors
// (VK_NV_cooperative_vector / DX12 SM 6.9 Cooperative Vectors).
//
// Provides:
// - Neural texture compression decoder (small MLP: 4->16->16->4)
// - ML denoiser inference (U-Net style weights in SSBO)
// - Neural Radiance Cache (NRC: hash-grid encoder + small MLP)
//
// Architecture follows NVIDIA RTXNS (RTX Neural Shading SDK) patterns:
// - CoopVec<T, N> for hardware-accelerated vector operations
// - LinearOp for matrix-vector multiply via Tensor Cores
// - IActivation interface for activation functions
//
// Dependencies: miki-core
// Usage: import miki_neural;
//
// Reference: NVIDIA RTXNS SDK, RTX Neural Texture Compression (NTC),
//            Muller et al. (2022) "Instant-NGP" hash encoding,
//            VK_NV_cooperative_vector spec.

module miki_neural;

__include "neural/neural_common.slang";
__include "neural/neural_texture.slang";
__include "neural/neural_denoiser.slang";
__include "neural/nrc.slang";

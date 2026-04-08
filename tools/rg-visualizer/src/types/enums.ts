/** Enum string literals mirroring C++ miki::rg / miki::rhi enums. */

export const QueueType = ['Graphics', 'AsyncCompute', 'Transfer'] as const;
export type QueueType = (typeof QueueType)[number];

export const HazardType = ['RAW', 'WAR', 'WAW'] as const;
export type HazardType = (typeof HazardType)[number];

export const HeapGroupType = ['RtDs', 'NonRtDs', 'Buffer', 'MixedFallback'] as const;
export type HeapGroupType = (typeof HeapGroupType)[number];

export const ResourceKind = ['Texture', 'Buffer'] as const;
export type ResourceKind = (typeof ResourceKind)[number];

export const SchedulerStrategy = ['MinBarriers', 'MinMemory', 'MinLatency', 'Balanced'] as const;
export type SchedulerStrategy = (typeof SchedulerStrategy)[number];

export const BackendType = ['Vulkan14', 'D3D12', 'VulkanCompat', 'WebGPU', 'OpenGL43', 'Mock'] as const;
export type BackendType = (typeof BackendType)[number];

export const TextureLayout = [
  'Undefined', 'General', 'ColorAttachment', 'DepthStencilAttachment',
  'DepthStencilReadOnly', 'ShaderReadOnly', 'TransferSrc', 'TransferDst',
  'Present', 'ShadingRate',
] as const;
export type TextureLayout = (typeof TextureLayout)[number];

export const TextureDimension = ['Tex1D', 'Tex2D', 'Tex3D', 'TexCube', 'Tex2DArray', 'TexCubeArray'] as const;
export type TextureDimension = (typeof TextureDimension)[number];

export const ResourceAccessFlag = [
  'ShaderReadOnly', 'DepthReadOnly', 'IndirectBuffer', 'TransferSrc',
  'InputAttachment', 'AccelStructRead', 'ShadingRateRead', 'PresentSrc',
  'ShaderWrite', 'ColorAttachWrite', 'DepthStencilWrite', 'TransferDst', 'AccelStructWrite',
] as const;
export type ResourceAccessFlag = (typeof ResourceAccessFlag)[number];

export const PassFlag = [
  'Graphics', 'Compute', 'AsyncCompute', 'Transfer', 'Present', 'SideEffects', 'NeverCull',
] as const;
export type PassFlag = (typeof PassFlag)[number];

export const AdaptationFeature = [
  'BufferMapWriteWithUsage', 'BufferPersistentMapping', 'PushConstants',
  'CmdBlitTexture', 'CmdFillBufferNonZero', 'CmdClearTexture',
  'MultiDrawIndirect', 'DepthOnlyStencilOps', 'TimelineSemaphore',
  'SparseBinding', 'DynamicDepthBias', 'ExecuteSecondary', 'MeshShader', 'RayTracing',
] as const;
export type AdaptationFeature = (typeof AdaptationFeature)[number];

export const AdaptationStrategy = [
  'Native', 'ParameterFixup', 'UboEmulation', 'EphemeralMap', 'CallbackEmulation',
  'ShadowBuffer', 'StagingCopy', 'LoopUnroll', 'ShaderEmulation', 'Unsupported',
] as const;
export type AdaptationStrategy = (typeof AdaptationStrategy)[number];

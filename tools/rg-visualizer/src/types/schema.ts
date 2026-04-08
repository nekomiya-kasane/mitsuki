/** Core JSON schema types — 1:1 mapping to C++ RenderGraphSerializer output. */

import { z } from 'zod';
import type {
  QueueType, HazardType, HeapGroupType, ResourceKind, SchedulerStrategy,
  BackendType, TextureLayout, TextureDimension, ResourceAccessFlag, PassFlag,
  AdaptationFeature, AdaptationStrategy,
} from './enums';

// ============================================================================
// Atomic types
// ============================================================================

export interface ResourceAccess {
  handle: number;
  resourceIndex: number;
  version: number;
  access: ResourceAccessFlag[];
  mipLevel: number;
  arrayLayer: number;
}

export interface TextureDesc {
  dimension: TextureDimension;
  format: string;
  width: number;
  height: number;
  depth: number;
  mipLevels: number;
  arrayLayers: number;
  sampleCount: number;
  debugName?: string;
}

export interface BufferDesc {
  size: number;
  debugName?: string;
}

// ============================================================================
// Pass & Resource nodes
// ============================================================================

export interface PassNode {
  index: number;
  name: string;
  flags: PassFlag[];
  queue: QueueType;
  orderHint: number;
  hasSideEffects: boolean;
  enabled: boolean;
  reads: ResourceAccess[];
  writes: ResourceAccess[];
}

export interface ResourceNode {
  index: number;
  kind: ResourceKind;
  imported: boolean;
  lifetimeExtended: boolean;
  currentVersion: number;
  name: string;
  textureDesc?: TextureDesc;
  bufferDesc?: BufferDesc;
}

// ============================================================================
// Compiled graph structures
// ============================================================================

export interface BarrierCommand {
  resourceIndex: number;
  srcAccess: ResourceAccessFlag[];
  dstAccess: ResourceAccessFlag[];
  srcLayout: TextureLayout;
  dstLayout: TextureLayout;
  mipLevel: number;
  arrayLayer: number;
  isSplitRelease: boolean;
  isSplitAcquire: boolean;
  isCrossQueue: boolean;
  isAliasingBarrier: boolean;
  srcQueue?: QueueType;
  dstQueue?: QueueType;
}

export interface CompiledPassInfo {
  passIndex: number;
  queue: QueueType;
  acquireBarriers: BarrierCommand[];
  releaseBarriers: BarrierCommand[];
}

export interface DependencyEdge {
  srcPass: number;
  dstPass: number;
  resourceIndex: number;
  hazard: HazardType;
}

export interface CrossQueueSyncPoint {
  srcQueue: QueueType;
  dstQueue: QueueType;
  srcPassIndex: number;
  dstPassIndex: number;
  timelineValue: number;
}

export interface ResourceLifetime {
  resourceIndex: number;
  firstPass: number;
  lastPass: number;
}

export interface AliasingSlot {
  slotIndex: number;
  heapGroup: HeapGroupType;
  size: number;
  alignment: number;
  heapOffset: number;
  lifetimeStart: number;
  lifetimeEnd: number;
}

export interface AliasingLayout {
  slots: AliasingSlot[];
  resourceToSlot: number[];
  heapGroupSizes: number[];
}

export interface SubpassDependency {
  srcSubpass: number;
  dstSubpass: number;
  srcAccess: ResourceAccessFlag[];
  dstAccess: ResourceAccessFlag[];
  srcLayout: TextureLayout;
  dstLayout: TextureLayout;
  byRegion: boolean;
}

export interface MergedRenderPassGroup {
  subpassIndices: number[];
  dependencies: SubpassDependency[];
  sharedAttachments: number[];
  renderAreaWidth: number;
  renderAreaHeight: number;
}

export interface AdaptationPassInfo {
  originalPassIndex: number;
  insertBeforePosition: number;
  queue: QueueType;
  feature: AdaptationFeature;
  strategy: AdaptationStrategy;
  description: string;
}

export interface BatchWaitEntry {
  srcQueue: QueueType;
  timelineValue: number;
}

export interface CommandBatch {
  queue: QueueType;
  passIndices: number[];
  signalTimeline: boolean;
  waits: BatchWaitEntry[];
}

export interface CompilerOptions {
  strategy: SchedulerStrategy;
  backendType: BackendType;
  enableSplitBarriers: boolean;
  enableAsyncCompute: boolean;
  enableTransientAliasing: boolean;
  enableRenderPassMerging: boolean;
  enableAdaptation: boolean;
  enableBarrierReordering: boolean;
}

export interface StructuralHash {
  passCount: number;
  resourceCount: number;
  edgeHash: string;
  conditionHash: string;
  descHash: string;
}

export interface ExecutionStats {
  transientTexturesAllocated: number;
  transientBuffersAllocated: number;
  transientTextureViewsCreated: number;
  heapsCreated: number;
  barriersEmitted: number;
  batchesSubmitted: number;
  passesRecorded: number;
  secondaryCmdBufsUsed: number;
  transientMemoryBytes: number;
}

// ============================================================================
// Root document
// ============================================================================

export interface RenderGraphDocument {
  version: number;
  generator: string;
  timestamp: string;
  frameNumber: number;
  passes: PassNode[];
  resources: ResourceNode[];
  compilerOptions: CompilerOptions;
  structuralHash: StructuralHash;
  edges: DependencyEdge[];
  topologicalOrder: number[];
  compiledPasses: CompiledPassInfo[];
  syncPoints: CrossQueueSyncPoint[];
  lifetimes: ResourceLifetime[];
  aliasing: AliasingLayout;
  mergedGroups: MergedRenderPassGroup[];
  adaptationPasses: AdaptationPassInfo[];
  batches: CommandBatch[];
  executionStats?: ExecutionStats;
}

// ============================================================================
// Zod validator for runtime JSON import
// ============================================================================

export const RenderGraphDocumentSchema: z.ZodType<RenderGraphDocument> = z.object({
  version: z.number(),
  generator: z.string(),
  timestamp: z.string(),
  frameNumber: z.number(),
  passes: z.array(z.any()),
  resources: z.array(z.any()),
  compilerOptions: z.any(),
  structuralHash: z.any(),
  edges: z.array(z.any()),
  topologicalOrder: z.array(z.number()),
  compiledPasses: z.array(z.any()),
  syncPoints: z.array(z.any()),
  lifetimes: z.array(z.any()),
  aliasing: z.any(),
  mergedGroups: z.array(z.any()),
  adaptationPasses: z.array(z.any()),
  batches: z.array(z.any()),
  executionStats: z.any().optional(),
}) as z.ZodType<RenderGraphDocument>;

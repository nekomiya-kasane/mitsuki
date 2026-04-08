/** Derived / computed types used by stores and composables. */

import type { QueueType, HazardType } from './enums';
import type { PassNode, ResourceNode, DependencyEdge, BarrierCommand, CompiledPassInfo } from './schema';

/** Pass with its compiled info merged. */
export interface EnrichedPass extends PassNode {
  compiled?: CompiledPassInfo;
  /** Resources read by this pass (resolved indices). */
  readResources: number[];
  /** Resources written by this pass (resolved indices). */
  writeResources: number[];
}

/** DAG node used by @vue-flow. */
export interface DagNodeData {
  pass: EnrichedPass;
  queue: QueueType;
  barrierCount: number;
}

/** DAG edge used by @vue-flow. */
export interface DagEdgeData {
  edge: DependencyEdge;
  hazard: HazardType;
  resourceName: string;
}

/** Timeline lane — one per queue. */
export interface TimelineLane {
  queue: QueueType;
  slots: TimelineSlot[];
}

export interface TimelineSlot {
  passIndex: number;
  passName: string;
  position: number;
  barriers: BarrierCommand[];
}

/** Memory heap visualization data. */
export interface HeapGroupData {
  heapGroup: string;
  totalSize: number;
  slots: HeapSlotData[];
}

export interface HeapSlotData {
  slotIndex: number;
  offset: number;
  size: number;
  resourceIndices: number[];
  resourceNames: string[];
}

/** Search/filter result item. */
export interface SearchResult {
  type: 'pass' | 'resource';
  index: number;
  name: string;
  score: number;
}

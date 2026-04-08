/** Composable for computing heap group memory visualization data. */

import { computed } from 'vue';
import { useGraphStore } from '@/stores';
import type { HeapGroupData, HeapSlotData } from '@/types';

const HEAP_GROUP_NAMES = ['RtDs', 'NonRtDs', 'Buffer', 'MixedFallback'] as const;

export function useMemoryLayout() {
  const graph = useGraphStore();

  const heapGroups = computed<HeapGroupData[]>(() => {
    const al = graph.aliasing;
    if (!al) return [];
    return HEAP_GROUP_NAMES.map((name, idx) => {
      const slots: HeapSlotData[] = al.slots
        .filter((s) => s.heapGroup === name)
        .map((s) => {
          const resIndices = al.resourceToSlot
            .map((slotIdx, resIdx) => (slotIdx === s.slotIndex ? resIdx : -1))
            .filter((i) => i >= 0);
          return {
            slotIndex: s.slotIndex,
            offset: Number(s.heapOffset),
            size: Number(s.size),
            resourceIndices: resIndices,
            resourceNames: resIndices.map((ri) => graph.resources[ri]?.name ?? `res_${ri}`),
          };
        });
      return { heapGroup: name, totalSize: Number(al.heapGroupSizes[idx] ?? 0), slots };
    }).filter((g) => g.totalSize > 0);
  });

  const totalTransientMemory = computed(() => heapGroups.value.reduce((sum, g) => sum + g.totalSize, 0));

  return { heapGroups, totalTransientMemory };
}

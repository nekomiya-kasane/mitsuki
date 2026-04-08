/** Composable for computing timeline lane data from compiled passes. */

import { computed } from 'vue';
import { useGraphStore } from '@/stores';
import type { QueueType, TimelineLane, TimelineSlot } from '@/types';

export function useTimeline() {
  const graph = useGraphStore();

  const lanes = computed<TimelineLane[]>(() => {
    const queueMap = new Map<QueueType, TimelineSlot[]>();
    for (const cp of graph.compiledPasses) {
      const pass = graph.passes.find((p) => p.index === cp.passIndex);
      if (!pass) continue;
      const q = cp.queue;
      if (!queueMap.has(q)) queueMap.set(q, []);
      queueMap.get(q)!.push({
        passIndex: cp.passIndex,
        passName: pass.name,
        position: queueMap.get(q)!.length,
        barriers: [...cp.acquireBarriers, ...cp.releaseBarriers],
      });
    }
    return Array.from(queueMap.entries()).map(([queue, slots]) => ({ queue, slots }));
  });

  const maxSlots = computed(() => Math.max(1, ...lanes.value.map((l) => l.slots.length)));

  return { lanes, maxSlots };
}

<script setup lang="ts">
import { useTimeline } from '@/composables';
import { useSelectionStore } from '@/stores';

const { lanes, maxSlots } = useTimeline();
const selection = useSelectionStore();

const queueColor: Record<string, string> = {
  Graphics: 'bg-indigo-600',
  AsyncCompute: 'bg-amber-600',
  Transfer: 'bg-emerald-600',
};
</script>

<template>
  <div class="h-full overflow-auto p-4">
    <div class="space-y-3">
      <div v-for="lane in lanes" :key="lane.queue" class="flex items-start gap-2">
        <div class="w-28 flex-shrink-0 text-xs font-medium text-zinc-400 pt-2">{{ lane.queue }}</div>
        <div class="flex gap-1 flex-1 overflow-x-auto">
          <button
            v-for="slot in lane.slots"
            :key="slot.passIndex"
            class="flex-shrink-0 rounded px-3 py-2 text-xs text-white cursor-pointer hover:brightness-125 transition-all"
            :class="[
              queueColor[lane.queue] ?? 'bg-zinc-600',
              selection.kind === 'pass' && selection.index === slot.passIndex ? 'ring-2 ring-white' : '',
            ]"
            @click="selection.selectPass(slot.passIndex)"
          >
            <div class="font-medium truncate max-w-[120px]">{{ slot.passName }}</div>
            <div v-if="slot.barriers.length" class="text-[10px] opacity-70 mt-0.5">
              {{ slot.barriers.length }} barriers
            </div>
          </button>
        </div>
      </div>
    </div>
    <div v-if="lanes.length === 0" class="flex items-center justify-center h-full text-zinc-500 text-sm">
      No compiled pass data
    </div>
  </div>
</template>

<script setup lang="ts">
import { useMemoryLayout } from '@/composables';

const { heapGroups, totalTransientMemory } = useMemoryLayout();

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
}
</script>

<template>
  <div class="h-full overflow-auto p-4">
    <div class="mb-4 text-sm text-zinc-400">
      Total transient memory: <span class="text-zinc-100 font-medium">{{ formatBytes(totalTransientMemory) }}</span>
    </div>

    <div v-for="group in heapGroups" :key="group.heapGroup" class="mb-6">
      <h3 class="text-xs font-semibold text-zinc-400 mb-2">
        {{ group.heapGroup }} — {{ formatBytes(group.totalSize) }}
      </h3>
      <div class="relative h-12 bg-zinc-800 rounded overflow-hidden">
        <div
          v-for="slot in group.slots"
          :key="slot.slotIndex"
          class="absolute top-0 h-full border-r border-zinc-900 flex items-center justify-center text-[10px] text-white"
          :class="slot.resourceIndices.length > 1 ? 'bg-purple-700' : 'bg-indigo-700'"
          :style="{
            left: `${(slot.offset / group.totalSize) * 100}%`,
            width: `${Math.max((slot.size / group.totalSize) * 100, 0.5)}%`,
          }"
          :title="slot.resourceNames.join(', ')"
        >
          <span v-if="(slot.size / group.totalSize) > 0.05" class="truncate px-1">
            {{ slot.resourceNames[0] }}{{ slot.resourceIndices.length > 1 ? ` +${slot.resourceIndices.length - 1}` : '' }}
          </span>
        </div>
      </div>
    </div>

    <div v-if="heapGroups.length === 0" class="flex items-center justify-center h-full text-zinc-500 text-sm">
      No aliasing data
    </div>
  </div>
</template>

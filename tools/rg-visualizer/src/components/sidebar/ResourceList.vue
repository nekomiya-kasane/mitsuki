<script setup lang="ts">
import { useGraphQuery } from '@/composables';
import { useSelectionStore } from '@/stores';

const { filteredResources } = useGraphQuery();
const selection = useSelectionStore();

const kindIcon: Record<string, string> = {
  Texture: 'T',
  Buffer: 'B',
};
</script>

<template>
  <section class="p-2 border-t border-zinc-700">
    <h2 class="text-[10px] font-semibold uppercase tracking-wider text-zinc-500 mb-1 px-1">
      Resources ({{ filteredResources.length }})
    </h2>
    <ul class="space-y-0.5">
      <li
        v-for="res in filteredResources"
        :key="res.index"
        class="flex items-center gap-1.5 px-1.5 py-1 rounded text-xs cursor-pointer hover:bg-zinc-800"
        :class="{ 'bg-zinc-800 ring-1 ring-indigo-500': selection.kind === 'resource' && selection.index === res.index }"
        @click="selection.selectResource(res.index)"
      >
        <span class="w-4 h-4 flex items-center justify-center rounded text-[10px] font-bold bg-zinc-700">
          {{ kindIcon[res.kind] ?? '?' }}
        </span>
        <span class="truncate">{{ res.name || `Resource ${res.index}` }}</span>
        <span v-if="res.imported" class="ml-auto text-[10px] text-cyan-600">ext</span>
      </li>
    </ul>
  </section>
</template>

<script setup lang="ts">
import { useGraphQuery } from '@/composables';
import { useSelectionStore, useUiStore } from '@/stores';

const { filteredPasses } = useGraphQuery();
const selection = useSelectionStore();
const ui = useUiStore();

const queueColor: Record<string, string> = {
  Graphics: 'bg-indigo-500',
  AsyncCompute: 'bg-amber-500',
  Transfer: 'bg-emerald-500',
};
</script>

<template>
  <section class="p-2">
    <h2 class="text-[10px] font-semibold uppercase tracking-wider text-zinc-500 mb-1 px-1">
      Passes ({{ filteredPasses.length }})
    </h2>
    <input
      v-model="ui.searchQuery"
      placeholder="Search..."
      class="w-full text-xs bg-zinc-800 border border-zinc-700 rounded px-2 py-1 mb-1 focus:outline-none focus:border-indigo-500"
    />
    <ul class="space-y-0.5">
      <li
        v-for="pass in filteredPasses"
        :key="pass.index"
        class="flex items-center gap-1.5 px-1.5 py-1 rounded text-xs cursor-pointer hover:bg-zinc-800"
        :class="{ 'bg-zinc-800 ring-1 ring-indigo-500': selection.kind === 'pass' && selection.index === pass.index }"
        @click="selection.selectPass(pass.index)"
      >
        <span class="w-1.5 h-1.5 rounded-full flex-shrink-0" :class="queueColor[pass.queue] ?? 'bg-zinc-600'" />
        <span class="truncate">{{ pass.name || `Pass ${pass.index}` }}</span>
        <span v-if="!pass.enabled" class="ml-auto text-[10px] text-zinc-600">culled</span>
      </li>
    </ul>
  </section>
</template>

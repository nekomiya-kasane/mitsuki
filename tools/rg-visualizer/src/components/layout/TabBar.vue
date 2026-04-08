<script setup lang="ts">
import { useUiStore, type ViewTab } from '@/stores/uiStore';

const ui = useUiStore();

const tabs: Array<{ id: ViewTab; label: string }> = [
  { id: 'dag', label: 'DAG' },
  { id: 'timeline', label: 'Timeline' },
  { id: 'memory', label: 'Memory' },
];
</script>

<template>
  <nav class="flex gap-0 bg-zinc-800 border-b border-zinc-700">
    <button
      v-for="tab in tabs"
      :key="tab.id"
      class="px-4 py-1.5 text-xs font-medium transition-colors"
      :class="ui.activeTab === tab.id
        ? 'text-indigo-400 border-b-2 border-indigo-400 bg-zinc-900'
        : 'text-zinc-500 hover:text-zinc-300'"
      @click="ui.setTab(tab.id)"
    >
      {{ tab.label }}
    </button>
  </nav>
</template>

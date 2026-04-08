<script setup lang="ts">
import { useUiStore, useGraphStore } from '@/stores';
import { useDataSource } from '@/composables';

const ui = useUiStore();
const graph = useGraphStore();
const { loadFromFile, sourceKind } = useDataSource();

function onDrop(event: DragEvent) {
  event.preventDefault();
  const file = event.dataTransfer?.files[0];
  if (file?.name.endsWith('.json')) void loadFromFile(file);
}

function onDragOver(event: DragEvent) { event.preventDefault(); }

function onFileInput(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (file) void loadFromFile(file);
}
</script>

<template>
  <header
    class="flex items-center justify-between px-4 py-2 bg-zinc-800 border-b border-zinc-700"
    @drop="onDrop"
    @dragover="onDragOver"
  >
    <div class="flex items-center gap-3">
      <h1 class="text-sm font-bold text-indigo-400">miki::rg</h1>
      <span v-if="graph.document" class="text-xs text-zinc-500">
        Frame #{{ graph.document.frameNumber }} | {{ graph.passCount }} passes | {{ graph.resourceCount }} resources
      </span>
    </div>

    <div class="flex items-center gap-2">
      <span class="text-xs px-2 py-0.5 rounded" :class="{
        'bg-green-900 text-green-300': sourceKind === 'websocket',
        'bg-zinc-700 text-zinc-400': sourceKind !== 'websocket',
      }">
        {{ sourceKind === 'websocket' ? 'Live' : sourceKind === 'file' ? 'File' : 'Idle' }}
      </span>

      <label class="text-xs text-zinc-400 cursor-pointer hover:text-zinc-200 px-2 py-1 rounded bg-zinc-700">
        Open JSON
        <input type="file" accept=".json" class="hidden" @change="onFileInput" />
      </label>

      <button
        class="text-xs px-2 py-1 rounded bg-zinc-700 text-zinc-400 hover:text-zinc-200"
        @click="ui.toggleSidebar()"
      >
        Sidebar
      </button>
      <button
        class="text-xs px-2 py-1 rounded bg-zinc-700 text-zinc-400 hover:text-zinc-200"
        @click="ui.toggleInspector()"
      >
        Inspector
      </button>
    </div>
  </header>
</template>

<script setup lang="ts">
import { useUiStore, useGraphStore } from '@/stores';
import TopBar from './TopBar.vue';
import TabBar from './TabBar.vue';
import PassList from '@/components/sidebar/PassList.vue';
import ResourceList from '@/components/sidebar/ResourceList.vue';
import InspectorPanel from '@/components/inspector/InspectorPanel.vue';
import DagView from '@/components/dag/DagView.vue';
import TimelineView from '@/components/timeline/TimelineView.vue';
import MemoryView from '@/components/memory/MemoryView.vue';

const ui = useUiStore();
const graph = useGraphStore();
</script>

<template>
  <div class="flex flex-col h-full bg-zinc-900 text-zinc-100">
    <TopBar />
    <TabBar />
    <div class="flex flex-1 overflow-hidden">
      <!-- Sidebar -->
      <aside
        v-if="ui.sidebarVisible && graph.document"
        class="border-r border-zinc-700 overflow-y-auto flex-shrink-0"
        :style="{ width: `${ui.sidebarWidth}px` }"
      >
        <PassList />
        <ResourceList />
      </aside>

      <!-- Main content -->
      <main class="flex-1 overflow-hidden relative">
        <div v-if="!graph.document" class="flex items-center justify-center h-full text-zinc-500">
          <div class="text-center">
            <p class="text-lg font-medium mb-2">No data loaded</p>
            <p class="text-sm">Drop a JSON file or connect via WebSocket</p>
          </div>
        </div>
        <DagView v-else-if="ui.activeTab === 'dag'" />
        <TimelineView v-else-if="ui.activeTab === 'timeline'" />
        <MemoryView v-else-if="ui.activeTab === 'memory'" />
      </main>

      <!-- Inspector -->
      <aside
        v-if="ui.inspectorVisible && graph.document"
        class="border-l border-zinc-700 overflow-y-auto flex-shrink-0"
        :style="{ width: `${ui.inspectorWidth}px` }"
      >
        <InspectorPanel />
      </aside>
    </div>
  </div>
</template>

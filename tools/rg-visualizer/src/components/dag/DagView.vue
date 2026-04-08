<script setup lang="ts">
import { computed } from 'vue';
import { VueFlow, type Node, type Edge } from '@vue-flow/core';
import { Background } from '@vue-flow/background';
import { Controls } from '@vue-flow/controls';
import { MiniMap } from '@vue-flow/minimap';
import { useGraphStore, useSelectionStore } from '@/stores';
import { useLayout } from '@/composables';
import PassNodeVue from './PassNode.vue';

const graph = useGraphStore();
const selection = useSelectionStore();
const { layout } = useLayout();

const queueColorMap: Record<string, string> = {
  Graphics: '#6366f1',
  AsyncCompute: '#f59e0b',
  Transfer: '#10b981',
};

const nodes = computed<Node[]>(() =>
  layout.value.nodes.map((n) => {
    const pass = graph.enrichedPasses.find((p) => p.index === Number(n.id));
    return {
      id: n.id,
      type: 'passNode',
      position: { x: n.x - n.width / 2, y: n.y - n.height / 2 },
      data: { pass, queue: pass?.queue ?? 'Graphics' },
      style: { width: `${n.width}px`, height: `${n.height}px` },
    };
  }),
);

const edges = computed<Edge[]>(() =>
  graph.edges.map((e, i) => ({
    id: `e-${i}`,
    source: String(e.srcPass),
    target: String(e.dstPass),
    animated: e.hazard === 'RAW',
    style: { stroke: e.hazard === 'RAW' ? '#ef4444' : e.hazard === 'WAW' ? '#8b5cf6' : '#f59e0b' },
    label: `${e.hazard}`,
    labelStyle: { fill: '#a1a1aa', fontSize: 10 },
  })),
);

function onNodeClick(event: { node: Node }) {
  selection.selectPass(Number(event.node.id));
}
</script>

<template>
  <div class="h-full w-full">
    <VueFlow
      :nodes="nodes"
      :edges="edges"
      :fit-view-on-init="true"
      :default-viewport="{ zoom: 0.8, x: 0, y: 0 }"
      @node-click="onNodeClick"
    >
      <template #node-passNode="{ data }">
        <PassNodeVue :data="data" />
      </template>
      <Background />
      <Controls />
      <MiniMap />
    </VueFlow>
  </div>
</template>

<style>
@import '@vue-flow/core/dist/style.css';
@import '@vue-flow/core/dist/theme-default.css';
@import '@vue-flow/controls/dist/style.css';
@import '@vue-flow/minimap/dist/style.css';
</style>

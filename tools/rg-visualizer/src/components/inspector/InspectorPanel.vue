<script setup lang="ts">
import { computed } from 'vue';
import { useSelectionStore, useGraphStore } from '@/stores';

const selection = useSelectionStore();
const graph = useGraphStore();

const passDetail = computed(() => {
  if (selection.kind !== 'pass' || selection.index === null) return null;
  const pass = graph.passes.find((p) => p.index === selection.index);
  if (!pass) return null;
  const compiled = graph.compiledPasses.find((cp) => cp.passIndex === pass.index);
  return { pass, compiled };
});

const resourceDetail = computed(() => {
  if (selection.kind !== 'resource' || selection.index === null) return null;
  return graph.resources.find((r) => r.index === selection.index) ?? null;
});
</script>

<template>
  <div class="p-3 text-xs">
    <h2 class="text-[10px] font-semibold uppercase tracking-wider text-zinc-500 mb-2">Inspector</h2>

    <!-- No selection -->
    <div v-if="!selection.kind" class="text-zinc-500">Click a pass or resource to inspect</div>

    <!-- Pass detail -->
    <div v-else-if="passDetail" class="space-y-3">
      <div>
        <h3 class="font-medium text-zinc-200 mb-1">{{ passDetail.pass.name }}</h3>
        <table class="w-full">
          <tr><td class="text-zinc-500 pr-2">Index</td><td>{{ passDetail.pass.index }}</td></tr>
          <tr><td class="text-zinc-500 pr-2">Queue</td><td>{{ passDetail.pass.queue }}</td></tr>
          <tr><td class="text-zinc-500 pr-2">Flags</td><td>{{ passDetail.pass.flags.join(', ') }}</td></tr>
          <tr><td class="text-zinc-500 pr-2">Enabled</td><td>{{ passDetail.pass.enabled }}</td></tr>
          <tr><td class="text-zinc-500 pr-2">Reads</td><td>{{ passDetail.pass.reads.length }}</td></tr>
          <tr><td class="text-zinc-500 pr-2">Writes</td><td>{{ passDetail.pass.writes.length }}</td></tr>
        </table>
      </div>

      <div v-if="passDetail.compiled">
        <h4 class="font-medium text-zinc-300 mb-1">Barriers</h4>
        <div class="text-zinc-400">
          Acquire: {{ passDetail.compiled.acquireBarriers.length }} |
          Release: {{ passDetail.compiled.releaseBarriers.length }}
        </div>
      </div>

      <div v-if="passDetail.pass.reads.length">
        <h4 class="font-medium text-zinc-300 mb-1">Reads</h4>
        <ul class="space-y-0.5">
          <li v-for="r in passDetail.pass.reads" :key="`r-${r.handle}`" class="text-zinc-400">
            res[{{ r.resourceIndex }}] v{{ r.version }} — {{ r.access.join(', ') }}
          </li>
        </ul>
      </div>

      <div v-if="passDetail.pass.writes.length">
        <h4 class="font-medium text-zinc-300 mb-1">Writes</h4>
        <ul class="space-y-0.5">
          <li v-for="w in passDetail.pass.writes" :key="`w-${w.handle}`" class="text-zinc-400">
            res[{{ w.resourceIndex }}] v{{ w.version }} — {{ w.access.join(', ') }}
          </li>
        </ul>
      </div>
    </div>

    <!-- Resource detail -->
    <div v-else-if="resourceDetail" class="space-y-2">
      <h3 class="font-medium text-zinc-200 mb-1">{{ resourceDetail.name }}</h3>
      <table class="w-full">
        <tr><td class="text-zinc-500 pr-2">Index</td><td>{{ resourceDetail.index }}</td></tr>
        <tr><td class="text-zinc-500 pr-2">Kind</td><td>{{ resourceDetail.kind }}</td></tr>
        <tr><td class="text-zinc-500 pr-2">Imported</td><td>{{ resourceDetail.imported }}</td></tr>
        <tr><td class="text-zinc-500 pr-2">Version</td><td>{{ resourceDetail.currentVersion }}</td></tr>
      </table>

      <div v-if="resourceDetail.textureDesc">
        <h4 class="font-medium text-zinc-300 mb-1 mt-2">Texture</h4>
        <table class="w-full">
          <tr><td class="text-zinc-500 pr-2">Dimension</td><td>{{ resourceDetail.textureDesc.dimension }}</td></tr>
          <tr><td class="text-zinc-500 pr-2">Format</td><td>{{ resourceDetail.textureDesc.format }}</td></tr>
          <tr>
            <td class="text-zinc-500 pr-2">Size</td>
            <td>{{ resourceDetail.textureDesc.width }}x{{ resourceDetail.textureDesc.height }}x{{ resourceDetail.textureDesc.depth }}</td>
          </tr>
          <tr><td class="text-zinc-500 pr-2">Mips</td><td>{{ resourceDetail.textureDesc.mipLevels }}</td></tr>
          <tr><td class="text-zinc-500 pr-2">Layers</td><td>{{ resourceDetail.textureDesc.arrayLayers }}</td></tr>
        </table>
      </div>

      <div v-if="resourceDetail.bufferDesc">
        <h4 class="font-medium text-zinc-300 mb-1 mt-2">Buffer</h4>
        <table class="w-full">
          <tr><td class="text-zinc-500 pr-2">Size</td><td>{{ resourceDetail.bufferDesc.size }} bytes</td></tr>
        </table>
      </div>
    </div>
  </div>
</template>

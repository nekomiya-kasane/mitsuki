import { defineStore } from 'pinia';
import { ref, computed } from 'vue';
import type { RenderGraphDocument, EnrichedPass } from '@/types';

export const useGraphStore = defineStore('graph', () => {
  const document = ref<RenderGraphDocument | null>(null);
  const loading = ref(false);
  const error = ref<string | null>(null);

  const passes = computed(() => document.value?.passes ?? []);
  const resources = computed(() => document.value?.resources ?? []);
  const edges = computed(() => document.value?.edges ?? []);
  const compiledPasses = computed(() => document.value?.compiledPasses ?? []);
  const syncPoints = computed(() => document.value?.syncPoints ?? []);
  const lifetimes = computed(() => document.value?.lifetimes ?? []);
  const aliasing = computed(() => document.value?.aliasing ?? null);
  const mergedGroups = computed(() => document.value?.mergedGroups ?? []);
  const adaptationPasses = computed(() => document.value?.adaptationPasses ?? []);
  const batches = computed(() => document.value?.batches ?? []);
  const compilerOptions = computed(() => document.value?.compilerOptions ?? null);
  const structuralHash = computed(() => document.value?.structuralHash ?? null);
  const executionStats = computed(() => document.value?.executionStats ?? null);

  const enrichedPasses = computed<EnrichedPass[]>(() => {
    if (!document.value) return [];
    const compiledMap = new Map(compiledPasses.value.map((cp) => [cp.passIndex, cp]));
    return passes.value.map((p) => ({
      ...p,
      compiled: compiledMap.get(p.index),
      readResources: p.reads.map((r) => r.resourceIndex),
      writeResources: p.writes.map((w) => w.resourceIndex),
    }));
  });

  const passCount = computed(() => passes.value.length);
  const resourceCount = computed(() => resources.value.length);
  const edgeCount = computed(() => edges.value.length);
  const totalBarriers = computed(() =>
    compiledPasses.value.reduce((sum, cp) => sum + cp.acquireBarriers.length + cp.releaseBarriers.length, 0),
  );

  function loadDocument(doc: RenderGraphDocument) {
    document.value = doc;
    error.value = null;
  }

  function setError(msg: string) {
    error.value = msg;
    loading.value = false;
  }

  function clear() {
    document.value = null;
    error.value = null;
  }

  return {
    document, loading, error,
    passes, resources, edges, compiledPasses, syncPoints, lifetimes,
    aliasing, mergedGroups, adaptationPasses, batches, compilerOptions,
    structuralHash, executionStats, enrichedPasses,
    passCount, resourceCount, edgeCount, totalBarriers,
    loadDocument, setError, clear,
  };
});

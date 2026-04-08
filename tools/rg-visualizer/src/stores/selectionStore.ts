import { defineStore } from 'pinia';
import { ref, computed } from 'vue';
import { useGraphStore } from './graphStore';

export type SelectionKind = 'pass' | 'resource' | 'edge' | 'barrier' | null;

export const useSelectionStore = defineStore('selection', () => {
  const kind = ref<SelectionKind>(null);
  const index = ref<number | null>(null);
  const secondaryIndex = ref<number | null>(null);

  const graph = useGraphStore();

  const selectedPass = computed(() => {
    if (kind.value !== 'pass' || index.value === null) return null;
    return graph.passes.find((p) => p.index === index.value) ?? null;
  });

  const selectedResource = computed(() => {
    if (kind.value !== 'resource' || index.value === null) return null;
    return graph.resources.find((r) => r.index === index.value) ?? null;
  });

  const selectedEdge = computed(() => {
    if (kind.value !== 'edge' || index.value === null) return null;
    return graph.edges[index.value] ?? null;
  });

  function selectPass(passIndex: number) {
    kind.value = 'pass';
    index.value = passIndex;
    secondaryIndex.value = null;
  }

  function selectResource(resourceIndex: number) {
    kind.value = 'resource';
    index.value = resourceIndex;
    secondaryIndex.value = null;
  }

  function selectEdge(edgeIndex: number) {
    kind.value = 'edge';
    index.value = edgeIndex;
    secondaryIndex.value = null;
  }

  function selectBarrier(passIndex: number, barrierIndex: number) {
    kind.value = 'barrier';
    index.value = passIndex;
    secondaryIndex.value = barrierIndex;
  }

  function clearSelection() {
    kind.value = null;
    index.value = null;
    secondaryIndex.value = null;
  }

  return {
    kind, index, secondaryIndex,
    selectedPass, selectedResource, selectedEdge,
    selectPass, selectResource, selectEdge, selectBarrier, clearSelection,
  };
});

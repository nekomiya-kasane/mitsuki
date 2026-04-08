/** Composable for querying and filtering graph data. */

import { computed } from 'vue';
import { useGraphStore, useUiStore } from '@/stores';
import type { SearchResult } from '@/types';

export function useGraphQuery() {
  const graph = useGraphStore();
  const ui = useUiStore();

  const filteredPasses = computed(() => {
    let result = graph.enrichedPasses;
    if (!ui.showCulledPasses) result = result.filter((p) => p.enabled);
    if (ui.filterQueue) result = result.filter((p) => p.queue === ui.filterQueue);
    return result;
  });

  const filteredResources = computed(() => {
    let result = graph.resources;
    if (!ui.showImportedResources) result = result.filter((r) => !r.imported);
    return result;
  });

  const searchResults = computed<SearchResult[]>(() => {
    const q = ui.searchQuery.toLowerCase().trim();
    if (!q) return [];
    const results: SearchResult[] = [];
    for (const p of graph.passes) {
      const score = fuzzyScore(p.name, q);
      if (score > 0) results.push({ type: 'pass', index: p.index, name: p.name, score });
    }
    for (const r of graph.resources) {
      const score = fuzzyScore(r.name, q);
      if (score > 0) results.push({ type: 'resource', index: r.index, name: r.name, score });
    }
    return results.sort((a, b) => b.score - a.score);
  });

  return { filteredPasses, filteredResources, searchResults };
}

function fuzzyScore(text: string, query: string): number {
  const lower = text.toLowerCase();
  if (lower === query) return 100;
  if (lower.startsWith(query)) return 80;
  if (lower.includes(query)) return 60;
  return 0;
}

/** Composable for DAG layout computation using dagre. */

import { computed } from 'vue';
import dagre from 'dagre';
import { useGraphStore } from '@/stores';

export interface LayoutNode {
  id: string;
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface LayoutEdge {
  id: string;
  source: string;
  target: string;
  points: Array<{ x: number; y: number }>;
}

export function useLayout() {
  const graph = useGraphStore();

  const layout = computed(() => {
    const g = new dagre.graphlib.Graph();
    g.setGraph({ rankdir: 'LR', nodesep: 30, ranksep: 60, marginx: 20, marginy: 20 });
    g.setDefaultEdgeLabel(() => ({}));

    for (const pass of graph.enrichedPasses) {
      g.setNode(String(pass.index), { width: 180, height: 60, label: pass.name });
    }

    for (const edge of graph.edges) {
      g.setEdge(String(edge.srcPass), String(edge.dstPass));
    }

    dagre.layout(g);

    const nodes: LayoutNode[] = g.nodes().map((id) => {
      const n = g.node(id);
      return { id, x: n.x, y: n.y, width: n.width, height: n.height };
    });

    const edges: LayoutEdge[] = g.edges().map((e) => {
      const edgeData = g.edge(e);
      return {
        id: `${e.v}->${e.w}`,
        source: e.v,
        target: e.w,
        points: edgeData.points ?? [],
      };
    });

    return { nodes, edges };
  });

  return { layout };
}

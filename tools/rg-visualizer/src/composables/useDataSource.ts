/** Composable for loading RenderGraph JSON from file, WebSocket, or stdin pipe. */

import { ref } from 'vue';
import { useGraphStore } from '@/stores';
import { RenderGraphDocumentSchema } from '@/types/schema';

export type DataSourceKind = 'file' | 'websocket' | 'none';

export function useDataSource() {
  const store = useGraphStore();
  const sourceKind = ref<DataSourceKind>('none');
  const wsConnection = ref<WebSocket | null>(null);

  async function loadFromFile(file: File) {
    store.loading = true;
    try {
      const text = await file.text();
      const raw = JSON.parse(text);
      const doc = RenderGraphDocumentSchema.parse(raw);
      store.loadDocument(doc);
      sourceKind.value = 'file';
    } catch (e) {
      store.setError(e instanceof Error ? e.message : String(e));
    } finally {
      store.loading = false;
    }
  }

  async function loadFromJson(jsonString: string) {
    store.loading = true;
    try {
      const raw = JSON.parse(jsonString);
      const doc = RenderGraphDocumentSchema.parse(raw);
      store.loadDocument(doc);
    } catch (e) {
      store.setError(e instanceof Error ? e.message : String(e));
    } finally {
      store.loading = false;
    }
  }

  function connectWebSocket(url: string) {
    if (wsConnection.value) wsConnection.value.close();
    const ws = new WebSocket(url);
    ws.onmessage = (event) => { void loadFromJson(String(event.data)); };
    ws.onopen = () => { sourceKind.value = 'websocket'; };
    ws.onerror = () => { store.setError(`WebSocket error: ${url}`); };
    ws.onclose = () => { if (sourceKind.value === 'websocket') sourceKind.value = 'none'; };
    wsConnection.value = ws;
  }

  function disconnect() {
    wsConnection.value?.close();
    wsConnection.value = null;
    sourceKind.value = 'none';
  }

  return { sourceKind, loadFromFile, loadFromJson, connectWebSocket, disconnect };
}

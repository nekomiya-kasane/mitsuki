import { defineStore } from 'pinia';
import { ref } from 'vue';

export type ViewTab = 'dag' | 'timeline' | 'memory';
export type ThemeMode = 'dark' | 'light';

export const useUiStore = defineStore('ui', () => {
  const activeTab = ref<ViewTab>('dag');
  const sidebarVisible = ref(true);
  const inspectorVisible = ref(true);
  const compilerPanelVisible = ref(false);
  const theme = ref<ThemeMode>('dark');
  const sidebarWidth = ref(260);
  const inspectorWidth = ref(320);
  const searchQuery = ref('');
  const filterQueue = ref<string | null>(null);
  const showCulledPasses = ref(false);
  const showImportedResources = ref(true);

  function setTab(tab: ViewTab) { activeTab.value = tab; }
  function toggleSidebar() { sidebarVisible.value = !sidebarVisible.value; }
  function toggleInspector() { inspectorVisible.value = !inspectorVisible.value; }
  function toggleCompilerPanel() { compilerPanelVisible.value = !compilerPanelVisible.value; }
  function toggleTheme() { theme.value = theme.value === 'dark' ? 'light' : 'dark'; }

  return {
    activeTab, sidebarVisible, inspectorVisible, compilerPanelVisible,
    theme, sidebarWidth, inspectorWidth, searchQuery, filterQueue,
    showCulledPasses, showImportedResources,
    setTab, toggleSidebar, toggleInspector, toggleCompilerPanel, toggleTheme,
  };
});

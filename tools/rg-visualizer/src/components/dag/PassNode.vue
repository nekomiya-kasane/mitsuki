<script setup lang="ts">
import type { EnrichedPass, QueueType } from '@/types';

const props = defineProps<{
  data: { pass: EnrichedPass; queue: QueueType };
}>();

const colorMap: Record<string, string> = {
  Graphics: 'border-indigo-500 bg-indigo-950',
  AsyncCompute: 'border-amber-500 bg-amber-950',
  Transfer: 'border-emerald-500 bg-emerald-950',
};
</script>

<template>
  <div
    class="rounded border px-3 py-2 text-xs"
    :class="colorMap[props.data.queue] ?? 'border-zinc-600 bg-zinc-800'"
  >
    <div class="font-medium text-zinc-100 truncate">{{ props.data.pass?.name ?? 'unnamed' }}</div>
    <div class="text-[10px] text-zinc-400 mt-0.5">
      {{ props.data.queue }} | {{ props.data.pass?.reads.length ?? 0 }}R {{ props.data.pass?.writes.length ?? 0 }}W
    </div>
  </div>
</template>

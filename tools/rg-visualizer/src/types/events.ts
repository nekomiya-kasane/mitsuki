/** Application-level events for cross-component communication. */

export interface DataLoadedEvent {
  type: 'data:loaded';
  source: 'file' | 'websocket' | 'pipe';
  frameNumber: number;
}

export interface SelectionChangedEvent {
  type: 'selection:changed';
  kind: 'pass' | 'resource' | 'edge' | 'barrier' | null;
  index: number | null;
}

export interface ViewChangedEvent {
  type: 'view:changed';
  view: 'dag' | 'timeline' | 'memory';
}

export type AppEvent = DataLoadedEvent | SelectionChangedEvent | ViewChangedEvent;

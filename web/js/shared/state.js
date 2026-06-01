/* ═══════════════════════════════════════════════════════════════
   shared/state.js — 全局状态树
   ═══════════════════════════════════════════════════════════════ */

export const state = {
  /* UI */
  dark: false,
  busy: false,

  /* Connection */
  apiReady: false,
  worldReady: false,

  /* Chat */
  chat: { messages: [] },

  /* World */
  world: { npcs: [], locations: [], tick: 0 },
  _npcs: [],

  /* Map (transient UI state) */
  map: {
    allPoints: [],
    currentLoc: null,
    nav: { level: 2, parents: [], zoom: 1.0, panX: 0, panY: 0 }
  },

  /* Debug */
  debug: { enabled: false, lastTrace: null }
};

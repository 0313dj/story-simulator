/* ═══════════════════════════════════════════════════════════════
   shared/eventbus.js — 全局事件总线，模块间解耦
   ═══════════════════════════════════════════════════════════════ */

const listeners = {};

export const EventBus = {
  on(event, cb) {
    if (!listeners[event]) listeners[event] = [];
    listeners[event].push(cb);
  },
  off(event, cb) {
    if (!listeners[event]) return;
    listeners[event] = listeners[event].filter(fn => fn !== cb);
  },
  emit(event, payload) {
    (listeners[event] || []).forEach(cb => {
      try { cb(payload); } catch (e) { console.error('[EventBus]', event, e); }
    });
  }
};

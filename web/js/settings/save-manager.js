/* ═══════════════════════════════════════════════════════════════
   settings/save-manager.js — 存档管理弹窗
   ═══════════════════════════════════════════════════════════════ */

import { EventBus } from '../shared/eventbus.js';
import { listSaves, loadSave, deleteSave } from '../shared/api.js';
import { $, escapeHtml } from '../shared/utils.js';
import { showModal, hideModal } from '../ui/modal.js';

export async function showSaveManager() {
  const saves = await listSaves();
  let html = '';
  if (Array.isArray(saves) && saves.length) {
    saves.forEach(s => {
      html += '<div class="save-entry"><div class="save-info">' +
        '<div class="save-name">' + escapeHtml(s.playerName) + '</div>' +
        '<div class="save-meta">' + s.year + '-' + String(s.month).padStart(2, '0') + '-' + String(s.day).padStart(2, '0') + '  ' + escapeHtml(s.saveTime) + '</div></div>' +
        '<div class="save-actions">' +
        '<button class="btn btn-secondary btn-sm load-save-btn" data-file="' + escapeHtml(s.filename) + '">加载</button>' +
        '<button class="btn btn-danger btn-sm del-save-btn" data-file="' + escapeHtml(s.filename) + '">删除</button></div></div>';
    });
  } else {
    html = '<p style="color:var(--text-muted)">无存档</p>';
  }
  showModal('存档管理', html, true);

  setTimeout(() => {
    document.querySelectorAll('.load-save-btn').forEach(b => { b.onclick = async () => { const r = await loadSave(b.dataset.file); if (r.ok) { EventBus.emit('state:refresh', r); EventBus.emit('chat:add', { role: 'system', text: '存档已加载' }); hideModal(); } else EventBus.emit('chat:add', { role: 'system', text: '加载失败' }); }; });
    document.querySelectorAll('.del-save-btn').forEach(b => { b.onclick = async () => { await deleteSave(b.dataset.file); showSaveManager(); }; });
  }, 10);
}

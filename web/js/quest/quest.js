/* ═══════════════════════════════════════════════════════════════
   quest/quest.js — 任务管理模块
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { EventBus } from '../shared/eventbus.js';
import { api } from '../shared/api.js';
import { escapeHtml } from '../shared/utils.js';
import { showModal } from '../ui/modal.js';

export function showQuestList(quests) {
  if (!quests?.length) { showModal('任务', '<p style="color:var(--text-muted);text-align:center">暂无进行中的任务</p>'); return; }

  let html = quests.map(q => {
    const c = q.status === 'active' ? 'var(--accent)' : q.status === 'completed' ? 'var(--success)' : 'var(--text-muted)';
    return '<div style="padding:8px 0;border-bottom:1px solid var(--border-light)">' +
      '<div style="display:flex;align-items:center;gap:8px">' +
      '<span style="color:' + c + ';font-weight:600">●</span>' +
      '<span style="font-weight:500;color:var(--text-primary)">' + escapeHtml(q.title || q.name || '???') + '</span>' +
      '<span style="font-size:11px;color:var(--text-muted);margin-left:auto">' + escapeHtml(q.status || '?') + '</span></div>' +
      (q.desc || q.description ? '<div style="font-size:12px;color:var(--text-secondary);margin-top:2px;margin-left:20px">' + escapeHtml(q.desc || q.description) + '</div>' : '') +
      '</div>';
  }).join('');
  showModal('任务列表', html);
}

export function initQuest() {
  EventBus.on('quest:show', showQuestList);
}

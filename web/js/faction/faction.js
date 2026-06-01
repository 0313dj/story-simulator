/* ═══════════════════════════════════════════════════════════════
   faction/faction.js — 派系关系模块
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { EventBus } from '../shared/eventbus.js';
import { api } from '../shared/api.js';
import { escapeHtml } from '../shared/utils.js';
import { showModal } from '../ui/modal.js';

export function showFactionList(factions) {
  if (!factions?.length) { showModal('派系', '<p style="color:var(--text-muted);text-align:center">暂无派系信息</p>'); return; }

  let html = factions.map(f => {
    const affColor = f.affinity > 0 ? 'var(--success)' : f.affinity < 0 ? 'var(--danger)' : 'var(--text-muted)';
    return '<div style="padding:8px 0;border-bottom:1px solid var(--border-light)">' +
      '<div style="display:flex;align-items:center;justify-content:space-between">' +
      '<span style="font-weight:500;color:var(--text-primary);font-size:14px">' + escapeHtml(f.name || '???') + '</span>' +
      '<span style="font-weight:600;color:' + affColor + '">' + (f.affinity > 0 ? '+' : '') + (f.affinity || 0) + '</span></div>' +
      (f.desc || f.description ? '<div style="font-size:12px;color:var(--text-secondary);margin-top:2px">' + escapeHtml(f.desc || f.description) + '</div>' : '') +
      '</div>';
  }).join('');
  showModal('派系关系', html);
}

export function initFaction() {
  EventBus.on('faction:show', showFactionList);
}

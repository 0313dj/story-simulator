/* ═══════════════════════════════════════════════════════════════
   inventory/inventory.js — NPC列表、NPC详情、道具/技能/关系弹窗
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { getState } from '../shared/api.js';
import { escapeHtml } from '../shared/utils.js';
import { showModal } from '../ui/modal.js';

/* ── NPC Detail ── */

export function showNpcDetail(n) {
  let html = '<div style="margin-bottom:10px"><strong style="font-size:16px">' + escapeHtml(n.name) + '</strong>';
  html += '  年龄:' + (n.age || '?') + '岁  性别:' + escapeHtml(n.gender || '?');
  if (n.clothing) html += '  衣着:' + escapeHtml(n.clothing);
  html += '</div>';

  if (n.attrs) {
    html += '<div style="margin-bottom:8px;font-size:12px;color:var(--text-secondary)">';
    html += '颜值:' + (n.attrs.appearance || 0) + '  体质:' + (n.attrs.constitution || 0) + '  智力:' + (n.attrs.intelligence || 0);
    html += '</div>';
  }

  html += '<div style="margin-bottom:8px;font-size:12px;color:var(--text-secondary)">';
  html += '性格:' + escapeHtml(n.personality || '?');
  if (n.home) html += '  住所:' + escapeHtml(n.home);
  html += '  状态:' + escapeHtml(n.statusText || '正常');
  html += '  好感度:' + (n.playerAffinity || 0) + '  金钱:' + (n.money || 0) + 'G</div>';

  const skillCount = n.skillCount || (n.skills?.length || 0);
  html += '<div class="section-label" style="margin-top:12px">技能 (' + skillCount + ')</div>';
  html += n.skills?.length
    ? '<div style="font-size:12px">' + n.skills.map(s => '<div style="padding:2px 0">' + escapeHtml(s.name) + '  <strong>Lv.' + s.level + '</strong></div>').join('') + '</div>'
    : '<div style="font-size:12px;color:var(--text-muted)">(无)</div>';

  const itemCount = n.itemCount || (n.items?.length || 0);
  html += '<div class="section-label" style="margin-top:10px">持有物 (' + itemCount + ')</div>';
  html += n.items?.length
    ? '<div style="font-size:13px">' + n.items.map(it => '<div style="padding:2px 0">' + escapeHtml(it.name) + '  x' + it.qty + '</div>').join('') + '</div>'
    : '<div style="font-size:13px;color:var(--text-muted)">(无)</div>';

  showModal('NPC: ' + n.name, html);
}

/* ── NPC List ── */

async function showNpcList() {
  const r = await getState();
  const npcs = r?.npcs || state._npcs || [];
  const presentNames = {};
  r?.location?.presentNpcs?.forEach(p => { presentNames[p.name] = true; });

  if (!npcs.length) { showModal('NPC 角色卡', '<p style="color:var(--text-muted)">(没有已记录的NPC角色卡)</p>'); return; }

  const sorted = npcs.slice().sort((a, b) => (presentNames[b.name] ? 1 : 0) - (presentNames[a.name] ? 1 : 0));
  let html = '';
  sorted.forEach((n, idx) => {
    const marker = presentNames[n.name] ? ' <span style="color:#34C759">●在场</span>' : '';
    const hasSkills = n.skills?.length > 0, hasItems = n.items?.length > 0;
    html += '<div class="npc-card-entry" style="padding:8px 0;border-bottom:1px solid var(--border-light)">' +
      '<div style="display:flex;align-items:flex-start;justify-content:space-between">' +
      '<div style="flex:1"><strong>' + escapeHtml(n.name) + '</strong>' + marker +
      '  <span style="font-size:12px;color:var(--text-secondary)">年龄:' + (n.age || '?') + '岁 性别:' + escapeHtml(n.gender || '?') + '</span><br>' +
      '<span style="font-size:11px;color:var(--text-muted)">身份:' + escapeHtml(n.personality || '?') +
      (n.home ? '  家:' + escapeHtml(n.home) : '') + '  状态:' + escapeHtml(n.statusText || '正常') +
      '  好感:' + (n.playerAffinity || 0) + '  金钱:' + (n.money || 0) + 'G</span></div>' +
      '<button class="btn btn-secondary btn-sm npc-detail-btn" data-idx="' + idx + '" title="查看详情">详情</button></div>';
    if (hasSkills) html += '<div style="margin-top:4px;font-size:12px;color:var(--accent)">技能: ' + n.skills.map((sk, i) => (i ? ', ' : '') + escapeHtml(sk.name) + ' Lv.' + sk.level).join('') + '</div>';
    if (hasItems) html += '<div style="margin-top:3px;font-size:12px;color:var(--text-secondary)">持有: ' + n.items.map((it, i) => (i ? ', ' : '') + escapeHtml(it.name) + ' x' + it.qty).join('') + '</div>';
    html += '</div>';
  });
  showModal('NPC 角色卡 (' + npcs.length + '个)', html, true);

  setTimeout(() => {
    document.querySelectorAll('.npc-detail-btn').forEach(btn => {
      btn.onclick = function() {
        const idx = parseInt(this.dataset.idx);
        getState().then(st => { const list = st?.npcs || npcs; if (idx >= 0 && idx < list.length) showNpcDetail(list[idx]); });
      };
    });
  }, 50);
}

/* ── Init ── */

export function initInventory() {
  document.getElementById('btn-npc').onclick = showNpcList;
}

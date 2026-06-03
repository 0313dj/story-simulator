/* ═══════════════════════════════════════════════════════════════
   character/character.js — 玩家角色面板
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { EventBus } from '../shared/eventbus.js';
import { api, createWorld } from '../shared/api.js';
import { $, escapeHtml, STATUS_COLORS } from '../shared/utils.js';
import { showModal, hideModal } from '../ui/modal.js';

/* ── Player update ── */

export function updatePlayer(data) {
  if (!data) return;
  $('char-name').textContent = data.name || '---';
  $('char-age').textContent = data.age || '--';
  $('char-gender').textContent = data.gender || '--';
  $('money-text').textContent = (data.money || 0) + ' G';

  const st = data.statusText || '正常';
  $('status-text').textContent = st;
  const color = STATUS_COLORS[st] || 'var(--text-primary)';
  $('status-text').style.color = color;
  $('status-dot').style.color = color;

  if (data.attrs) {
    updateAttr('attr-app', '颜值', data.attrs.appearance);
    updateAttr('attr-con', '体质', data.attrs.constitution);
    updateAttr('attr-int', '智力', data.attrs.intelligence);
  }

  $('btn-skills').textContent = '技能 (' + (data.skillCount || 0) + ')';
  $('btn-items').textContent = '持有物 (' + (data.itemCount || 0) + ')';
  $('btn-relations').textContent = '关系 (' + (data.relationCount || 0) + ')';
}

function updateAttr(id, label, val) {
  const bar = $(id); if (!bar) return;
  const fill = bar.querySelector('.attr-fill'), lbl = bar.querySelector('.attr-label'), v = bar.querySelector('.attr-val');
  if (lbl) lbl.textContent = label;
  if (v) v.textContent = (val || 0) + '/100';
  if (fill) fill.style.width = Math.max(2, Math.min(100, val || 0)) + '%';
}

/* ── New World ── */

function showNewWorld() {
  let html = '<div class="form-row"><label>姓名</label><input id="wc-name" value=""></div>';
  html += '<div class="form-row"><label>年龄</label><input id="wc-age" value="25"></div>';
  html += '<div class="form-row"><label>性别</label><select id="wc-gender"><option value="">--</option><option value="男">男</option><option value="女">女</option><option value="其他">其他</option></select></div>';
  html += '<div class="form-row"><label>衣着</label><input id="wc-clothing" value=""></div>';
  html += '<div class="form-row"><label>金钱</label><input id="wc-money" value="100"></div>';
  html += '<div class="form-row" style="display:flex;gap:8px">' +
    '<div style="flex:1"><label>颜值</label><input id="wc-app" value="50"></div>' +
    '<div style="flex:1"><label>体质</label><input id="wc-con" value="50"></div>' +
    '<div style="flex:1"><label>智力</label><input id="wc-int" value="50"></div></div>';
  html += '<div class="form-row"><label>技能 (每行一个)</label><textarea id="wc-skills"></textarea></div>';
  html += '<div class="form-row"><label>持有物 (每行一个)</label><textarea id="wc-items"></textarea></div>';
  html += '<div class="form-row"><label>角色故事</label><textarea id="wc-story" style="min-height:120px"></textarea></div>';
  html += '<button class="btn btn-primary" id="wc-create-btn">创建世界</button>';
  showModal('创建新世界', html, true);

  $('wc-create-btn').onclick = async () => {
    /* ── Frontend validation ── */
    const name = $('wc-name').value.trim();
    if (!name) {
      EventBus.emit('chat:add', { role: 'system', text: '请输入角色姓名' });
      return;
    }
    const age = parseInt($('wc-age').value, 10);
    if (isNaN(age) || age < 1 || age > 150) {
      EventBus.emit('chat:add', { role: 'system', text: '年龄需为 1-150 之间的数字' });
      return;
    }
    const money = parseInt($('wc-money').value, 10);
    if (isNaN(money) || money < 0) {
      EventBus.emit('chat:add', { role: 'system', text: '金钱需为非负整数' });
      return;
    }
    const app = parseInt($('wc-app').value, 10);
    const con = parseInt($('wc-con').value, 10);
    const intel = parseInt($('wc-int').value, 10);
    if (isNaN(app) || app < 0 || app > 100 ||
        isNaN(con) || con < 0 || con > 100 ||
        isNaN(intel) || intel < 0 || intel > 100) {
      EventBus.emit('chat:add', { role: 'system', text: '颜值/体质/智力需为 0-100 之间的数字' });
      return;
    }

    $('wc-create-btn').innerHTML = '<span class="spinner"></span>AI 创建中...';
    $('wc-create-btn').disabled = true;
    const r = await createWorld({
      name, age: String(age), gender: $('wc-gender').value,
      clothing: $('wc-clothing').value.trim(), money: String(money),
      appearance: String(app), constitution: String(con), intelligence: String(intel),
      skills: $('wc-skills').value, items: $('wc-items').value, story: $('wc-story').value
    });
    if (r.ok) {
      hideModal();
      EventBus.emit('state:refresh', r.state || r);
      EventBus.emit('chat:add', { role: 'system', text: r.message + '  存档: ' + r.saveFile });
    } else {
      EventBus.emit('chat:add', { role: 'system', text: '创建失败: ' + r.error });
      $('wc-create-btn').innerHTML = '创建世界'; $('wc-create-btn').disabled = false;
    }
  };
}

/* ── Detail popups ── */

export function showSkillDetails(skills) {
  let html = (!skills?.length) ? '<p style="color:var(--text-muted)">(无)</p>'
    : skills.map(s => '<div style="padding:4px 0;font-size:13px">' + escapeHtml(s.name) + '  <strong>Lv.' + s.level + '</strong></div>').join('');
  showModal('技能列表', html);
}

export function showItemDetails(items) {
  let html = (!items?.length) ? '<p style="color:var(--text-muted)">(无)</p>'
    : items.map(i => '<div style="padding:4px 0;font-size:13px">' + escapeHtml(i.name) + '  x' + i.qty + '</div>').join('');
  showModal('持有物', html);
}

export function showRelationDetails(relations) {
  let html = (!relations?.length) ? '<p style="color:var(--text-muted)">(无)</p>'
    : relations.map(r => '<div style="padding:4px 0;font-size:13px">' + escapeHtml(r.target) + '  [' + escapeHtml(r.type) + ']  好感度: ' + r.affinity + '</div>').join('');
  showModal('人物关系', html);
}

/* ── Init ── */

export function initCharacter() {
  EventBus.on('state:refresh', data => {
    if (data.character) updatePlayer(data.character);
    else if (data.player) updatePlayer(data.player);
    state.apiReady = data.apiReady;
    state.worldReady = data.worldReady;
    state._npcs = data.npcs || [];
    $('btn-npc').textContent = 'NPC (' + state._npcs.length + ')';
  });

  $('btn-new').onclick = showNewWorld;

  $('btn-skills').onclick = async () => { const r = await api('get_state'); const ch = r.character || r.player; if (r.ok && ch) showSkillDetails(ch.skills); };
  $('btn-items').onclick = async () => { const r = await api('get_state'); const ch = r.character || r.player; if (r.ok && ch) showItemDetails(ch.items); };
  $('btn-relations').onclick = async () => { const r = await api('get_state'); const ch = r.character || r.player; if (r.ok && ch) showRelationDetails(ch.relations); };

  $('btn-save').onclick = async () => { const r = await api('quick_save'); EventBus.emit('chat:add', { role: 'system', text: r.ok ? '已保存' : r.error }); };
  $('btn-load').onclick = async () => { const r = await api('quick_load'); if (r.ok) { EventBus.emit('state:refresh', r); EventBus.emit('chat:add', { role: 'system', text: '已读取快速存档' }); } else EventBus.emit('chat:add', { role: 'system', text: r.error }); };
}

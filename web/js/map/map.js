/* ═══════════════════════════════════════════════════════════════
   map/map.js — 地图渲染与交互
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { EventBus } from '../shared/eventbus.js';
import { getState, renameLocation, deleteLocation } from '../shared/api.js';
import { $, escapeHtml, MAP_COLORS, MAP_LEVEL_NAMES } from '../shared/utils.js';
import { showModal } from '../ui/modal.js';

function mapGetVisible(allPoints, nav) {
  if (!allPoints?.length) return [];
  if (nav.level === 2) return allPoints.filter(p => p.level === 2);
  if (!nav.parents.length) return allPoints.filter(p => p.level === nav.level);

  const parent = nav.parents[nav.parents.length - 1];
  const candidates = allPoints.filter(p => p.level === nav.level);
  const higher = allPoints.filter(p => p.level > nav.level);

  return candidates.filter(c => {
    let nearestName = null, nearestDist = 1e9;
    higher.forEach(hp => {
      const dist = (c.x - hp.x) ** 2 + (c.y - hp.y) ** 2;
      if (dist < nearestDist) { nearestDist = dist; nearestName = hp.name; }
    });
    return nearestName === parent.name;
  });
}

function mapRenderModal() {
  const nav = state.map.nav;
  const allPoints = state.map.allPoints;
  const visible = mapGetVisible(allPoints, nav);

  const zoom = nav.zoom || 1.0;
  const W = Math.round(520 * zoom), H = Math.round(420 * zoom);
  const PAD = Math.round(30 * zoom), FONT = Math.round(10 * zoom);
  const sx = (W - PAD * 2) / 1000, sy = (H - PAD * 2) / 1000;
  const radii = [Math.round(4 * zoom), Math.round(5 * zoom), Math.round(7 * zoom)];

  /* Breadcrumb */
  let bc = '';
  nav.parents.forEach((p, i) => {
    if (i > 0) bc += ' <span style="color:var(--text-muted)">&rsaquo;</span> ';
    bc += '<span style="color:' + MAP_COLORS[p.level] + ';font-weight:500">' + escapeHtml(p.name) + '</span>';
  });

  /* Controls */
  let ctrl = '<div style="margin-bottom:8px;display:flex;align-items:center;justify-content:center;gap:8px">';
  ctrl += '<button class="btn btn-secondary btn-sm" id="map-zoom-out-btn" title="缩小"' + (zoom <= 0.6 ? ' disabled' : '') + '>&#x2796; 缩小</button>';
  if (nav.parents.length) ctrl += '<button class="btn btn-secondary btn-sm" id="map-back-btn">&larr; 返回</button>';
  else ctrl += '<span style="width:64px"></span>';
  ctrl += '<span style="font-size:12px;color:var(--text-primary)">' + (bc || '全部大地点') + '</span>';
  ctrl += '<span style="font-size:11px;color:var(--text-muted)">（' + visible.length + '个' + MAP_LEVEL_NAMES[nav.level] + '）</span>';
  ctrl += '<button class="btn btn-secondary btn-sm" id="map-zoom-in-btn" title="放大"' + (zoom >= 3.0 ? ' disabled' : '') + '>&#x2795; 放大</button>';
  if (nav.level < 2) ctrl += '<button class="btn btn-secondary btn-sm" id="map-top-btn" title="回到顶层">&#x21E7; 顶层</button>';
  ctrl += '</div>';

  /* SVG */
  const px = nav.panX || 0, py = nav.panY || 0;
  let svg = '<svg width="' + W + '" height="' + H + '" style="background:#1a1a2e;border-radius:8px;cursor:grab" id="map-svg">';
  svg += '<g id="map-pan-group" transform="translate(' + px.toFixed(1) + ',' + py.toFixed(1) + ')">';
  for (let g = 0; g <= 1000; g += 200) {
    const gx = PAD + g * sx, gy = PAD + g * sy;
    svg += '<line x1="' + gx + '" y1="' + PAD + '" x2="' + gx + '" y2="' + (H - PAD) + '" stroke="#ffffff10"/>';
    svg += '<line x1="' + PAD + '" y1="' + gy + '" x2="' + (W - PAD) + '" y2="' + gy + '" stroke="#ffffff10"/>';
  }
  visible.forEach(pt => {
    const cx = PAD + pt.x * sx, cy = PAD + pt.y * sy;
    const color = MAP_COLORS[pt.level] || '#888';
    const r = radii[pt.level] || 4;
    const canDrill = pt.level > 0 && pt.level === nav.level;
    const cls = canDrill ? 'map-point-drillable' : 'map-point-leaf';
    svg += '<circle cx="' + cx.toFixed(1) + '" cy="' + cy.toFixed(1) + '" r="' + r +
      '" fill="' + color + '" stroke="#fff" stroke-width="0.6" class="' + cls +
      '" data-name="' + escapeHtml(pt.name) + '" data-level="' + pt.level +
      '" data-x="' + pt.x + '" data-y="' + pt.y + '"/>';
    svg += '<text x="' + (cx + Math.round(9 * zoom)).toFixed(1) + '" y="' + (cy + Math.round(4 * zoom)).toFixed(1) +
      '" fill="' + color + '" font-size="' + FONT + '" style="pointer-events:none;font-weight:500">' + escapeHtml(pt.name) + '</text>';
  });
  svg += '</g></svg>';

  /* Legend */
  let legend = '<div style="text-align:center;margin-top:6px;font-size:11px;color:var(--text-muted)">';
  legend += '<span style="color:#FF9500">● 大地点</span>  <span style="color:#007AFF">● 小地点</span>  <span style="color:#34C759">● 具体地点</span>';
  legend += '  <span style="color:var(--text-muted)">| 缩放: ' + zoom.toFixed(1) + 'x | 点击地点下钻</span></div>';
  if (state.map.currentLoc) {
    const loc = state.map.currentLoc;
    legend += '<div style="text-align:center;margin-top:2px;font-size:11px;color:var(--text-muted)">当前位置: ' +
      '<span style="color:var(--accent)">' + escapeHtml(loc.area || '?') + ' &rsaquo; ' +
      escapeHtml(loc.district || '?') + ' &rsaquo; ' + escapeHtml(loc.spot || '?') + '</span></div>';
  }

  /* Edit section */
  let ed = '<div style="margin-top:12px;padding-top:10px;border-top:1px solid var(--border-light)">' +
    '<div style="font-size:12px;color:var(--text-muted);margin-bottom:6px">编辑地点:</div>';
  visible.forEach((dp, d) => {
    ed += '<div style="display:flex;align-items:center;justify-content:space-between;padding:3px 0;font-size:12px">' +
      '<span style="flex:1"><span style="color:' + MAP_COLORS[dp.level] + '">●</span> ' +
      '<span class="map-loc-name" id="loc-name-' + d + '">' + escapeHtml(dp.name) + '</span>' +
      '<input class="map-rename-input" id="loc-input-' + d + '" value="' + escapeHtml(dp.name) +
      '" style="display:none;width:140px;font-size:12px;padding:1px 4px;border:1px solid var(--accent);border-radius:3px;background:#1a1a2e;color:var(--text-primary)"/>' +
      ' <span style="color:var(--text-muted);font-size:10px">(' + MAP_LEVEL_NAMES[dp.level] + ')</span></span>' +
      '<span style="flex-shrink:0">' +
      '<button class="btn btn-secondary btn-sm map-edit-btn" data-idx="' + d + '" data-name="' + escapeHtml(dp.name) +
      '" data-level="' + dp.level + '" title="重命名" style="padding:0 6px;font-size:11px">✎</button>' +
      '<button class="btn btn-danger btn-sm map-del-btn" data-name="' + escapeHtml(dp.name) +
      '" data-level="' + dp.level + '" title="删除" style="margin-left:4px">✕</button></span></div>';
  });
  ed += '</div>';

  showModal('地图 — ' + MAP_LEVEL_NAMES[nav.level], ctrl + '<div style="overflow:auto;max-height:450px;text-align:center;border-radius:8px">' + svg + '</div>' + legend + ed, true);

  /* Event bindings */
  setTimeout(() => {
    document.getElementById('map-back-btn')?.addEventListener('click', () => { nav.parents.pop(); nav.level = Math.min(2, nav.level + 1); nav.panX = nav.panY = 0; mapRenderModal(); });
    document.getElementById('map-zoom-in-btn')?.addEventListener('click', () => { nav.zoom = Math.min(3.0, (nav.zoom || 1) + 0.5); mapRenderModal(); });
    document.getElementById('map-zoom-out-btn')?.addEventListener('click', () => { nav.zoom = Math.max(0.5, (nav.zoom || 1) - 0.5); mapRenderModal(); });
    document.getElementById('map-top-btn')?.addEventListener('click', () => { nav.parents = []; nav.level = 2; nav.zoom = 1; nav.panX = nav.panY = 0; mapRenderModal(); });

    const svgEl = document.getElementById('map-svg');
    if (svgEl) {
      let drag = { on: false, sx: 0, sy: 0, px: 0, py: 0 };
      svgEl.onmousedown = e => {
        if (e.button !== 0) return;
        drag = { on: true, sx: e.clientX, sy: e.clientY, px: nav.panX || 0, py: nav.panY || 0 };
        svgEl.style.cursor = 'grabbing'; e.preventDefault();
      };
      window.addEventListener('mousemove', e => {
        if (!drag.on) return;
        nav.panX = drag.px + (e.clientX - drag.sx); nav.panY = drag.py + (e.clientY - drag.sy);
        document.getElementById('map-pan-group')?.setAttribute('transform', 'translate(' + nav.panX.toFixed(1) + ',' + nav.panY.toFixed(1) + ')');
      });
      window.addEventListener('mouseup', () => { if (drag.on) { drag.on = false; svgEl.style.cursor = 'grab'; } });
      svgEl.onwheel = e => { e.preventDefault(); nav.zoom = Math.max(0.5, Math.min(3.0, (nav.zoom || 1) + (e.deltaY < 0 ? 0.25 : -0.25))); mapRenderModal(); return false; };
      svgEl.querySelectorAll('circle[data-level]').forEach(c => {
        c.onclick = () => {
          const lv = parseInt(c.dataset.level);
          if (lv > 0) {
            nav.parents.push({ name: c.dataset.name, level: lv, x: parseInt(c.dataset.x), y: parseInt(c.dataset.y) });
            nav.level = lv - 1; nav.panX = nav.panY = 0; mapRenderModal();
          }
        };
      });
    }

    document.querySelectorAll('.map-edit-btn').forEach(btn => {
      btn.onclick = function() {
        const idx = this.dataset.idx, nameSpan = document.getElementById('loc-name-' + idx), inputEl = document.getElementById('loc-input-' + idx);
        if (!nameSpan || !inputEl) return;
        if (inputEl.style.display === 'none') {
          nameSpan.style.display = 'none'; inputEl.style.display = 'inline'; inputEl.focus(); inputEl.select();
          this.textContent = '✓'; this.title = '确认';
        } else {
          const oldName = this.dataset.name, newName = inputEl.value.trim(), level = this.dataset.level;
          if (!newName || newName === oldName) { nameSpan.style.display = 'inline'; inputEl.style.display = 'none'; this.textContent = '✎'; this.title = '重命名'; return; }
          (async () => {
            const r = await renameLocation({ level, oldName, newName });
            if (r.ok) {
              EventBus.emit('chat:add', { role: 'system', text: '已重命名: ' + oldName + ' → ' + newName });
              if (r.state?.mapPoints) state.map.allPoints = r.state.mapPoints;
              mapRenderModal();
            } else {
              EventBus.emit('chat:add', { role: 'system', text: '重命名失败: ' + (r.error || '未知错误') });
              nameSpan.style.display = 'inline'; inputEl.style.display = 'none'; this.textContent = '✎'; this.title = '重命名';
            }
          })();
        }
      };
    });

    document.querySelectorAll('.map-del-btn').forEach(btn => {
      btn.onclick = async function() {
        const name = this.dataset.name, level = this.dataset.level;
        if (!confirm('确定删除 "' + name + '" 吗？')) return;
        const r = await deleteLocation({ level, name });
        if (r.ok) {
          EventBus.emit('chat:add', { role: 'system', text: '已删除: ' + name });
          if (r.state?.mapPoints) state.map.allPoints = r.state.mapPoints;
          mapRenderModal();
        } else EventBus.emit('chat:add', { role: 'system', text: '删除失败: ' + (r.error || '未知错误') });
      };
    });
  }, 50);
}

export async function openMap() {
  try {
    const data = await getState();
    const points = data?.mapPoints?.length ? data.mapPoints : [];
    if (!points.length) { showModal('地图', '<p style="color:var(--text-muted);text-align:center">暂无地点数据</p>'); return; }
    state.map.currentLoc = data.location || data.env || {};
    state.map.allPoints = points;
    state.map.nav = { level: 2, parents: [], zoom: 1.0, panX: 0, panY: 0 };
    mapRenderModal();
  } catch (e) { showModal('地图', '<p style="color:red">加载失败: ' + e.message + '</p>'); }
}

export function initMap() { $('btn-map').onclick = openMap; }

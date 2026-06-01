/* ═══════════════════════════════════════════════════════════════
   ui/timeline.js — 时间轴组件
   ═══════════════════════════════════════════════════════════════ */

import { escapeHtml } from '../shared/utils.js';

export function renderTimeline({ events, containerId }) {
  const container = document.getElementById(containerId);
  if (!container) return;

  let html = '<div style="position:relative;padding-left:24px">';
  html += '<div style="position:absolute;left:8px;top:4px;bottom:4px;width:2px;background:var(--border-light)"></div>';

  events.forEach(ev => {
    html += '<div style="position:relative;margin-bottom:14px">';
    html += '<div style="position:absolute;left:-20px;top:4px;width:10px;height:10px;' +
      'border-radius:50%;background:var(--accent);border:2px solid var(--bg-card)"></div>';
    html += '<div style="font-size:11px;color:var(--text-muted);margin-bottom:2px">' +
      escapeHtml(ev.time) + '</div>';
    html += '<div style="font-size:13px;font-weight:500;color:var(--text-primary)">' +
      escapeHtml(ev.title) + '</div>';
    if (ev.detail) {
      html += '<div style="font-size:12px;color:var(--text-secondary);margin-top:2px">' +
        escapeHtml(ev.detail) + '</div>';
    }
    html += '</div>';
  });

  html += '</div>';
  container.innerHTML = html;
}

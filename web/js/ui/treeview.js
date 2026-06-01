/* ═══════════════════════════════════════════════════════════════
   ui/treeview.js — 树结构视图组件
   ═══════════════════════════════════════════════════════════════ */

import { escapeHtml } from '../shared/utils.js';

function renderNode(node, depth) {
  const indent = depth * 16;
  const hasChildren = node.children?.length > 0;
  const toggleId = 'tv_' + Math.random().toString(36).slice(2, 8);

  let html = '<div style="padding:3px 0;padding-left:' + indent + 'px;font-size:13px;">';
  if (hasChildren) {
    html += '<span id="' + toggleId + '" style="cursor:pointer;color:var(--text-muted);margin-right:4px;user-select:none">▶</span>';
    html += '<span style="color:var(--text-primary)">' + escapeHtml(node.label) + '</span>';
    html += '<div id="' + toggleId + '_children" style="display:none">';
    node.children.forEach(c => { html += renderNode(c, depth + 1); });
    html += '</div>';
  } else {
    html += '<span style="display:inline-block;width:16px;margin-right:4px"></span>';
    html += '<span style="color:var(--text-secondary)">' + escapeHtml(node.label) + '</span>';
  }
  html += '</div>';
  return html;
}

export function renderTreeView({ data, containerId }) {
  const container = document.getElementById(containerId);
  if (!container) return;

  let html = '';
  data.forEach(n => { html += renderNode(n, 0); });
  container.innerHTML = html;

  container.querySelectorAll('[id$="_children"]').forEach(childrenEl => {
    const toggleId = childrenEl.id.replace('_children', '');
    const toggle = document.getElementById(toggleId);
    if (toggle) {
      toggle.onclick = () => {
        const hidden = childrenEl.style.display === 'none';
        childrenEl.style.display = hidden ? '' : 'none';
        toggle.textContent = hidden ? '▼' : '▶';
      };
    }
  });
}

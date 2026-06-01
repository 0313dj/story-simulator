/* ═══════════════════════════════════════════════════════════════
   ui/panel.js — 可折叠面板组件
   ═══════════════════════════════════════════════════════════════ */

export function createPanel({ title, content, collapsed }) {
  const wrapper = document.createElement('div');
  wrapper.className = 'panel card';

  const header = document.createElement('div');
  header.className = 'panel-header';
  Object.assign(header.style, {
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
    cursor: 'pointer', marginBottom: '8px'
  });

  const titleEl = document.createElement('span');
  titleEl.className = 'panel-title';
  titleEl.textContent = title;
  Object.assign(titleEl.style, {
    fontSize: '13px', fontWeight: '600', color: 'var(--text-primary)'
  });

  const toggle = document.createElement('span');
  toggle.className = 'panel-toggle';
  toggle.textContent = collapsed ? '▶' : '▼';
  Object.assign(toggle.style, {
    fontSize: '10px', color: 'var(--text-muted)'
  });

  header.append(titleEl, toggle);

  const body = document.createElement('div');
  body.className = 'panel-body';
  body.innerHTML = content;
  if (collapsed) body.style.display = 'none';

  header.onclick = () => {
    const hidden = body.style.display === 'none';
    body.style.display = hidden ? '' : 'none';
    toggle.textContent = hidden ? '▼' : '▶';
  };

  wrapper.append(header, body);
  return wrapper;
}

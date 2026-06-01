/* ═══════════════════════════════════════════════════════════════
   ui/tabs.js — 标签页组件
   ═══════════════════════════════════════════════════════════════ */

export function createTabs({ tabs, activeId }) {
  const wrapper = document.createElement('div');
  wrapper.className = 'tabs';

  const bar = document.createElement('div');
  bar.className = 'tabs-bar';
  Object.assign(bar.style, {
    display: 'flex', gap: '2px',
    borderBottom: '2px solid var(--border-light)', marginBottom: '10px'
  });

  const panels = document.createElement('div');
  panels.className = 'tabs-panels';
  const tabDefs = {};

  tabs.forEach(tab => {
    const btn = document.createElement('button');
    btn.className = 'tab-btn';
    btn.textContent = tab.label;
    btn.dataset.tabId = tab.id;
    Object.assign(btn.style, {
      padding: '6px 14px', border: 'none', borderRadius: '6px 6px 0 0',
      background: 'transparent', color: 'var(--text-muted)',
      fontFamily: 'var(--font)', fontSize: '13px', cursor: 'pointer',
      transition: 'all 0.15s', borderBottom: '2px solid transparent',
      marginBottom: '-2px'
    });
    bar.appendChild(btn);

    const panel = document.createElement('div');
    panel.className = 'tab-panel';
    panel.dataset.tabId = tab.id;
    panel.innerHTML = tab.content;
    panel.style.display = 'none';
    panels.appendChild(panel);

    tabDefs[tab.id] = { btn, panel };
  });

  function activate(id) {
    Object.values(tabDefs).forEach(t => {
      t.panel.style.display = 'none';
      t.btn.style.color = 'var(--text-muted)';
      t.btn.style.borderBottomColor = 'transparent';
      t.btn.style.fontWeight = '400';
    });
    if (tabDefs[id]) {
      tabDefs[id].panel.style.display = '';
      tabDefs[id].btn.style.color = 'var(--accent)';
      tabDefs[id].btn.style.borderBottomColor = 'var(--accent)';
      tabDefs[id].btn.style.fontWeight = '600';
    }
  }

  bar.addEventListener('click', e => {
    const btn = e.target.closest('.tab-btn');
    if (btn?.dataset.tabId) activate(btn.dataset.tabId);
  });

  if (activeId && tabDefs[activeId]) activate(activeId);
  else if (tabs.length) activate(tabs[0].id);

  wrapper.append(bar, panels);
  return wrapper;
}

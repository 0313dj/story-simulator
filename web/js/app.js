/* ═══════════════════════════════════════════════════════════════
   App state
   ═══════════════════════════════════════════════════════════════ */
const state = {
  dark: false,
  busy: false,
  apiReady: false,
  worldReady: false,
  debugMode: false,
  lastDebugTrace: null,
};

/* ═══════════════════════════════════════════════════════════════
   API call
   ═══════════════════════════════════════════════════════════════ */
async function api(cmd, extra = {}) {
  const resp = await fetch('/api', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ cmd, ...extra }),
  });
  if (!resp.ok) {
    addSystemMsg('服务器连接失败');
    return { ok: false, error: 'HTTP ' + resp.status };
  }
  /* Bug #44: catch non-JSON responses (e.g. 500 error pages) */
  try {
    return await resp.json();
  } catch (e) {
    addSystemMsg('服务器返回无效响应');
    return { ok: false, error: 'Invalid JSON response' };
  }
}

/* ═══════════════════════════════════════════════════════════════
   UI helpers
   ═══════════════════════════════════════════════════════════════ */

function $(id) { return document.getElementById(id); }

/* Bug #16 fix: HTML-escape helper to prevent XSS from AI-generated text */
function escapeHtml(text) {
  if (!text) return '';
  return String(text)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function addMsg(html, cls) {
  const container = $('chat-messages');
  const div = document.createElement('div');
  div.className = cls || '';
  div.innerHTML = html;
  container.appendChild(div);
  $('chat-area').scrollTop = $('chat-area').scrollHeight;
}

function addSystemMsg(text) {
  addMsg(escapeHtml('[系统] ' + text), 'msg-system');
}

function addAIMsg(text) {
  addMsg(escapeHtml(text), 'msg-ai');
  addMsg('──────────────────────────────', 'msg-sep');
}

function addPlayerMsg(text) {
  addMsg(escapeHtml('> ' + text), 'msg-player');
}

function updatePlayer(data) {
  if (!data) return;
  $('char-name').textContent = data.name || '---';
  $('char-age').textContent = data.age || '--';
  $('money-text').textContent = (data.money || 0) + ' G';

  const st = data.statusText || '正常';
  $('status-text').textContent = st;
  const colors = { '正常':'#34C759','饥饿':'#FF9500','疲惫':'#FF9500',
    '生病':'#FF9500','受伤':'#FF3B30','兴奋':'#007AFF','愤怒':'#FF3B30',
    '悲伤':'#007AFF','开心':'#34C759' };
  $('status-text').style.color = colors[st] || 'var(--text-primary)';
  $('status-dot').style.color = colors[st] || 'var(--text-primary)';

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
  const bar = $(id);
  const fill = bar.querySelector('.attr-fill');
  const lbl = bar.querySelector('.attr-label');
  const v = bar.querySelector('.attr-val');
  lbl.textContent = label;
  v.textContent = (val || 0) + '/100';
  fill.style.width = Math.max(2, Math.min(100, val || 0)) + '%';
}

function updateEnv(envData, locData) {
  if (!envData) return;

  $('env-era').textContent = envData.era || '---';
  $('env-weather').textContent = envData.weather || '---';

  const wicons = {
    '晴':'☀', '多云':'⛅', '阴':'☁', '小雨':'☔', '大雨':'☂',
    '雷暴':'⛈', '雪':'❄', '暴风雪':'❄❄', '雾':'☁', '大风':'≈', '沙尘暴':'≈'
  };
  $('weather-icon').textContent = wicons[envData.weather] || '☀';

  /* Time: new format nested under .time, old format flat */
  const t = envData.time || envData;
  if (t.year) {
    $('env-time').textContent =
      t.year + '年' + String(t.month).padStart(2,'0') + '月' +
      String(t.day).padStart(2,'0') + '日 ' + (t.weekday||'') +
      '  ' + String(t.hour).padStart(2,'0') + ':' + String(t.minute).padStart(2,'0');
  }

  /* Location: from locData or fall back to envData (old format) */
  const loc = locData || envData;
  if (loc.area) {
    $('env-location').innerHTML =
      loc.area + ' <span class="sep">›</span> ' +
      (loc.district || '') + ' <span class="sep">›</span> ' +
      (loc.spot || '');
  }

  /* NPC total count set in refreshUI after npcs data loaded */
}

function refreshUI(data) {
  /* New format: character / npcs / location / environment */
  /* Backward compat: player / env */
  if (data.character) updatePlayer(data.character);
  else if (data.player) updatePlayer(data.player);

  if (data.environment) updateEnv(data.environment, data.location);
  else if (data.env) updateEnv(data.env, data.env);

  state.apiReady = data.apiReady;
  state.worldReady = data.worldReady;
  state._npcs = data.npcs || [];
  state._mapPoints = data.mapPoints || [];

  /* Update NPC button with total NPC character card count */
  $('btn-npc').textContent = 'NPC (' + state._npcs.length + ')';
}

/* ═══════════════════════════════════════════════════════════════
   Actions
   ═══════════════════════════════════════════════════════════════ */

async function sendMessage() {
  const field = $('input-field');
  const text = field.value.trim();
  if (!text || state.busy) return;
  field.value = '';

  addPlayerMsg(text);
  $('btn-send').disabled = true;
  $('btn-send').innerHTML = '<span class="spinner"></span>等待';
  state.busy = true;

  const r = await api('send_message', { text });

  $('btn-send').disabled = false;
  $('btn-send').textContent = '发送';
  state.busy = false;

  if (r.ok) {
    if (r.reply) addAIMsg(r.reply);
    if (r.notification) addSystemMsg(r.notification);
    if (r.state) refreshUI(r.state);
    /* Stage 6: capture debug trace for Developer Mode */
    if (r.debugTrace) {
      renderDebugTrace(r.debugTrace);
    }
  } else {
    addSystemMsg('错误: ' + (r.error || '未知'));
  }
}

async function loadState() {
  const r = await api('get_state');
  if (r.ok) refreshUI(r);
}

/* ═══════════════════════════════════════════════════════════════
   Modals
   ═══════════════════════════════════════════════════════════════ */

function showModal(title, body, wide) {
  $('modal-title').textContent = title;
  $('modal-body').innerHTML = body;
  if (wide) $('modal-box').style.width = '560px';
  else $('modal-box').style.width = '480px';
  $('modal-overlay').classList.remove('hidden');
}
function hideModal() { $('modal-overlay').classList.add('hidden'); }

/* ── API Management ── */
async function showApiManager() {
  const profiles = await api('get_profiles');
  let html = '<div class="form-row"><label>提供商</label><select id="api-provider">' +
    '<option value="">--</option>' +
    '<option value="DeepSeek|https://api.deepseek.com/v1|deepseek-chat">DeepSeek</option>' +
    '<option value="OpenAI|https://api.openai.com/v1|gpt-4o">OpenAI</option>' +
    '<option value="阿里云|https://dashscope.aliyuncs.com/compatible-mode/v1|qwen-plus">阿里云 DashScope</option>' +
    '<option value="智谱|https://open.bigmodel.cn/api/paas/v4|glm-4-flash">智谱 GLM</option>' +
    '<option value="月之暗面|https://api.moonshot.cn/v1|moonshot-v1-8k">月之暗面 Kimi</option>' +
    '<option value="百川|https://api.baichuan-ai.com/v1|baichuan2-turbo">百川</option>' +
    '<option value="硅基流动|https://api.siliconflow.cn/v1|Qwen/Qwen2.5-7B-Instruct">硅基流动</option>' +
    '</select></div>';
  html += '<div class="form-row"><label>地址</label><input id="api-ep" placeholder="https://"></div>';
  html += '<div class="form-row"><label>模型</label><input id="api-model" placeholder="model-name"></div>';
  html += '<div class="form-row"><label>密钥</label><input id="api-key" type="password" placeholder="sk-..."></div>';
  html += '<div class="form-row"><label>配置名称</label><input id="api-name" placeholder="Default"></div>';
  html += '<button class="btn btn-primary" id="api-save-btn">保存配置</button>';

  /* Get current active model to highlight */
  const status = await api('get_api_status');
  const activeModel = (status && status.model) ? status.model : '';

  if (Array.isArray(profiles) && profiles.length > 0) {
    html += '<div style="margin-top:16px"><strong>已保存配置:</strong></div>';
    profiles.forEach(p => {
      const isActive = (activeModel && p.model === activeModel);
      html += '<div class="save-entry">' +
        '<div class="save-info">' +
        '<div class="save-name">' + (isActive ? '★ ' : '') + p.name + '</div>' +
        '<div class="save-meta">' + p.model + (isActive ? ' <span style="color:#34C759">●当前使用</span>' : '') + '</div>' +
        '</div>' +
        '<button class="btn btn-primary btn-sm api-use-btn" data-name="' + p.name + '"' + (isActive ? ' disabled' : '') + '>使用</button>' +
        '<button class="btn btn-danger btn-sm api-del-btn" data-name="' + p.name + '">删除</button></div>';
    });
  }
  showModal('API 管理', html);

  $('api-provider').onchange = function() {
    const parts = this.value.split('|');
    if (parts.length >= 3) {
      $('api-ep').value = parts[1];
      $('api-model').value = parts[2];
      $('api-name').value = parts[0];
    }
  };
  $('api-save-btn').onclick = async function() {
    const r = await api('save_profile', {
      name: $('api-name').value,
      endpoint: $('api-ep').value,
      apiKey: $('api-key').value,
      model: $('api-model').value,
    });
    if (r.ok) { addSystemMsg('API 配置已保存，已自动切换为当前配置'); hideModal(); }
    else addSystemMsg('保存失败: ' + r.error);
  };
  document.querySelectorAll('.api-use-btn').forEach(b => {
    b.onclick = async function() {
      const r = await api('activate_profile', { name: this.dataset.name });
      if (r.ok) { addSystemMsg('已切换至配置: ' + this.dataset.name); showApiManager(); }
      else addSystemMsg('切换失败: ' + r.error);
    };
  });
  document.querySelectorAll('.api-del-btn').forEach(b => {
    b.onclick = async function() {
      const r = await api('delete_profile', { name: this.dataset.name });
      if (r.ok) showApiManager();
    };
  });
}

/* ── Save Manager ── */
async function showSaveManager() {
  const saves = await api('list_saves');
  let html = '';
  if (Array.isArray(saves) && saves.length > 0) {
    saves.forEach(s => {
      html += '<div class="save-entry"><div class="save-info">' +
        '<div class="save-name">' + s.playerName + '</div>' +
        '<div class="save-meta">' + s.year + '-' + String(s.month).padStart(2,'0') +
        '-' + String(s.day).padStart(2,'0') + '  ' + s.saveTime + '</div>' +
        '</div><div class="save-actions">' +
        '<button class="btn btn-secondary btn-sm load-save-btn" data-file="' + s.filename + '">加载</button>' +
        '<button class="btn btn-danger btn-sm del-save-btn" data-file="' + s.filename + '">删除</button>' +
        '</div></div>';
    });
  } else {
    html = '<p style="color:var(--text-muted)">无存档</p>';
  }
  showModal('存档管理', html, true);

  document.querySelectorAll('.load-save-btn').forEach(b => {
    b.onclick = async function() {
      const r = await api('load_save', { filename: this.dataset.file });
      if (r.ok) { refreshUI(r); addSystemMsg('存档已加载'); hideModal(); }
      else addSystemMsg('加载失败');
    };
  });
  document.querySelectorAll('.del-save-btn').forEach(b => {
    b.onclick = async function() {
      await api('delete_save', { filename: this.dataset.file });
      showSaveManager();
    };
  });
}

/* ── New World ── */
function showNewWorld() {
  let html = '<div class="form-row"><label>姓名</label><input id="wc-name" value=""></div>';
  html += '<div class="form-row"><label>年龄</label><input id="wc-age" value="25"></div>';
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

  $('wc-create-btn').onclick = async function() {
    $('wc-create-btn').innerHTML = '<span class="spinner"></span>AI 创建中...';
    $('wc-create-btn').disabled = true;
    const r = await api('create_world', {
      name: $('wc-name').value,
      age: $('wc-age').value,
      clothing: $('wc-clothing').value,
      money: $('wc-money').value,
      appearance: $('wc-app').value,
      constitution: $('wc-con').value,
      intelligence: $('wc-int').value,
      skills: $('wc-skills').value,
      items: $('wc-items').value,
      story: $('wc-story').value,
    });
    if (r.ok) {
      hideModal();
      if (r.state) refreshUI(r.state);
      addSystemMsg(r.message + '  存档: ' + r.saveFile);
    } else {
      addSystemMsg('创建失败: ' + r.error);
      $('wc-create-btn').innerHTML = '创建世界';
      $('wc-create-btn').disabled = false;
    }
  };
}

/* ── Detail popups ── */
function showDetails(title, items, type) {
  let html = '';
  if (!items || items.length === 0) {
    html = '<p style="color:var(--text-muted)">(无)</p>';
  } else if (type === 'skills') {
    items.forEach(s => { html += '<div style="padding:4px 0">' + s.name + '  <strong>Lv.' + s.level + '</strong></div>'; });
  } else if (type === 'items') {
    items.forEach(i => { html += '<div style="padding:4px 0">' + i.name + '  x' + i.qty + '</div>'; });
  } else if (type === 'relations') {
    items.forEach(r => {
      html += '<div style="padding:4px 0">' + r.target + '  [' + r.type + ']  好感度: ' + r.affinity + '</div>';
    });
  }
  showModal(title, html);
}

/* ═══════════════════════════════════════════════════════════════
   Event bindings
   ═══════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════
   Developer Mode — Debug Panel
   ═══════════════════════════════════════════════════════════════ */

function toggleDebugMode() {
  state.debugMode = !state.debugMode;
  var panel = $('debug-panel');
  var btn = $('btn-debug');
  if (state.debugMode) {
    panel.classList.remove('hidden');
    btn.style.color = '#0ea5e9';
    btn.style.background = 'rgba(14,165,233,0.15)';
    if (state.lastDebugTrace) renderDebugTrace(state.lastDebugTrace);
  } else {
    panel.classList.add('hidden');
    btn.style.color = '';
    btn.style.background = '';
  }
}

function renderDebugTrace(trace) {
  if (!state.debugMode) return;
  if (!trace) { $('debug-body').innerHTML = '<div style="color:#666">无追踪数据</div>'; return; }

  state.lastDebugTrace = trace;
  var html = '';

  /* ── Helper: escape HTML ── */
  function esc(s) {
    if (!s) return '';
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  }

  /* ── Helper: add a pipeline step ── */
  function step(cls, icon, label, body) {
    html += '<div class="debug-step ' + cls + '">';
    html += '<div class="debug-step-label"><span class="debug-step-icon">' + icon + '</span>' + esc(label) + '</div>';
    if (body) html += '<div class="debug-step-body">' + body + '</div>';
    html += '</div>';
    html += '<div class="debug-arrow">&#x2193;</div>';
  }

  /* ── 1. User Input ── */
  if (trace.userInput) {
    step('debug-intent', '👤', '用户输入', esc(trace.userInput));
  }

  /* ── 2. Intent ── */
  if (trace.intent) {
    var ibody = '';
    if (trace.intent.type) ibody += '<div class="debug-kv"><span class="debug-kv-key">类型:</span><span class="debug-kv-val">' + esc(trace.intent.type) + '</span></div>';
    if (trace.intent.target) ibody += '<div class="debug-kv"><span class="debug-kv-key">目标:</span><span class="debug-kv-val">' + esc(trace.intent.target) + '</span></div>';
    if (trace.intent.confidence != null) ibody += '<div class="debug-kv"><span class="debug-kv-key">置信度:</span><span class="debug-kv-val">' + (trace.intent.confidence * 100).toFixed(0) + '% (Level ' + (trace.intent.level || 1) + ')</span></div>';
    step('debug-intent', '🎯', 'Intent 意图识别', ibody);
  }

  /* ── 3. Plan ── */
  if (trace.plan) {
    var pbody = '';
    if (trace.plan.goal) pbody += '<div class="debug-kv"><span class="debug-kv-key">目标:</span><span class="debug-kv-val">' + esc(trace.plan.goal) + '</span></div>';
    pbody += '<div class="debug-kv"><span class="debug-kv-key">步骤数:</span><span class="debug-kv-val">' + (trace.plan.stepCount || 0) + '</span></div>';
    if (trace.plan.steps && trace.plan.steps.length > 0) {
      pbody += '<pre>';
      trace.plan.steps.forEach(function(s, i) {
        pbody += '  ' + (i+1) + '. ' + esc(s.action);
        if (s.target) pbody += ' → ' + esc(s.target);
        pbody += ' (' + (s.estimatedTicks || '?') + 'min)\n';
      });
      pbody += '</pre>';
    }
    step('debug-plan', '📋', 'Planner 计划生成', pbody);
  }

  /* ── 4. ActionProposal ── */
  if (trace.actionProposal) {
    var abody = '<pre>' + esc(trace.actionProposal) + '</pre>';
    step('debug-proposal', '📝', 'ActionProposal 动作提案', abody);
  }

  /* ── 5. Changes ── */
  if (trace.changes) {
    var cbody = '<pre>' + esc(trace.changes) + '</pre>';
    step('debug-changes', '🔄', 'Changes 变更指令', cbody);
  }

  /* ── 6. Rule Hits ── */
  if (trace.ruleHits && trace.ruleHits.length > 0) {
    var rbody = '<div class="debug-kv"><span class="debug-kv-key">命中规则:</span><span class="debug-kv-val">' + trace.ruleHits.length + ' 条</span></div>';
    trace.ruleHits.forEach(function(r) {
      rbody += '<div style="padding:2px 0;font-size:10px">';
      rbody += '  <span style="color:#f38ba8">#' + r.ruleId + '</span>';
      if (r.condition) rbody += ' <span style="color:#6c7086">IF</span> <span style="color:#a6e3a1">' + esc(r.condition) + '</span>';
      rbody += '</div>';
    });
    step('debug-rulehits', '⚡', 'Rule Engine 规则命中', rbody);
  } else if (trace.ruleHits) {
    step('debug-rulehits', '⚡', 'Rule Engine 规则命中', '<span style="color:#6c7086">无规则命中</span>');
  }

  /* ── 7. Narrative ── */
  if (trace.narrative) {
    var nbody = '';
    if (trace.narrative.source) nbody += '<div class="debug-kv"><span class="debug-kv-key">来源:</span><span class="debug-kv-val">' + esc(trace.narrative.source) + '</span></div>';
    if (trace.narrative.length) nbody += '<div class="debug-kv"><span class="debug-kv-key">长度:</span><span class="debug-kv-val">' + trace.narrative.length + ' 字符</span></div>';
    if (trace.narrative.style) nbody += '<div class="debug-kv"><span class="debug-kv-key">风格:</span><span class="debug-kv-val">' + esc(trace.narrative.style) + '</span></div>';
    step('debug-narrative', '💬', 'Narrative 叙事生成', nbody);
  }

  /* ── 8. NPC Actions ── */
  if (trace.npcActions && trace.npcActions.length > 0) {
    var nabody = '<pre>';
    trace.npcActions.forEach(function(a) {
      nabody += esc(a.npcName || '?') + ': ' + esc(a.goalType || '?');
      if (a.action) nabody += ' → ' + esc(a.action);
      if (a.rulesFired) nabody += ' [规则命中:' + a.rulesFired + ']';
      nabody += '\n';
    });
    nabody += '</pre>';
    step('debug-npcactions', '🤖', 'NPC Brain 自主行为 (' + trace.npcActions.length + '个NPC)', nabody);
  }

  /* ── 9. World Simulation ── */
  if (trace.worldSim) {
    step('debug-worldsim', '🌍', 'World Director 世界模拟', '<pre>' + esc(trace.worldSim) + '</pre>');
  }

  /* Remove trailing arrow */
  if (html.endsWith('<div class="debug-arrow">↓</div>')) {
    html = html.substring(0, html.length - 35);
  }

  $('debug-body').innerHTML = html || '<div style="color:#666">无追踪数据</div>';
}

$('btn-debug').onclick = toggleDebugMode;
$('debug-close').onclick = function() { toggleDebugMode(); };

$('btn-send').onclick = sendMessage;
$('input-field').onkeydown = function(e) { if (e.key === 'Enter') sendMessage(); };

$('btn-theme').onclick = function() {
  state.dark = !state.dark;
  document.body.classList.toggle('dark', state.dark);
  $('btn-theme').textContent = state.dark ? '☼' : '☾';
};

$('btn-save').onclick = async function() {
  const r = await api('quick_save');
  addSystemMsg(r.ok ? '已保存' : r.error);
};
$('btn-load').onclick = async function() {
  const r = await api('quick_load');
  if (r.ok) { refreshUI(r); addSystemMsg('已读取快速存档'); }
  else addSystemMsg(r.error);
};
$('btn-new').onclick = showNewWorld;

$('btn-apimgr').onclick = showApiManager;
$('btn-savemgr').onclick = showSaveManager;

$('btn-skills').onclick = async function() {
  const r = await api('get_state');
  const ch = r.character || r.player;
  if (r.ok && ch) showDetails('技能列表', ch.skills, 'skills');
};
$('btn-items').onclick = async function() {
  const r = await api('get_state');
  const ch = r.character || r.player;
  if (r.ok && ch) showDetails('持有物', ch.items, 'items');
};
$('btn-relations').onclick = async function() {
  const r = await api('get_state');
  const ch = r.character || r.player;
  if (r.ok && ch) showDetails('人物关系', ch.relations, 'relations');
};
$('btn-map').onclick = async function() {
  try {
    const data = await api('get_state');
    const points = (data && data.mapPoints && data.mapPoints.length) ? data.mapPoints : [];
    if (!points.length) {
      showModal('地图', '<p style="color:var(--text-muted);text-align:center">暂无地点数据<br>请先创建世界或探索新地点</p>');
      return;
    }
    const loc = data.location || data.env || {};
    const current = (loc.area||'?') + ' / ' + (loc.district||'?') + ' / ' + (loc.spot||'?');
    const labels = ['具体地点','小地点','大地点'];
    const colors = ['#34C759','#007AFF','#FF9500'];
    const W = 500, H = 400, PAD = 30;
    const sx = (W - PAD*2) / 1000;
    const sy = (H - PAD*2) / 1000;

    let svg = '<svg width="'+W+'" height="'+H+'" style="background:#1a1a2e;border-radius:8px">';
    for (let g = 0; g <= 1000; g += 200) {
      let gx = PAD + g * sx;
      let gy = PAD + g * sy;
      svg += '<line x1="'+gx+'" y1="'+PAD+'" x2="'+gx+'" y2="'+(H-PAD)+'" stroke="#ffffff10"/>';
      svg += '<line x1="'+PAD+'" y1="'+gy+'" x2="'+(W-PAD)+'" y2="'+gy+'" stroke="#ffffff10"/>';
    }
    for (let i = 0; i < points.length; i++) {
      let pt = points[i];
      let cx = PAD + pt.x * sx;
      let cy = PAD + pt.y * sy;
      let color = colors[pt.level] || '#888';
      let radius = pt.level === 2 ? 6 : (pt.level === 1 ? 4 : 3);
      svg += '<circle cx="'+cx.toFixed(1)+'" cy="'+cy.toFixed(1)+'" r="'+radius+'" fill="'+color+'" stroke="#fff" stroke-width="0.5"/>';
      svg += '<text x="'+(cx+8).toFixed(1)+'" y="'+(cy+4).toFixed(1)+'" fill="'+color+'" font-size="10">'+pt.name+'</text>';
    }
    svg += '</svg>';

    var html = '<div style="text-align:center;margin-bottom:6px;color:var(--text-muted)">当前: '+current+'</div>';
    html += '<div style="text-align:center">'+svg+'</div>';
    html += '<div style="text-align:center;margin-top:6px;font-size:11px;color:var(--text-muted)">';
    html += '<span style="color:#FF9500">●大地点</span> ';
    html += '<span style="color:#007AFF">●小地点</span> ';
    html += '<span style="color:#34C759">●具体地点</span></div>';
    showModal('地图 ('+points.length+'个地点)', html, true);
  } catch(e) {
    showModal('地图', '<p style="color:red">加载失败: '+e.message+'</p>');
  }
};

$('btn-npc').onclick = async function() {
  const r = await api('get_state');
  const npcs = (r && r.npcs) ? r.npcs : (state._npcs || []);
  const presentNames = {};
  if (r && r.location && r.location.presentNpcs) {
    r.location.presentNpcs.forEach(function(p) { presentNames[p.name] = true; });
  }
  let html = '';
  if (!npcs.length) {
    html = '<p style="color:var(--text-muted)">(没有已记录的NPC角色卡)</p>';
  } else {
    let sorted = npcs.slice().sort(function(a,b) {
      return (presentNames[b.name] ? 1 : 0) - (presentNames[a.name] ? 1 : 0);
    });
    sorted.forEach(function(n) {
      let st = n.statusText || '正常';
      let marker = presentNames[n.name] ? ' <span style="color:#34C759">●在场</span>' : '';
      html += '<div style="padding:6px 0;border-bottom:1px solid var(--border-light)">' +
        '<strong>' + n.name + '</strong>' + marker +
        '  年龄:' + (n.age||'?') + '岁<br>' +
        '<span style="font-size:11px;color:var(--text-muted)">身份:' + (n.personality||'?') +
        '  状态:' + st + '  好感:' + (n.playerAffinity||0) +
        '  金钱:' + (n.money||0) + 'G</span>' +
        '</div>';
    });
  }
  showModal('NPC 角色卡 (' + npcs.length + '个)', html);
};

$('modal-close').onclick = hideModal;
$('modal-overlay').onclick = function(e) {
  if (e.target === $('modal-overlay')) hideModal();
};

/* ═══════════════════════════════════════════════════════════════
   Startup
   ═══════════════════════════════════════════════════════════════ */
(async function init() {
  try {
    addSystemMsg('正在连接服务器...');
    const r = await api('get_state');
    if (r.ok) {
      refreshUI(r);
      if (r.worldReady) {
        addSystemMsg('世界已就绪。输入指令开始冒险。');
      } else if (r.apiReady) {
        addSystemMsg('API 已配置。点击 [新建] 创建新世界。');
      } else {
        addSystemMsg('请先配置 API：点击右上角 ⚙ API 管理');
      }
    } else {
      addSystemMsg('连接失败，请刷新页面');
    }
  } catch (e) {
    /* Bug #48: friendly error on init failure */
    addSystemMsg('初始化失败: ' + e.message + '。请检查服务器是否运行，然后刷新页面。');
  }
})();

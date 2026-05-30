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
  $('char-gender').textContent = data.gender || '--';
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

  /* Update NPC button with total NPC character card count */
  $('btn-npc').textContent = 'NPC (' + state._npcs.length + ')';

  /* Token usage display */
  if (data.tokenUsage && data.tokenUsage.calls > 0) {
    const tu = data.tokenUsage;
    const total = parseInt(tu.total) || 0;
    const calls = tu.calls || 0;
    if (total >= 1000000) {
      $('env-tokens').textContent = (total / 1000000).toFixed(1) + 'M (' + calls + '次)';
    } else if (total >= 1000) {
      $('env-tokens').textContent = (total / 1000).toFixed(1) + 'K (' + calls + '次)';
    } else {
      $('env-tokens').textContent = total + ' (' + calls + '次)';
    }
  }
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

  $('wc-create-btn').onclick = async function() {
    $('wc-create-btn').innerHTML = '<span class="spinner"></span>AI 创建中...';
    $('wc-create-btn').disabled = true;
    const r = await api('create_world', {
      name: $('wc-name').value,
      age: $('wc-age').value,
      gender: $('wc-gender').value,
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
    items.forEach(s => { html += '<div style="padding:4px 0;font-size:13px">' + escapeHtml(s.name) + '  <strong>Lv.' + s.level + '</strong></div>'; });
  } else if (type === 'items') {
    items.forEach(i => { html += '<div style="padding:4px 0;font-size:13px">' + escapeHtml(i.name) + '  x' + i.qty + '</div>'; });
  } else if (type === 'relations') {
    items.forEach(r => {
      html += '<div style="padding:4px 0;font-size:13px">' + escapeHtml(r.target) + '  [' + escapeHtml(r.type) + ']  好感度: ' + r.affinity + '</div>';
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
/* ═══════════════════════════════════════════════════════════════
   Map drill-down helpers

   Two independent axes:
     nav.level  — drill-down level (2=大地点 1=小地点 0=具体地点)
                  changed by clicking a point (drill in) or [返回] (drill out)
     nav.zoom   — visual zoom factor (default 1.0, range 0.5–3.0)
                  changed by [放大]/[缩小] buttons and mouse wheel
   ═══════════════════════════════════════════════════════════════ */

function mapGetVisible(allPoints, nav) {
  if (!allPoints || !allPoints.length) return [];

  if (nav.level === 2) {
    return allPoints.filter(function(p) { return p.level === 2; });
  }

  if (nav.parents.length === 0) {
    return allPoints.filter(function(p) { return p.level === nav.level; });
  }

  var parent = nav.parents[nav.parents.length - 1];
  var targetLevel = nav.level;
  var candidates = allPoints.filter(function(p) { return p.level === targetLevel; });
  var higherPoints = allPoints.filter(function(p) { return p.level > targetLevel; });

  return candidates.filter(function(c) {
    var nearestName = null;
    var nearestDist = 1e9;
    for (var i = 0; i < higherPoints.length; i++) {
      var hp = higherPoints[i];
      var dx = c.x - hp.x;
      var dy = c.y - hp.y;
      var dist = dx * dx + dy * dy;
      if (dist < nearestDist) { nearestDist = dist; nearestName = hp.name; }
    }
    return nearestName === parent.name;
  });
}

function mapRenderModal() {
  var nav = window._mapNav;
  var allPoints = window._mapAllPoints;
  var visible = mapGetVisible(allPoints, nav);

  /* Visual zoom — independent of drill-down level */
  var zoomScale = nav.zoom || 1.0;
  var W = Math.round(520 * zoomScale);
  var H = Math.round(420 * zoomScale);
  var PAD = Math.round(30 * zoomScale);
  var FONT = Math.round(10 * zoomScale);
  var sx = (W - PAD * 2) / 1000;
  var sy = (H - PAD * 2) / 1000;
  var colors = ['#34C759', '#007AFF', '#FF9500'];
  var levelNames = ['具体地点', '小地点', '大地点'];
  var radiusByLevel = [Math.round(4*zoomScale), Math.round(5*zoomScale), Math.round(7*zoomScale)];

  /* ── Breadcrumb ── */
  var bcHtml = '';
  for (var i = 0; i < nav.parents.length; i++) {
    var p = nav.parents[i];
    if (i > 0) bcHtml += ' <span style="color:var(--text-muted)">&rsaquo;</span> ';
    bcHtml += '<span style="color:' + colors[p.level] + ';font-weight:500">' + p.name + '</span>';
  }

  /* ── Controls ── */
  var ctrlHtml = '<div style="margin-bottom:8px;display:flex;align-items:center;justify-content:center;gap:8px">';
  /* Zoom out (visual) — always available unless at min zoom */
  ctrlHtml += '<button class="btn btn-secondary btn-sm" id="map-zoom-out-btn" title="缩小"' +
    (zoomScale <= 0.6 ? ' disabled' : '') + '>&#x2796; 缩小</button>';
  /* Back (drill-up) */
  if (nav.parents.length > 0) {
    ctrlHtml += '<button class="btn btn-secondary btn-sm" id="map-back-btn">&larr; 返回</button>';
  } else {
    ctrlHtml += '<span style="width:64px"></span>';
  }
  ctrlHtml += '<span style="font-size:12px;color:var(--text-primary)">';
  ctrlHtml += (bcHtml || '全部大地点');
  ctrlHtml += '</span>';
  ctrlHtml += '<span style="font-size:11px;color:var(--text-muted)">（' + visible.length + '个' + levelNames[nav.level] + '）</span>';
  /* Zoom in (visual) — always available unless at max zoom */
  ctrlHtml += '<button class="btn btn-secondary btn-sm" id="map-zoom-in-btn" title="放大"' +
    (zoomScale >= 3.0 ? ' disabled' : '') + '>&#x2795; 放大</button>';
  /* Top level */
  if (nav.level < 2) {
    ctrlHtml += '<button class="btn btn-secondary btn-sm" id="map-top-btn" title="回到顶层">&#x21E7; 顶层</button>';
  }
  ctrlHtml += '</div>';

  /* ── SVG (wrapped in <g> for pan) ── */
  var px = nav.panX || 0, py = nav.panY || 0;
  var svg = '<svg width="' + W + '" height="' + H + '" style="background:#1a1a2e;border-radius:8px;cursor:grab" id="map-svg">';
  svg += '<g id="map-pan-group" transform="translate(' + px.toFixed(1) + ',' + py.toFixed(1) + ')">';
  for (var g = 0; g <= 1000; g += 200) {
    var gx = PAD + g * sx;
    var gy = PAD + g * sy;
    svg += '<line x1="' + gx + '" y1="' + PAD + '" x2="' + gx + '" y2="' + (H - PAD) + '" stroke="#ffffff10"/>';
    svg += '<line x1="' + PAD + '" y1="' + gy + '" x2="' + (W - PAD) + '" y2="' + gy + '" stroke="#ffffff10"/>';
  }
  for (var i = 0; i < visible.length; i++) {
    var pt = visible[i];
    var cx = PAD + pt.x * sx;
    var cy = PAD + pt.y * sy;
    var color = colors[pt.level] || '#888';
    var radius = radiusByLevel[pt.level] || 4;
    var canDrill = pt.level > 0 && pt.level === nav.level;
    var cls = canDrill ? 'map-point-drillable' : 'map-point-leaf';
    svg += '<circle cx="' + cx.toFixed(1) + '" cy="' + cy.toFixed(1) + '" r="' + radius + '" fill="' + color + '" stroke="#fff" stroke-width="0.6" class="' + cls + '" data-name="' + pt.name + '" data-level="' + pt.level + '" data-x="' + pt.x + '" data-y="' + pt.y + '"/>';
    svg += '<text x="' + (cx + Math.round(9*zoomScale)).toFixed(1) + '" y="' + (cy + Math.round(4*zoomScale)).toFixed(1) + '" fill="' + color + '" font-size="' + FONT + '" style="pointer-events:none;font-weight:500">' + pt.name + '</text>';
  }
  svg += '</g></svg>';

  /* ── Legend ── */
  var legend = '<div style="text-align:center;margin-top:6px;font-size:11px;color:var(--text-muted)">';
  legend += '<span style="color:#FF9500">● 大地点</span>  <span style="color:#007AFF">● 小地点</span>  <span style="color:#34C759">● 具体地点</span>';
  legend += '  <span style="color:var(--text-muted)">| 缩放: ' + zoomScale.toFixed(1) + 'x | 点击地点下钻</span>';
  legend += '</div>';

  var loc = window._mapCurrentLoc;
  if (loc) {
    legend += '<div style="text-align:center;margin-top:2px;font-size:11px;color:var(--text-muted)">当前位置: ' +
      '<span style="color:var(--accent)">' + (loc.area || '?') + ' &rsaquo; ' + (loc.district || '?') + ' &rsaquo; ' + (loc.spot || '?') + '</span></div>';
  }

  /* ── Edit / Delete location section ── */
  var edHtml = '<div style="margin-top:12px;padding-top:10px;border-top:1px solid var(--border-light)">' +
    '<div style="font-size:12px;color:var(--text-muted);margin-bottom:6px">编辑地点:</div>';
  for (var d = 0; d < visible.length; d++) {
    var dp = visible[d];
    var safeName = dp.name.replace(/"/g, '&quot;').replace(/'/g, '&#39;');
    edHtml += '<div style="display:flex;align-items:center;justify-content:space-between;padding:3px 0;font-size:12px">' +
      '<span style="flex:1"><span style="color:' + colors[dp.level] + '">●</span> ' +
      '<span class="map-loc-name" id="loc-name-' + d + '">' + escapeHtml(dp.name) + '</span>' +
      '<input class="map-rename-input" id="loc-input-' + d + '" value="' + escapeHtml(dp.name) + '" style="display:none;width:140px;font-size:12px;padding:1px 4px;border:1px solid var(--accent);border-radius:3px;background:#1a1a2e;color:var(--text-primary)"/>' +
      ' <span style="color:var(--text-muted);font-size:10px">(' + levelNames[dp.level] + ')</span></span>' +
      '<span style="flex-shrink:0">' +
      '<button class="btn btn-secondary btn-sm map-edit-btn" data-idx="' + d + '" data-name="' + safeName + '" data-level="' + dp.level + '" title="重命名" style="padding:0 6px;font-size:11px">✎</button>' +
      '<button class="btn btn-danger btn-sm map-del-btn" data-name="' + safeName + '" data-level="' + dp.level + '" title="删除" style="margin-left:4px">✕</button>' +
      '</span></div>';
  }
  edHtml += '</div>';

  var html = ctrlHtml + '<div style="overflow:auto;max-height:450px;text-align:center;border-radius:8px">' + svg + '</div>' + legend + edHtml;
  showModal('地图 — ' + levelNames[nav.level], html, true);

  /* ── Event listeners ── */
  setTimeout(function() {
    /* Back: drill up one level (reset pan) */
    var backBtn = document.getElementById('map-back-btn');
    if (backBtn) backBtn.onclick = function() {
      nav.parents.pop();
      nav.level = Math.min(2, nav.level + 1);
      nav.panX = 0; nav.panY = 0;
      mapRenderModal();
    };

    /* Visual zoom in — same level, larger scale */
    var zoomInBtn = document.getElementById('map-zoom-in-btn');
    if (zoomInBtn) zoomInBtn.onclick = function() {
      nav.zoom = Math.min(3.0, (nav.zoom || 1.0) + 0.5);
      mapRenderModal();
    };

    /* Visual zoom out — same level, smaller scale */
    var zoomOutBtn = document.getElementById('map-zoom-out-btn');
    if (zoomOutBtn) zoomOutBtn.onclick = function() {
      nav.zoom = Math.max(0.5, (nav.zoom || 1.0) - 0.5);
      mapRenderModal();
    };

    /* Top: return to L2 + reset zoom + reset pan */
    var topBtn = document.getElementById('map-top-btn');
    if (topBtn) topBtn.onclick = function() {
      nav.parents = [];
      nav.level = 2;
      nav.zoom = 1.0;
      nav.panX = 0; nav.panY = 0;
      mapRenderModal();
    };

    var svgEl = document.getElementById('map-svg');
    if (svgEl) {
      /* ── Mouse drag-to-pan ── */
      var drag = { on: false, sx: 0, sy: 0, px: nav.panX || 0, py: nav.panY || 0 };
      svgEl.onmousedown = function(e) {
        if (e.button !== 0) return;
        drag.on = true; drag.sx = e.clientX; drag.sy = e.clientY;
        drag.px = nav.panX || 0; drag.py = nav.panY || 0;
        svgEl.style.cursor = 'grabbing';
        e.preventDefault();
      };
      window.addEventListener('mousemove', function(e) {
        if (!drag.on) return;
        nav.panX = drag.px + (e.clientX - drag.sx);
        nav.panY = drag.py + (e.clientY - drag.sy);
        var g = document.getElementById('map-pan-group');
        if (g) g.setAttribute('transform', 'translate(' + nav.panX.toFixed(1) + ',' + nav.panY.toFixed(1) + ')');
      });
      window.addEventListener('mouseup', function() {
        if (!drag.on) return;
        drag.on = false;
        var s = document.getElementById('map-svg');
        if (s) s.style.cursor = 'grab';
      });

      /* Mouse wheel: visual zoom (same level) */
      svgEl.onwheel = function(e) {
        e.preventDefault();
        if (e.deltaY < 0) {
          nav.zoom = Math.min(3.0, (nav.zoom || 1.0) + 0.25);
        } else {
          nav.zoom = Math.max(0.5, (nav.zoom || 1.0) - 0.25);
        }
        mapRenderModal();
        return false;
      };

      /* Point click: drill down (reset pan) */
      var circles = svgEl.querySelectorAll('circle[data-level]');
      for (var k = 0; k < circles.length; k++) {
        (function(circle) {
          circle.onclick = function() {
            var lv = parseInt(circle.getAttribute('data-level'));
            if (lv > 0) {
              nav.parents.push({
                name: circle.getAttribute('data-name'),
                level: lv,
                x: parseInt(circle.getAttribute('data-x')),
                y: parseInt(circle.getAttribute('data-y'))
              });
              nav.level = lv - 1;
              nav.panX = 0; nav.panY = 0;
              mapRenderModal();
            }
          };
        })(circles[k]);
      }
    }

    /* Rename location buttons */
    var editBtns = document.querySelectorAll('.map-edit-btn');
    for (var eb = 0; eb < editBtns.length; eb++) {
      (function(btn) {
        btn.onclick = function() {
          var idx = btn.getAttribute('data-idx');
          var nameSpan = document.getElementById('loc-name-' + idx);
          var inputEl = document.getElementById('loc-input-' + idx);
          if (!nameSpan || !inputEl) return;

          if (inputEl.style.display === 'none') {
            /* Switch to edit mode */
            nameSpan.style.display = 'none';
            inputEl.style.display = 'inline';
            inputEl.focus();
            inputEl.select();
            btn.textContent = '✓';
            btn.title = '确认';
          } else {
            /* Confirm rename */
            var oldName = btn.getAttribute('data-name');
            var newName = inputEl.value.trim();
            var locLevel = btn.getAttribute('data-level');
            if (!newName || newName === oldName) {
              nameSpan.style.display = 'inline';
              inputEl.style.display = 'none';
              btn.textContent = '✎';
              btn.title = '重命名';
              return;
            }
            /* Async rename via API */
            (async function() {
              var r = await api('rename_location', { level: locLevel, oldName: oldName, newName: newName });
              if (r.ok) {
                addSystemMsg('已重命名: ' + oldName + ' → ' + newName);
                if (r.state && r.state.mapPoints) window._mapAllPoints = r.state.mapPoints;
                mapRenderModal();
              } else {
                addSystemMsg('重命名失败: ' + (r.error || '未知错误'));
                nameSpan.style.display = 'inline';
                inputEl.style.display = 'none';
                btn.textContent = '✎';
                btn.title = '重命名';
              }
            })();
          }
        };
      })(editBtns[eb]);
    }

    /* Delete location buttons */
    var delBtns = document.querySelectorAll('.map-del-btn');
    for (var db = 0; db < delBtns.length; db++) {
      (function(btn) {
        btn.onclick = async function() {
          var locName = btn.getAttribute('data-name');
          var locLevel = btn.getAttribute('data-level');
          if (!confirm('确定删除 "' + locName + '" 吗？')) return;
          var r = await api('delete_location', { level: locLevel, name: locName });
          if (r.ok) {
            addSystemMsg('已删除: ' + locName);
            if (r.state && r.state.mapPoints) window._mapAllPoints = r.state.mapPoints;
            mapRenderModal();
          } else {
            addSystemMsg('删除失败: ' + (r.error || '未知错误'));
          }
        };
      })(delBtns[db]);
    }
  }, 50);
}

$('btn-map').onclick = async function() {
  try {
    var data = await api('get_state');
    var points = (data && data.mapPoints && data.mapPoints.length) ? data.mapPoints : [];
    if (!points.length) {
      showModal('地图', '<p style="color:var(--text-muted);text-align:center">暂无地点数据</p>');
      return;
    }
    window._mapCurrentLoc = data.location || data.env || {};
    window._mapAllPoints = points;
    window._mapNav = { level: 2, parents: [], zoom: 1.0, panX: 0, panY: 0 };
    mapRenderModal();
  } catch(e) {
    showModal('地图', '<p style="color:red">加载失败: ' + e.message + '</p>');
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
    sorted.forEach(function(n, idx) {
      let st = n.statusText || '正常';
      let marker = presentNames[n.name] ? ' <span style="color:#34C759">●在场</span>' : '';
      let hasSkills = n.skills && n.skills.length > 0;
      let hasItems = n.items && n.items.length > 0;
      html += '<div class="npc-card-entry" style="padding:8px 0;border-bottom:1px solid var(--border-light)">' +
        '<div style="display:flex;align-items:flex-start;justify-content:space-between">' +
        '<div style="flex:1">' +
        '<strong>' + escapeHtml(n.name) + '</strong>' + marker +
        '  <span style="font-size:12px;color:var(--text-secondary)">年龄:' + (n.age||'?') + '岁 性别:' + escapeHtml(n.gender||'?') + '</span>' +
        '<br>' +
        '<span style="font-size:11px;color:var(--text-muted)">身份:' + escapeHtml(n.personality||'?') +
        (n.home ? '  家:' + escapeHtml(n.home) : '') +
        '  状态:' + escapeHtml(st) + '  好感:' + (n.playerAffinity||0) +
        '  金钱:' + (n.money||0) + 'G</span>' +
        '</div>' +
        '<button class="btn btn-secondary btn-sm npc-detail-btn" data-idx="' + idx + '" title="查看详情">详情</button>' +
        '</div>';
      /* Show skill summary if any */
      if (hasSkills) {
        html += '<div style="margin-top:4px;font-size:12px;color:var(--accent)">技能: ';
        n.skills.forEach(function(sk, i) {
          if (i > 0) html += ', ';
          html += escapeHtml(sk.name) + ' Lv.' + sk.level;
        });
        html += '</div>';
      }
      if (hasItems) {
        html += '<div style="margin-top:3px;font-size:12px;color:var(--text-secondary)">持有: ';
        n.items.forEach(function(it, i) {
          if (i > 0) html += ', ';
          html += escapeHtml(it.name) + ' x' + it.qty;
        });
        html += '</div>';
      }
      html += '</div>';
    });
  }
  showModal('NPC 角色卡 (' + npcs.length + '个)', html, true);

  /* Bind detail buttons after DOM is populated */
  setTimeout(function() {
    var detailBtns = document.querySelectorAll('.npc-detail-btn');
    for (var i = 0; i < detailBtns.length; i++) {
      (function(btn) {
        btn.onclick = function() {
          var idx = parseInt(btn.getAttribute('data-idx'));
          /* Re-fetch state to get fresh NPC data */
          api('get_state').then(function(state) {
            var npcList = (state && state.npcs) ? state.npcs : npcs;
            if (idx >= 0 && idx < npcList.length) {
              showNpcDetail(npcList[idx]);
            }
          });
        };
      })(detailBtns[i]);
    }
  }, 50);
};

/* Show detailed view of a single NPC */
function showNpcDetail(n) {
  var html = '';
  /* Basic info */
  html += '<div style="margin-bottom:10px">';
  html += '<strong style="font-size:16px">' + escapeHtml(n.name) + '</strong>';
  html += '  年龄:' + (n.age||'?') + '岁  性别:' + escapeHtml(n.gender||'?');
  if (n.clothing) html += '  衣着:' + escapeHtml(n.clothing);
  html += '</div>';

  /* Attrs */
  if (n.attrs) {
    html += '<div style="margin-bottom:8px;font-size:12px;color:var(--text-secondary)">';
    html += '颜值:' + (n.attrs.appearance||0) + '  体质:' + (n.attrs.constitution||0) + '  智力:' + (n.attrs.intelligence||0);
    html += '</div>';
  }

  /* Personality & Status */
  html += '<div style="margin-bottom:8px;font-size:12px;color:var(--text-secondary)">';
  html += '性格:' + escapeHtml(n.personality||'?');
  if (n.home) html += '  住所:' + escapeHtml(n.home);
  html += '  状态:' + escapeHtml(n.statusText||'正常');
  html += '  好感度:' + (n.playerAffinity||0) + '  金钱:' + (n.money||0) + 'G';
  html += '</div>';

  /* Skills */
  html += '<div class="section-label" style="margin-top:12px">技能 (' + (n.skillCount || (n.skills ? n.skills.length : 0)) + ')</div>';
  if (n.skills && n.skills.length > 0) {
    html += '<div style="font-size:12px">';
    n.skills.forEach(function(s) {
      html += '<div style="padding:2px 0">' + escapeHtml(s.name) + '  <strong>Lv.' + s.level + '</strong></div>';
    });
    html += '</div>';
  } else {
    html += '<div style="font-size:12px;color:var(--text-muted)">(无)</div>';
  }

  /* Items */
  html += '<div class="section-label" style="margin-top:10px">持有物 (' + (n.itemCount || (n.items ? n.items.length : 0)) + ')</div>';
  if (n.items && n.items.length > 0) {
    html += '<div style="font-size:13px">';
    n.items.forEach(function(it) {
      html += '<div style="padding:2px 0">' + escapeHtml(it.name) + '  x' + it.qty + '</div>';
    });
    html += '</div>';
  } else {
    html += '<div style="font-size:13px;color:var(--text-muted)">(无)</div>';
  }

  showModal('NPC: ' + n.name, html);
}

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
        /* Load chat history */
        try {
          const chat = await api('get_chat_history');
          if (Array.isArray(chat) && chat.length > 0) {
            addSystemMsg('── 以下为历史聊天记录 ──');
            chat.forEach(function(msg) {
              if (msg.role === 'player') addPlayerMsg(msg.text);
              else if (msg.role === 'ai') addAIMsg(msg.text);
            });
            addSystemMsg('── 历史记录结束，世界已就绪 ──');
          } else {
            addSystemMsg('世界已就绪。输入指令开始冒险。');
          }
        } catch (e2) {
          addSystemMsg('世界已就绪。输入指令开始冒险。');
        }
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

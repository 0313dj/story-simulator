/* ═══════════════════════════════════════════════════════════════
   debug/debug.js — 开发者模式调试面板
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { EventBus } from '../shared/eventbus.js';
import { $, escapeHtml } from '../shared/utils.js';

function toggleDebugMode() {
  state.debug.enabled = !state.debug.enabled;
  const panel = $('debug-panel'), btn = $('btn-debug');
  if (state.debug.enabled) {
    panel.classList.remove('hidden');
    btn.style.color = '#0ea5e9'; btn.style.background = 'rgba(14,165,233,0.15)';
    if (state.debug.lastTrace) renderDebugTrace(state.debug.lastTrace);
  } else {
    panel.classList.add('hidden');
    btn.style.color = ''; btn.style.background = '';
  }
}

function renderDebugTrace(trace) {
  if (!state.debug.enabled) return;
  if (!trace) { $('debug-body').innerHTML = '<div style="color:#666">无追踪数据</div>'; return; }
  state.debug.lastTrace = trace;

  const esc = s => s ? String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;') : '';
  let html = '';

  const step = (cls, icon, label, body) => {
    html += '<div class="debug-step ' + cls + '"><div class="debug-step-label"><span class="debug-step-icon">' + icon + '</span>' + esc(label) + '</div>';
    if (body) html += '<div class="debug-step-body">' + body + '</div>';
    html += '</div><div class="debug-arrow">&#x2193;</div>';
  };

  if (trace.userInput) step('debug-intent', '👤', '用户输入', esc(trace.userInput));
  if (trace.intent) {
    let b = '';
    if (trace.intent.type) b += '<div class="debug-kv"><span class="debug-kv-key">类型:</span><span class="debug-kv-val">' + esc(trace.intent.type) + '</span></div>';
    if (trace.intent.target) b += '<div class="debug-kv"><span class="debug-kv-key">目标:</span><span class="debug-kv-val">' + esc(trace.intent.target) + '</span></div>';
    if (trace.intent.confidence != null) b += '<div class="debug-kv"><span class="debug-kv-key">置信度:</span><span class="debug-kv-val">' + (trace.intent.confidence * 100).toFixed(0) + '% (Level ' + (trace.intent.level || 1) + ')</span></div>';
    step('debug-intent', '🎯', 'Intent 意图识别', b);
  }
  if (trace.plan) {
    let b = '';
    if (trace.plan.goal) b += '<div class="debug-kv"><span class="debug-kv-key">目标:</span><span class="debug-kv-val">' + esc(trace.plan.goal) + '</span></div>';
    b += '<div class="debug-kv"><span class="debug-kv-key">步骤数:</span><span class="debug-kv-val">' + (trace.plan.stepCount || 0) + '</span></div>';
    if (trace.plan.steps?.length) {
      b += '<pre>' + trace.plan.steps.map((s, i) => '  ' + (i + 1) + '. ' + esc(s.action) + (s.target ? ' → ' + esc(s.target) : '') + ' (' + (s.estimatedTicks || '?') + 'min)\n').join('') + '</pre>';
    }
    step('debug-plan', '📋', 'Planner 计划生成', b);
  }
  if (trace.actionProposal) step('debug-proposal', '📝', 'ActionProposal 动作提案', '<pre>' + esc(trace.actionProposal) + '</pre>');
  if (trace.changes) step('debug-changes', '🔄', 'Changes 变更指令', '<pre>' + esc(trace.changes) + '</pre>');
  if (trace.ruleHits?.length) {
    let b = '<div class="debug-kv"><span class="debug-kv-key">命中规则:</span><span class="debug-kv-val">' + trace.ruleHits.length + ' 条</span></div>';
    trace.ruleHits.forEach(r => { b += '<div style="padding:2px 0;font-size:10px"><span style="color:#f38ba8">#' + r.ruleId + '</span>' + (r.condition ? ' <span style="color:#6c7086">IF</span> <span style="color:#a6e3a1">' + esc(r.condition) + '</span>' : '') + '</div>'; });
    step('debug-rulehits', '⚡', 'Rule Engine 规则命中', b);
  } else if (trace.ruleHits) step('debug-rulehits', '⚡', 'Rule Engine 规则命中', '<span style="color:#6c7086">无规则命中</span>');
  if (trace.narrative) {
    let b = '';
    if (trace.narrative.source) b += '<div class="debug-kv"><span class="debug-kv-key">来源:</span><span class="debug-kv-val">' + esc(trace.narrative.source) + '</span></div>';
    if (trace.narrative.length) b += '<div class="debug-kv"><span class="debug-kv-key">长度:</span><span class="debug-kv-val">' + trace.narrative.length + ' 字符</span></div>';
    if (trace.narrative.style) b += '<div class="debug-kv"><span class="debug-kv-key">风格:</span><span class="debug-kv-val">' + esc(trace.narrative.style) + '</span></div>';
    step('debug-narrative', '💬', 'Narrative 叙事生成', b);
  }
  if (trace.npcActions?.length) {
    step('debug-npcactions', '🤖', 'NPC Brain 自主行为 (' + trace.npcActions.length + '个NPC)',
      '<pre>' + trace.npcActions.map(a => esc(a.npcName || '?') + ': ' + esc(a.goalType || '?') + (a.action ? ' → ' + esc(a.action) : '') + (a.rulesFired ? ' [规则命中:' + a.rulesFired + ']' : '') + '\n').join('') + '</pre>');
  }
  if (trace.worldSim) step('debug-worldsim', '🌍', 'World Director 世界模拟', '<pre>' + esc(trace.worldSim) + '</pre>');

  if (html.endsWith('<div class="debug-arrow">↓</div>')) html = html.slice(0, -35);
  $('debug-body').innerHTML = html || '<div style="color:#666">无追踪数据</div>';
}

export function initDebug() {
  $('btn-debug').onclick = toggleDebugMode;
  $('debug-close').onclick = () => toggleDebugMode();
  EventBus.on('debug:trace', trace => {
    if (state.debug.enabled) renderDebugTrace(trace);
    else state.debug.lastTrace = trace;
  });
}

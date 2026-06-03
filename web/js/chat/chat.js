/* ═══════════════════════════════════════════════════════════════
   chat/chat.js — 聊天输入输出
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { EventBus } from '../shared/eventbus.js';
import { sendMessage as apiSendMessage, getChatHistory, getState } from '../shared/api.js';
import { $, escapeHtml } from '../shared/utils.js';

/* ── Constants ── */
const MAX_MSG_LENGTH = 2048;   /* matches backend CTX_MAX_CHAT_TEXT */
const MAX_MSG_NODES  = 200;    /* cap DOM nodes for performance */

/* ── DOM helpers ── */

function addMsg(html, cls) {
  const container = $('chat-messages');
  const div = document.createElement('div');
  div.className = cls || '';
  div.innerHTML = html;
  container.appendChild(div);

  /* Cap message nodes: remove oldest when exceeding limit */
  while (container.children.length > MAX_MSG_NODES) {
    container.removeChild(container.firstChild);
  }

  $('chat-area').scrollTop = $('chat-area').scrollHeight;
}

function addSystemMsg(text) { addMsg(escapeHtml('[系统] ' + text), 'msg-system'); }
function addAIMsg(text)      { addMsg(escapeHtml(text), 'msg-ai'); addMsg('──────────────────────────────', 'msg-sep'); }
function addPlayerMsg(text)  { addMsg(escapeHtml('> ' + text), 'msg-player'); }

/* ── Send ── */

async function sendMessage() {
  const field = $('input-field');
  const text = field.value.trim();

  /* Guard: empty, busy, or too long */
  if (!text) return;
  if (state.busy) {
    addSystemMsg('请等待当前请求完成');
    return;
  }
  if (text.length > MAX_MSG_LENGTH) {
    addSystemMsg('消息过长（最大 ' + MAX_MSG_LENGTH + ' 字符，当前 ' + text.length + '）');
    return;
  }

  field.value = '';

  addPlayerMsg(text);
  $('btn-send').disabled = true;
  $('btn-send').innerHTML = '<span class="spinner"></span>等待';
  state.busy = true;

  const r = await apiSendMessage(text);

  $('btn-send').disabled = false;
  $('btn-send').textContent = '发送';
  state.busy = false;

  if (r.ok) {
    if (r.reply) addAIMsg(r.reply);
    if (r.notification) addSystemMsg(r.notification);
    if (r.state) EventBus.emit('state:refresh', r.state);
    if (r.debugTrace) EventBus.emit('debug:trace', r.debugTrace);
  } else {
    addSystemMsg('错误: ' + (r.error || '未知'));
  }
}

/* ── Init ── */

export function initChat() {
  EventBus.on('chat:add', msg => {
    if (msg.role === 'player') addPlayerMsg(msg.text);
    else if (msg.role === 'ai') addAIMsg(msg.text);
    else addSystemMsg(msg.text);
  });

  $('btn-send').onclick = sendMessage;
  $('input-field').onkeydown = function(e) {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMessage(); }
  };

  /* Startup */
  (async function init() {
    try {
      addSystemMsg('正在连接服务器...');
      const r = await getState();
      if (r.ok) {
        EventBus.emit('state:refresh', r);
        if (r.worldReady) {
          try {
            const chat = await getChatHistory();
            if (Array.isArray(chat) && chat.length > 0) {
              addSystemMsg('── 以下为历史聊天记录 ──');
              chat.forEach(msg => {
                if (msg.role === 'player') addPlayerMsg(msg.text);
                else if (msg.role === 'ai') addAIMsg(msg.text);
              });
              addSystemMsg('── 历史记录结束，世界已就绪 ──');
            } else {
              addSystemMsg('世界已就绪。输入指令开始冒险。');
            }
          } catch (e2) { addSystemMsg('世界已就绪。输入指令开始冒险。'); }
        } else if (r.apiReady) {
          addSystemMsg('API 已配置。点击 [新建] 创建新世界。');
        } else {
          addSystemMsg('请先配置 API：点击右上角 ⚙ API 管理');
        }
      } else {
        addSystemMsg('连接失败，请刷新页面');
      }
    } catch (e) {
      addSystemMsg('初始化失败: ' + e.message + '。请检查服务器是否运行，然后刷新页面。');
    }
  })();
}

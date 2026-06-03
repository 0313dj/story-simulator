/* ═══════════════════════════════════════════════════════════════
   shared/api.js — 后端接口封装
   ═══════════════════════════════════════════════════════════════ */

import { state } from './state.js';
import { EventBus } from './eventbus.js';

const REQUEST_TIMEOUT_MS = 30000;  /* 30-second timeout */
const MAX_RETRIES = 2;

/* ── Loading state management ── */
export const LoadingState = {
  _active: new Set(),
  start(key) { this._active.add(key); EventBus.emit('loading:start', key); },
  end(key)   { this._active.delete(key); EventBus.emit('loading:end', key); },
  isActive(key) { return this._active.has(key); },
  isAnything() { return this._active.size > 0; }
};

/* ── Core API call with retry ── */
export async function api(cmd, extra = {}, retries = MAX_RETRIES) {
  let lastError = null;

  for (let attempt = 0; attempt <= retries; attempt++) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);

    try {
      const resp = await fetch('/api', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd, ...extra }),
        signal: controller.signal
      });
      clearTimeout(timer);

      if (!resp.ok) {
        const msg = resp.status === 0 ? '网络连接中断' :
                    resp.status >= 500 ? '服务器内部错误 (HTTP ' + resp.status + ')' :
                    '服务器连接失败 (HTTP ' + resp.status + ')';
        EventBus.emit('chat:add', { role: 'system', text: msg });
        return { ok: false, error: 'HTTP ' + resp.status };
      }
      try {
        return await resp.json();
      } catch (e) {
        EventBus.emit('chat:add', { role: 'system', text: '服务器返回无效响应' });
        return { ok: false, error: 'Invalid JSON response' };
      }
    } catch (e) {
      clearTimeout(timer);
      lastError = e;

      /* Retry on network errors and timeouts only */
      const shouldRetry = attempt < retries &&
        (e.name === 'AbortError' ||
         e.message === 'Failed to fetch' ||
         (e.message && e.message.includes('NetworkError')));

      if (shouldRetry) {
        await new Promise(r => setTimeout(r, 1000 * (attempt + 1))); /* exponential backoff */
        continue;
      }

      if (e.name === 'AbortError') {
        EventBus.emit('chat:add', { role: 'system', text: '请求超时（30秒），请重试' });
        return { ok: false, error: 'Timeout' };
      }
      if (e.message === 'Failed to fetch' || (e.message && e.message.includes('NetworkError'))) {
        EventBus.emit('chat:add', { role: 'system', text: '无法连接服务器，请确认模拟器已启动' });
      } else {
        EventBus.emit('chat:add', { role: 'system', text: '网络请求失败: ' + e.message });
      }
      return { ok: false, error: e.message };
    }
  }
  return { ok: false, error: lastError ? lastError.message : 'Unknown error' };
}

/* ── Convenience wrappers with loading state ── */

export function getState()            { return api('get_state'); }
export function sendMessage(text)     { LoadingState.start('send'); return api('send_message', { text }).finally(() => LoadingState.end('send')); }
export function getChatHistory()      { return api('get_chat_history'); }
export function quickSave()           { return api('quick_save'); }
export function quickLoad()           { LoadingState.start('load'); return api('quick_load').finally(() => LoadingState.end('load')); }
export function getProfiles()         { return api('get_profiles'); }
export function getApiStatus()        { return api('get_api_status'); }

/* ── Profile management (with client-side validation) ── */

/**
 * 保存 API 配置（保存前校验必填字段）
 * @param {{ name: string, endpoint: string, apiKey: string, model: string }} data
 * @returns {Promise<{ ok: boolean, error?: string }>}
 */
export async function saveProfile(data) {
  if (!data || !data.apiKey || !data.apiKey.trim()) {
    EventBus.emit('chat:add', { role: 'system', text: '请输入 API Key' });
    return { ok: false, error: 'API Key 不能为空' };
  }
  if (!data.endpoint || !data.endpoint.trim()) {
    EventBus.emit('chat:add', { role: 'system', text: '请输入 API Endpoint' });
    return { ok: false, error: 'Endpoint 不能为空' };
  }
  const payload = {
    name: (data.name || 'default').trim() || 'default',
    endpoint: data.endpoint.trim(),
    apiKey: data.apiKey.trim(),
    model: (data.model || 'gpt-4o').trim() || 'gpt-4o'
  };
  try {
    const result = await api('save_profile', payload);
    if (result.ok) {
      EventBus.emit('chat:add', { role: 'system', text: 'API 配置已保存，已自动切换为当前配置' });
    }
    return result;
  } catch (e) {
    EventBus.emit('chat:add', { role: 'system', text: '保存配置时发生错误: ' + e.message });
    return { ok: false, error: e.message };
  }
}

export async function activateProfile(name) {
  if (!name || !name.trim()) {
    EventBus.emit('chat:add', { role: 'system', text: '请指定要激活的配置名称' });
    return { ok: false, error: '配置名称不能为空' };
  }
  return api('activate_profile', { name: name.trim() });
}

export async function deleteProfile(name) {
  if (!name || !name.trim()) {
    return { ok: false, error: '配置名称不能为空' };
  }
  return api('delete_profile', { name: name.trim() });
}

/* ── Save management ── */

export function listSaves()           { return api('list_saves'); }
export function loadSave(filename)    { LoadingState.start('load'); return api('load_save', { filename }).finally(() => LoadingState.end('load')); }
export function deleteSave(filename)  { return api('delete_save', { filename }); }

/* ── World management ── */

export function createWorld(data)     { LoadingState.start('create'); return api('create_world', data).finally(() => LoadingState.end('create')); }
export function renameLocation(data)  { return api('rename_location', data); }
export function deleteLocation(data)  { return api('delete_location', data); }

/* ── DOM-based helpers (for direct use in settings pages) ── */

/**
 * 从 DOM 输入框读取 API 配置并保存（绑定按钮事件用）
 * 需要页面中存在 #apikey 输入框和 #saveApiKeyBtn 按钮，
 * 可选 #endpoint、#model 输入框。
 */
export function bindApiKeySave(btnId = 'saveApiKeyBtn') {
  const btn = document.getElementById(btnId);
  if (!btn) return;

  btn.addEventListener('click', async () => {
    const apiKeyInput = document.getElementById('apikey');
    const endpointInput = document.getElementById('endpoint');
    const modelInput = document.getElementById('model');

    const apiKey = apiKeyInput?.value?.trim() || '';
    const endpoint = endpointInput?.value?.trim() || 'https://api.openai.com/v1';
    const model = modelInput?.value?.trim() || 'gpt-4o';

    if (!apiKey) {
      EventBus.emit('chat:add', { role: 'system', text: '请输入 API Key' });
      return;
    }

    const result = await saveProfile({ name: 'default', endpoint, apiKey, model });
    if (!result.ok) {
      EventBus.emit('chat:add', { role: 'system', text: `保存失败: ${result.error}` });
    }
  });
}

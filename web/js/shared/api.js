/* ═══════════════════════════════════════════════════════════════
   shared/api.js — 后端接口封装
   ═══════════════════════════════════════════════════════════════ */

import { state } from './state.js';
import { EventBus } from './eventbus.js';

export async function api(cmd, extra = {}) {
  try {
    const resp = await fetch('/api', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd, ...extra })
    });
    if (!resp.ok) {
      EventBus.emit('chat:add', { role: 'system', text: '服务器连接失败' });
      return { ok: false, error: 'HTTP ' + resp.status };
    }
    try {
      return await resp.json();
    } catch (e) {
      EventBus.emit('chat:add', { role: 'system', text: '服务器返回无效响应' });
      return { ok: false, error: 'Invalid JSON response' };
    }
  } catch (e) {
    EventBus.emit('chat:add', { role: 'system', text: '网络请求失败: ' + e.message });
    return { ok: false, error: e.message };
  }
}

/* ── Convenience wrappers ── */

export function getState()            { return api('get_state'); }
export function sendMessage(text)     { return api('send_message', { text }); }
export function getChatHistory()      { return api('get_chat_history'); }
export function quickSave()           { return api('quick_save'); }
export function quickLoad()           { return api('quick_load'); }
export function getProfiles()         { return api('get_profiles'); }
export function getApiStatus()        { return api('get_api_status'); }
export function saveProfile(data)     { return api('save_profile', data); }
export function activateProfile(name) { return api('activate_profile', { name }); }
export function deleteProfile(name)   { return api('delete_profile', { name }); }
export function listSaves()           { return api('list_saves'); }
export function loadSave(filename)    { return api('load_save', { filename }); }
export function deleteSave(filename)  { return api('delete_save', { filename }); }
export function createWorld(data)     { return api('create_world', data); }
export function renameLocation(data)  { return api('rename_location', data); }
export function deleteLocation(data)  { return api('delete_location', data); }

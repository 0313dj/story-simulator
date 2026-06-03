/* ═══════════════════════════════════════════════════════════════
   settings/api-manager.js — API 配置管理弹窗
   ═══════════════════════════════════════════════════════════════ */

import { EventBus } from '../shared/eventbus.js';
import { getProfiles, getApiStatus, saveProfile, activateProfile, deleteProfile } from '../shared/api.js';
import { $, escapeHtml, API_PROVIDERS } from '../shared/utils.js';
import { showModal, hideModal } from '../ui/modal.js';

export async function showApiManager() {
  const profiles = await getProfiles();
  let html = '<div class="form-row"><label>提供商</label><select id="api-provider"><option value="">--</option>';
  API_PROVIDERS.forEach(p => { html += '<option value="' + p[0] + '|' + p[1] + '|' + p[2] + '">' + p[0] + '</option>'; });
  html += '</select></div>';
  html += '<div class="form-row"><label>地址</label><input id="api-ep" placeholder="https://"></div>';
  html += '<div class="form-row"><label>模型</label><input id="api-model" placeholder="model-name"></div>';
  html += '<div class="form-row"><label>密钥</label><input id="api-key" type="password" placeholder="sk-..."></div>';
  html += '<div class="form-row"><label>配置名称</label><input id="api-name" placeholder="Default"></div>';
  html += '<button class="btn btn-primary" id="api-save-btn">保存配置</button>';

  const status = await getApiStatus();
  const activeModel = status?.model || '';

  if (Array.isArray(profiles) && profiles.length) {
    html += '<div style="margin-top:16px"><strong>已保存配置:</strong></div>';
    profiles.forEach(p => {
      const isActive = activeModel && p.model === activeModel;
      html += '<div class="save-entry"><div class="save-info">' +
        '<div class="save-name">' + (isActive ? '★ ' : '') + escapeHtml(p.name) + '</div>' +
        '<div class="save-meta">' + escapeHtml(p.model) + (isActive ? ' <span style="color:#34C759">●当前使用</span>' : '') + '</div></div>' +
        '<button class="btn btn-primary btn-sm api-use-btn" data-name="' + escapeHtml(p.name) + '"' + (isActive ? ' disabled' : '') + '>使用</button>' +
        '<button class="btn btn-danger btn-sm api-del-btn" data-name="' + escapeHtml(p.name) + '">删除</button></div>';
    });
  }
  showModal('API 管理', html);

  $('api-provider').onchange = function() {
    const parts = this.value.split('|');
    if (parts.length >= 3) { $('api-ep').value = parts[1]; $('api-model').value = parts[2]; $('api-name').value = parts[0]; }
  };
  $('api-save-btn').onclick = async () => {
    const apiKey = $('api-key').value.trim();
    if (!apiKey) {
      EventBus.emit('chat:add', { role: 'system', text: '请输入 API Key' });
      return;
    }
    const r = await saveProfile({
      name: $('api-name').value,
      endpoint: $('api-ep').value,
      apiKey,
      model: $('api-model').value
    });
    if (r.ok) { hideModal(); }
    else { EventBus.emit('chat:add', { role: 'system', text: '保存失败: ' + r.error }); }
  };

  setTimeout(() => {
    document.querySelectorAll('.api-use-btn').forEach(b => { b.onclick = async () => { const r = await activateProfile(b.dataset.name); if (r.ok) { EventBus.emit('chat:add', { role: 'system', text: '已切换至配置: ' + b.dataset.name }); showApiManager(); } else EventBus.emit('chat:add', { role: 'system', text: '切换失败: ' + r.error }); }; });
    document.querySelectorAll('.api-del-btn').forEach(b => { b.onclick = async () => { await deleteProfile(b.dataset.name); showApiManager(); }; });
  }, 10);
}

/* ═══════════════════════════════════════════════════════════════
   ui/modal.js — 通用弹窗组件
   ═══════════════════════════════════════════════════════════════ */

import { $ } from '../shared/utils.js';

export function showModal(title, body, wide) {
  $('modal-title').textContent = title;
  $('modal-body').innerHTML = body;
  $('modal-box').style.width = wide ? '560px' : '480px';
  $('modal-overlay').classList.remove('hidden');
}

export function hideModal() {
  $('modal-overlay').classList.add('hidden');
}

export function initModal() {
  $('modal-close').onclick = hideModal;
  $('modal-overlay').onclick = function(e) {
    if (e.target === $('modal-overlay')) hideModal();
  };
}

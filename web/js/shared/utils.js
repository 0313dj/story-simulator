/* ═══════════════════════════════════════════════════════════════
   shared/utils.js — 工具函数与配置常量
   ═══════════════════════════════════════════════════════════════ */

/** Shortcut: document.getElementById */
export function $(id) { return document.getElementById(id); }

/** HTML-escape helper to prevent XSS from AI-generated text */
export function escapeHtml(text) {
  if (!text) return '';
  return String(text)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

/** Status → color mapping */
export const STATUS_COLORS = {
  '正常': '#34C759', '饥饿': '#FF9500', '疲惫': '#FF9500',
  '生病': '#FF9500', '受伤': '#FF3B30', '兴奋': '#007AFF',
  '愤怒': '#FF3B30', '悲伤': '#007AFF', '开心': '#34C759'
};

/** Weather → icon mapping */
export const WEATHER_ICONS = {
  '晴': '☀', '多云': '⛅', '阴': '☁', '小雨': '☔', '大雨': '☂',
  '雷暴': '⛈', '雪': '❄', '暴风雪': '❄❄', '雾': '☁', '大风': '≈', '沙尘暴': '≈'
};

/** Map level colors & names */
export const MAP_COLORS = ['#34C759', '#007AFF', '#FF9500'];
export const MAP_LEVEL_NAMES = ['具体地点', '小地点', '大地点'];

/** API provider presets: [label, endpoint, model] */
export const API_PROVIDERS = [
  ['DeepSeek', 'https://api.deepseek.com/v1', 'deepseek-chat'],
  ['OpenAI', 'https://api.openai.com/v1', 'gpt-4o'],
  ['阿里云', 'https://dashscope.aliyuncs.com/compatible-mode/v1', 'qwen-plus'],
  ['智谱', 'https://open.bigmodel.cn/api/paas/v4', 'glm-4-flash'],
  ['月之暗面', 'https://api.moonshot.cn/v1', 'moonshot-v1-8k'],
  ['百川', 'https://api.baichuan-ai.com/v1', 'baichuan2-turbo'],
  ['硅基流动', 'https://api.siliconflow.cn/v1', 'Qwen/Qwen2.5-7B-Instruct']
];

/* ═══════════════════════════════════════════════════════════════
   world/world.js — 世界信息显示
   ═══════════════════════════════════════════════════════════════ */

import { state } from '../shared/state.js';
import { EventBus } from '../shared/eventbus.js';
import { $, WEATHER_ICONS } from '../shared/utils.js';

export function updateEnv(envData, locData) {
  if (!envData) return;
  $('env-era').textContent = envData.era || '---';
  $('env-weather').textContent = envData.weather || '---';
  $('weather-icon').textContent = WEATHER_ICONS[envData.weather] || '☀';

  const t = envData.time || envData;
  if (t.year) {
    $('env-time').textContent = t.year + '年' + String(t.month).padStart(2, '0') + '月' +
      String(t.day).padStart(2, '0') + '日 ' + (t.weekday || '') +
      '  ' + String(t.hour).padStart(2, '0') + ':' + String(t.minute).padStart(2, '0');
  }

  const loc = locData || envData;
  if (loc.area) {
    $('env-location').innerHTML = loc.area + ' <span class="sep">›</span> ' +
      (loc.district || '') + ' <span class="sep">›</span> ' + (loc.spot || '');
  }
}

export function updateTokenUsage(tokenUsage) {
  if (!tokenUsage || tokenUsage.calls <= 0) return;
  const total = parseInt(tokenUsage.total) || 0, calls = tokenUsage.calls || 0;
  if (total >= 1000000) $('env-tokens').textContent = (total / 1000000).toFixed(1) + 'M (' + calls + '次)';
  else if (total >= 1000) $('env-tokens').textContent = (total / 1000).toFixed(1) + 'K (' + calls + '次)';
  else $('env-tokens').textContent = total + ' (' + calls + '次)';
}

export function initWorld() {
  EventBus.on('state:refresh', data => {
    if (data.environment) updateEnv(data.environment, data.location);
    else if (data.env) updateEnv(data.env, data.env);
    if (data.tokenUsage) updateTokenUsage(data.tokenUsage);
  });
}

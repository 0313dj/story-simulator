/* ═══════════════════════════════════════════════════════════════
   app.js — 主入口，导入并初始化所有功能模块
   ═══════════════════════════════════════════════════════════════ */

/* ── Shared ── */
import { state } from './shared/state.js';
import { EventBus } from './shared/eventbus.js';
import { $ } from './shared/utils.js';

/* ── UI ── */
import { initModal } from './ui/modal.js';

/* ── Settings ── */
import { showApiManager } from './settings/api-manager.js';
import { showSaveManager } from './settings/save-manager.js';

/* ── Feature modules ── */
import { initWorld }     from './world/world.js';
import { initCharacter } from './character/character.js';
import { initChat }      from './chat/chat.js';
import { initMap }       from './map/map.js';
import { initInventory } from './inventory/inventory.js';
import { initDebug }     from './debug/debug.js';
import { initQuest }     from './quest/quest.js';
import { initFaction }   from './faction/faction.js';

/* ═══════════════════════════════════════════════════════════════
   Theme Toggle
   ═══════════════════════════════════════════════════════════════ */

function initTheme() {
  $('btn-theme').onclick = () => {
    state.dark = !state.dark;
    document.body.classList.toggle('dark', state.dark);
    $('btn-theme').textContent = state.dark ? '☼' : '☾';
  };
}

/* ═══════════════════════════════════════════════════════════════
   Bootstrap
   ═══════════════════════════════════════════════════════════════ */

(function bootstrap() {
  initTheme();
  initModal();

  /* Toolbar */
  $('btn-apimgr').onclick  = showApiManager;
  $('btn-savemgr').onclick = showSaveManager;

  /* Feature modules */
  initWorld();
  initCharacter();
  initChat();       // triggers startup (getState + chat history)
  initMap();
  initInventory();
  initDebug();
  initQuest();
  initFaction();

  console.log('[app] All modules initialized.');
})();

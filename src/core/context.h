#ifndef CONTEXT_H
#define CONTEXT_H

/* ═══════════════════════════════════════════════════════════════
   GameContext — unified state container (USE v2.0)
   Replaces the 20+ global variables in backend.c with a single
   context struct. This is Phase 1 of the architecture refactor;
   subsequent phases will change function signatures to accept
   GameContext* instead of relying on the global instance.

   Per USE Spec Chapter 2: all mutable game state lives here.
   ═══════════════════════════════════════════════════════════════ */

#include <stdbool.h>
#include <windows.h>
#include "character_card.h"
#include "environment.h"
#include "api.h"
#include "crypto.h"
#include "map.h"
#include "npc.h"
#include "worldstate.h"
#include "event.h"
#include "rule.h"
#include "registry.h"

/* Chat history entry (moved from backend.c static definition) */
#define CTX_MAX_CHAT_MSGS  512
#define CTX_MAX_CHAT_TEXT  2048

typedef struct {
    char role[16];       /* "player" or "ai" */
    char text[CTX_MAX_CHAT_TEXT];
    long long tick;
} ChatMsg;

/* ── Main context struct ── */
typedef struct {
    /* Core state */
    CharacterCard  player;
    CharacterCard  npcs[MAX_NPC_CARDS];
    int            npc_count;
    NpcManager     npc_mgr;
    Environment    env;
    ApiClient      api;
    GameMap        map;
    bool           api_ready;
    bool           world_ready;

    /* USE Architecture: canonical state + event log + rules */
    WorldState     ws;
    EventLog       events;
    RuleEngine     rule_engine;

    /* Data-Driven Architecture: type Registry (v2.0 groundwork) */
    Registry       registry;

    /* Chat history for persistence */
    ChatMsg        chat_history[CTX_MAX_CHAT_MSGS];
    int            chat_count;

    /* Filesystem */
    char           saves_root[512];
    char           base_dir[512];

    /* API profiles cache (loaded once at init, updated on save/delete) */
    ApiProfile     api_profiles[MAX_API_PROFILES];
    int            api_profile_count;

    /* Thread safety */
    CRITICAL_SECTION lock;
} GameContext;

/* ── Lifecycle ── */
void ctx_init(GameContext *ctx);
void ctx_destroy(GameContext *ctx);

/* ── Perf metrics (reserved for Phase 5: performance optimization) ── */
typedef struct {
    long long last_tick_time_ms;
    long long total_ticks;
    long long total_api_calls;
    long long total_api_tokens;
} PerfMetrics;

#endif

#ifndef WORLDSTATE_H
#define WORLDSTATE_H

#include <stdbool.h>
#include "character_card.h"
#include "environment.h"
#include "map.h"
#include "npc.h"

/* ── Schema version for save compatibility ── */
#define WS_SCHEMA_VERSION  2

/* ── Max counts ── */
#define WS_MAX_ENTITIES    (MAX_NPC_CARDS + 1)   /* +1 for player */
#define WS_MAX_LOCATIONS   128
#define WS_MAX_VARIABLES   64

/* ═══════════════════════════════════════════════════════════════
   EntityType — per USE Spec Chapter 4
   ═══════════════════════════════════════════════════════════════ */

/* (defined in character_card.h to avoid circular deps) */

/* ═══════════════════════════════════════════════════════════════
   VariableType — per USE Spec Chapter 6
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    VAR_INT,
    VAR_FLOAT,
    VAR_BOOL,
    VAR_STRING,
    VAR_ENUM,
} VariableType;

/* ── WorldVariable — per USE Spec Chapter 6 ── */
typedef struct {
    int     id;
    char    name[64];
    VariableType type;
    /* value storage */
    union {
        int   int_val;
        float float_val;
        bool  bool_val;
    };
    char    str_val[128];     /* for string/enum */
    int     min_val;
    int     max_val;
    int     flags;            /* bitflags for future use */
} WorldVariable;

/* ═══════════════════════════════════════════════════════════════
   LocationNode — per USE Spec Chapter 5 (infinite tree)
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int     id;
    int     parent_id;        /* -1 = root node */
    char    name[64];
    char    tags[256];        /* comma-separated tags */
    int     x, y;             /* coordinates 0-999 */
} LocationNode;

/* ═══════════════════════════════════════════════════════════════
   WorldState — per USE Spec Chapter 3 (single source of truth)
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int         schema_version;
    long long   tick;              /* monotonic tick counter */

    /* Entities: player always at index 0, then NPCs */
    CharacterCard entities[WS_MAX_ENTITIES];
    int           entity_count;

    /* Location tree (new format, infinite depth) */
    LocationNode  locations[WS_MAX_LOCATIONS];
    int           location_count;

    /* World variables (economy, faction standings, etc.) */
    WorldVariable variables[WS_MAX_VARIABLES];
    int           variable_count;

    /* Calendar — wraps weather, time, era, location */
    Environment   calendar;

    /* Runtime subsystems */
    NpcManager    npc_mgr;
    GameMap       map;
} WorldState;

/* ═══════════════════════════════════════════════════════════════
   WorldState API
   ═══════════════════════════════════════════════════════════════ */

/* Initialise a fresh WorldState with defaults */
void ws_init(WorldState *ws);

/* ── Entity access ── */

/* Get the player entity (always entities[0]) */
static inline CharacterCard* ws_player(WorldState *ws) {
    return &ws->entities[0];
}

/* Get NPC entity by index (0-based, from NPC pool) */
static inline CharacterCard* ws_npc(WorldState *ws, int idx) {
    return &ws->entities[1 + idx];
}

/* Get NPC count (entities minus player if present) */
static inline int ws_npc_count(const WorldState *ws) {
    if (ws->entity_count <= 0) return 0;
    return ws->entity_count - 1;
}

/* ── Variable access ── */

/* Find a world variable by name, returns NULL if not found */
WorldVariable* ws_find_variable(WorldState *ws, const char *name);

/* Set a world variable value (creates if not found) */
bool ws_set_variable(WorldState *ws, const char *name, VariableType type,
                     int int_val, float float_val, const char *str_val);

/* ── Location access ── */

/* Find a location node by name, returns NULL if not found */
LocationNode* ws_find_location_by_name(WorldState *ws, const char *name);

#endif

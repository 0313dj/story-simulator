#include "worldstate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Initialisation
   ═══════════════════════════════════════════════════════════════ */

void ws_init(WorldState *ws)
{
    memset(ws, 0, sizeof(*ws));
    ws->schema_version = WS_SCHEMA_VERSION;
    ws->tick = 0;

    /* Initialise sub-systems */
    env_init(&ws->calendar);
    map_init(&ws->map);
    npc_init(&ws->npc_mgr);

    /* Player entity at index 0 (created later via create_world) */
    /* entity_count stays 0 until world is created */
}

/* ═══════════════════════════════════════════════════════════════
   Variable access
   ═══════════════════════════════════════════════════════════════ */

WorldVariable* ws_find_variable(WorldState *ws, const char *name)
{
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0)
            return &ws->variables[i];
    }
    return NULL;
}

bool ws_set_variable(WorldState *ws, const char *name, VariableType type,
                     int int_val, float float_val, const char *str_val)
{
    if (!name || !name[0]) return false;

    WorldVariable *v = ws_find_variable(ws, name);
    if (!v) {
        /* Create new variable */
        if (ws->variable_count >= WS_MAX_VARIABLES) return false;
        v = &ws->variables[ws->variable_count];
        memset(v, 0, sizeof(*v));
        v->id = ws->variable_count;
        strncpy(v->name, name, sizeof(v->name) - 1);
        v->name[sizeof(v->name) - 1] = '\0';
        v->type = type;
        v->min_val = -1000000;
        v->max_val = 1000000;
        ws->variable_count++;
    }

    v->type = type;
    switch (type) {
    case VAR_INT:
        v->int_val = int_val;
        break;
    case VAR_FLOAT:
        v->float_val = float_val;
        break;
    case VAR_BOOL:
        v->bool_val = (int_val != 0);
        break;
    case VAR_STRING:
    case VAR_ENUM:
        if (str_val) {
            strncpy(v->str_val, str_val, sizeof(v->str_val) - 1);
            v->str_val[sizeof(v->str_val) - 1] = '\0';
        }
        break;
    }
    return true;
}

/* ═══════════════════════════════════════════════════════════════
   Location access
   ═══════════════════════════════════════════════════════════════ */

LocationNode* ws_find_location_by_name(WorldState *ws, const char *name)
{
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < ws->location_count; i++) {
        if (strcmp(ws->locations[i].name, name) == 0)
            return &ws->locations[i];
    }
    return NULL;
}

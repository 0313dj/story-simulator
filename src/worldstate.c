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

LocationNode* ws_find_location(WorldState *ws, int id)
{
    for (int i = 0; i < ws->location_count; i++) {
        if (ws->locations[i].id == id)
            return &ws->locations[i];
    }
    return NULL;
}

LocationNode* ws_find_location_by_name(WorldState *ws, const char *name)
{
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < ws->location_count; i++) {
        if (strcmp(ws->locations[i].name, name) == 0)
            return &ws->locations[i];
    }
    return NULL;
}

int ws_add_location(WorldState *ws, int parent_id, const char *name,
                    int x, int y, const char *tags)
{
    if (!name || !name[0]) return -1;
    if (ws->location_count >= WS_MAX_LOCATIONS) return -1;

    /* Check for duplicate name */
    if (ws_find_location_by_name(ws, name)) return -1;

    LocationNode *loc = &ws->locations[ws->location_count];
    memset(loc, 0, sizeof(*loc));
    loc->id = ws->location_count;
    loc->parent_id = parent_id;
    strncpy(loc->name, name, sizeof(loc->name) - 1);
    loc->name[sizeof(loc->name) - 1] = '\0';
    loc->x = x;
    loc->y = y;
    if (tags) {
        strncpy(loc->tags, tags, sizeof(loc->tags) - 1);
        loc->tags[sizeof(loc->tags) - 1] = '\0';
    }

    ws->location_count++;
    return loc->id;
}

/* ═══════════════════════════════════════════════════════════════
   Validation
   ═══════════════════════════════════════════════════════════════ */

bool ws_world_ready(const WorldState *ws)
{
    /* World is ready if player entity exists and has a name */
    if (ws->entity_count < 1) return false;
    if (!ws->entities[0].name[0]) return false;
    return true;
}

/* ═══════════════════════════════════════════════════════════════
   Export helpers
   ═══════════════════════════════════════════════════════════════ */

int ws_export_variable_catalog(const WorldState *ws, char *out, int out_size)
{
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos,
        "【世界变量目录】（仅名称和类型）\n");

    for (int i = 0; i < ws->variable_count; i++) {
        const WorldVariable *v = &ws->variables[i];
        const char *type_str = "int";
        switch (v->type) {
        case VAR_INT:    type_str = "int";    break;
        case VAR_FLOAT:  type_str = "float";  break;
        case VAR_BOOL:   type_str = "bool";   break;
        case VAR_STRING: type_str = "string"; break;
        case VAR_ENUM:   type_str = "enum";   break;
        }
        pos += snprintf(out + pos, out_size - pos, "%s(%s) ", v->name, type_str);
    }
    if (ws->variable_count == 0) {
        pos += snprintf(out + pos, out_size - pos, "(无)");
    }
    pos += snprintf(out + pos, out_size - pos, "\n");
    return pos;
}

int ws_export_variable_values(const WorldState *ws, char *out, int out_size)
{
    int pos = 0;

    for (int i = 0; i < ws->variable_count; i++) {
        const WorldVariable *v = &ws->variables[i];
        pos += snprintf(out + pos, out_size - pos, "%s=", v->name);
        switch (v->type) {
        case VAR_INT:
            pos += snprintf(out + pos, out_size - pos, "%d", v->int_val);
            break;
        case VAR_FLOAT:
            pos += snprintf(out + pos, out_size - pos, "%.2f", v->float_val);
            break;
        case VAR_BOOL:
            pos += snprintf(out + pos, out_size - pos, "%s",
                v->bool_val ? "true" : "false");
            break;
        case VAR_STRING:
        case VAR_ENUM:
            pos += snprintf(out + pos, out_size - pos, "%s", v->str_val);
            break;
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }
    return pos;
}

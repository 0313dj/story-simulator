#include "faction_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Internal helpers
   ═══════════════════════════════════════════════════════════════ */

static unsigned int hash_tick(long long tick)
{
    unsigned int h = (unsigned int)(tick & 0xFFFFFFFFU);
    h = (h * 1103515245U + 12345U) & 0x7fffffffU;
    return h;
}

static int clamp_rel(int v) { return v < -100 ? -100 : (v > 100 ? 100 : v); }
static int clamp100(int v)  { return v < 0 ? 0 : (v > 100 ? 100 : v); }

/* ── Variable access (same pattern as economy_sim) ── */

static int get_int_var(const WorldState *ws, const char *name, int default_val)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0)
            return ws->variables[i].int_val;
    }
    return default_val;
}

static void set_int_var(WorldState *ws, const char *name, int val)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0) {
            ws->variables[i].int_val = val;
            ws->variables[i].type = VAR_INT;
            return;
        }
    }
    if (ws->variable_count >= WS_MAX_VARIABLES) return;
    WorldVariable *v = &ws->variables[ws->variable_count++];
    memset(v, 0, sizeof(*v));
    v->id = ws->variable_count - 1;
    strncpy(v->name, name, sizeof(v->name) - 1);
    v->type = VAR_INT;
    v->int_val = val;
}

/* ── Collect faction entities from WorldState ── */

static int collect_faction_names(const WorldState *ws,
                                  char names[][64], int max_count)
{
    int count = 0;
    for (int i = 0; i < ws->entity_count && count < max_count; i++) {
        if (ws->entities[i].entity_type == ENTITY_FACTION ||
            ws->entities[i].entity_type == ENTITY_ORGANIZATION) {
            strncpy(names[count], ws->entities[i].name, 63);
            names[count][63] = '\0';
            count++;
        }
    }

    /* If no faction entities found, use built-in generic factions */
    if (count == 0) {
        strncpy(names[0], "王国", 63);
        strncpy(names[1], "商会", 63);
        strncpy(names[2], "冒险者公会", 63);
        count = 3;
    }

    return count;
}

/* ═══════════════════════════════════════════════════════════════
   Initialization
   ═══════════════════════════════════════════════════════════════ */

void faction_sim_init(WorldState *ws)
{
    char names[FACTION_MAX_FACTIONS][64];
    int count = collect_faction_names(ws, names, FACTION_MAX_FACTIONS);

    /* Initialize power levels */
    for (int i = 0; i < count; i++) {
        char vname[128];
        snprintf(vname, sizeof(vname), "%s%s", FAC_VAR_PREFIX_POWER, names[i]);
        set_int_var(ws, vname, 50);
    }

    /* Initialize pairwise relations */
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            char vname[256];
            snprintf(vname, sizeof(vname), "%s%s_%s",
                     FAC_VAR_PREFIX_RELATION, names[i], names[j]);
            set_int_var(ws, vname, 0);  /* neutral by default */
        }
    }

    set_int_var(ws, FAC_VAR_ACTIVE_CONFLICTS, 0);
}

/* ═══════════════════════════════════════════════════════════════
   Tick
   ═══════════════════════════════════════════════════════════════ */

void faction_sim_tick(WorldState *ws, EventLog *events, long long tick)
{
    char names[FACTION_MAX_FACTIONS][64];
    int count = collect_faction_names(ws, names, FACTION_MAX_FACTIONS);
    if (count < 2) return;

    unsigned int seed = hash_tick(tick);

    /* ── Power drift ── */
    for (int i = 0; i < count; i++) {
        char vname[128];
        snprintf(vname, sizeof(vname), "%s%s", FAC_VAR_PREFIX_POWER, names[i]);
        int power = get_int_var(ws, vname, 50);

        /* Mean reversion toward 50, small random drift */
        int drift = (50 - power) / 15;
        int noise = (int)((seed + (unsigned int)i * 31U) % 7) - 3;
        power = clamp100(power + drift + noise);

        set_int_var(ws, vname, power);
    }

    /* ── Relation drift ── */
    int conflict_count = 0;
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            char vname[256];
            snprintf(vname, sizeof(vname), "%s%s_%s",
                     FAC_VAR_PREFIX_RELATION, names[i], names[j]);
            int rel = get_int_var(ws, vname, 0);

            /* Relations drift toward 0 (neutral) unless conflict-triggered */
            int drift = -rel / 20;  /* slow return to neutral */
            int noise = (int)((seed + (unsigned int)(i * 7 + j * 13)) % 5) - 2;
            rel = clamp_rel(rel + drift + noise);

            /* Small chance of conflict escalation if relation is low */
            if (rel < -30 && (int)((seed + (unsigned int)(i * 17 + j * 29)) % 100) < 10) {
                rel = clamp_rel(rel - 10);
                conflict_count++;
                event_push(events, tick, -1, -1, EVENT_WORLD_TICK,
                    "{\"subsystem\":\"faction\",\"event\":\"conflict_escalation\","
                    "\"factionA\":\"%s\",\"factionB\":\"%s\",\"relation\":%d}",
                    names[i], names[j], rel);
            }

            /* Small chance of peace if relation is recovering */
            if (rel < -50 && (rel > -70) &&
                (int)((seed + (unsigned int)(i * 37 + j * 41)) % 100) < 5) {
                rel = clamp_rel(rel + 15);
                event_push(events, tick, -1, -1, EVENT_WORLD_TICK,
                    "{\"subsystem\":\"faction\",\"event\":\"peace_overture\","
                    "\"factionA\":\"%s\",\"factionB\":\"%s\",\"relation\":%d}",
                    names[i], names[j], rel);
            }

            set_int_var(ws, vname, rel);
        }
    }

    set_int_var(ws, FAC_VAR_ACTIVE_CONFLICTS, conflict_count);
}

/* ═══════════════════════════════════════════════════════════════
   Export
   ═══════════════════════════════════════════════════════════════ */

int faction_sim_export(const WorldState *ws, char *out, int out_size)
{
    char names[FACTION_MAX_FACTIONS][64];
    int count = collect_faction_names(ws, names, FACTION_MAX_FACTIONS);
    int pos = 0;

    pos += snprintf(out + pos, out_size - pos, "派系状态 (%d个):\n", count);

    for (int i = 0; i < count; i++) {
        char vname[128];
        snprintf(vname, sizeof(vname), "%s%s", FAC_VAR_PREFIX_POWER, names[i]);
        int power = get_int_var(ws, vname, 50);
        pos += snprintf(out + pos, out_size - pos,
            "  %s: 势力:%d/100", names[i], power);

        /* Show relations with other factions */
        int shown = 0;
        for (int j = 0; j < count; j++) {
            if (i == j) continue;
            char rname[256];
            /* Order: smaller index first */
            int a = i < j ? i : j;
            int b = i < j ? j : i;
            snprintf(rname, sizeof(rname), "%s%s_%s",
                     FAC_VAR_PREFIX_RELATION, names[a], names[b]);
            int rel = get_int_var(ws, rname, 0);
            if (abs(rel) > 10) {
                if (!shown) { pos += snprintf(out + pos, out_size - pos, " 关系:"); shown = 1; }
                pos += snprintf(out + pos, out_size - pos,
                    " %s%+d", names[j], rel);
            }
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }

    return pos;
}

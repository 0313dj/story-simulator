#include "backend.h"
#include "character_card.h"
#include "environment.h"
#include "api.h"
#include "map.h"
#include "npc.h"
#include "crypto.h"
#include "json.h"
#include "log.h"
#include "worldstate.h"
#include "event.h"
#include "rule.h"
#include "changeset.h"
#include "intent.h"
#include "planner.h"
#include "narrative.h"
#include "npc_brain.h"
#include "world_director.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* Bug #3 fix: strtok_s compat — MinGW may use strtok_r instead */
#if !defined(strtok_s) && !defined(_MSC_VER)
#define strtok_s(s, d, c) strtok_r(s, d, c)
#endif

/* MAX_NPC_CARDS defined in npc.h (Bug #51) */

/* ── Chat history persistence ── */
#define MAX_CHAT_MSGS      512
#define MAX_CHAT_TEXT      2048

typedef struct {
    char role[16];       /* "player" or "ai" */
    char text[MAX_CHAT_TEXT];
    long long tick;
} ChatMsg;

static ChatMsg g_chat_history[MAX_CHAT_MSGS];
static int    g_chat_count = 0;

/* ── Global game state ── */
static CharacterCard  g_player;
static CharacterCard  g_npc_cards[MAX_NPC_CARDS];
static int            g_npc_card_count;
static NpcManager     g_npc_mgr;
static Environment    g_env;
static ApiClient      g_api;
static GameMap        g_map;
static bool           g_api_ready;
static bool           g_world_ready;
static char           g_saves_root[512];  /* absolute path to saves directory */
static CRITICAL_SECTION g_state_lock;

/* ── USE Architecture: WorldState (canonical) + EventLog ── */
static WorldState     g_ws;
static EventLog       g_events;
static RuleEngine     g_rule_engine;

/* ── Sync runtime globals → WorldState (called before save) ── */
static void ws_sync_from_globals(void)
{
    /* Entities */
    g_ws.entity_count = 1 + g_npc_card_count;
    g_ws.entities[0] = g_player;
    for (int i = 0; i < g_npc_card_count; i++)
        g_ws.entities[1 + i] = g_npc_cards[i];

    /* Calendar */
    g_ws.calendar = g_env;

    /* Map */
    g_ws.map = g_map;

    /* NPC manager */
    g_ws.npc_mgr = g_npc_mgr;

    /* Tick (use day-based epoch as monotonic counter) */
    g_ws.tick = (long long)g_env.time.year * 525600LL
              + (long long)g_env.time.month * 43200LL
              + (long long)g_env.time.day * 1440LL
              + (long long)g_env.time.hour * 60LL
              + (long long)g_env.time.minute;

    g_ws.schema_version = WS_SCHEMA_VERSION;
}


/* ── forward ── */
static char *build_state_json(void);
static bool  save_to_file(const char *name);
static bool  load_from_file(const char *name);
static int   next_save_number(void);
static void  build_var_catalog(char *out, int out_size);
static void  extract_var_values(const char *var_names, char *out, int out_size);
static bool  name_in_list(const char *name, const char *list);
static bool  npc_is_present(const char *name);
static int   npc_compact_line(const CharacterCard *c, char *out, int out_size);
static int   npc_full_entry(const CharacterCard *c, char *out, int out_size);

/* ── Stage 3: NPC Brain helpers ── */

/* Initialize brains for all NPCs that don't have one.
   Called after world creation or load. */
static void npc_brains_init(void)
{
    long long tick = g_ws.tick;
    for (int i = 0; i < g_npc_card_count; i++) {
        CharacterCard *npc = &g_npc_cards[i];
        if (npc->entity_type == ENTITY_PLAYER) continue;
        if (npc->brain) continue;  /* already initialized */

        npc->brain = (NpcBrain*)calloc(1, sizeof(NpcBrain));
        if (!npc->brain) continue;

        nb_init(npc->brain, tick);
        nb_generate_personality(npc->brain, npc);

        char pers_buf[128];
        nb_export_personality(npc->brain, pers_buf, sizeof(pers_buf));
        log_info("NPC Brain: %s %s", npc->name, pers_buf);
    }
}

/* Free all NPC brains */
static void npc_brains_free(void)
{
    for (int i = 0; i < g_npc_card_count; i++) {
        if (g_npc_cards[i].brain) {
            free(g_npc_cards[i].brain);
            g_npc_cards[i].brain = NULL;
        }
    }
}

/* Process all NPC ticks for the current world tick.
   Returns a malloc'd JSON array string of NPC action summaries,
   or NULL if no NPCs acted. Caller must free. */
static char* npc_brains_tick_all(void)
{
    JsonBuf arr; jb_init(&arr); jb_arr_open(&arr);
    int acted = 0;

    for (int i = 0; i < g_npc_card_count; i++) {
        CharacterCard *npc = &g_npc_cards[i];
        if (!npc->brain) continue;

        char *summary = nb_tick(npc->brain, npc, &g_player,
                                 &g_rule_engine, &g_ws, &g_events, &g_api);
        if (summary) {
            if (acted > 0) jb_str(&arr, ",");
            jb_str(&arr, summary);
            free(summary);
            acted++;
        }
    }

    jb_arr_close(&arr);

    if (acted == 0) {
        char *empty = jb_detach(&arr);
        free(empty);
        return NULL;
    }

    return jb_detach(&arr);
}

/* ═══════════════════════════════════════════════════════════════
   JSON response helpers (json_get_str/int now shared in json.h)
   ═══════════════════════════════════════════════════════════════ */

static char *ok_json(const char *data_key, const char *data_json)
{
    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j, "ok", 1);
    if (data_key && data_json) jb_kv_raw(&j, data_key, data_json);
    jb_obj_close(&j);
    return jb_detach(&j);
}

static char *err_json(const char *msg)
{
    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j, "ok", 0);
    jb_kv_str(&j, "error", msg);
    jb_obj_close(&j);
    return jb_detach(&j);
}

/* ═══════════════════════════════════════════════════════════════
   SAVE  (player state → JSON → file)
   ═══════════════════════════════════════════════════════════════ */

static void state_player_json(JsonBuf *j)
{
    jb_obj_open(j);
    jb_kv_str(j, "name", g_player.name);
    jb_kv_int(j, "age", g_player.age);
    jb_kv_str(j, "gender", g_player.gender);
    jb_kv_int(j, "money", g_player.money);
    jb_kv_str(j, "clothing", g_player.clothing);
    jb_kv_int(j, "status", (int)g_player.status);
    {
        const char *st_str = "正常";
        switch (g_player.status) {
        case STATUS_NORMAL:  st_str="正常"; break;
        case STATUS_HUNGRY:  st_str="饥饿"; break;
        case STATUS_TIRED:   st_str="疲惫"; break;
        case STATUS_SICK:    st_str="生病"; break;
        case STATUS_INJURED: st_str="受伤"; break;
        case STATUS_EXCITED: st_str="兴奋"; break;
        case STATUS_ANGRY:   st_str="愤怒"; break;
        case STATUS_SAD:     st_str="悲伤"; break;
        case STATUS_HAPPY:   st_str="开心"; break;
        }
        jb_kv_str(j, "statusText", st_str);
    }
    jb_str(j, ",\"attrs\":{");
    jb_kv_int(j, "appearance", g_player.attr.appearance);
    jb_kv_int(j, "constitution", g_player.attr.constitution);
    jb_kv_int(j, "intelligence", g_player.attr.intelligence);
    jb_str(j, "}");
    jb_str(j, ",\"skills\":[");
    for (int i = 0; i < g_player.skill_count; i++) {
        if (i) jb_str(j, ",");
        jb_obj_open(j); jb_kv_str(j, "name", g_player.skills[i].name);
        jb_kv_int(j, "level", g_player.skills[i].level); jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_str(j, ",\"items\":[");
    for (int i = 0; i < g_player.item_count; i++) {
        if (i) jb_str(j, ",");
        jb_obj_open(j); jb_kv_str(j, "name", g_player.items[i].name);
        jb_kv_int(j, "qty", g_player.items[i].quantity); jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_str(j, ",\"relations\":[");
    for (int i = 0; i < g_player.relation_count; i++) {
        if (i) jb_str(j, ",");
        jb_obj_open(j); jb_kv_str(j, "target", g_player.relations[i].target);
        jb_kv_str(j, "type", relation_type_str(g_player.relations[i].type));
        jb_kv_int(j, "affinity", g_player.relations[i].affinity); jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_kv_int(j, "playerAffinity", g_player.player_affinity);
    jb_kv_int(j, "skillCount", g_player.skill_count);
    jb_kv_int(j, "itemCount", g_player.item_count);
    jb_kv_int(j, "relationCount", g_player.relation_count);
    jb_obj_close(j);
}

/* ── Location: current place + present NPCs ── */
static void state_location_json(JsonBuf *j)
{
    jb_obj_open(j);
    jb_kv_str(j, "area", g_env.location.area);
    jb_kv_str(j, "district", g_env.location.district);
    jb_kv_str(j, "spot", g_env.location.spot);

    /* Present NPCs at this location */
    jb_str(j, ",\"presentNpcs\":[");
    int first = 1;
    for (int i = 0; i < g_npc_mgr.spawn_count; i++) {
        if (g_npc_mgr.spawns[i].present) {
            if (!first) jb_str(j, ","); first = 0;
            jb_obj_open(j);
            jb_kv_str(j, "name", g_npc_mgr.spawns[i].name);
            jb_kv_int(j, "affinity", g_npc_mgr.spawns[i].player_affinity);
            jb_obj_close(j);
        }
    }
    for (int i = 0; i < g_npc_mgr.temp_count; i++) {
        if (!first) jb_str(j, ","); first = 0;
        jb_obj_open(j);
        jb_kv_str(j, "name", g_npc_mgr.temps[i].name);
        jb_kv_str(j, "desc", g_npc_mgr.temps[i].description);
        jb_kv_bool(j, "temp", true);
        jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_obj_close(j);
}

/* ── Environment: weather, time, era ── */
static void state_environment_json(JsonBuf *j)
{
    jb_obj_open(j);
    jb_kv_str(j, "era", g_env.era);
    jb_kv_str(j, "weather", weather_str(g_env.weather));
    jb_kv_int(j, "weatherCode", (int)g_env.weather);
    jb_str(j, ",\"time\":{");
    jb_kv_int(j, "year", g_env.time.year);
    jb_kv_int(j, "month", g_env.time.month);
    jb_kv_int(j, "day", g_env.time.day);
    jb_kv_int(j, "hour", g_env.time.hour);
    jb_kv_int(j, "minute", g_env.time.minute);
    jb_kv_str(j, "weekday", weekday_str(g_env.time.weekday));
    jb_str(j, "}");
    jb_obj_close(j);
}

/* Quick sanity check: is this NPC entry valid? */
static bool npc_card_valid(const CharacterCard *c)
{
    if (!c->name[0]) return false;
    /* Name must have at least one non-ASCII byte (Chinese) or be alphanumeric */
    int len = (int)strlen(c->name);
    if (len < 1 || len > 60) return false;
    /* Reject names that look like raw variable dumps */
    if (strchr(c->name, '=') || strchr(c->name, '\n') || strchr(c->name, '\r'))
        return false;
    return true;
}

static void state_npcs_json(JsonBuf *j)
{
    int first = 1;
    jb_str(j, "[");
    for (int i = 0; i < g_npc_card_count; i++) {
        if (!npc_card_valid(&g_npc_cards[i])) continue;
        if (!first) jb_str(j, ",");
        first = 0;
        jb_obj_open(j);
        jb_kv_str(j, "name", g_npc_cards[i].name);
        jb_kv_int(j, "age", g_npc_cards[i].age);
        jb_kv_str(j, "gender", g_npc_cards[i].gender);
        jb_kv_str(j, "clothing", g_npc_cards[i].clothing);
        jb_kv_str(j, "personality", g_npc_cards[i].personality);
        jb_kv_str(j, "home", g_npc_cards[i].home);
        jb_kv_int(j, "status", (int)g_npc_cards[i].status);
        {
            const char *st = "正常";
            switch (g_npc_cards[i].status) {
            case STATUS_NORMAL:  st="正常"; break;
            case STATUS_HUNGRY:  st="饥饿"; break;
            case STATUS_TIRED:   st="疲惫"; break;
            case STATUS_SICK:    st="生病"; break;
            case STATUS_INJURED: st="受伤"; break;
            case STATUS_EXCITED: st="兴奋"; break;
            case STATUS_ANGRY:   st="愤怒"; break;
            case STATUS_SAD:     st="悲伤"; break;
            case STATUS_HAPPY:   st="开心"; break;
            }
            jb_kv_str(j, "statusText", st);
        }
        jb_kv_int(j, "money", g_npc_cards[i].money);
        jb_str(j, ",\"attrs\":{");
        jb_kv_int(j, "appearance", g_npc_cards[i].attr.appearance);
        jb_kv_int(j, "constitution", g_npc_cards[i].attr.constitution);
        jb_kv_int(j, "intelligence", g_npc_cards[i].attr.intelligence);
        jb_str(j, "}");
        jb_kv_int(j, "playerAffinity", g_npc_cards[i].player_affinity);
        /* Check if this NPC is currently present at the location */
        {
            bool present = false;
            for (int s = 0; s < g_npc_mgr.spawn_count; s++) {
                if (g_npc_mgr.spawns[s].present &&
                    strcmp(g_npc_mgr.spawns[s].name, g_npc_cards[i].name) == 0) {
                    present = true; break;
                }
            }
            jb_kv_bool(j, "present", present);
        }
        jb_str(j, ",\"skills\":[");
        for (int k = 0; k < g_npc_cards[i].skill_count; k++) {
            if (k) jb_str(j, ",");
            jb_obj_open(j);
            jb_kv_str(j, "name", g_npc_cards[i].skills[k].name);
            jb_kv_int(j, "level", g_npc_cards[i].skills[k].level);
            jb_obj_close(j);
        }
        jb_str(j, "]");
        jb_str(j, ",\"items\":[");
        for (int k = 0; k < g_npc_cards[i].item_count; k++) {
            if (k) jb_str(j, ",");
            jb_obj_open(j);
            jb_kv_str(j, "name", g_npc_cards[i].items[k].name);
            jb_kv_int(j, "qty", g_npc_cards[i].items[k].quantity);
            jb_obj_close(j);
        }
        jb_str(j, "]");
        jb_kv_int(j, "skillCount", g_npc_cards[i].skill_count);
        jb_kv_int(j, "itemCount", g_npc_cards[i].item_count);
        jb_obj_close(j);
    }
    jb_str(j, "]");
}

static char *build_state_json(void)
{
    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j, "ok", 1);
    jb_kv_bool(&j, "worldReady", g_world_ready);
    jb_kv_bool(&j, "apiReady", g_api_ready);

    /* Token usage stats */
    {
        long long tok_prompt = 0, tok_completion = 0, tok_total = 0;
        int calls = api_get_token_usage(&g_api, &tok_prompt, &tok_completion, &tok_total);
        jb_str(&j, ",\"tokenUsage\":{");
        jb_kv_int(&j, "calls", calls);
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", tok_total);
            jb_kv_str(&j, "total", buf);
            snprintf(buf, sizeof(buf), "%lld", tok_prompt);
            jb_kv_str(&j, "prompt", buf);
            snprintf(buf, sizeof(buf), "%lld", tok_completion);
            jb_kv_str(&j, "completion", buf);
        }
        jb_str(&j, "}");
    }

    /* USE Architecture: schema version and tick */
    jb_kv_int(&j, "schemaVersion", WS_SCHEMA_VERSION);
    {
        long long tick = (long long)g_env.time.year * 525600LL
                       + (long long)g_env.time.month * 43200LL
                       + (long long)g_env.time.day * 1440LL
                       + (long long)g_env.time.hour * 60LL
                       + (long long)g_env.time.minute;
        char tick_str[32];
        snprintf(tick_str, sizeof(tick_str), "%lld", tick);
        jb_kv_str(&j, "tick", tick_str);
    }

    /* 1. 用户角色卡 */
    jb_str(&j, ",\"character\":"); state_player_json(&j);

    /* 2. 有角色卡的NPC */
    jb_str(&j, ",\"npcs\":"); state_npcs_json(&j);

    /* 3. 地点及在场NPC */
    jb_str(&j, ",\"location\":"); state_location_json(&j);

    /* 4. 天气、时间、时代等环境变量 */
    jb_str(&j, ",\"environment\":"); state_environment_json(&j);

    /* Map points (UI display) */
    {
        char map_json[4096];
        map_export_json(&g_map, map_json, sizeof(map_json));
        /* Bug #12: ensure comma before raw JSON insertion */
        if (j.buf[j.len - 1] != '{' && j.buf[j.len - 1] != '[')
            jb_str(&j, ",");
        jb_str(&j, "\"mapPoints\":");
        jb_str(&j, map_json);
    }
    /* NPC temp cache (save/load persistence, not for UI) */
    jb_str(&j, ",\"npcTempCache\":");
    npc_cache_to_json(&g_npc_mgr, &j);

    /* World variables summary */
    if (g_ws.variable_count > 0) {
        jb_str(&j, ",\"worldVariables\":[");
        for (int i = 0; i < g_ws.variable_count; i++) {
            if (i > 0) jb_str(&j, ",");
            WorldVariable *v = &g_ws.variables[i];
            jb_obj_open(&j);
            jb_kv_str(&j, "name", v->name);
            switch (v->type) {
            case VAR_INT:    jb_kv_int(&j, "value", v->int_val); break;
            case VAR_FLOAT:  jb_kv_dbl(&j, "value", v->float_val); break;
            case VAR_BOOL:   jb_kv_bool(&j, "value", v->bool_val); break;
            case VAR_STRING: case VAR_ENUM:
                jb_kv_str(&j, "value", v->str_val); break;
            }
            jb_obj_close(&j);
        }
        jb_str(&j, "]");
    }

    /* Event log summary */
    {
        jb_kv_int(&j, "eventCount", g_events.count);
    }

    jb_obj_close(&j);
    return jb_detach(&j);
}

static bool write_json_file(const char *path, const char *json)
{
    if (!json || !json[0]) {
        log_error("save: 拒绝写入空JSON -> %s", path);
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        log_error("save: 无法创建文件 %s (errno=%d)", path, errno);
        return false;
    }
    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, fp);
    fclose(fp);
    if (written != len) {
        log_error("save: 写入不完整 %s (%zu/%zu)", path, written, len);
        return false;
    }
    return true;
}

static bool save_to_file(const char *name)
{
    char dir[576];
    snprintf(dir, sizeof(dir), "%s\\%s", g_saves_root, name);

    /* Create target directory — ensure full path exists */
    wchar_t wdir[576];
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, 576);
    if (!CreateDirectoryW(wdir, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            log_error("save: 创建目录失败 %s (err=%lu)", dir, err);
            return false;
        }
    }

    bool ok = true;

    /* ── 1. character.json ── */
    {
        JsonBuf j; jb_init(&j);
        state_player_json(&j);
        char *json = jb_detach(&j);
        char path[576]; snprintf(path, sizeof(path), "%s\\character.json", dir);
        if (!write_json_file(path, json)) ok = false;
        else log_info("save: character.json (%d bytes)", (int)strlen(json));
        free(json);
    }

    /* ── 2. npcs.json ── */
    {
        JsonBuf j; jb_init(&j);
        state_npcs_json(&j);
        char *json = jb_detach(&j);
        char path[576]; snprintf(path, sizeof(path), "%s\\npcs.json", dir);
        if (!write_json_file(path, json)) ok = false;
        else log_info("save: npcs.json (%d bytes, %d NPCs)", (int)strlen(json), g_npc_card_count);
        free(json);
    }

    /* ── 3. world.json (schema_version + location + environment + map + temp cache) ── */
    {
        JsonBuf j; jb_init(&j); jb_obj_open(&j);
        /* Schema version for forward/backward compatibility */
        jb_kv_int(&j, "schema_version", WS_SCHEMA_VERSION);
        jb_str(&j, ",\"location\":");    state_location_json(&j);
        jb_str(&j, ",\"environment\":"); state_environment_json(&j);
        /* Map points */
        {
            char map_json[4096]; map_export_json(&g_map, map_json, sizeof(map_json));
            jb_str(&j, ",\"mapPoints\":"); jb_str(&j, map_json);
        }
        /* NPC temp cache */
        jb_str(&j, ",\"npcTempCache\":");
        npc_cache_to_json(&g_npc_mgr, &j);
        jb_obj_close(&j);
        char *json = jb_detach(&j);
        char path[576]; snprintf(path, sizeof(path), "%s\\world.json", dir);
        if (!write_json_file(path, json)) ok = false;
        else log_info("save: world.json (%d bytes)", (int)strlen(json));
        free(json);
    }

    /* ── 4. variables.json (world variables) ── */
    {
        JsonBuf j; jb_init(&j); jb_obj_open(&j);
        jb_kv_int(&j, "schema_version", WS_SCHEMA_VERSION);
        jb_str(&j, ",\"variables\":[");
        for (int i = 0; i < g_ws.variable_count; i++) {
            if (i > 0) jb_str(&j, ",");
            WorldVariable *v = &g_ws.variables[i];
            jb_obj_open(&j);
            jb_kv_int(&j, "id", v->id);
            jb_kv_str(&j, "name", v->name);
            jb_kv_int(&j, "type", (int)v->type);
            switch (v->type) {
            case VAR_INT:    jb_kv_int(&j, "value", v->int_val); break;
            case VAR_FLOAT:  jb_kv_dbl(&j, "value", v->float_val); break;
            case VAR_BOOL:   jb_kv_bool(&j, "value", v->bool_val); break;
            case VAR_STRING: case VAR_ENUM:
                jb_kv_str(&j, "value", v->str_val); break;
            }
            jb_kv_int(&j, "min", v->min_val);
            jb_kv_int(&j, "max", v->max_val);
            jb_kv_int(&j, "flags", v->flags);
            jb_obj_close(&j);
        }
        jb_str(&j, "]");
        jb_obj_close(&j);
        char *json = jb_detach(&j);
        char path[576]; snprintf(path, sizeof(path), "%s\\variables.json", dir);
        if (!write_json_file(path, json)) ok = false;
        else log_info("save: variables.json (%d bytes, %d vars)", (int)strlen(json), g_ws.variable_count);
        free(json);
    }

    /* ── 5. events.json (event log) ── */
    {
        char *json = event_export_json(&g_events);
        if (json) {
            char path[576]; snprintf(path, sizeof(path), "%s\\events.json", dir);
            if (!write_json_file(path, json)) ok = false;
            else log_info("save: events.json (%d bytes, %d events)", (int)strlen(json), g_events.count);
            free(json);
        }
    }

    /* ── 6. rules.json (rule engine state) ── */
    {
        JsonBuf j; jb_init(&j); jb_arr_open(&j);
        for (int i = 0; i < g_rule_engine.count; i++) {
            if (i > 0) jb_str(&j, ",");
            Rule *r = &g_rule_engine.rules[i];
            jb_obj_open(&j);
            jb_kv_int(&j, "id", r->id);
            jb_kv_int(&j, "priority", r->priority);
            jb_kv_str(&j, "condition", r->condition);
            jb_kv_bool(&j, "enabled", r->enabled);
            jb_kv_int(&j, "effectCount", r->effect_count);
            jb_str(&j, ",\"effects\":[");
            for (int e = 0; e < r->effect_count; e++) {
                if (e > 0) jb_str(&j, ",");
                jb_obj_open(&j);
                jb_kv_int(&j, "type", (int)r->effects[e].type);
                jb_kv_str(&j, "target", r->effects[e].target);
                jb_kv_int(&j, "intValue", r->effects[e].int_value);
                jb_kv_int(&j, "intValue2", r->effects[e].int_value2);
                jb_kv_str(&j, "strValue", r->effects[e].str_value);
                jb_obj_close(&j);
            }
            jb_str(&j, "]");
            jb_obj_close(&j);
        }
        jb_arr_close(&j);
        char *json = jb_detach(&j);
        char path[576]; snprintf(path, sizeof(path), "%s\\rules.json", dir);
        if (!write_json_file(path, json)) ok = false;
        else log_info("save: rules.json (%d bytes, %d rules)", (int)strlen(json), g_rule_engine.count);
        free(json);
    }

    /* ── 7. chat.json (chat history) ── */
    {
        JsonBuf j; jb_init(&j); jb_arr_open(&j);
        for (int i = 0; i < g_chat_count; i++) {
            if (i > 0) jb_str(&j, ",");
            jb_obj_open(&j);
            jb_kv_str(&j, "role", g_chat_history[i].role);
            jb_kv_str(&j, "text", g_chat_history[i].text);
            {
                char tick_str[32];
                snprintf(tick_str, sizeof(tick_str), "%lld", g_chat_history[i].tick);
                jb_kv_str(&j, "tick", tick_str);
            }
            jb_obj_close(&j);
        }
        jb_arr_close(&j);
        char *json = jb_detach(&j);
        char path[576]; snprintf(path, sizeof(path), "%s\\chat.json", dir);
        if (!write_json_file(path, json)) ok = false;
        else log_info("save: chat.json (%d bytes, %d messages)", (int)strlen(json), g_chat_count);
        free(json);
    }

    if (ok)
        log_info("save: 成功写入 %s", name);
    else
        log_error("save: 写入失败 %s", name);
    return ok;
}

/* ═══════════════════════════════════════════════════════════════
   NPC DATABASE  (long-term persistent storage, survives world resets)
   ═══════════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════════
   LOAD  (file → JSON → player + env state)
   ═══════════════════════════════════════════════════════════════ */

static const char *extract_sub_obj(const char *json, const char *key)
{
    /* Find "key":{ and return pointer just inside the opening brace */
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":{", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    /* Bug #7 fix: check strchr result before adding 1 */
    const char *brace = strchr(p, '{');
    if (!brace) return NULL;
    return brace + 1;
}

/* Read a counted array of objects from JSON.  Caller provides a
   starting position (just after '['), element callback, and userdata.
   Returns count parsed. */
typedef void (*elem_fn)(const char *obj_start, void *ud, int idx);

static int json_parse_array(const char *json, const char *key,
                             elem_fn cb, void *ud)
{
    /* Bug #8 fix: when key is NULL, json is assumed to point directly at '[' */
    if (key) {
        char pat[128];
        snprintf(pat, sizeof(pat), "\"%s\":[", key);
        const char *p = strstr(json, pat);
        if (!p) return 0;
        p = strchr(p, '[');
        if (!p) return 0;
        json = p + 1;
    } else {
        /* key is NULL: parse from current position (json should point at '[' or after) */
        const char *p = strchr(json, '[');
        if (!p) return 0;
        json = p + 1;
    }
    const char *p = json;
    if (*p == ']') return 0;
    int count = 0;
    while (*p) {
        if (*p == ']' || *p == '\0') break;
        if (*p == '{') {
            cb(p, ud, count);
            count++;
            p = strchr(p, '}');
            if (!p) break;
        }
        p++;
    }
    return count;
}

/* Parse context: points to the target CharacterCard */
typedef struct { CharacterCard *cc; } ParseCtx;

static void parse_skill_cb(const char *obj, void *ud, int idx)
{
    ParseCtx *cx = (ParseCtx *)ud;
    Skill *s = &cx->cc->skills[idx];
    json_get_str(obj, "name", s->name, MAX_SKILL_NAME_LEN);
    json_get_int(obj, "level", &s->level);
}

static void parse_item_cb(const char *obj, void *ud, int idx)
{
    ParseCtx *cx = (ParseCtx *)ud;
    Item *it = &cx->cc->items[idx];
    json_get_str(obj, "name", it->name, MAX_ITEM_NAME_LEN);
    json_get_int(obj, "qty", &it->quantity);
}

static RelationType parse_relation_type_str(const char *s)
{
    if (strstr(s, "PARENT") || strstr(s, "父母"))   return REL_PARENT;
    if (strstr(s, "CHILD") || strstr(s, "子女"))    return REL_CHILD;
    if (strstr(s, "SIBLING") || strstr(s, "兄弟姐妹")) return REL_SIBLING;
    if (strstr(s, "SPOUSE") || strstr(s, "配偶"))   return REL_SPOUSE;
    if (strstr(s, "LOVER") || strstr(s, "恋人"))    return REL_LOVER;
    if (strstr(s, "EX") || strstr(s, "前任"))       return REL_EX;
    if (strstr(s, "KIN") || strstr(s, "亲戚"))      return REL_KIN;
    if (strstr(s, "BEST") || strstr(s, "挚友"))     return REL_BEST_FRIEND;
    if (strstr(s, "FRIEND") || strstr(s, "朋友"))   return REL_FRIEND;
    if (strstr(s, "RIVAL") || strstr(s, "对手"))    return REL_RIVAL;
    if (strstr(s, "ENEMY") || strstr(s, "仇敌"))    return REL_ENEMY;
    if (strstr(s, "COLLEAGUE") || strstr(s, "同事")) return REL_COLLEAGUE;
    if (strstr(s, "MASTER") || strstr(s, "师父"))   return REL_MASTER;
    if (strstr(s, "DISCIPLE") || strstr(s, "徒弟")) return REL_DISCIPLE;
    if (strstr(s, "ACQUAINTANCE") || strstr(s, "熟人")) return REL_ACQUAINTANCE;
    return REL_STRANGER;
}

static void parse_relation_cb(const char *obj, void *ud, int idx)
{
    ParseCtx *cx = (ParseCtx *)ud;
    Relationship *r = &cx->cc->relations[idx];
    json_get_str(obj, "target", r->target, MAX_REL_TARGET_LEN);
    json_get_int(obj, "affinity", &r->affinity);
    char type_str[64] = {0};
    json_get_str(obj, "type", type_str, sizeof(type_str));
    r->type = type_str[0] ? parse_relation_type_str(type_str) : REL_STRANGER;
}

static void parse_npc_obj(const char *obj, void *ud, int idx)
{
    (void)ud;  /* unused — writes directly to g_npc_cards[idx] */
    if (idx >= MAX_NPC_CARDS) return;
    CharacterCard *cc = &g_npc_cards[idx];
    memset(cc, 0, sizeof(*cc));
    json_get_str(obj, "name", cc->name, 64);
    json_get_int(obj, "age", &cc->age);
    json_get_str(obj, "gender", cc->gender, MAX_GENDER_LEN);
    json_get_str(obj, "clothing", cc->clothing, MAX_CLOTHING_LEN);
    json_get_str(obj, "personality", cc->personality, MAX_PERSONALITY_LEN);
    json_get_str(obj, "home", cc->home, MAX_HOME_LEN);
    json_get_int(obj, "status", (int*)&cc->status);
    json_get_int(obj, "money", &cc->money);
    json_get_int(obj, "appearance", &cc->attr.appearance);
    json_get_int(obj, "constitution", &cc->attr.constitution);
    json_get_int(obj, "intelligence", &cc->attr.intelligence);
    json_get_int(obj, "playerAffinity", &cc->player_affinity);

    ParseCtx cx = { cc };
    cc->skill_count = json_parse_array(obj, "skills", parse_skill_cb, &cx);
    if (cc->skill_count > MAX_SKILLS) cc->skill_count = MAX_SKILLS;
    cc->item_count   = json_parse_array(obj, "items", parse_item_cb, &cx);
    if (cc->item_count > MAX_ITEMS) cc->item_count = MAX_ITEMS;
    cc->relation_count = json_parse_array(obj, "relations", parse_relation_cb, &cx);
    if (cc->relation_count > MAX_RELATIONS) cc->relation_count = MAX_RELATIONS;
}

/* ── NPC database loader (defined here to access static parse helpers) ── */
/* Helper: read entire file into malloc'd buffer, return NULL on failure */
static char *slurp_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 4*1024*1024) { fclose(fp); return NULL; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t rd = fread(buf, 1, sz, fp);
    fclose(fp);
    if (rd != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = '\0';
    return buf;
}

/* Parse player data from a JSON char-card scope into g_player */
static void parse_player_scope(const char *scope)
{
    json_get_str(scope, "name",     g_player.name, 64);
    json_get_int(scope, "age",      &g_player.age);
    json_get_str(scope, "gender",   g_player.gender, MAX_GENDER_LEN);
    json_get_int(scope, "money",    &g_player.money);
    json_get_str(scope, "clothing", g_player.clothing, MAX_CLOTHING_LEN);
    json_get_int(scope, "status",   (int*)&g_player.status);
    json_get_int(scope, "appearance", &g_player.attr.appearance);
    json_get_int(scope, "constitution", &g_player.attr.constitution);
    json_get_int(scope, "intelligence", &g_player.attr.intelligence);
    ParseCtx pcx = { &g_player };
    g_player.skill_count = json_parse_array(scope, "skills", parse_skill_cb, &pcx);
    if (g_player.skill_count > MAX_SKILLS) g_player.skill_count = MAX_SKILLS;
    g_player.item_count = json_parse_array(scope, "items", parse_item_cb, &pcx);
    if (g_player.item_count > MAX_ITEMS) g_player.item_count = MAX_ITEMS;
    g_player.relation_count = json_parse_array(scope, "relations", parse_relation_cb, &pcx);
    if (g_player.relation_count > MAX_RELATIONS) g_player.relation_count = MAX_RELATIONS;
    json_get_int(scope, "playerAffinity", &g_player.player_affinity);
}

static bool load_from_file(const char *name)
{
    char dir[576];
    snprintf(dir, sizeof(dir), "%s\\%s", g_saves_root, name);
    log_info("load: 尝试读取 %s", dir);

    /* ── New format: folder with character.json + npcs.json + world.json ── */
    char ch_path[576], np_path[576], wo_path[576];
    snprintf(ch_path, sizeof(ch_path), "%s\\character.json", dir);
    snprintf(np_path, sizeof(np_path), "%s\\npcs.json", dir);
    snprintf(wo_path, sizeof(wo_path), "%s\\world.json", dir);

    char *ch_json = slurp_file(ch_path);
    if (ch_json) {
        /* New folder format */
        npc_brains_free();  /* Stage 3: clean up old brains before reload */
        memset(&g_player, 0, sizeof(g_player));
        g_npc_card_count = 0;
        map_init(&g_map);
        parse_player_scope(ch_json);
        free(ch_json);

        if (g_player.name[0] == '\0')
            log_warn("load: 玩家名为空");

        /* NPCs */
        char *np_json = slurp_file(np_path);
        if (np_json) {
            g_npc_card_count = json_parse_array(np_json, NULL, parse_npc_obj, NULL);
            if (g_npc_card_count > MAX_NPC_CARDS) g_npc_card_count = MAX_NPC_CARDS;
            log_info("load: 恢复了 %d 个NPC", g_npc_card_count);
            free(np_json);
        }

        /* World (schema_version + location + environment + map + temp cache) */
        char *wo_json = slurp_file(wo_path);
        if (wo_json) {
            /* Read schema version (may be absent in old saves) */
            int saved_schema = 0;
            json_get_int(wo_json, "schema_version", &saved_schema);
            if (saved_schema > 0 && saved_schema < WS_SCHEMA_VERSION) {
                log_info("load: 旧存档格式 v%d → 将自动升级到 v%d",
                         saved_schema, WS_SCHEMA_VERSION);
            }

            /* location */
            const char *lscope = extract_sub_obj(wo_json, "location");
            if (lscope) {
                json_get_str(lscope, "area",     g_env.location.area, 32);
                json_get_str(lscope, "district", g_env.location.district, 32);
                json_get_str(lscope, "spot",     g_env.location.spot, 32);
            }
            /* environment */
            const char *escope = extract_sub_obj(wo_json, "environment");
            if (escope) {
                json_get_str(escope, "era",     g_env.era, 64);
                json_get_int(escope, "weatherCode", (int*)&g_env.weather);
                const char *tscope = extract_sub_obj(escope, "time");
                json_get_int(tscope?tscope:escope, "year",    &g_env.time.year);
                json_get_int(tscope?tscope:escope, "month",   &g_env.time.month);
                json_get_int(tscope?tscope:escope, "day",     &g_env.time.day);
                json_get_int(tscope?tscope:escope, "hour",    &g_env.time.hour);
                json_get_int(tscope?tscope:escope, "minute",  &g_env.time.minute);
                json_get_int(tscope?tscope:escope, "weekday", (int*)&g_env.time.weekday);
            }
            /* Map points */
            {
                const char *mp = extract_sub_obj(wo_json, "mapPoints");
                if (!mp) {
                    /* mapPoints is an array, not an object — try direct key search */
                    const char *mk = strstr(wo_json, "\"mapPoints\":");
                    if (mk) {
                        mk = strchr(mk, ':');
                        if (mk) { mk++; while (*mk == ' ') mk++; }
                    }
                    if (mk) map_import_json(&g_map, mk);
                } else {
                    /* If it was extracted as an object, it's actually an array — back up to '[' */
                    const char *arr_start = strstr(wo_json, "\"mapPoints\":");
                    if (arr_start) {
                        arr_start = strchr(arr_start, '[');
                        if (arr_start) map_import_json(&g_map, arr_start);
                    }
                }
            }
            /* NPC temp cache */
            npc_parse_cache_from_json(&g_npc_mgr, wo_json);
            free(wo_json);
        }

        /* ── Load world variables (variables.json, optional) ── */
        {
            char var_path[576];
            snprintf(var_path, sizeof(var_path), "%s\\variables.json", dir);
            char *var_json = slurp_file(var_path);
            if (var_json) {
                /* Parse variables array */
                const char *arr = strstr(var_json, "\"variables\":[");
                if (arr) {
                    arr = strchr(arr, '[');
                    if (arr) {
                        arr++; /* skip '[' */
                        const char *p = arr;
                        while (*p && g_ws.variable_count < WS_MAX_VARIABLES) {
                            if (*p == '{') {
                                WorldVariable *v = &g_ws.variables[g_ws.variable_count];
                                memset(v, 0, sizeof(*v));
                                json_get_int(p, "id", &v->id);
                                json_get_str(p, "name", v->name, sizeof(v->name));
                                int itype = 0;
                                json_get_int(p, "type", &itype);
                                v->type = (VariableType)itype;
                                switch (v->type) {
                                case VAR_INT:
                                    json_get_int(p, "value", &v->int_val);
                                    break;
                                case VAR_FLOAT: {
                                    /* json_get_int won't work for float, parse manual */
                                    const char *vk = strstr(p, "\"value\":");
                                    if (vk) {
                                        vk = strchr(vk, ':');
                                        if (vk) v->float_val = (float)atof(vk + 1);
                                    }
                                    break;
                                }
                                case VAR_BOOL: {
                                    int bv = 0;
                                    json_get_int(p, "value", &bv);
                                    v->bool_val = (bv != 0);
                                    break;
                                }
                                case VAR_STRING: case VAR_ENUM:
                                    json_get_str(p, "value", v->str_val, sizeof(v->str_val));
                                    break;
                                }
                                json_get_int(p, "min", &v->min_val);
                                json_get_int(p, "max", &v->max_val);
                                json_get_int(p, "flags", &v->flags);
                                g_ws.variable_count++;
                                p = strchr(p, '}');
                                if (!p) break;
                            }
                            p++;
                        }
                    }
                }
                free(var_json);
                log_info("load: 恢复了 %d 个世界变量", g_ws.variable_count);
            }
        }

        /* ── Load event log (events.json, optional) ── */
        {
            char ev_path[576];
            snprintf(ev_path, sizeof(ev_path), "%s\\events.json", dir);
            char *ev_json = slurp_file(ev_path);
            if (ev_json) {
                /* Parse event array — restore event count for continuity */
                const char *p = ev_json;
                while (*p && *p != '[') p++;
                if (*p == '[') {
                    p++;
                    int ev_count = 0;
                    while (*p && ev_count < MAX_EVENTS) {
                        if (*p == '{') {
                            Event *e = &g_events.events[ev_count];
                            memset(e, 0, sizeof(*e));
                            json_get_int(p, "id", &e->id);
                            /* tick is long long, stored as number */
                            {
                                const char *tk = strstr(p, "\"tick\":");
                                if (tk) {
                                    tk = strchr(tk, ':');
                                    if (tk) e->tick = (long long)atoll(tk + 1);
                                }
                            }
                            json_get_int(p, "source", &e->source_id);
                            json_get_int(p, "target", &e->target_id);
                            {
                                char tstr[32] = {0};
                                json_get_str(p, "type", tstr, sizeof(tstr));
                                if (strcmp(tstr, "SYSTEM") == 0) e->type = EVENT_SYSTEM;
                                else if (strcmp(tstr, "ACTION") == 0) e->type = EVENT_ACTION;
                                else if (strcmp(tstr, "RULE_HIT") == 0) e->type = EVENT_RULE_HIT;
                                else if (strcmp(tstr, "STATE_CHANGE") == 0) e->type = EVENT_STATE_CHANGE;
                                else if (strcmp(tstr, "NARRATIVE") == 0) e->type = EVENT_NARRATIVE;
                                else if (strcmp(tstr, "WORLD_TICK") == 0) e->type = EVENT_WORLD_TICK;
                            }
                            json_get_str(p, "payload", e->payload, sizeof(e->payload));
                            ev_count++;
                            p = strchr(p, '}');
                            if (!p) break;
                        }
                        p++;
                    }
                    g_events.count = ev_count;
                    if (ev_count > 0) g_events.next_id = g_events.events[ev_count - 1].id + 1;
                }
                free(ev_json);
                log_info("load: 恢复了 %d 条事件", g_events.count);
            }
        }

        /* ── Load rules (rules.json, optional) ──
           Built-in rules are always re-registered on startup via
           re_register_builtins(). Custom rules loaded here would be
           appended after built-ins. Currently reserved for future
           custom rule support. */
        {
            char rl_path[576];
            snprintf(rl_path, sizeof(rl_path), "%s\\rules.json", dir);
            char *rl_json = slurp_file(rl_path);
            if (rl_json) {
                log_info("load: rules.json present (%d bytes)", (int)strlen(rl_json));
                free(rl_json);
            }
        }

        /* ── Load chat history (chat.json, optional) ── */
        {
            char ch_path2[576];
            snprintf(ch_path2, sizeof(ch_path2), "%s\\chat.json", dir);
            char *ch_json = slurp_file(ch_path2);
            if (ch_json) {
                g_chat_count = 0;
                const char *p = ch_json;
                while (*p && *p != '[') p++;
                if (*p == '[') {
                    p++;
                    while (*p && g_chat_count < MAX_CHAT_MSGS) {
                        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == ',') p++;
                        if (*p == ']' || *p == '\0') break;
                        if (*p == '{') {
                            ChatMsg *cm = &g_chat_history[g_chat_count];
                            memset(cm, 0, sizeof(*cm));
                            json_get_str(p, "role", cm->role, sizeof(cm->role));
                            json_get_str(p, "text", cm->text, sizeof(cm->text));
                            {
                                const char *tk = strstr(p, "\"tick\":");
                                if (tk) {
                                    tk = strchr(tk, ':');
                                    if (tk) cm->tick = (long long)atoll(tk + 1);
                                }
                            }
                            if (cm->role[0] && cm->text[0]) g_chat_count++;
                            p = strchr(p, '}');
                            if (!p) break;
                        }
                        p++;
                    }
                }
                free(ch_json);
                log_info("load: 恢复了 %d 条聊天记录", g_chat_count);
            }
        }

        /* Sync WorldState from loaded globals */
        ws_sync_from_globals();

        /* Repair runtime fields */
        mem_init(&g_player.memory);
        for (int i = 0; i < g_npc_card_count; i++)
            mem_init(&g_npc_cards[i].memory);

        npc_init(&g_npc_mgr);
        {
            char current_loc[MAX_NPC_LOC];
            npc_make_location_key(&g_env.location, current_loc, sizeof(current_loc));
            npc_restore_temps(&g_npc_mgr, current_loc);
        }

        map_locate(&g_map, g_env.location.area,
                   g_env.location.district, g_env.location.spot);
        g_world_ready = true;
        /* Stage 3: initialize NPC brains */
        npc_brains_init();
        log_info("load: 成功(folder) — %s", g_player.name);
        return true;
    }

    /* ── Old format: single binary-wrapped JSON file ── */
    {
        char old_path[576];
        snprintf(old_path, sizeof(old_path), "%s\\%s.dat", g_saves_root, name);

        FILE *fp = fopen(old_path, "rb");
        if (!fp) {
            /* Try alternate: name without .dat extension */
            snprintf(old_path, sizeof(old_path), "%s\\%s", g_saves_root, name);
            fp = fopen(old_path, "rb");
        }
        if (!fp) return false;

        uint32_t magic = 0, version = 0, json_len = 0;
        if (fread(&magic, 4, 1, fp) != 1 ||
            fread(&version, 4, 1, fp) != 1 ||
            fread(&json_len, 4, 1, fp) != 1) {
            log_error("load: 读取旧格式文件头失败");
            fclose(fp); return false;
        }
        if (!(magic == 0x32564153 && version == 2)) {
            log_error("load: 旧格式不匹配");
            fclose(fp); return false;
        }
        if (json_len == 0 || json_len > 16*1024*1024) {
            fclose(fp); return false;
        }
        /* Bug #9 fix: validate json_len against remaining file size */
        {
            long cur = ftell(fp);
            fseek(fp, 0, SEEK_END);
            long remaining = ftell(fp) - cur;
            fseek(fp, cur, SEEK_SET);
            if (json_len > remaining) {
                fclose(fp); return false;
            }
        }
        char *json = (char *)malloc(json_len + 1);
        if (!json) { fclose(fp); return false; }
        size_t rd = fread(json, 1, json_len, fp);
        if (rd != (size_t)json_len) {
            /* Partial read: still usable if we got enough */
            if (rd == 0) { free(json); fclose(fp); return false; }
            json_len = (int)rd;
        }
        fclose(fp);
        json[json_len] = '\0';

        npc_brains_free();  /* Stage 3: clean up old brains before reload */
        memset(&g_player, 0, sizeof(g_player));
        g_npc_card_count = 0;
        map_init(&g_map);

        const char *pscope = extract_sub_obj(json, "character");
        if (!pscope) pscope = extract_sub_obj(json, "player");
        if (!pscope) { log_error("load: 找不到角色数据"); free(json); return false; }
        parse_player_scope(pscope);

        /* location */
        const char *lscope = extract_sub_obj(json, "location");
        if (!lscope) lscope = extract_sub_obj(json, "env");
        if (lscope) {
            json_get_str(lscope, "area",     g_env.location.area, 32);
            json_get_str(lscope, "district", g_env.location.district, 32);
            json_get_str(lscope, "spot",     g_env.location.spot, 32);
        }
        /* environment */
        const char *escope = extract_sub_obj(json, "environment");
        if (!escope) escope = extract_sub_obj(json, "env");
        if (escope) {
            json_get_str(escope, "era", g_env.era, 64);
            json_get_int(escope, "weatherCode", (int*)&g_env.weather);
            const char *tscope = extract_sub_obj(escope, "time");
            json_get_int(tscope?tscope:escope, "year",    &g_env.time.year);
            json_get_int(tscope?tscope:escope, "month",   &g_env.time.month);
            json_get_int(tscope?tscope:escope, "day",     &g_env.time.day);
            json_get_int(tscope?tscope:escope, "hour",    &g_env.time.hour);
            json_get_int(tscope?tscope:escope, "minute",  &g_env.time.minute);
            json_get_int(tscope?tscope:escope, "weekday", (int*)&g_env.time.weekday);
        }

        /* Map points */
        {
            const char *mk = strstr(json, "\"mapPoints\":");
            if (mk) {
                mk = strchr(mk, ':');
                if (mk) { mk++; while (*mk == ' ') mk++; }
            }
            if (mk) map_import_json(&g_map, mk);
        }

        g_npc_card_count = json_parse_array(json, "npcs", parse_npc_obj, NULL);
        if (g_npc_card_count > MAX_NPC_CARDS) g_npc_card_count = MAX_NPC_CARDS;

        mem_init(&g_player.memory);
        for (int i = 0; i < g_npc_card_count; i++)
            mem_init(&g_npc_cards[i].memory);

        npc_init(&g_npc_mgr);
        npc_parse_cache_from_json(&g_npc_mgr, json);
        {
            char current_loc[MAX_NPC_LOC];
            npc_make_location_key(&g_env.location, current_loc, sizeof(current_loc));
            npc_restore_temps(&g_npc_mgr, current_loc);
        }
        free(json);

        map_locate(&g_map, g_env.location.area,
                   g_env.location.district, g_env.location.spot);
        g_world_ready = true;

        /* Stage 3: initialize NPC brains */
        npc_brains_init();

        /* Sync to WorldState before auto-migration save */
        ws_sync_from_globals();

        /* Auto-migrate to new folder format */
        save_to_file(name);

        log_info("load: 成功(old→migrated) — %s", g_player.name);
        return true;
    }
}

/* ═══════════════════════════════════════════════════════════════
   Game logic (called while holding g_state_lock)
   ═══════════════════════════════════════════════════════════════ */

/* Check if name appears in a comma-separated list */
static bool name_in_list(const char *name, const char *list)
{
    if (!name || !list || !*list) return false;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        const char *end = strchr(p, ',');
        int len = end ? (int)(end - p) : (int)strlen(p);
        if (len > 0 && (int)strlen(name) == len && strncmp(name, p, len) == 0)
            return true;
        p = end ? end + 1 : p + strlen(p);
    }
    return false;
}

static bool npc_is_present(const char *name)
{
    for (int i = 0; i < g_npc_mgr.spawn_count; i++)
        if (g_npc_mgr.spawns[i].present &&
            strcmp(g_npc_mgr.spawns[i].name, name) == 0)
            return true;
    for (int i = 0; i < g_npc_mgr.temp_count; i++)
        if (strcmp(g_npc_mgr.temps[i].name, name) == 0)
            return true;
    return false;
}

/* Output a single NPC in compact one-line form */
static int npc_compact_line(const CharacterCard *c, char *out, int out_size)
{
    const char *st = "正常";
    switch (c->status) {
        case STATUS_NORMAL:  st="正常"; break;
        case STATUS_HUNGRY:  st="饥饿"; break;
        case STATUS_TIRED:   st="疲惫"; break;
        case STATUS_SICK:    st="生病"; break;
        case STATUS_INJURED: st="受伤"; break;
        case STATUS_EXCITED: st="兴奋"; break;
        case STATUS_ANGRY:   st="愤怒"; break;
        case STATUS_SAD:     st="悲伤"; break;
        case STATUS_HAPPY:   st="开心"; break;
    }
    return snprintf(out, out_size,
        "%s | 年龄:%d | %s%s%s | 状态:%s | 好感%+d\n",
        c->name, c->age,
        c->personality[0] ? c->personality : "?",
        c->home[0] ? " 家:" : "",
        c->home[0] ? c->home : "",
        st, c->player_affinity);
}

/* Output a single NPC with full details (skills, items, relations) */
static int npc_full_entry(const CharacterCard *c, char *out, int out_size)
{
    int p = 0;
    const char *st = "正常";
    switch (c->status) {
        case STATUS_NORMAL:  st="正常"; break;
        case STATUS_HUNGRY:  st="饥饿"; break;
        case STATUS_TIRED:   st="疲惫"; break;
        case STATUS_SICK:    st="生病"; break;
        case STATUS_INJURED: st="受伤"; break;
        case STATUS_EXCITED: st="兴奋"; break;
        case STATUS_ANGRY:   st="愤怒"; break;
        case STATUS_SAD:     st="悲伤"; break;
        case STATUS_HAPPY:   st="开心"; break;
    }
    p += snprintf(out + p, out_size - p,
        "%s | 年龄:%d | 性格:%s | 状态:%s | 金钱:%dG",
        c->name, c->age, c->personality, st, c->money);
    if (c->home[0]) p += snprintf(out + p, out_size - p, " | 住所:%s", c->home);
    p += snprintf(out + p, out_size - p, "\n");
    if (c->skill_count > 0) {
        p += snprintf(out + p, out_size - p, "  技能: ");
        for (int k = 0; k < c->skill_count; k++)
            p += snprintf(out + p, out_size - p, "%s(Lv.%d) ",
                c->skills[k].name, c->skills[k].level);
        p += snprintf(out + p, out_size - p, "\n");
    }
    if (c->item_count > 0) {
        p += snprintf(out + p, out_size - p, "  持有: ");
        for (int k = 0; k < c->item_count; k++)
            p += snprintf(out + p, out_size - p, "%s×%d ",
                c->items[k].name, c->items[k].quantity);
        p += snprintf(out + p, out_size - p, "\n");
    }
    p += snprintf(out + p, out_size - p, "  与玩家好感度: %+d\n", c->player_affinity);
    if (c->relation_count > 0) {
        p += snprintf(out + p, out_size - p, "  社会关系: ");
        for (int k = 0; k < c->relation_count; k++)
            p += snprintf(out + p, out_size - p, "%s(%s,好感%+d) ",
                c->relations[k].target,
                relation_type_str(c->relations[k].type),
                c->relations[k].affinity);
        p += snprintf(out + p, out_size - p, "\n");
    }
    return p;
}

static int next_save_number(void)
{
    int max = 0;
    WIN32_FIND_DATAW fd;
    /* Match both old .dat files and new folder saves */
    wchar_t wpattern[576];
    MultiByteToWideChar(CP_UTF8, 0, g_saves_root, -1, wpattern, 576);
    wcscat(wpattern, L"\\save_*");
    HANDLE h = FindFirstFileW(wpattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;
    do {
        int n = 0;
        const wchar_t *p = fd.cFileName + 5;
        while (*p >= L'0' && *p <= L'9') { n = n * 10 + (*p - L'0'); p++; }
        if (n > max) max = n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return max + 1;
}

static void build_var_catalog(char *out, int out_size)
{
    int p = 0;
    p += snprintf(out + p, out_size - p,
        "【玩家属性变量】\n"
        "name(姓名) age(年龄) gender(性别) clothing(衣着) status(状态) money(金钱)\n"
        "attr.appearance(颜值) attr.constitution(体质) attr.intelligence(智力)\n");
    if (g_player.skill_count > 0) {
        p += snprintf(out + p, out_size - p, "技能变量: ");
        for (int i = 0; i < g_player.skill_count; i++)
            p += snprintf(out + p, out_size - p, "%s(等级%d), ",
                g_player.skills[i].name, g_player.skills[i].level);
        p += snprintf(out + p, out_size - p, "\n");
    }
    if (g_player.item_count > 0) {
        p += snprintf(out + p, out_size - p, "物品变量: ");
        for (int i = 0; i < g_player.item_count; i++)
            p += snprintf(out + p, out_size - p, "%s(数量%d), ",
                g_player.items[i].name, g_player.items[i].quantity);
        p += snprintf(out + p, out_size - p, "\n");
    }
    if (g_player.relation_count > 0) {
        p += snprintf(out + p, out_size - p, "关系变量: ");
        for (int i = 0; i < g_player.relation_count; i++)
            p += snprintf(out + p, out_size - p, "%s.affinity(%s好感%d), ",
                g_player.relations[i].target,
                relation_type_str(g_player.relations[i].type),
                g_player.relations[i].affinity);
        p += snprintf(out + p, out_size - p, "\n");
    }
    if (g_npc_card_count > 0) {
        p += snprintf(out + p, out_size - p, "\n【NPC好感度变量】\n");
        for (int i = 0; i < g_npc_card_count; i++)
            p += snprintf(out + p, out_size - p, "%s.player_affinity(好感度%d), ",
                g_npc_cards[i].name, g_npc_cards[i].player_affinity);
        p += snprintf(out + p, out_size - p, "\n");
    }
    p += snprintf(out + p, out_size - p,
        "\n【环境变量】\n"
        "weather(天气) time(时间) location(地点) era(时代)\n");

    /* World variables from WorldState */
    if (g_ws.variable_count > 0) {
        p += snprintf(out + p, out_size - p, "\n【世界变量】\n");
        for (int i = 0; i < g_ws.variable_count; i++) {
            const char *type_str = "int";
            switch (g_ws.variables[i].type) {
            case VAR_INT:    type_str = "int";    break;
            case VAR_FLOAT:  type_str = "float";  break;
            case VAR_BOOL:   type_str = "bool";   break;
            case VAR_STRING: type_str = "string"; break;
            case VAR_ENUM:   type_str = "enum";   break;
            }
            p += snprintf(out + p, out_size - p, "%s(%s) ",
                g_ws.variables[i].name, type_str);
        }
        p += snprintf(out + p, out_size - p, "\n");
    }

    out[p] = '\0';
}

static void extract_var_values(const char *var_names, char *out, int out_size)
{
    if (!var_names || !*var_names) { out[0] = '\0'; return; }
    char names[1024];
    strncpy(names, var_names, sizeof(names) - 1);
    names[sizeof(names) - 1] = '\0';

    char full[16384];
    int fp = 0;
    fp += cc_export_state(&g_player, full + fp, sizeof(full) - fp, true);
    fp += env_export_state(&g_env, full + fp, sizeof(full) - fp);
    full[fp] = '\0';

    int pos = 0;
    char *ctx = NULL;
    char *token = strtok_s(names, ",", &ctx);
    while (token) {
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';
        if (*token == '\0') { token = strtok_s(NULL, ",", &ctx); continue; }
        char *line = strstr(full, token);
        if (line) {
            char *nl = strchr(line, '\n');
            int len = nl ? (int)(nl - line) : (int)strlen(line);
            if (pos + len + 2 < out_size) {
                if (pos > 0) out[pos++] = '\n';
                memcpy(out + pos, line, len);
                pos += len;
                out[pos] = '\0';
            }
        }
        token = strtok_s(NULL, ",", &ctx);
    }
    out[pos] = '\0';
}

static void cc_free_internals(CharacterCard *cc)
{
    mem_free(&cc->memory);
    /* Stage 3: free NPC brain if allocated */
    if (cc->brain) {
        free(cc->brain);
        cc->brain = NULL;
    }
    (void)cc;
}

/* Return index of NPC with oldest last_interaction (year>month>day>hour>min) */
static int find_oldest_npc(void)
{
    int oldest = 0;
    for (int k = 1; k < g_npc_card_count; k++) {
        const GameTime *a = &g_npc_cards[k].last_interaction;
        const GameTime *b = &g_npc_cards[oldest].last_interaction;
        if (a->year < b->year ||
            (a->year == b->year && a->month < b->month) ||
            (a->year == b->year && a->month == b->month && a->day < b->day) ||
            (a->year == b->year && a->month == b->month && a->day == b->day && a->hour < b->hour) ||
            (a->year == b->year && a->month == b->month && a->day == b->day && a->hour == b->hour && a->minute < b->minute)) {
            oldest = k;
        }
    }
    return oldest;
}

static void apply_response(FullResponse *resp)
{
    log_info("apply_response: ENTERED, changes='%.100s'", resp->changes ? resp->changes : "(null)");
    log_info("apply_response: action_proposal='%.100s'", resp->action_proposal ? resp->action_proposal : "(null)");
    log_info("apply_response: npc_spawn='%.100s'", resp->npc_spawn ? resp->npc_spawn : "(null)");
    log_info("apply_response: npc_create='%.100s'", resp->npc_create ? resp->npc_create : "(null)");

    /* ── Defensive: ensure all string fields are null-terminated and
       their apparent length does not exceed their buffer size ── */
    #define CLAMP_STRLEN(field, max_sz) do { \
        size_t _n = strlen(field); \
        if (_n >= (max_sz)) { \
            log_warn("apply_response: field " #field " overflow (%zu >= %zu), clamping", \
                     _n, (size_t)(max_sz)); \
            (field)[(max_sz) - 1] = '\0'; \
        } \
    } while(0)
    CLAMP_STRLEN(resp->text,             sizeof(resp->text));
    CLAMP_STRLEN(resp->changes,          sizeof(resp->changes));
    CLAMP_STRLEN(resp->location,         sizeof(resp->location));
    CLAMP_STRLEN(resp->mem_short,        sizeof(resp->mem_short));
    CLAMP_STRLEN(resp->mem_long,         sizeof(resp->mem_long));
    CLAMP_STRLEN(resp->mem_permanent,    sizeof(resp->mem_permanent));
    CLAMP_STRLEN(resp->mem_chars,        sizeof(resp->mem_chars));
    CLAMP_STRLEN(resp->mem_summary,      sizeof(resp->mem_summary));
    CLAMP_STRLEN(resp->npc_temp,         sizeof(resp->npc_temp));
    CLAMP_STRLEN(resp->npc_spawn,        sizeof(resp->npc_spawn));
    CLAMP_STRLEN(resp->npc_create,       sizeof(resp->npc_create));
    CLAMP_STRLEN(resp->action_proposal,  sizeof(resp->action_proposal));
    #undef CLAMP_STRLEN

    /* Capture old location BEFORE any changes */
    char old_loc[MAX_NPC_LOC];
    npc_make_location_key(&g_env.location, old_loc, sizeof(old_loc));

    /* Record action event */
    event_push(&g_events, g_ws.tick, 0, -1, EVENT_ACTION,
               "{\"changes\":\"%.200s\"}", resp->changes[0] ? resp->changes : "(none)");

    /* ── USE Phase 1: Route changes through Rule Engine ──
       Bug #8 fix: merge legacy CHANGES text and structured ActionProposal
       into a single ActionProposal, process through Rule Engine once,
       and apply the resulting ChangeSet atomically. This prevents
       double-application when AI provides both CHANGES and ACTION_PROPOSAL. */
    {
        ActionProposal ap;
        ap_init(&ap);

        /* Step A: Parse legacy CHANGES text into ActionProposal */
        if (resp->changes[0]) {
            log_info("apply_response: parsing CHANGES text...");
            int ch_parsed = ap_parse_changes_text(resp->changes, &ap);
            if (ch_parsed > 0) {
                log_info("apply_response: CHANGES -> %d actions", ch_parsed);
            } else {
                log_info("apply_response: CHANGES parsed 0 actions");
            }
        }

        /* Step B: Parse structured ActionProposal JSON */
        int ap_count_before_json = ap.count;
        if (resp->action_proposal[0]) {
            const char *p = resp->action_proposal;
            while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
            if (*p == '[') p++;
            while (*p && ap.count < AP_MAX_ACTIONS) {
                p = strchr(p, '{');
                if (!p) break;
                {
                    const char *bracket = strchr(p + 1, ']');
                    const char *close = strchr(p, '}');
                    if (bracket && close && bracket < close) {
                        p = bracket + 1;
                        continue;
                    }
                }
                const char *close = strchr(p, '}');
                if (!close) break;

                char entity[64] = {0}, field[64] = {0}, type_str[32] = {0};
                json_get_str(p, "entity", entity, sizeof(entity));
                json_get_str(p, "field", field, sizeof(field));
                json_get_str(p, "type", type_str, sizeof(type_str));

                if (entity[0]) {
                    if (strcmp(type_str, "change") == 0 || strcmp(type_str, "delta") == 0) {
                        int delta = 0;
                        json_get_int(p, "delta", &delta);
                        ap_add_int_change(&ap, entity, field, delta);
                    } else if (strcmp(type_str, "set") == 0) {
                        int value = 0;
                        json_get_int(p, "value", &value);
                        ap_add_int_set(&ap, entity, field, value);
                    } else if (strcmp(type_str, "status") == 0) {
                        int value = 0;
                        json_get_int(p, "value", &value);
                        ap_add_status_set(&ap, entity, value);
                    } else if (strcmp(type_str, "str") == 0) {
                        char str_val[128] = {0};
                        json_get_str(p, "value", str_val, sizeof(str_val));
                        ap_add_str_set(&ap, entity, field, str_val);
                    } else {
                        int delta = 0, value = 0;
                        if (json_get_int(p, "delta", &delta)) {
                            ap_add_int_change(&ap, entity, field[0] ? field : "money", delta);
                        } else if (json_get_int(p, "value", &value)) {
                            ap_add_int_set(&ap, entity, field[0] ? field : "money", value);
                        }
                    }
                }
                p = close + 1;
            }
            log_info("apply_response: parsed %d actions from ACTION_PROPOSAL JSON",
                     ap.count - ap_count_before_json);
        }

        /* Step C: Process combined ActionProposal through Rule Engine ONCE */
        if (ap.count > 0) {
            log_info("apply_response: %d total actions, processing via Rule Engine...", ap.count);
            ChangeSetFull cs;
            int rules_fired = re_process_proposal(&g_rule_engine, &ap,
                &g_player, g_npc_cards, g_npc_card_count, &g_ws,
                &cs, &g_events, g_ws.tick);
            log_info("apply_response: re_process_proposal returned %d rules_fired, cs.count=%d",
                     rules_fired, cs.count);
            cs_apply(&cs, &g_player, g_npc_cards, g_npc_card_count,
                     &g_events, g_ws.tick);
            log_info("apply_response: cs_apply completed");
            log_info("apply_response: ChangeSet applied (%d entries, %d rules fired)",
                     cs.count, rules_fired);
        } else {
            log_info("apply_response: no actions to apply");
        }
    }

    /* ── Rule Engine post-apply evaluation: evaluate all rules against
           the player after changes have been applied, firing any
           post-condition rules (e.g., money<0 → status change) ── */
    {
        int hit_ids[RE_MAX_RULES];
        int n_hits = re_evaluate_all(&g_rule_engine, &g_player, &g_ws,
                                     hit_ids, RE_MAX_RULES);
        for (int i = 0; i < n_hits; i++) {
            for (int j = 0; j < g_rule_engine.count; j++) {
                if (g_rule_engine.rules[j].id == hit_ids[i]) {
                    re_apply_effects(&g_rule_engine.rules[j], &g_player,
                                    &g_events, g_ws.tick);
                    break;
                }
            }
        }
        /* Also evaluate against all NPCs */
        for (int n = 0; n < g_npc_card_count; n++) {
            int npc_hits = re_evaluate_all(&g_rule_engine, &g_npc_cards[n],
                                           &g_ws, hit_ids, RE_MAX_RULES);
            for (int i = 0; i < npc_hits; i++) {
                for (int j = 0; j < g_rule_engine.count; j++) {
                    if (g_rule_engine.rules[j].id == hit_ids[i]) {
                        re_apply_effects(&g_rule_engine.rules[j], &g_npc_cards[n],
                                        &g_events, g_ws.tick);
                        break;
                    }
                }
            }
        }
    }

    if (resp->time_advance > 0) {
        log_info("apply_response: time +%d min", resp->time_advance);
        env_advance_time(&g_env, resp->time_advance);
    }
    if (resp->weather >= 0 && resp->weather <= 10) {
        log_info("apply_response: weather -> %d", resp->weather);
        env_set_weather_int(&g_env, resp->weather);
    }
    if (resp->location[0]) {
        log_info("apply_response: location -> %s", resp->location);
        env_parse_location(&g_env, resp->location);
        map_ensure_location(&g_map, g_env.location.area,
            g_env.location.district, g_env.location.spot);
    }
    if (resp->mem_summary[0])
        mem_add(&g_player.memory, resp->mem_summary, resp->mem_chars, &g_env.time, MEM_LONG_TERM);
    if (resp->mem_short[0]) {
        log_info("apply_response: mem_short <- %.60s", resp->mem_short);
        mem_add(&g_player.memory, resp->mem_short, resp->mem_chars, &g_env.time, MEM_SHORT_TERM);
    }
    if (resp->mem_long[0]) {
        log_info("apply_response: mem_long <- %.60s", resp->mem_long);
        mem_add(&g_player.memory, resp->mem_long, resp->mem_chars, &g_env.time, MEM_LONG_TERM);
    }
    if (resp->mem_permanent[0]) {
        log_info("apply_response: mem_permanent <- %.60s", resp->mem_permanent);
        mem_add(&g_player.memory, resp->mem_permanent, resp->mem_chars, &g_env.time, MEM_PERMANENT);
    }
    mem_cleanup(&g_player.memory, &g_env.time);

    /* Bug #10 fix: only cache temps at old location if it actually differs from
       the current location. When location didn't change, caching+clearing is
       wasteful; we only clear if the AI explicitly gives new NPC_TEMP data. */
    char cur_loc[MAX_NPC_LOC];
    npc_make_location_key(&g_env.location, cur_loc, sizeof(cur_loc));

    if (strcmp(old_loc, cur_loc) != 0) {
        /* Location changed: save old location's temps, clear for new location */
        log_info("apply_response: location changed, caching old temps");
        npc_cache_temps(&g_npc_mgr, old_loc);
        npc_clear_temps(&g_npc_mgr);
    }

    if (resp->npc_temp[0]) {
        /* AI provided fresh temp NPCs — parse and use them */
        /* Bug #10: when location didn't change, clear existing temps first */
        if (strcmp(old_loc, cur_loc) == 0)
            npc_clear_temps(&g_npc_mgr);
        char buf[2048]; strncpy(buf, resp->npc_temp, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
        char *npctx = NULL;
        char *line = strtok_s(buf, "\n", &npctx);
        while (line) {
            while (*line==' '||*line=='\r') line++;
            if (*line) {
                TempNpc tp;
                if (npc_parse_temp_line(line, &tp)) {
                    snprintf(tp.location, sizeof(tp.location), "%s/%s/%s",
                        g_env.location.area, g_env.location.district, g_env.location.spot);
                    npc_add_temp(&g_npc_mgr, tp.name, tp.description, tp.location);
                }
            }
            line = strtok_s(NULL, "\n", &npctx);
        }
    } else {
        /* AI provided no temp NPCs — restore from cache for current location */
        npc_restore_temps(&g_npc_mgr, cur_loc);
    }
    if (resp->npc_spawn[0]) {
        log_info("apply_response: processing NPC_SPAWN...");
        NpcSpawn list[32]; int count=0;
        char buf[2048]; strncpy(buf, resp->npc_spawn, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
        char *spctx = NULL;
        char *line = strtok_s(buf, "\n", &spctx);
        while (line && count<32) {
            while (*line==' '||*line=='\r') line++;
            if (*line) { NpcSpawn sp; if (npc_parse_spawn_line(line,&sp)) list[count++]=sp; }
            line = strtok_s(NULL, "\n", &spctx);
        }
        npc_set_spawns(&g_npc_mgr, list, count);
        log_info("apply_response: npc_set_spawns done, spawn_count=%d", g_npc_mgr.spawn_count);
        for (int i=0; i<g_npc_mgr.spawn_count; i++)
            for (int j=0; j<g_npc_card_count; j++)
                if (strcmp(g_npc_cards[j].name, g_npc_mgr.spawns[i].name)==0)
                    g_npc_mgr.spawns[i].player_affinity = g_npc_cards[j].player_affinity;
        npc_resolve_spawns(&g_npc_mgr);
        int present_count = 0;
        for (int i=0; i<g_npc_mgr.spawn_count; i++)
            if (g_npc_mgr.spawns[i].present) {
                present_count++;
                for (int j=0; j<g_npc_card_count; j++)
                    if (strcmp(g_npc_cards[j].name, g_npc_mgr.spawns[i].name)==0)
                        cc_touch_interaction(&g_npc_cards[j], &g_env.time);
            }
        log_info("apply_response: NPC_SPAWN -> %d spawns, %d present", count, present_count);
    }
    if (resp->npc_create[0]) {
        CharacterCard nc;
        if (npc_parse_create(resp->npc_create, &nc) && npc_card_valid(&nc)) {
            cc_touch_interaction(&nc, &g_env.time);
            if (g_npc_card_count >= MAX_NPC_CARDS) {
                int oldest = find_oldest_npc();
                log_info("NPC满员，移除最久未互动: %s", g_npc_cards[oldest].name);
                cc_free_internals(&g_npc_cards[oldest]);
                g_npc_cards[oldest] = nc;
            } else {
                g_npc_cards[g_npc_card_count++] = nc;
            }
            log_info("新NPC: %s", nc.name);
            log_info("apply_response: npc_parse_create done, npc_count=%d", g_npc_card_count);
        }
    }
    for (int i=g_npc_card_count-1; i>=0; i--) {
        if (cc_is_stale_npc(&g_npc_cards[i], &g_env.time)) {
            log_info("NPC过期: %s", g_npc_cards[i].name);
            cc_free_internals(&g_npc_cards[i]);
            for (int j=i; j<g_npc_card_count-1; j++) g_npc_cards[j]=g_npc_cards[j+1];
            g_npc_card_count--;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════════════════════ */

bool backend_init(void)
{
    InitializeCriticalSection(&g_state_lock);

    /* Build absolute saves root: <exe_dir>\saves */
    {
        wchar_t edir[512];
        GetModuleFileNameW(NULL, edir, 512);
        wchar_t *slash = wcsrchr(edir, L'\\');
        if (slash) *slash = L'\0';
        /* Convert to UTF-8 and append \saves */
        WideCharToMultiByte(CP_UTF8, 0, edir, -1,
                            g_saves_root, (int)sizeof(g_saves_root), NULL, NULL);
        int len = (int)strlen(g_saves_root);
        snprintf(g_saves_root + len, sizeof(g_saves_root) - len, "\\saves");
        /* Create saves directory */
        wchar_t wsaves[576];
        MultiByteToWideChar(CP_UTF8, 0, g_saves_root, -1, wsaves, 576);
        CreateDirectoryW(wsaves, NULL);
    }

    log_init(g_saves_root);
    log_info("--- 模拟器启动 ---");
    log_info("saves root: %s", g_saves_root);

    cc_init(&g_player, ENTITY_PLAYER);
    env_init(&g_env);
    map_init(&g_map);
    npc_init(&g_npc_mgr);

    /* USE: initialise canonical WorldState and EventLog */
    ws_init(&g_ws);
    event_init(&g_events);
    event_push(&g_events, 0, -1, -1, EVENT_SYSTEM,
               "{\"msg\":\"simulator started\"}");

    /* USE Phase 1: initialise Rule Engine and register built-in rules */
    re_init(&g_rule_engine);
    re_register_builtins(&g_rule_engine);
    log_info("Rule Engine: %d rules registered", g_rule_engine.count);

    char ep[256]="https://api.openai.com/v1", key[256]="", md[64]="gpt-4o";
    ApiProfile profiles[MAX_API_PROFILES];
    int pc = crypto_load_profiles(profiles, MAX_API_PROFILES);
    if (pc > 0) {
        safe_strcpy(ep,  profiles[0].endpoint, sizeof(ep));
        safe_strcpy(key, profiles[0].api_key,  sizeof(key));
        safe_strcpy(md,  profiles[0].model,    sizeof(md));
        log_info("加载了 %d 个API配置", pc);
    } else {
        const char *e,*k,*m;
        if ((e=getenv("SIM_API_ENDPOINT"))) safe_strcpy(ep,e,sizeof(ep));
        if ((k=getenv("SIM_API_KEY")))      safe_strcpy(key,k,sizeof(key));
        if ((m=getenv("SIM_API_MODEL")))    safe_strcpy(md,m,sizeof(md));
    }
    api_init(&g_api, ep, key, md);
    g_api_ready = (key[0]!='\0');
    log_info("API: %s, 模型: %s", g_api_ready?"就绪":"未配置", md);

    load_from_file("autosave");
    /* Note: npc_brains_init() is called inside load_from_file()
       after g_world_ready is set — no need to call it again here. */
    log_info("初始化完成 worldReady=%d", g_world_ready);
    return true;
}

void backend_shutdown(void)
{
    log_info("--- 模拟器关闭 ---");
    npc_brains_free();
    ws_sync_from_globals();
    event_push(&g_events, g_ws.tick, -1, -1, EVENT_SYSTEM,
               "{\"msg\":\"simulator shutdown\"}");
    log_close();
    DeleteCriticalSection(&g_state_lock);
}

bool backend_has_api(void)     { return g_api_ready; }
bool backend_world_ready(void) { return g_world_ready; }

void backend_set_api(const char *ep, const char *key, const char *md)
{
    EnterCriticalSection(&g_state_lock);
    api_init(&g_api, ep, key, md);
    g_api_ready = (key && key[0]!='\0');
    log_info("API更新");
    LeaveCriticalSection(&g_state_lock);
}

char *backend_get_api_status(void)
{
    EnterCriticalSection(&g_state_lock);
    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j, "ok", 1);
    jb_kv_bool(&j, "apiReady", g_api_ready);
    jb_kv_str(&j, "model", g_api_ready ? g_api.model : "");
    jb_obj_close(&j);
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_get_profiles(void)
{
    EnterCriticalSection(&g_state_lock);
    ApiProfile pro[MAX_API_PROFILES];
    int n = crypto_load_profiles(pro, MAX_API_PROFILES);
    JsonBuf j; jb_init(&j); jb_arr_open(&j);
    for (int i=0; i<n; i++) {
        if (i) jb_str(&j, ",");
        jb_obj_open(&j);
        jb_kv_str(&j, "name", pro[i].name);
        jb_kv_str(&j, "endpoint", pro[i].endpoint);
        jb_kv_str(&j, "model", pro[i].model);
        jb_kv_bool(&j, "hasKey", pro[i].api_key[0]!='\0');
        jb_obj_close(&j);
    }
    jb_arr_close(&j);
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_save_profile(const char *name, const char *ep,
                           const char *key, const char *md)
{
    EnterCriticalSection(&g_state_lock);
    ApiProfile pro[MAX_API_PROFILES];
    int n = crypto_load_profiles(pro, MAX_API_PROFILES);
    int idx = -1;
    for (int i=0; i<n; i++) if (strcmp(pro[i].name,name)==0) { idx=i; break; }
    if (idx<0) {
        if (n>=MAX_API_PROFILES) { LeaveCriticalSection(&g_state_lock); return err_json("上限"); }
        idx = n++;
    }
    safe_strcpy(pro[idx].name, name, MAX_PROFILE_NAME);
    safe_strcpy(pro[idx].endpoint, ep, sizeof(pro[idx].endpoint));
    safe_strcpy(pro[idx].api_key, key, sizeof(pro[idx].api_key));
    safe_strcpy(pro[idx].model, md, sizeof(pro[idx].model));
    char *r;
    if (crypto_save_profiles(pro, n)) {
        api_init(&g_api, ep, key, md); g_api_ready=(key[0]!='\0');
        log_info("API保存: %s", name);
        r = ok_json(NULL, NULL);
    } else { log_error("API保存失败"); r = err_json("保存失败"); }
    LeaveCriticalSection(&g_state_lock);
    return r;
}

char *backend_delete_profile(const char *name)
{
    EnterCriticalSection(&g_state_lock);
    ApiProfile pro[MAX_API_PROFILES];
    int n = crypto_load_profiles(pro, MAX_API_PROFILES);
    int idx = -1;
    for (int i=0; i<n; i++) if (strcmp(pro[i].name,name)==0) { idx=i; break; }
    if (idx<0) { LeaveCriticalSection(&g_state_lock); return err_json("不存在"); }
    for (int i=idx; i<n-1; i++) pro[i]=pro[i+1];
    n--;
    crypto_save_profiles(pro, n);
    log_info("API删除: %s", name);
    LeaveCriticalSection(&g_state_lock);
    return ok_json(NULL, NULL);
}

char *backend_activate_profile(const char *name)
{
    EnterCriticalSection(&g_state_lock);
    ApiProfile pro[MAX_API_PROFILES];
    int n = crypto_load_profiles(pro, MAX_API_PROFILES);
    int idx = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(pro[i].name, name) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        LeaveCriticalSection(&g_state_lock);
        return err_json("配置不存在");
    }
    api_init(&g_api, pro[idx].endpoint, pro[idx].api_key, pro[idx].model);
    g_api_ready = (pro[idx].api_key[0] != '\0');
    log_info("API激活: %s (模型: %s)", name, pro[idx].model);
    LeaveCriticalSection(&g_state_lock);
    return ok_json(NULL, NULL);
}

char *backend_send_message(const char *text)
{
    EnterCriticalSection(&g_state_lock);
    if (!g_api_ready)  { LeaveCriticalSection(&g_state_lock); return err_json("API 未配置"); }
    if (!g_world_ready) { LeaveCriticalSection(&g_state_lock); return err_json("未创建世界"); }

    log_info("========================================");
    log_info("SEND_MSG: player=%.30s text=%.60s", g_player.name, text);
    log_info("SEND_MSG: Pipeline START");

    /* ── Token-saving: detect same-location continuation ──
       If the last narrative was generated at the current location,
       prepend "(续前场景)" to user input.  The system prompt instructs
       the AI to skip environment re-description when it sees this flag.
       This saves ~300 tokens per request vs. injecting 200 chars of
       previous narrative text. */
    char final_text[1024];
    {
        char cur_loc[256];
        snprintf(cur_loc, sizeof(cur_loc), "%s/%s/%s",
                 g_env.location.area, g_env.location.district, g_env.location.spot);
        WorldVariable *last_loc = ws_find_variable(&g_ws, "last_narrative_location");
        if (last_loc && last_loc->type == VAR_STRING &&
            last_loc->str_val[0] &&
            strcmp(last_loc->str_val, cur_loc) == 0) {
            snprintf(final_text, sizeof(final_text), "(续前场景) %s", text);
            log_info("SEND_MSG: same location, prepended continuation flag");
        } else {
            safe_strcpy(final_text, text, sizeof(final_text));
        }
    }

    /* ═══════════════════════════════════════════════════════════
       Stage 3: NPC Brain ticks
       Process NPC autonomous behavior before player's action.
       NPCs may change state (eat, rest, work, etc.) which is
       then visible in the game state sent to the AI.
       ═══════════════════════════════════════════════════════════ */
    log_info("SEND_MSG: [Stage 1/6] NPC Brain tick...");
    ws_sync_from_globals();  /* ensure tick is up to date */
    char *npc_actions_json = npc_brains_tick_all();
    if (npc_actions_json) {
        log_info("SEND_MSG: [Stage 1/6] NPCs acted: %s", npc_actions_json);
    } else {
        log_info("SEND_MSG: [Stage 1/6] No NPCs acted this tick");
    }

    /* ═══════════════════════════════════════════════════════════
       Stage 5: World Director — background simulation tick
       Runs weather, economy, faction, and population sims
       at their scheduled intervals.
       ═══════════════════════════════════════════════════════════ */
    log_info("SEND_MSG: [Stage 2/6] World Director tick...");
    {
        int wd_ran = wd_tick(&g_ws, &g_events, g_ws.tick);
        if (wd_ran > 0) {
            log_info("SEND_MSG: [Stage 2/6] World Director: %d subsystem(s) ticked", wd_ran);
        }
    }

    /* ═══════════════════════════════════════════════════════════
       USE Phase 2: New 6-stage pipeline
       Intent → Plan → ActionProposal → RuleEngine → ChangeSet → Narrative
       ═══════════════════════════════════════════════════════════ */

    /* ── Build debug trace accumulator ── */
    JsonBuf debug_trace; jb_init(&debug_trace); jb_obj_open(&debug_trace);
    jb_kv_str(&debug_trace, "userInput", text);

    /* Include NPC action summaries in debug trace */
    if (npc_actions_json) {
        jb_str(&debug_trace, ",\"npcActions\":");
        jb_str(&debug_trace, npc_actions_json);
        free(npc_actions_json);
    }

    /* ── Build known names list for intent target extraction ── */
    char known_names[2048] = "";
    {
        int kn = 0;
        /* NPC names */
        for (int i = 0; i < g_npc_card_count && kn < (int)sizeof(known_names) - 100; i++) {
            if (i > 0) kn += snprintf(known_names + kn, sizeof(known_names) - kn, ",");
            kn += snprintf(known_names + kn, sizeof(known_names) - kn, "%s",
                g_npc_cards[i].name);
        }
        /* Location names */
        for (int i = 0; i < g_map.levels[2].count && kn < (int)sizeof(known_names) - 100; i++) {
            kn += snprintf(known_names + kn, sizeof(known_names) - kn, ",");
            kn += snprintf(known_names + kn, sizeof(known_names) - kn, "%s",
                g_map.levels[2].points[i].name);
        }
    }

    /* ── Step 1: Intent Recognition (three-level cascade) ── */
    log_info("SEND_MSG: [Stage 3/6] Intent recognition...");
    char brief_context[1024];
    snprintf(brief_context, sizeof(brief_context),
        "地点: %s/%s/%s 时间: %d-%02d-%02d %02d:%02d 天气: %s",
        g_env.location.area, g_env.location.district, g_env.location.spot,
        g_env.time.year, g_env.time.month, g_env.time.day,
        g_env.time.hour, g_env.time.minute,
        weather_str(g_env.weather));

    IntentResult intent = intent_recognize(&g_api, text, known_names, brief_context);
    log_info("SEND_MSG: [Stage 3/6] Intent=%s conf=%.2f level=%d target=%.30s",
        intent_type_str(intent.type), intent.confidence, intent.level,
        intent.target[0] ? intent.target : "(none)");

    /* Debug: intent */
    {
        jb_str(&debug_trace, ",\"intent\":{");
        jb_kv_str(&debug_trace, "type", intent_type_str(intent.type));
        if (intent.target[0]) jb_kv_str(&debug_trace, "target", intent.target);
        jb_kv_dbl(&debug_trace, "confidence", (double)intent.confidence);
        jb_kv_int(&debug_trace, "level", intent.level);
        jb_str(&debug_trace, "}");
    }

    /* ── Step 2: Variable + character selection (existing, informed by intent) ── */
    log_info("SEND_MSG: [Stage 4/6] Variable selection...");
    char catalog[8192]; build_var_catalog(catalog, sizeof(catalog));
    VarSelectResult sel;
    if (!api_select_vars(&g_api, text, catalog, &sel)) {
        const char *e = api_last_error(&g_api);
        log_error("SEND_MSG: [Stage 4/6] Variable selection FAILED: %s", e);
        free(jb_detach(&debug_trace));
        LeaveCriticalSection(&g_state_lock); return err_json(e);
    }
    log_info("SEND_MSG: [Stage 4/6] Selected vars=%.120s chars=%.120s",
             sel.var_names, sel.char_names);
    char var_values[8192]; extract_var_values(sel.var_names, var_values, sizeof(var_values));

    /* ── Step 3: Build game state (heap, 32KB — enough for enriched content) ── */
    #define FS_SIZE 32768
    char *full_state = (char*)malloc(FS_SIZE);
    if (!full_state) {
        log_error("SEND_MSG: OUT OF MEMORY for full_state");
        free(jb_detach(&debug_trace));
        LeaveCriticalSection(&g_state_lock); return err_json("内存不足");
    }
    int pos = 0;

    /* Environment */
    pos += snprintf(full_state + pos, FS_SIZE - pos,
        "【环境信息】\n");
    pos += env_export_state(&g_env, full_state + pos, FS_SIZE - pos);
    pos += snprintf(full_state + pos, FS_SIZE - pos, "\n");

    /* Player */
    pos += cc_export_state(&g_player, full_state + pos, FS_SIZE - pos, true);
    pos += snprintf(full_state + pos, FS_SIZE - pos, "\n");

    /* NPC roster — split into relevant and background tiers */
    if (g_npc_card_count > 0) {
        int relevant_count = 0, bg_count = 0;

        for (int i = 0; i < g_npc_card_count; i++) {
            if (name_in_list(g_npc_cards[i].name, sel.char_names) ||
                npc_is_present(g_npc_cards[i].name))
                relevant_count++;
            else
                bg_count++;
        }

        if (relevant_count > 0) {
            pos += snprintf(full_state + pos, FS_SIZE - pos,
                "【相关NPC详情】\n");
            for (int i = 0; i < g_npc_card_count; i++) {
                if (name_in_list(g_npc_cards[i].name, sel.char_names) ||
                    npc_is_present(g_npc_cards[i].name)) {
                    pos += npc_full_entry(&g_npc_cards[i],
                        full_state + pos, (int)FS_SIZE - pos);
                }
            }
            pos += snprintf(full_state + pos, FS_SIZE - pos, "\n");
        }

        if (bg_count > 0) {
            pos += snprintf(full_state + pos, FS_SIZE - pos,
                "【其他已知NPC】（仅摘要，需要时可参考）\n");
            for (int i = 0; i < g_npc_card_count; i++) {
                if (!name_in_list(g_npc_cards[i].name, sel.char_names) &&
                    !npc_is_present(g_npc_cards[i].name)) {
                    pos += npc_compact_line(&g_npc_cards[i],
                        full_state + pos, (int)FS_SIZE - pos);
                }
            }
            pos += snprintf(full_state + pos, FS_SIZE - pos, "\n");
        }
    }

    /* Active NPCs (present at location) */
    {
        char present_buf[2048] = "";
        int pn = npc_export_present(&g_npc_mgr, present_buf, sizeof(present_buf));
        if (pn > 0) {
            pos += snprintf(full_state + pos, FS_SIZE - pos,
                "【在场人物】\n%s\n", present_buf);
        }
    }

    /* Selected variable values */
    if (var_values[0]) {
        pos += snprintf(full_state + pos, FS_SIZE - pos,
            "【相关变量详情】\n%s\n", var_values);
    }
    if (sel.char_names[0]) {
        pos += snprintf(full_state + pos, FS_SIZE - pos,
            "【AI识别到的相关人物】%s\n", sel.char_names);
    }

    /* ── Stage 5: World Director simulation summary ── */
    {
        char wd_summary[2048];
        int wlen = wd_export(&g_ws, wd_summary, sizeof(wd_summary));
        if (wlen > 0 && wlen < (int)FS_SIZE - pos - 1) {
            pos += snprintf(full_state + pos, FS_SIZE - pos,
                "\n%s\n", wd_summary);
        }
    }

    /* Ensure null termination and detect truncation */
    if (pos >= (int)FS_SIZE) {
        pos = FS_SIZE - 1;
        full_state[pos] = '\0';
        log_warn("full_state truncated to %d bytes — game state too large",
                 (int)FS_SIZE);
    }

    /* ── Step 4: Plan Generation ── */
    log_info("SEND_MSG: [Stage 5/6] Plan generation...");
    Plan plan;
    plan_init(&plan);
    bool plan_ok = planner_generate(&g_api, text, &intent, full_state, &plan);
    if (plan_ok) {
        log_info("SEND_MSG: [Stage 5/6] Plan OK: goal=%.60s steps=%d", plan.goal, plan.step_count);
    } else {
        log_warn("SEND_MSG: [Stage 5/6] Plan generation FAILED, using fallback single-step plan");
        /* Fallback: create simple single-step plan from intent */
        snprintf(plan.goal, sizeof(plan.goal), "%s", text);
        plan.steps[0].estimated_ticks = 5;
        if (intent.target[0]) {
            snprintf(plan.steps[0].action, sizeof(plan.steps[0].action),
                "%s %s", intent_action_verb(intent.type), intent.target);
            safe_strcpy(plan.steps[0].target, intent.target, PLAN_MAX_TARGET_LEN);
        } else {
            snprintf(plan.steps[0].action, sizeof(plan.steps[0].action),
                "%s", text);
        }
        plan.step_count = 1;
    }

    /* Debug: plan */
    {
        jb_str(&debug_trace, ",\"plan\":{");
        jb_kv_str(&debug_trace, "goal", plan.goal);
        jb_kv_int(&debug_trace, "stepCount", plan.step_count);
        jb_str(&debug_trace, ",\"steps\":[");
        for (int i = 0; i < plan.step_count; i++) {
            if (i > 0) jb_str(&debug_trace, ",");
            jb_obj_open(&debug_trace);
            jb_kv_str(&debug_trace, "action", plan.steps[i].action);
            if (plan.steps[i].target[0])
                jb_kv_str(&debug_trace, "target", plan.steps[i].target);
            jb_kv_int(&debug_trace, "estimatedTicks", plan.steps[i].estimated_ticks);
            jb_obj_close(&debug_trace);
        }
        jb_str(&debug_trace, "]");
        jb_str(&debug_trace, "}");
    }

    /* ── Step 5: Prepend intent + plan context to game state for AI ── */
    {
        /* Build prefix on stack (small), then shift full_state content
           to make room — avoids a second large heap allocation. */
        char prefix[4096];
        int pp = 0;

        /* ── Recent story summary for coherence ── */
        {
            MemoryStore *mem = &g_player.memory;
            int recent_count = 0;
            pp += snprintf(prefix + pp, sizeof(prefix) - pp,
                "═══ 近期事件回顾 ═══\n");
            /* Short-term memories (last 5) */
            int st_start = mem->short_count > 5 ? mem->short_count - 5 : 0;
            for (int i = st_start; i < mem->short_count; i++) {
                if (mem->short_term[i].content[0]) {
                    pp += snprintf(prefix + pp, sizeof(prefix) - pp,
                        "- %s\n", mem->short_term[i].content);
                    recent_count++;
                }
            }
            /* Long-term memories (last 3, if they don't duplicate short-term) */
            int lt_start = mem->long_count > 3 ? mem->long_count - 3 : 0;
            for (int i = lt_start; i < mem->long_count; i++) {
                if (mem->long_term[i].content[0]) {
                    pp += snprintf(prefix + pp, sizeof(prefix) - pp,
                        "- %s\n", mem->long_term[i].content);
                    recent_count++;
                }
            }
            if (recent_count == 0)
                pp += snprintf(prefix + pp, sizeof(prefix) - pp, "（暂无）\n");
            pp += snprintf(prefix + pp, sizeof(prefix) - pp, "\n");
        }

        pp += snprintf(prefix + pp, sizeof(prefix) - pp,
            "═══ 意图与计划 ═══\n"
            "用户意图: %s (置信度: %.0f%%)\n",
            intent_type_str(intent.type), intent.confidence * 100.0f);
        if (intent.target[0]) {
            pp += snprintf(prefix + pp, sizeof(prefix) - pp,
                "意图目标: %s\n", intent.target);
        }
        pp += snprintf(prefix + pp, sizeof(prefix) - pp,
            "计划目标: %s\n"
            "执行步骤:\n", plan.goal);
        for (int i = 0; i < plan.step_count; i++) {
            pp += snprintf(prefix + pp, sizeof(prefix) - pp,
                "  %d. %s", i + 1, plan.steps[i].action);
            if (plan.steps[i].target[0]) {
                pp += snprintf(prefix + pp, sizeof(prefix) - pp,
                    " → %s", plan.steps[i].target);
            }
            pp += snprintf(prefix + pp, sizeof(prefix) - pp,
                " (约%d分钟)\n", plan.steps[i].estimated_ticks);
        }
        pp += snprintf(prefix + pp, sizeof(prefix) - pp,
            "\n═══ 游戏状态 ═══\n");

        int prefix_len = pp;
        int body_len = (int)strlen(full_state);
        int total = prefix_len + body_len;

        if (total >= FS_SIZE - 1) {
            /* Truncate body to fit */
            body_len = FS_SIZE - 1 - prefix_len;
            if (body_len < 0) body_len = 0;
            total = prefix_len + body_len;
            log_warn("SEND_MSG: enriched state truncated to %d bytes", total);
        }

        /* Shift body right to make room for prefix (in-place) */
        if (body_len > 0)
            memmove(full_state + prefix_len, full_state, body_len + 1); /* +1 for \0 */
        else
            full_state[prefix_len] = '\0';

        /* Write prefix at start */
        memcpy(full_state, prefix, prefix_len);
        full_state[total] = '\0';
    }

    /* ── Step 6: AI Generation (narrative + changes) ── */
    log_info("SEND_MSG: [Stage 6/6] AI generation...");
    FullResponse *resp = (FullResponse*)calloc(1, sizeof(FullResponse));
    if (!resp) {
        log_error("SEND_MSG: [Stage 6/6] OUT OF MEMORY for FullResponse");
        free(full_state);
        free(jb_detach(&debug_trace));
        LeaveCriticalSection(&g_state_lock); return err_json("内存不足");
    }
    if (!api_generate(&g_api, final_text, full_state, resp)) {
        char msg[512]; snprintf(msg, sizeof(msg), "AI: %s", api_last_error(&g_api));
        log_error("SEND_MSG: [Stage 6/6] AI generation FAILED: %s", msg);
        free(resp);
        free(full_state);
        free(jb_detach(&debug_trace));
        LeaveCriticalSection(&g_state_lock); return err_json(msg);
    }
    fflush(NULL);  // 强制刷新日志，确保 api_generate 的 token 统计落盘
    log_info("SEND_MSG: [Stage 6/6] AI reply: text=%d changes=%d action_proposal=%d",
             (int)strlen(resp->text), (int)strlen(resp->changes),
             (int)strlen(resp->action_proposal));

    /* ── Step 7: Apply response through Rule Engine ── */
    apply_response(resp);

    /* Sync to WorldState and record events */
    ws_sync_from_globals();
    event_push(&g_events, g_ws.tick, 0, -1, EVENT_STATE_CHANGE,
               "{\"changes\":\"%.200s\"}", resp->changes[0] ? resp->changes : "(none)");
    if (resp->text[0]) {
        event_push(&g_events, g_ws.tick, 0, -1, EVENT_NARRATIVE,
                   "{\"text\":\"%.200s\"}", resp->text);
        /* Save location where narrative was generated, so the next
           request can detect same-location continuation. */
        char loc_key[256];
        snprintf(loc_key, sizeof(loc_key), "%s/%s/%s",
                 g_env.location.area, g_env.location.district, g_env.location.spot);
        ws_set_variable(&g_ws, "last_narrative_location", VAR_STRING, 0, 0.0f, loc_key);
    }

    /* Debug: rule engine results */
    {
        int hit_ids[RE_MAX_RULES];
        int n_hits = re_evaluate_all(&g_rule_engine, &g_player, &g_ws,
                                     hit_ids, RE_MAX_RULES);
        jb_str(&debug_trace, ",\"ruleHits\":[");
        for (int i = 0; i < n_hits; i++) {
            if (i > 0) jb_str(&debug_trace, ",");
            jb_obj_open(&debug_trace);
            jb_kv_int(&debug_trace, "ruleId", hit_ids[i]);
            /* Find rule to get condition */
            for (int j = 0; j < g_rule_engine.count; j++) {
                if (g_rule_engine.rules[j].id == hit_ids[i]) {
                    jb_kv_str(&debug_trace, "condition",
                        g_rule_engine.rules[j].condition);
                    break;
                }
            }
            jb_obj_close(&debug_trace);
        }
        jb_str(&debug_trace, "]");
    }

    /* ── Phase 4: Narrative post-processing (Layer 3) ──
       Assemble WorldResult from current state + changes and pass
       to the Narrative module for text generation. This is the
       canonical final step in the USE pipeline. */
    {
        WorldResult wr;
        wr_init(&wr);

        /* User input */
        safe_strcpy(wr.user_input, text, sizeof(wr.user_input));

        /* Intent */
        safe_strcpy(wr.intent_type, intent_type_str(intent.type),
                sizeof(wr.intent_type));
        if (intent.target[0])
            safe_strcpy(wr.intent_target, intent.target, sizeof(wr.intent_target));
        wr.intent_confidence = intent.confidence;

        /* Plan summary */
        if (plan.step_count > 0) {
            snprintf(wr.plan_summary, sizeof(wr.plan_summary),
                "%s (%d步)", plan.goal, plan.step_count);
        }

        /* Events summary from the changes applied */
        snprintf(wr.events_summary, sizeof(wr.events_summary),
            "变更: %.400s", resp->changes[0] ? resp->changes : "(无)");

        /* Context: location + time + weather */
        snprintf(wr.context, sizeof(wr.context),
            "地点: %s/%s/%s  时间: %d-%02d-%02d %02d:%02d  天气: %s",
            g_env.location.area, g_env.location.district, g_env.location.spot,
            g_env.time.year, g_env.time.month, g_env.time.day,
            g_env.time.hour, g_env.time.minute,
            weather_str(g_env.weather));

        wr.style = NSTYLE_AUTO;

        /* If the main AI call did NOT produce narrative text,
           generate it now via the Narrative module */
        if (!resp->text[0]) {
            NarrativeText nt;
            if (narrative_generate(&g_api, &wr, full_state, &nt)) {
                safe_strcpy(resp->text, nt.text, sizeof(resp->text));
                log_info("Narrative: generated %d chars", (int)strlen(nt.text));
                jb_str(&debug_trace, ",\"narrative\":{");
                jb_kv_str(&debug_trace, "source", "generated");
                jb_kv_int(&debug_trace, "length", (int)strlen(nt.text));
                jb_kv_str(&debug_trace, "style", nt.style);
                jb_str(&debug_trace, "}");
            }
        } else {
            /* Text exists — mark as provided by unified call */
            jb_str(&debug_trace, ",\"narrative\":{");
            jb_kv_str(&debug_trace, "source", "unified_call");
            jb_kv_int(&debug_trace, "length", (int)strlen(resp->text));
            jb_str(&debug_trace, "}");
        }
    }

    /* ── Stage 6: ActionProposal + Changes in debug trace ── */
    if (resp->action_proposal[0]) {
        jb_str(&debug_trace, ",\"actionProposal\":");
        jb_str(&debug_trace, resp->action_proposal);
    }
    if (resp->changes[0]) {
        jb_kv_str(&debug_trace, "changes", resp->changes);
    }

    /* ── Stage 6: World simulation summary ── */
    {
        char wd_sum[1024];
        int wlen = wd_export(&g_ws, wd_sum, sizeof(wd_sum));
        if (wlen > 0) {
            jb_kv_str(&debug_trace, "worldSim", wd_sum);
        }
    }

    /* Complete debug trace */
    jb_obj_close(&debug_trace);
    char *debug_trace_json = jb_detach(&debug_trace);

    bool saved = save_to_file("autosave");

    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j, "ok", 1);
    if (resp->text[0]) jb_kv_str(&j, "reply", resp->text);
    if (resp->changes[0]) jb_kv_str(&j, "changes", resp->changes);
    /* USE Phase 2: include debug trace in response */
    jb_str(&j, ",\"debugTrace\":"); jb_str(&j, debug_trace_json);
    free(debug_trace_json);
    char notify[512]="";
    if (resp->time_advance>0) {
        int h=resp->time_advance/60, m=resp->time_advance%60;
        snprintf(notify,sizeof(notify), h>0?"时间推进 %dh%dm":"时间推进 %dm", h, m);
    }
    if (!saved) {
        if (notify[0]) strncat(notify, " | ", sizeof(notify)-strlen(notify)-1);
        strncat(notify, "⚠自动存档失败", sizeof(notify)-strlen(notify)-1);
    }
    jb_kv_str(&j, "notification", notify);
    jb_str(&j, ",\"state\":"); char *s=build_state_json(); jb_str(&j,s); free(s);
    jb_obj_close(&j);

    log_info("SEND_MSG: Pipeline END — reply=%d chars, changes=%d chars, saved=%d",
             (int)strlen(resp->text), (int)strlen(resp->changes), saved);
    {
        long long tok_prompt = 0, tok_completion = 0, tok_total = 0;
        int calls = api_get_token_usage(&g_api, &tok_prompt, &tok_completion, &tok_total);
        log_info("SEND_MSG: token usage — calls=%d prompt=%lld completion=%lld total=%lld",
                 calls, tok_prompt, tok_completion, tok_total);
    }
    log_info("========================================");

    /* Record chat messages */
    if (g_chat_count < MAX_CHAT_MSGS) {
        ChatMsg *cm = &g_chat_history[g_chat_count++];
        safe_strcpy(cm->role, "player", sizeof(cm->role));
        safe_strcpy(cm->text, text, sizeof(cm->text));
        cm->tick = g_ws.tick;
    }
    if (resp->text[0] && g_chat_count < MAX_CHAT_MSGS) {
        ChatMsg *cm = &g_chat_history[g_chat_count++];
        safe_strcpy(cm->role, "ai", sizeof(cm->role));
        safe_strcpy(cm->text, resp->text, sizeof(cm->text));
        cm->tick = g_ws.tick;
    }

    free(resp);
    free(full_state);
    #undef FS_SIZE
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_create_world(const char *name, const char *age,
    const char *gender, const char *clothing, const char *money,
    const char *app, const char *con, const char *intel,
    const char *skills, const char *items, const char *story)
{
    EnterCriticalSection(&g_state_lock);
    if (!g_api_ready) { LeaveCriticalSection(&g_state_lock); return err_json("API 未配置"); }
    log_info("CREATE_WORLD: name=%s age=%s gender=%s...", name, age, gender ? gender : "");

    WorldCreateResult wcr;
    if (!api_create_world(&g_api, name,age,gender,clothing,money,app,con,intel,skills,items,story,&wcr)) {
        const char *e = api_last_error(&g_api);
        log_error("CREATE_WORLD: AI call FAILED: %s", e);
        LeaveCriticalSection(&g_state_lock); return err_json(e);
    }
    log_info("CREATE_WORLD: AI returned — player=%d bytes, locations=%d bytes, npcs=%d bytes",
        (int)strlen(wcr.player_card), (int)strlen(wcr.locations), (int)strlen(wcr.npc_cards));
    npc_parse_create(wcr.player_card, &g_player);
    log_info("CREATE_WORLD: player name after parse: '%s'", g_player.name);
    if (g_player.name[0] == '\0') {
        log_error("CREATE_WORLD: player name is empty after parse — AI returned unparseable card, aborting");
        LeaveCriticalSection(&g_state_lock);
        return err_json("角色创建失败：AI 返回的角色卡无法解析出有效姓名");
    }
    memset(g_player.personality, 0, sizeof(g_player.personality));

    if (wcr.locations[0]) {
        char buf[1024]; strncpy(buf,wcr.locations,sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
        MapLevel *l3=&g_map.levels[2];
        char *locctx = NULL;
        char *line=strtok_s(buf,"\n",&locctx);
        while (line && l3->count<MAX_MAP_POINTS) {
            while (*line==' '||*line=='\r') line++;
            char pn[64]={0}; int x=-1,y=-1;
            if (sscanf(line,"%63[^|]|%d|%d",pn,&x,&y)>=1) {
                strncpy(l3->points[l3->count].name, pn, MAP_NAME_LEN-1);
                l3->points[l3->count].name[MAP_NAME_LEN-1]='\0';
                l3->points[l3->count].x=(x>=0)?x:(rand()%900+50);
                l3->points[l3->count].y=(y>=0)?y:(rand()%900+50);
                l3->count++;
            }
            line=strtok_s(NULL,"\n",&locctx);
        }
    }
    if (wcr.start_location[0]) env_parse_location(&g_env, wcr.start_location);

    /* Apply era */
    if (wcr.era[0]) {
        env_set_era(&g_env, wcr.era);
        log_info("CREATE_WORLD: era set to '%s'", g_env.era);
    }

    /* Parse start time: format "年|月|日|时|分|星期" */
    if (wcr.start_time[0]) {
        int y = 1, m = 1, d = 1, h = 8, min = 0;
        if (sscanf(wcr.start_time, "%d|%d|%d|%d|%d",
                   &y, &m, &d, &h, &min) >= 5) {
            if (y >= 1 && y <= 9999) g_env.time.year = y;
            if (m >= 1 && m <= 12)   g_env.time.month = m;
            if (d >= 1 && d <= 31)   g_env.time.day = d;
            if (h >= 0 && h <= 23)   g_env.time.hour = h;
            if (min >= 0 && min <= 59) g_env.time.minute = min;
            g_env.time.weekday = env_calc_weekday(y, m, d);
            log_info("CREATE_WORLD: start time set to %d-%02d-%02d %02d:%02d",
                     g_env.time.year, g_env.time.month, g_env.time.day,
                     g_env.time.hour, g_env.time.minute);
        }
    }

    if (wcr.npc_cards[0]) {
        log_info("CREATE_WORLD: starting NPC parsing, buffer:\n%.200s", wcr.npc_cards);
        char buf[8192];
        strncpy(buf, wcr.npc_cards, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';
        char *cur = buf;
        int npc_index = 0;
        while (*cur) {
            while (*cur == '\n' || *cur == '\r' || *cur == ' ' || *cur == '-') cur++;
            if (!*cur) break;
            char *npc_start = cur;
            char *sep = strstr(cur, "\n---\n");
            if (!sep) sep = strstr(cur, "\n---");
            if (!sep) sep = strstr(cur, "---\n");
            if (!sep) sep = strstr(cur, "---");
            if (sep) {
                *sep = '\0';
                cur = sep + 3;
                while (*cur == '-' || *cur == '\n' || *cur == '\r' || *cur == ' ') cur++;
            } else {
                cur = NULL;
            }
            CharacterCard cc;
            if (npc_parse_create(npc_start, &cc) && npc_card_valid(&cc)) {
                bool dup = false;
                for (int i = 0; i < g_npc_card_count; i++) {
                    if (strcmp(g_npc_cards[i].name, cc.name) == 0) { dup = true; break; }
                }
                if (!dup) {
                    if (g_npc_card_count >= MAX_NPC_CARDS) {
                        int oldest = find_oldest_npc();
                        log_info("NPC满员，移除最久未互动: %s", g_npc_cards[oldest].name);
                        cc_free_internals(&g_npc_cards[oldest]);
                        g_npc_cards[oldest] = cc;
                    } else {
                        g_npc_cards[g_npc_card_count++] = cc;
                    }
                    log_info("CREATE_WORLD: parsed NPC #%d: %s", npc_index+1, cc.name);
                }
            } else {
                log_warn("CREATE_WORLD: failed to parse NPC block starting: %.60s", npc_start);
            }
            npc_index++;
            if (!cur) break;
        }
        log_info("CREATE_WORLD: finished parsing NPCs, total=%d", g_npc_card_count);
        fflush(NULL);  // 强制刷新日志
    }
    log_info("CREATE_WORLD: after parsing NPCs, npc_count=%d", g_npc_card_count);
    log_info("CREATE_WORLD: calling npc_init...");
    npc_init(&g_npc_mgr);
    log_info("CREATE_WORLD: npc_init done, calling map_locate...");
    map_locate(&g_map, g_env.location.area, g_env.location.district, g_env.location.spot);
    log_info("CREATE_WORLD: map_locate done, setting world_ready...");
    g_world_ready = true;
    log_info("CREATE_WORLD: calling npc_brains_init...");
    npc_brains_init();
    log_info("CREATE_WORLD: npc_brains_init done, calling wd_init...");
    wd_init(&g_ws, g_ws.tick);
    log_info("CREATE_WORLD: wd_init done, calling ws_sync_from_globals...");
    ws_sync_from_globals();
    log_info("CREATE_WORLD: ws_sync done, calling event_push...");
    event_push(&g_events, g_ws.tick, 0, -1, EVENT_SYSTEM,
               "{\"msg\":\"world created\",\"player\":\"%s\"}", g_player.name);
    log_info("CREATE_WORLD: event_push done, computing save number...");
    int num = next_save_number();
    log_info("CREATE_WORLD: next_save_number=%d", num);
    char sf[256];
    snprintf(sf, sizeof(sf), "save_%03d", num);
    log_info("CREATE_WORLD: saving to %s...", sf);
    bool saved_ok = save_to_file(sf) && save_to_file("autosave");
    log_info("CREATE_WORLD: save result = %d", saved_ok);

    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j,"ok",1);
    jb_kv_str(&j,"message", saved_ok ? "世界创建完成" : "世界创建完成（但存档写入失败！）");
    jb_kv_str(&j,"saveFile",sf);
    jb_kv_int(&j,"locationCount",g_map.levels[2].count);
    jb_kv_int(&j,"npcCount",g_npc_card_count);
    jb_str(&j,",\"state\":"); char *s=build_state_json(); jb_str(&j,s); free(s);
    jb_obj_close(&j);
    {
        long long tok_prompt = 0, tok_completion = 0, tok_total = 0;
        int calls = api_get_token_usage(&g_api, &tok_prompt, &tok_completion, &tok_total);
        log_info("CREATE_WORLD: 完成! 地点=%d NPC=%d save=%s saved=%d | "
                 "API calls=%d tokens: prompt=%lld completion=%lld total=%lld",
                 g_map.levels[2].count, g_npc_card_count, sf, saved_ok,
                 calls, tok_prompt, tok_completion, tok_total);
    }
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_quick_save(void)
{
    EnterCriticalSection(&g_state_lock);
    if (!g_world_ready) { LeaveCriticalSection(&g_state_lock); return err_json("无存档"); }
    ws_sync_from_globals();
    bool saved = save_to_file("autosave");
    LeaveCriticalSection(&g_state_lock);
    return saved ? ok_json(NULL, NULL) : err_json("写入失败，请检查磁盘空间");
}

char *backend_quick_load(void)
{
    EnterCriticalSection(&g_state_lock);
    char *r = load_from_file("autosave") ? build_state_json() : err_json("未找到快速存档");
    LeaveCriticalSection(&g_state_lock);
    return r;
}

char *backend_list_saves(void)
{
    EnterCriticalSection(&g_state_lock);
    JsonBuf j; jb_init(&j); jb_arr_open(&j);
    int first=1;

    /* Scan save folders (new format) */
    WIN32_FIND_DATAW fd;
    wchar_t wpattern[576];
    MultiByteToWideChar(CP_UTF8, 0, g_saves_root, -1, wpattern, 576);
    wcscat(wpattern, L"\\save_*");
    HANDLE h = FindFirstFileW(wpattern, &fd);
    if (h!=INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (wcscmp(fd.cFileName, L".")==0 || wcscmp(fd.cFileName, L"..")==0) continue;
            char fn[256]; WideCharToMultiByte(CP_UTF8,0,fd.cFileName,-1,fn,sizeof(fn),NULL,NULL);
            /* Read character.json from the folder */
            char ch_path[576]; snprintf(ch_path,sizeof(ch_path),"%s\\%s\\character.json",g_saves_root,fn);
            char *js = slurp_file(ch_path);
            if (!js) {
                /* Try old .dat file */
                char old_path[576]; snprintf(old_path,sizeof(old_path),"%s\\%s.dat",g_saves_root,fn);
                js = slurp_file(old_path);
                if (!js) continue;
                /* Old binary format — skip magic/version/length header */
                /* (slurp_file already read the whole thing; skip 12-byte header) */
                if (strlen(js) < 12) { free(js); continue; }
                /* Actually old format has binary header, json starts at offset 12 */
                /* For simplicity, try parsing the raw JSON embedded */
            }
            char pn[64]={0};
            json_get_str(js,"name",pn,sizeof(pn));
            if (!pn[0]) {
                const char *ps=extract_sub_obj(js,"character");
                if (!ps) ps=extract_sub_obj(js,"player");
                if (ps) json_get_str(ps,"name",pn,sizeof(pn));
            }
            if (!pn[0]) strcpy(pn,"(未命名)");
            int y=0,m=0,d=0, hh=0,mm=0;
            /* Try world.json for time */
            char wo_path[576]; snprintf(wo_path,sizeof(wo_path),"%s\\%s\\world.json",g_saves_root,fn);
            char *wo = slurp_file(wo_path);
            if (wo) {
                const char *es=extract_sub_obj(wo,"environment");
                if (es) {
                    const char *ts=extract_sub_obj(es,"time");
                    json_get_int(ts?ts:es,"year",&y);
                    json_get_int(ts?ts:es,"month",&m);
                    json_get_int(ts?ts:es,"day",&d);
                    json_get_int(ts?ts:es,"hour",&hh);
                    json_get_int(ts?ts:es,"minute",&mm);
                }
                free(wo);
            }
            char save_time[32];
            if (y > 0) {
                snprintf(save_time, sizeof(save_time), "%d-%02d-%02d %02d:%02d", y, m, d, hh, mm);
            } else {
                /* Fallback: use file modification time */
                HANDLE fh = CreateFileA(ch_path, GENERIC_READ, FILE_SHARE_READ,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (fh != INVALID_HANDLE_VALUE) {
                    FILETIME ft; SYSTEMTIME st;
                    if (GetFileTime(fh, NULL, NULL, &ft) && FileTimeToSystemTime(&ft, &st))
                        snprintf(save_time, sizeof(save_time), "%d-%02d-%02d %02d:%02d",
                            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                    else
                        strcpy(save_time, "未知");
                    CloseHandle(fh);
                } else {
                    strcpy(save_time, "未知");
                }
            }
            if (!first) jb_str(&j, ","); first=0;
            jb_obj_open(&j); jb_kv_str(&j,"filename",fn);
            jb_kv_str(&j,"playerName",pn);
            jb_kv_int(&j,"year",y); jb_kv_int(&j,"month",m); jb_kv_int(&j,"day",d);
            jb_kv_str(&j,"saveTime",save_time); jb_obj_close(&j);
            free(js);
        } while (FindNextFileW(h,&fd));
        FindClose(h);
    }

    jb_arr_close(&j);
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_load_save(const char *filename)
{
    EnterCriticalSection(&g_state_lock);
    char *r = load_from_file(filename) ? build_state_json() : err_json("加载失败");
    LeaveCriticalSection(&g_state_lock);
    return r;
}

char *backend_delete_save(const char *filename)
{
    EnterCriticalSection(&g_state_lock);
    bool ok = false;

    /* Safety: reject empty/suspicious filenames */
    if (!filename || !filename[0] ||
        strchr(filename, '/') || strchr(filename, '\\') ||
        strchr(filename, '.') || strchr(filename, ':')) {
        LeaveCriticalSection(&g_state_lock);
        return err_json("无效的存档名");
    }

    /* Build dir path and delete all files inside */
    char dir[576]; snprintf(dir,sizeof(dir),"%s\\%s",g_saves_root,filename);

    /* Enumerate and delete all files in the directory */
    WIN32_FIND_DATAW fdata;
    wchar_t wsearch[640], wdir[576];
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, 576);
    swprintf(wsearch, 640, L"%s\\*", wdir);
    HANDLE fh = FindFirstFileW(wsearch, &fdata);
    if (fh != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fdata.cFileName, L".") == 0 ||
                wcscmp(fdata.cFileName, L"..") == 0) continue;
            wchar_t wfull[640];
            swprintf(wfull, 640, L"%s\\%s", wdir, fdata.cFileName);
            DeleteFileW(wfull);
        } while (FindNextFileW(fh, &fdata));
        FindClose(fh);
    }
    ok = RemoveDirectoryA(dir);

    if (!ok) {
        /* Try old single-file format */
        char old[576]; snprintf(old,sizeof(old),"%s\\%s.dat",g_saves_root,filename);
        ok = DeleteFileA(old);
    }
    if (ok) log_info("删除存档: %s",filename);
    else log_error("删除失败: %s",filename);
    LeaveCriticalSection(&g_state_lock);
    return ok ? ok_json(NULL,NULL) : err_json("删除失败");
}

char *backend_travel(const char *from, const char *to, double dist, const char *method)
{
    EnterCriticalSection(&g_state_lock);
    if (!g_api_ready) { LeaveCriticalSection(&g_state_lock); return err_json("API 未配置"); }
    log_info("TRAVEL: %s→%s dist=%.0fkm method=%s",from,to,dist,method);
    int mins=0;
    if (!api_query_travel(&g_api,dist,method,&mins)) {
        const char *e=api_last_error(&g_api);
        log_error("TRAVEL: AI query FAILED: %s",e);
        LeaveCriticalSection(&g_state_lock); return err_json(e);
    }
    log_info("TRAVEL: estimated %d minutes", mins);
    /* Cache temps for departing location */
    char old_loc_t[MAX_NPC_LOC];
    npc_make_location_key(&g_env.location, old_loc_t, sizeof(old_loc_t));
    npc_cache_temps(&g_npc_mgr, old_loc_t);

    env_advance_time(&g_env,mins); env_parse_location(&g_env,to);

    /* Clear active temps, restore cached temps for destination */
    npc_clear_temps(&g_npc_mgr);
    char new_loc_t[MAX_NPC_LOC];
    npc_make_location_key(&g_env.location, new_loc_t, sizeof(new_loc_t));
    npc_restore_temps(&g_npc_mgr, new_loc_t);

    char mem[256]; int h=mins/60, m=mins%60;
    snprintf(mem,sizeof(mem),h>0?"从%s前往%s，耗时%d小时%d分钟":"从%s前往%s，耗时%d分钟",from,to,h>0?h:m,h>0?m:m);
    mem_add(&g_player.memory,mem,"",&g_env.time,MEM_LONG_TERM);

    /* Sync to WorldState and record travel event */
    ws_sync_from_globals();
    event_push(&g_events, g_ws.tick, 0, -1, EVENT_ACTION,
               "{\"travel\":\"%s→%s\",\"dist_km\":%.1f,\"method\":\"%s\",\"mins\":%d}",
               from, to, dist, method, mins);

    bool saved = save_to_file("autosave");
    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j,"ok",1); jb_kv_int(&j,"minutes",mins);
    if (!saved) jb_kv_str(&j,"warning","自动存档写入失败");
    jb_str(&j,",\"state\":"); char *s=build_state_json(); jb_str(&j,s); free(s);
    jb_obj_close(&j);
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_delete_location(const char *level_str, const char *name)
{
    EnterCriticalSection(&g_state_lock);
    if (!g_world_ready) { LeaveCriticalSection(&g_state_lock); return err_json("无存档"); }

    int level = atoi(level_str);
    if (level < 0 || level > 2) {
        LeaveCriticalSection(&g_state_lock);
        return err_json("无效的地图层级 (0=具体地点 1=小地点 2=大地点)");
    }
    if (!name || !name[0]) {
        LeaveCriticalSection(&g_state_lock);
        return err_json("请指定要删除的地点名称");
    }

    if (!map_has_point(&g_map, level, name)) {
        /* Log existing points at this level to help debug */
        log_warn("DELETE_LOCATION: point '%s' not found at level %d. Existing points:", name, level);
        for (int i = 0; i < g_map.levels[level].count; i++) {
            log_warn("  [%d] '%s'", i, g_map.levels[level].points[i].name);
        }
        LeaveCriticalSection(&g_state_lock);
        return err_json("地点不存在");
    }

    if (!map_remove_point(&g_map, level, name)) {
        LeaveCriticalSection(&g_state_lock);
        return err_json("删除失败");
    }

    log_info("DELETE_LOCATION: level=%d name=%s", level, name);
    ws_sync_from_globals();
    bool saved = save_to_file("autosave");
    event_push(&g_events, g_ws.tick, -1, -1, EVENT_SYSTEM,
               "{\"msg\":\"deleted location\",\"level\":%d,\"name\":\"%s\"}", level, name);

    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_bool(&j, "ok", 1);
    if (!saved) jb_kv_str(&j, "warning", "自动存档写入失败");
    jb_str(&j, ",\"state\":"); char *s = build_state_json(); jb_str(&j, s); free(s);
    jb_obj_close(&j);
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_get_chat_history(void)
{
    EnterCriticalSection(&g_state_lock);
    JsonBuf j; jb_init(&j); jb_arr_open(&j);
    for (int i = 0; i < g_chat_count; i++) {
        if (i > 0) jb_str(&j, ",");
        jb_obj_open(&j);
        jb_kv_str(&j, "role", g_chat_history[i].role);
        jb_kv_str(&j, "text", g_chat_history[i].text);
        jb_obj_close(&j);
    }
    jb_arr_close(&j);
    LeaveCriticalSection(&g_state_lock);
    return jb_detach(&j);
}

char *backend_get_state(void)
{
    EnterCriticalSection(&g_state_lock);
    char *r = build_state_json();
    LeaveCriticalSection(&g_state_lock);
    return r;
}

/* ═══════════════════════════════════════════════════════════════
   State JSON building — serializes game state for the web frontend.
   Originally part of backend.c, extracted for modularity.
   ═══════════════════════════════════════════════════════════════ */

#include "backend_internal.h"

/* ── Player character card as JSON ── */
void state_player_json(JsonBuf *j)
{
    jb_obj_open(j);
    jb_kv_str(j, "name", g_ctx.player.name);
    jb_kv_int(j, "age", g_ctx.player.age);
    jb_kv_str(j, "gender", g_ctx.player.gender);
    jb_kv_int(j, "money", g_ctx.player.money);
    jb_kv_str(j, "clothing", g_ctx.player.clothing);
    jb_kv_int(j, "status", (int)g_ctx.player.status);
    {
        const char *st_str = "正常";
        switch (g_ctx.player.status) {
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
    jb_kv_int(j, "appearance", g_ctx.player.attr.appearance);
    jb_kv_int(j, "constitution", g_ctx.player.attr.constitution);
    jb_kv_int(j, "intelligence", g_ctx.player.attr.intelligence);
    jb_str(j, "}");
    jb_str(j, ",\"skills\":[");
    for (int i = 0; i < g_ctx.player.skill_count; i++) {
        if (i) jb_str(j, ",");
        jb_obj_open(j); jb_kv_str(j, "name", g_ctx.player.skills[i].name);
        jb_kv_int(j, "level", g_ctx.player.skills[i].level); jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_str(j, ",\"items\":[");
    for (int i = 0; i < g_ctx.player.item_count; i++) {
        if (i) jb_str(j, ",");
        jb_obj_open(j); jb_kv_str(j, "name", g_ctx.player.items[i].name);
        jb_kv_int(j, "qty", g_ctx.player.items[i].quantity); jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_str(j, ",\"relations\":[");
    for (int i = 0; i < g_ctx.player.relation_count; i++) {
        if (i) jb_str(j, ",");
        jb_obj_open(j); jb_kv_str(j, "target", g_ctx.player.relations[i].target);
        jb_kv_str(j, "type", relation_type_str(g_ctx.player.relations[i].type));
        jb_kv_int(j, "affinity", g_ctx.player.relations[i].affinity); jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_kv_int(j, "playerAffinity", g_ctx.player.player_affinity);
    jb_kv_int(j, "skillCount", g_ctx.player.skill_count);
    jb_kv_int(j, "itemCount", g_ctx.player.item_count);
    jb_kv_int(j, "relationCount", g_ctx.player.relation_count);
    jb_obj_close(j);
}

/* ── Location: current place + present NPCs ── */
void state_location_json(JsonBuf *j)
{
    jb_obj_open(j);
    jb_kv_str(j, "area", g_ctx.env.location.area);
    jb_kv_str(j, "district", g_ctx.env.location.district);
    jb_kv_str(j, "spot", g_ctx.env.location.spot);

    /* Present NPCs at this location */
    jb_str(j, ",\"presentNpcs\":[");
    int first = 1;
    for (int i = 0; i < g_ctx.npc_mgr.spawn_count; i++) {
        if (g_ctx.npc_mgr.spawns[i].present) {
            if (!first) jb_str(j, ","); first = 0;
            jb_obj_open(j);
            jb_kv_str(j, "name", g_ctx.npc_mgr.spawns[i].name);
            jb_kv_int(j, "affinity", g_ctx.npc_mgr.spawns[i].player_affinity);
            jb_obj_close(j);
        }
    }
    for (int i = 0; i < g_ctx.npc_mgr.temp_count; i++) {
        if (!first) jb_str(j, ","); first = 0;
        jb_obj_open(j);
        jb_kv_str(j, "name", g_ctx.npc_mgr.temps[i].name);
        jb_kv_str(j, "desc", g_ctx.npc_mgr.temps[i].description);
        jb_kv_bool(j, "temp", true);
        jb_obj_close(j);
    }
    jb_str(j, "]");
    jb_obj_close(j);
}

/* ── Environment: weather, time, era ── */
void state_environment_json(JsonBuf *j)
{
    jb_obj_open(j);
    jb_kv_str(j, "era", g_ctx.env.era);
    jb_kv_str(j, "weather", weather_str(g_ctx.env.weather));
    jb_kv_int(j, "weatherCode", (int)g_ctx.env.weather);
    jb_str(j, ",\"time\":{");
    jb_kv_int(j, "year", g_ctx.env.time.year);
    jb_kv_int(j, "month", g_ctx.env.time.month);
    jb_kv_int(j, "day", g_ctx.env.time.day);
    jb_kv_int(j, "hour", g_ctx.env.time.hour);
    jb_kv_int(j, "minute", g_ctx.env.time.minute);
    jb_kv_str(j, "weekday", weekday_str(g_ctx.env.time.weekday));
    jb_str(j, "}");
    jb_obj_close(j);
}

/* Quick sanity check: is this NPC entry valid? */
bool npc_card_valid(const CharacterCard *c)
{
    if (!c) return false;
    if (!c->name) return false;
    if (!c->name[0]) return false;
    /* Name must have at least one non-ASCII byte (Chinese) or be alphanumeric */
    int len = (int)strlen(c->name);
    if (len < 1 || len > 60) return false;
    /* Reject names that look like raw variable dumps */
    if (strchr(c->name, '=') || strchr(c->name, '\n') || strchr(c->name, '\r'))
        return false;
    return true;
}

/* ── NPC character cards as JSON array ── */
void state_npcs_json(JsonBuf *j)
{
    int first = 1;
    jb_str(j, "[");
    for (int i = 0; i < g_ctx.npc_count; i++) {
        if (!npc_card_valid(&g_ctx.npcs[i])) continue;
        if (!first) jb_str(j, ",");
        first = 0;
        jb_obj_open(j);
        jb_kv_str(j, "name", g_ctx.npcs[i].name);
        jb_kv_int(j, "age", g_ctx.npcs[i].age);
        jb_kv_str(j, "gender", g_ctx.npcs[i].gender);
        jb_kv_str(j, "clothing", g_ctx.npcs[i].clothing);
        jb_kv_str(j, "personality", g_ctx.npcs[i].personality);
        jb_kv_str(j, "home", g_ctx.npcs[i].home);
        jb_kv_int(j, "status", (int)g_ctx.npcs[i].status);
        {
            const char *st = "正常";
            switch (g_ctx.npcs[i].status) {
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
        jb_kv_int(j, "money", g_ctx.npcs[i].money);
        jb_str(j, ",\"attrs\":{");
        jb_kv_int(j, "appearance", g_ctx.npcs[i].attr.appearance);
        jb_kv_int(j, "constitution", g_ctx.npcs[i].attr.constitution);
        jb_kv_int(j, "intelligence", g_ctx.npcs[i].attr.intelligence);
        jb_str(j, "}");
        jb_kv_int(j, "playerAffinity", g_ctx.npcs[i].player_affinity);
        /* Check if this NPC is currently present at the location */
        {
            bool present = false;
            for (int s = 0; s < g_ctx.npc_mgr.spawn_count; s++) {
                if (g_ctx.npc_mgr.spawns[s].present &&
                    g_ctx.npc_mgr.spawns[s].name[0] &&
                    g_ctx.npcs[i].name[0] &&
                    strcmp(g_ctx.npc_mgr.spawns[s].name, g_ctx.npcs[i].name) == 0) {
                    present = true; break;
                }
            }
            jb_kv_bool(j, "present", present);
        }
        jb_str(j, ",\"skills\":[");
        for (int k = 0; k < g_ctx.npcs[i].skill_count; k++) {
            if (k) jb_str(j, ",");
            jb_obj_open(j);
            jb_kv_str(j, "name", g_ctx.npcs[i].skills[k].name);
            jb_kv_int(j, "level", g_ctx.npcs[i].skills[k].level);
            jb_obj_close(j);
        }
        jb_str(j, "]");
        jb_str(j, ",\"items\":[");
        for (int k = 0; k < g_ctx.npcs[i].item_count; k++) {
            if (k) jb_str(j, ",");
            jb_obj_open(j);
            jb_kv_str(j, "name", g_ctx.npcs[i].items[k].name);
            jb_kv_int(j, "qty", g_ctx.npcs[i].items[k].quantity);
            jb_obj_close(j);
        }
        jb_str(j, "]");
        jb_kv_int(j, "skillCount", g_ctx.npcs[i].skill_count);
        jb_kv_int(j, "itemCount", g_ctx.npcs[i].item_count);
        jb_obj_close(j);
    }
    jb_str(j, "]");
}

/* ═══════════════════════════════════════════════════════════════
   build_state_json — full game state as JSON response
   ═══════════════════════════════════════════════════════════════ */

char *build_state_json(void)
{
    JsonBuf j; jb_init(&j);
    jb_obj_open(&j);
    jb_kv_bool(&j, "ok", 1);
    jb_kv_bool(&j, "worldReady", g_ctx.world_ready);
    jb_kv_bool(&j, "apiReady", g_ctx.api_ready);

    /* Token usage stats (safe: api client is always initialized) */
    {
        long long tok_prompt = 0, tok_completion = 0, tok_total = 0;
        int calls = api_get_token_usage(&g_ctx.api, &tok_prompt, &tok_completion, &tok_total);
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

    jb_kv_int(&j, "schemaVersion", WS_SCHEMA_VERSION);

    /* If the world hasn't been created yet, stop here.
       Character, NPCs, location, environment, map, and tick data
       are all uninitialized — serializing them would dereference
       NULL strings or read garbage values. The frontend checks
       worldReady and shows "尚未创建世界" accordingly. */
    if (!g_ctx.world_ready) {
        jb_obj_close(&j);
        return jb_detach(&j);
    }

    /* USE Architecture: tick */
    {
        long long tick = (long long)g_ctx.env.time.year * 525600LL
                       + (long long)g_ctx.env.time.month * 43200LL
                       + (long long)g_ctx.env.time.day * 1440LL
                       + (long long)g_ctx.env.time.hour * 60LL
                       + (long long)g_ctx.env.time.minute;
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
        map_export_json(&g_ctx.map, map_json, sizeof(map_json));
        /* Bug #12: ensure comma before raw JSON insertion */
        if (j.len > 0 && j.buf[j.len - 1] != '{' && j.buf[j.len - 1] != '[')
            jb_str(&j, ",");
        jb_str(&j, "\"mapPoints\":");
        jb_str(&j, map_json);
    }
    /* NPC temp cache (save/load persistence, not for UI) */
    jb_str(&j, ",\"npcTempCache\":");
    npc_cache_to_json(&g_ctx.npc_mgr, &j);

    /* World variables summary */
    if (g_ctx.ws.variable_count > 0) {
        jb_str(&j, ",\"worldVariables\":[");
        for (int i = 0; i < g_ctx.ws.variable_count; i++) {
            if (i > 0) jb_str(&j, ",");
            WorldVariable *v = &g_ctx.ws.variables[i];
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
        jb_kv_int(&j, "eventCount", g_ctx.events.count);
    }

    jb_obj_close(&j);
    return jb_detach(&j);
}

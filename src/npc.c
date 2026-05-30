#include "npc.h"
#include "character_card.h"
#include "environment.h"
#include "json.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void npc_init(NpcManager *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
}

void npc_clear_temps(NpcManager *mgr)
{
    mgr->temp_count = 0;
}

bool npc_add_temp(NpcManager *mgr, const char *name, const char *desc, const char *loc)
{
    if (mgr->temp_count >= MAX_TEMP_NPCS) return false;
    TempNpc *t = &mgr->temps[mgr->temp_count];
    safe_strcpy(t->name,        name, MAX_NPC_NAME);
    safe_strcpy(t->description, desc, MAX_NPC_DESC);
    safe_strcpy(t->location,    loc,  MAX_NPC_LOC);
    mgr->temp_count++;
    return true;
}

void npc_set_spawns(NpcManager *mgr, const NpcSpawn *list, int count)
{
    mgr->spawn_count = 0;
    for (int i = 0; i < count && i < MAX_TEMP_NPCS; i++) {
        mgr->spawns[i] = list[i];
    }
    mgr->spawn_count = count < MAX_TEMP_NPCS ? count : MAX_TEMP_NPCS;
}

void npc_resolve_spawns(NpcManager *mgr)
{
    for (int i = 0; i < mgr->spawn_count; i++) {
        NpcSpawn *s = &mgr->spawns[i];
        switch (s->category) {
        case 0: /* MUST */
            s->present = true;
            break;
        case 1: /* MAYBE — 20%概率 */
            s->present = (rand() % 100 < 20);
            break;
        case 2: /* NEVER */
            s->present = false;
            break;
        default:
            s->present = false;
            break;
        }
    }
}

int npc_present_count(const NpcManager *mgr)
{
    int n = mgr->temp_count;
    for (int i = 0; i < mgr->spawn_count; i++) {
        if (mgr->spawns[i].present) n++;
    }
    return n;
}

int npc_export_present(const NpcManager *mgr, char *out, int out_size)
{
    int pos = 0;
    /* 临时NPC */
    for (int i = 0; i < mgr->temp_count; i++) {
        pos += snprintf(out + pos, out_size - pos, "%s: %s (位于%s)\n",
            mgr->temps[i].name, mgr->temps[i].description, mgr->temps[i].location);
    }
    /* 在场的角色卡NPC */
    for (int i = 0; i < mgr->spawn_count; i++) {
        if (mgr->spawns[i].present) {
            pos += snprintf(out + pos, out_size - pos, "%s (在场)  好感度: %+d\n",
                mgr->spawns[i].name, mgr->spawns[i].player_affinity);
        }
    }
    return pos;
}

bool npc_parse_spawn_line(const char *line, NpcSpawn *sp)
{
    /* 格式: name|MUST|location  或  name|MAYBE|location  或  name|NEVER */
    memset(sp, 0, sizeof(*sp));

    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *s1 = buf;
    char *s2 = strchr(buf, '|');
    char *s3 = s2 ? strchr(s2 + 1, '|') : NULL;
    if (!s1 || !s2) return false;
    *s2 = '\0';
    if (s3) *s3 = '\0';

    safe_strcpy(sp->name, s1, MAX_NPC_NAME);

    if (strstr(s2 + 1, "MUST") || strstr(s2 + 1, "must") || strstr(s2 + 1, "一定"))
        sp->category = 0;
    else if (strstr(s2 + 1, "MAYBE") || strstr(s2 + 1, "maybe") || strstr(s2 + 1, "可能"))
        sp->category = 1;
    else
        sp->category = 2;

    if (s3) {
        safe_strcpy(sp->location, s3 + 1, MAX_NPC_LOC);
    }
    return true;
}

bool npc_parse_temp_line(const char *line, TempNpc *tp)
{
    /* 格式: name|description */
    memset(tp, 0, sizeof(*tp));

    char buf[384];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *s1 = buf;
    char *s2 = strchr(buf, '|');
    if (!s1 || !s2) return false;
    *s2 = '\0';

    safe_strcpy(tp->name, s1, MAX_NPC_NAME);
    safe_strcpy(tp->description, s2 + 1, MAX_NPC_DESC);
    return true;
}

/* 解析单行 "key=value" 或 "key+value" 或 "key-value"，填充到角色卡 */
static void apply_create_line(CharacterCard *card, const char *line)
{
    if (!line || !*line) return;

    /* Limit single-line length to prevent buffer overflow from
       malformed AI output (e.g. unescaped newlines in personality). */
    if (strlen(line) > 512) {
        log_warn("apply_create_line: line too long (%zu chars), truncating: %.40s...",
                 strlen(line), line);
    }

    char buf[513];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* 去除首尾空白 */
    char *s = buf;
    while (*s == ' ' || *s == '\r') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\r' || *end == '\n')) *end-- = '\0';
    if (!*s) return;

    log_info("apply_create_line: line='%s'", s);

    /* 找 = 或 + 或 - */
    char *eq = strchr(s, '=');
    char *plus = strchr(s, '+');
    char *minus = strchr(s, '-');

    char *op = eq;
    int is_delta = 0;
    if (!op) { op = plus; is_delta = 1; }
    if (!op) { op = minus; is_delta = -1; }
    if (!op) return;

    *op = '\0';
    char *key = s;
    char *val = op + 1;

    /* Guard: skip empty key or val to avoid downstream strangeness */
    if (!*key || !*val) return;

    /* skill.xxx=level */
    if (strncmp(key, "skill.", 6) == 0) {
        int lv = atoi(val);
        if (is_delta < 0) lv = -lv;

        /* Validate skill level range: AI should output values within 0-100.
           cc_add_skill will clamp, but we warn here so developers can
           detect when the AI is not conforming to the spec. */
        if (lv < 0 || lv > 100) {
            log_warn("apply_create_line: skill '%s' level %d out of range [0,100] "
                     "(AI output violation, will be clamped)", key + 6, lv);
        }

        if (is_delta > 0) {
            /* skill.xxx+delta: find existing and add */
            for (int i = 0; i < card->skill_count; i++) {
                if (strcmp(card->skills[i].name, key + 6) == 0) {
                    int new_level = card->skills[i].level + lv;
                    if (new_level < 0 || new_level > 100) {
                        log_warn("apply_create_line: skill '%s' delta %d -> %d "
                                 "out of range [0,100] (AI output violation)",
                                 key + 6, lv, new_level);
                    }
                    cc_add_skill(card, key + 6, new_level);
                    return;
                }
            }
        }
        cc_add_skill(card, key + 6, lv);
        return;
    }

    /* item.xxx=quantity */
    if (strncmp(key, "item.", 5) == 0) {
        int qty = atoi(val);
        if (is_delta) {
            /* Bug #42: check for existing item; create new one if not found */
            bool found_item = false;
            for (int i = 0; i < card->item_count; i++) {
                if (strcmp(card->items[i].name, key + 5) == 0) {
                    card->items[i].quantity += (is_delta < 0 ? -qty : qty);
                    if (card->items[i].quantity <= 0)
                        cc_remove_item(card, key + 5);
                    found_item = true;
                    break;
                }
            }
            if (!found_item && qty > 0) {
                /* Create new item with the delta quantity */
                cc_add_item(card, key + 5, (is_delta < 0 ? 0 : qty));
            }
        } else if (qty > 0) {
            cc_add_item(card, key + 5, qty);
        }
        return;
    }

    /* relation.xxx=TYPE+affinity */
    if (strncmp(key, "relation.", 9) == 0) {
        /* val 格式: FRIEND+30 或 ENEMY-50 或 SPOUSE=80 */
        char rtype_str[32] = {0};
        int aff = 0;
        char *rplus = strchr(val, '+');
        char *rminus = strchr(val, '-');
        char *req = strchr(val, '=');
        if (rplus) {
            *rplus = '\0';
            safe_strcpy(rtype_str, val, sizeof(rtype_str));
            aff = atoi(rplus + 1);
        } else if (rminus) {
            *rminus = '\0';
            safe_strcpy(rtype_str, val, sizeof(rtype_str));
            aff = -atoi(rminus + 1);
        } else if (req) {
            *req = '\0';
            safe_strcpy(rtype_str, val, sizeof(rtype_str));
            aff = atoi(req + 1);
        } else {
            safe_strcpy(rtype_str, val, sizeof(rtype_str));
            aff = 0;
        }

        RelationType rt = REL_STRANGER;
        if (strstr(rtype_str, "PARENT")) rt = REL_PARENT;
        else if (strstr(rtype_str, "CHILD")) rt = REL_CHILD;
        else if (strstr(rtype_str, "SIBLING") || strstr(rtype_str, "兄弟姐妹")) rt = REL_SIBLING;
        else if (strstr(rtype_str, "SPOUSE") || strstr(rtype_str, "配偶")) rt = REL_SPOUSE;
        else if (strstr(rtype_str, "LOVER") || strstr(rtype_str, "恋人")) rt = REL_LOVER;
        else if (strstr(rtype_str, "EX") || strstr(rtype_str, "前任")) rt = REL_EX;
        else if (strstr(rtype_str, "KIN") || strstr(rtype_str, "亲戚")) rt = REL_KIN;
        else if (strstr(rtype_str, "BEST") || strstr(rtype_str, "挚友")) rt = REL_BEST_FRIEND;
        else if (strstr(rtype_str, "FRIEND") || strstr(rtype_str, "朋友")) rt = REL_FRIEND;
        else if (strstr(rtype_str, "RIVAL") || strstr(rtype_str, "对手")) rt = REL_RIVAL;
        else if (strstr(rtype_str, "ENEMY") || strstr(rtype_str, "仇敌")) rt = REL_ENEMY;
        else if (strstr(rtype_str, "COLLEAGUE") || strstr(rtype_str, "同事")) rt = REL_COLLEAGUE;
        else if (strstr(rtype_str, "MASTER") || strstr(rtype_str, "师父")) rt = REL_MASTER;
        else if (strstr(rtype_str, "DISCIPLE") || strstr(rtype_str, "徒弟")) rt = REL_DISCIPLE;
        else if (strstr(rtype_str, "ACQUAINTANCE") || strstr(rtype_str, "熟人")) rt = REL_ACQUAINTANCE;
        else if (strstr(rtype_str, "STRANGER") || strstr(rtype_str, "陌生人")) rt = REL_STRANGER;

        cc_add_relation(card, key + 9, rt, aff);
        return;
    }

    /* 标准字段 */
    int ival = atoi(val);
    if (is_delta < 0) ival = -ival;

    if (strcmp(key, "name") == 0) {
        safe_strcpy(card->name, val, MAX_NAME_LEN);
    } else if (strcmp(key, "age") == 0) {
        card->age = is_delta ? card->age + ival : atoi(val);
    } else if (strcmp(key, "personality") == 0) {
        safe_strcpy(card->personality, val, MAX_PERSONALITY_LEN);
    } else if (strcmp(key, "clothing") == 0) {
        safe_strcpy(card->clothing, val, MAX_CLOTHING_LEN);
    } else if (strcmp(key, "gender") == 0) {
        safe_strcpy(card->gender, val, MAX_GENDER_LEN);
    } else if (strcmp(key, "home") == 0) {
        safe_strcpy(card->home, val, MAX_HOME_LEN);
    } else if (strcmp(key, "money") == 0) {
        card->money = is_delta ? card->money + ival : atoi(val);
        if (card->money < 0) card->money = 0;
    } else if (strcmp(key, "player_affinity") == 0) {
        cc_set_player_affinity(card, is_delta ? card->player_affinity + ival : atoi(val));
    } else if (strcmp(key, "status") == 0) {
        if (strcmp(val, "HUNGRY") == 0) card->status = STATUS_HUNGRY;
        else if (strcmp(val, "TIRED") == 0) card->status = STATUS_TIRED;
        else if (strcmp(val, "SICK") == 0) card->status = STATUS_SICK;
        else if (strcmp(val, "INJURED") == 0) card->status = STATUS_INJURED;
        else if (strcmp(val, "EXCITED") == 0) card->status = STATUS_EXCITED;
        else if (strcmp(val, "ANGRY") == 0) card->status = STATUS_ANGRY;
        else if (strcmp(val, "SAD") == 0) card->status = STATUS_SAD;
        else if (strcmp(val, "HAPPY") == 0) card->status = STATUS_HAPPY;
        else card->status = STATUS_NORMAL;
    } else if (strcmp(key, "attr.appearance") == 0 || strcmp(key, "appearance") == 0) {
        /* Bug #17: use cc_set_attributes for proper clamping */
        cc_set_attributes(card,
            is_delta ? card->attr.appearance + ival : ival,
            card->attr.constitution,
            card->attr.intelligence);
    } else if (strcmp(key, "attr.constitution") == 0 || strcmp(key, "constitution") == 0) {
        cc_set_attributes(card,
            card->attr.appearance,
            is_delta ? card->attr.constitution + ival : ival,
            card->attr.intelligence);
    } else if (strcmp(key, "attr.intelligence") == 0 || strcmp(key, "intelligence") == 0) {
        cc_set_attributes(card,
            card->attr.appearance,
            card->attr.constitution,
            is_delta ? card->attr.intelligence + ival : ival);
    }
}

bool npc_parse_create(const char *text, CharacterCard *card)
{
    if (!text || !text[0]) {
        log_warn("npc_parse_create: null or empty input text");
        return false;
    }
    log_info("npc_parse_create: input length=%d, start=%.100s", (int)strlen(text), text);
    if (!card) {
        log_error("npc_parse_create: null card pointer");
        return false;
    }

    cc_init(card, ENTITY_CHARACTER);

    /* Reject input that is clearly not a character card (e.g. raw JSON or
       narrative text).  A valid card must contain at least one "name=" line. */
    if (!strstr(text, "name=") && !strstr(text, "name+") && !strstr(text, "name-")) {
        log_warn("npc_parse_create: input missing 'name=' field, rejecting (%d bytes)",
                 (int)strlen(text));
        return false;
    }

    char buf[4096];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Warn if input was truncated — AI may have returned junk */
    if (strlen(text) >= sizeof(buf)) {
        log_warn("npc_parse_create: input truncated (%zu bytes -> %d), "
                 "AI output may be malformed",
                 strlen(text), (int)(sizeof(buf) - 1));
    }

    /* Bug #40: avoid strtok (not thread-safe); use manual line split.
       Limit to 256 lines to prevent infinite loops from malformed input
       that lacks newline characters. */
    #define PARSE_MAX_LINES 256
    int line_count = 0;
    char *line = buf;
    while (line && *line && line_count < PARSE_MAX_LINES) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        while (*line == ' ' || *line == '\r') line++;
        if (*line) {
            apply_create_line(card, line);
            line_count++;
        }
        line = nl ? nl + 1 : NULL;
    }
    if (line_count >= PARSE_MAX_LINES) {
        log_warn("npc_parse_create: hit line limit (%d), input may be malformed",
                 PARSE_MAX_LINES);
    }

    /* Must have at least a name — reject empty-name cards early so
       callers don't waste time on useless entities. */
    if (card->name[0] == '\0') {
        log_warn("npc_parse_create: parsed card has empty name — discarding");
        return false;
    }

    log_info("npc_parse_create: parsed '%s' (age=%d, %d skills, %d items)",
             card->name, card->age, card->skill_count, card->item_count);
    return true;
    #undef PARSE_MAX_LINES
}

/* ═══════════════════════════════════════════════════════════════
   Per-location temp NPC cache
   ═══════════════════════════════════════════════════════════════ */

void npc_make_location_key(const Location *loc, char *out, int out_size)
{
    snprintf(out, out_size, "%s/%s/%s", loc->area, loc->district, loc->spot);
}

void npc_cache_temps(NpcManager *mgr, const char *location_key)
{
    if (!location_key || !location_key[0]) return;

    /* Find existing slot for this location */
    int slot = -1;
    for (int i = 0; i < mgr->loc_cache_count; i++) {
        if (strcmp(mgr->loc_cache[i].location, location_key) == 0) {
            slot = i;
            break;
        }
    }

    /* Allocate new slot if needed */
    if (slot < 0) {
        if (mgr->loc_cache_count >= MAX_CACHED_LOCS) {
            /* FIFO evict: shift-left drops slot 0 */
            memmove(&mgr->loc_cache[0], &mgr->loc_cache[1],
                    sizeof(LocTempCacheSlot) * (MAX_CACHED_LOCS - 1));
            slot = MAX_CACHED_LOCS - 1;
        } else {
            slot = mgr->loc_cache_count;
            mgr->loc_cache_count++;
        }
    }

    /* Populate cache slot from active temps */
    LocTempCacheSlot *c = &mgr->loc_cache[slot];
    memset(c, 0, sizeof(*c));
    strncpy(c->location, location_key, MAX_NPC_LOC - 1);
    c->location[MAX_NPC_LOC - 1] = '\0';

    int n = mgr->temp_count;
    if (n > MAX_NPC_PER_CACHE) n = MAX_NPC_PER_CACHE;
    for (int i = 0; i < n; i++) {
        c->temps[i] = mgr->temps[i];
    }
    c->temp_count = n;
}

void npc_restore_temps(NpcManager *mgr, const char *location_key)
{
    if (!location_key || !location_key[0]) return;

    for (int i = 0; i < mgr->loc_cache_count; i++) {
        if (strcmp(mgr->loc_cache[i].location, location_key) == 0) {
            LocTempCacheSlot *c = &mgr->loc_cache[i];
            for (int j = 0; j < c->temp_count; j++) {
                npc_add_temp(mgr, c->temps[j].name,
                             c->temps[j].description, c->temps[j].location);
            }
            return;
        }
    }
    /* Not found: first visit, active temps stay empty */
}

void npc_cache_to_json(const NpcManager *mgr, JsonBuf *j)
{
    jb_arr_open(j);
    int first = 1;
    for (int i = 0; i < mgr->loc_cache_count; i++) {
        const LocTempCacheSlot *c = &mgr->loc_cache[i];
        if (c->temp_count == 0) continue;
        if (!first) jb_str(j, ",");
        first = 0;

        jb_obj_open(j);
        jb_kv_str(j, "location", c->location);
        jb_str(j, ",\"npcs\":[");
        for (int k = 0; k < c->temp_count; k++) {
            if (k) jb_str(j, ",");
            jb_obj_open(j);
            jb_kv_str(j, "name", c->temps[k].name);
            jb_kv_str(j, "desc", c->temps[k].description);
            jb_kv_str(j, "loc",  c->temps[k].location);
            jb_obj_close(j);
        }
        jb_str(j, "]");
        jb_obj_close(j);
    }
    jb_arr_close(j);
}

int npc_parse_cache_from_json(NpcManager *mgr, const char *json)
{
    mgr->loc_cache_count = 0;
    if (!json || !json[0]) return 0;

    int jlen = (int)strlen(json);
    if (jlen <= 0) return 0;

    /* Find "npcTempCache":[...] safely */
    const char *key = "npcTempCache";
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":[", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;  /* skip '[' */
    if (p - json >= jlen) return 0;

    while (*p && *p != ']' && (p - json) < jlen) {
        if (*p == '{') {
            if (mgr->loc_cache_count >= MAX_CACHED_LOCS) break;

            LocTempCacheSlot *slot = &mgr->loc_cache[mgr->loc_cache_count];
            memset(slot, 0, sizeof(*slot));
            json_get_str(p, "location", slot->location, MAX_NPC_LOC);

            /* Parse inner "npcs":[...] array — only if it comes before
               the closing brace of the cache slot object */
            const char *closing_brace = strchr(p, '}');
            if (!closing_brace || closing_brace - json >= jlen) {
                p = closing_brace;
                if (!p) break;
                p++;
                continue;
            }
            const char *npcs_key = strstr(p, "\"npcs\":[");
            if (npcs_key && npcs_key < closing_brace) {
                const char *arr = strchr(npcs_key, '[');
                if (arr && arr < closing_brace) {
                    arr++;  /* skip '[' */
                    while (*arr && *arr != ']' && (arr - json) < jlen
                           && slot->temp_count < MAX_NPC_PER_CACHE) {
                        if (*arr == '{') {
                            /* Make sure we don't read past buffer */
                            if (arr - json >= jlen) break;
                            TempNpc *tp = &slot->temps[slot->temp_count];
                            memset(tp, 0, sizeof(*tp));
                            json_get_str(arr, "name", tp->name, MAX_NPC_NAME);
                            json_get_str(arr, "desc", tp->description, MAX_NPC_DESC);
                            json_get_str(arr, "loc",  tp->location, MAX_NPC_LOC);
                            slot->temp_count++;
                            arr = strchr(arr, '}');
                            if (!arr || arr - json >= jlen) break;
                        }
                        arr++;
                    }
                }
            }

            if (slot->location[0]) {
                mgr->loc_cache_count++;
            }
            p = strchr(p, '}');
            if (!p) break;
        }
        p++;
    }
    return mgr->loc_cache_count;
}

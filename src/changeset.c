#include "changeset.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════
   ChangeSetFull — full-featured change set with string support
   ═══════════════════════════════════════════════════════════════════ */

void cs_full_init(ChangeSetFull *cs)
{
    memset(cs, 0, sizeof(*cs));
}

bool cs_full_add_delta(ChangeSetFull *cs, const char *entity, const char *field, int delta)
{
    if (cs->count >= CS_MAX_CHANGES) return false;
    ChangeEntryFull *e = &cs->entries[cs->count];
    memset(e, 0, sizeof(*e));
    e->type = CS_VARIABLE;
    safe_strcpy(e->entity_name, entity, sizeof(e->entity_name));
    safe_strcpy(e->field, field, sizeof(e->field));
    e->delta = delta;
    cs->count++;
    return true;
}

bool cs_full_add_set_int(ChangeSetFull *cs, const char *entity, const char *field, int new_value)
{
    if (cs->count >= CS_MAX_CHANGES) return false;
    ChangeEntryFull *e = &cs->entries[cs->count];
    memset(e, 0, sizeof(*e));
    e->type = CS_SET;
    safe_strcpy(e->entity_name, entity, sizeof(e->entity_name));
    safe_strcpy(e->field, field, sizeof(e->field));
    e->new_value = new_value;
    cs->count++;
    return true;
}

bool cs_full_add_status(ChangeSetFull *cs, const char *entity, int status_code)
{
    if (cs->count >= CS_MAX_CHANGES) return false;
    ChangeEntryFull *e = &cs->entries[cs->count];
    memset(e, 0, sizeof(*e));
    e->type = CS_STATUS;
    safe_strcpy(e->entity_name, entity, sizeof(e->entity_name));
    e->status_code = status_code;
    cs->count++;
    return true;
}

bool cs_full_add_set_str(ChangeSetFull *cs, const char *entity, const char *field,
                         const char *str_value)
{
    if (cs->count >= CS_MAX_CHANGES) return false;
    ChangeEntryFull *e = &cs->entries[cs->count];
    memset(e, 0, sizeof(*e));
    e->type = CS_SET;
    safe_strcpy(e->entity_name, entity, sizeof(e->entity_name));
    safe_strcpy(e->field, field, sizeof(e->field));
    safe_strcpy(e->str_value, str_value, sizeof(e->str_value));
    cs->count++;
    return true;
}

bool cs_full_add_clamp(ChangeSetFull *cs, const char *entity, const char *field,
                       int min_val, int max_val)
{
    if (cs->count >= CS_MAX_CHANGES) return false;
    ChangeEntryFull *e = &cs->entries[cs->count];
    memset(e, 0, sizeof(*e));
    e->type = CS_CLAMP;
    safe_strcpy(e->entity_name, entity, sizeof(e->entity_name));
    safe_strcpy(e->field, field, sizeof(e->field));
    e->clamp_min = min_val;
    e->clamp_max = max_val;
    cs->count++;
    return true;
}

/* ── Entity lookup ── */

CharacterCard* cs_find_entity(const char *name, CharacterCard *player,
                               CharacterCard *npcs, int npc_count)
{
    if (!name || !*name) return NULL;

    /* Check player */
    if (strcmp(name, "player") == 0 || strcmp(name, player->name) == 0)
        return player;

    /* Check NPCs */
    for (int i = 0; i < npc_count; i++) {
        if (strcmp(name, npcs[i].name) == 0)
            return &npcs[i];
    }

    return NULL;
}

/* ── Apply a single ChangeEntryFull to the target entity ── */

static void cs_apply_one(const ChangeEntryFull *e, CharacterCard *target,
                         EventLog *events, long long tick)
{
    if (!target) return;

    switch (e->type) {
    case CS_VARIABLE:
        if (strcmp(e->field, "money") == 0) {
            cc_change_money(target, e->delta);
        } else if (strcmp(e->field, "player_affinity") == 0) {
            cc_set_player_affinity(target,
                target->player_affinity + e->delta);
        } else if (strcmp(e->field, "attr.appearance") == 0 ||
                   strcmp(e->field, "appearance") == 0) {
            cc_set_attributes(target,
                target->attr.appearance + e->delta,
                target->attr.constitution,
                target->attr.intelligence);
        } else if (strcmp(e->field, "attr.constitution") == 0 ||
                   strcmp(e->field, "constitution") == 0) {
            cc_set_attributes(target,
                target->attr.appearance,
                target->attr.constitution + e->delta,
                target->attr.intelligence);
        } else if (strcmp(e->field, "attr.intelligence") == 0 ||
                   strcmp(e->field, "intelligence") == 0) {
            cc_set_attributes(target,
                target->attr.appearance,
                target->attr.constitution,
                target->attr.intelligence + e->delta);
        } else if (strcmp(e->field, "age") == 0) {
            target->age += e->delta;
            if (target->age < 0) target->age = 0;
        } else {
            /* Check if it's a skill delta */
            for (int i = 0; i < target->skill_count; i++) {
                if (strcmp(target->skills[i].name, e->field) == 0) {
                    int new_level = target->skills[i].level + e->delta;
                    if (new_level < 0 || new_level > 100) {
                        log_warn("cs_apply: skill '%s' delta %d -> %d "
                                 "out of range [0,100] (will be clamped)",
                                 e->field, e->delta, new_level);
                    }
                    cc_add_skill(target, e->field, new_level);
                    goto applied;
                }
            }
            /* Try relation affinity: field = "NPC名.affinity" */
            {
                char *dot = strchr(e->field, '.');
                if (dot) {
                    char target_name[64];
                    int len = (int)(dot - e->field);
                    if (len >= (int)sizeof(target_name)) len = sizeof(target_name) - 1;
                    memcpy(target_name, e->field, len);
                    target_name[len] = '\0';
                    if (strcmp(dot + 1, "affinity") == 0) {
                        cc_set_affinity(target, target_name, e->delta);
                    }
                }
            }
        }
        applied:
        if (events) {
            event_push(events, tick, -1, -1, EVENT_STATE_CHANGE,
                "{\"entity\":\"%s\",\"field\":\"%s\",\"delta\":%d}",
                target->name, e->field, e->delta);
        }
        break;

    case CS_SET:
        if (e->str_value[0]) {
            /* String set */
            if (strcmp(e->field, "name") == 0) {
                safe_strcpy(target->name, e->str_value, MAX_NAME_LEN);
            } else if (strcmp(e->field, "clothing") == 0) {
                safe_strcpy(target->clothing, e->str_value, MAX_CLOTHING_LEN);
            } else if (strcmp(e->field, "personality") == 0) {
                safe_strcpy(target->personality, e->str_value, MAX_PERSONALITY_LEN);
            } else if (strcmp(e->field, "gender") == 0) {
                safe_strcpy(target->gender, e->str_value, MAX_GENDER_LEN);
            }
        } else {
            /* Integer set */
            if (strcmp(e->field, "money") == 0) {
                target->money = e->new_value;
                if (target->money < 0) target->money = 0;
            } else if (strcmp(e->field, "player_affinity") == 0) {
                cc_set_player_affinity(target, e->new_value);
            } else if (strcmp(e->field, "attr.appearance") == 0 ||
                       strcmp(e->field, "appearance") == 0) {
                cc_set_attributes(target, e->new_value,
                    target->attr.constitution, target->attr.intelligence);
            } else if (strcmp(e->field, "attr.constitution") == 0 ||
                       strcmp(e->field, "constitution") == 0) {
                cc_set_attributes(target, target->attr.appearance,
                    e->new_value, target->attr.intelligence);
            } else if (strcmp(e->field, "attr.intelligence") == 0 ||
                       strcmp(e->field, "intelligence") == 0) {
                cc_set_attributes(target, target->attr.appearance,
                    target->attr.constitution, e->new_value);
            } else if (strcmp(e->field, "age") == 0) {
                target->age = e->new_value;
                if (target->age < 0) target->age = 0;
            } else {
                /* Check if it's a skill set */
                for (int i = 0; i < target->skill_count; i++) {
                    if (strcmp(target->skills[i].name, e->field) == 0) {
                        if (e->new_value < 0 || e->new_value > 100) {
                            log_warn("cs_apply: skill '%s' set to %d "
                                     "out of range [0,100] (will be clamped)",
                                     e->field, e->new_value);
                        }
                        cc_add_skill(target, e->field, e->new_value);
                        goto set_applied;
                    }
                }
                /* Try relation set: field = "NPC名.affinity" */
                {
                    char *dot = strchr(e->field, '.');
                    if (dot) {
                        char target_name[64];
                        int len = (int)(dot - e->field);
                        if (len >= (int)sizeof(target_name)) len = sizeof(target_name) - 1;
                        memcpy(target_name, e->field, len);
                        target_name[len] = '\0';
                        if (strcmp(dot + 1, "affinity") == 0) {
                            /* Use cc_add_relation to set (it updates if exists) */
                            cc_add_relation(target, target_name, REL_STRANGER, e->new_value);
                        }
                    }
                }
            }
        }
        set_applied:
        if (events) {
            event_push(events, tick, -1, -1, EVENT_STATE_CHANGE,
                "{\"entity\":\"%s\",\"field\":\"%s\",\"set\":%d}",
                target->name, e->field, e->new_value);
        }
        break;

    case CS_STATUS:
        if (e->status_code >= 0 && e->status_code <= STATUS_HAPPY) {
            target->status = (StatusType)e->status_code;
            if (events) {
                event_push(events, tick, -1, -1, EVENT_STATE_CHANGE,
                    "{\"entity\":\"%s\",\"status\":%d}",
                    target->name, e->status_code);
            }
        }
        break;

    case CS_CLAMP:
        /* Read current field value, then clamp to [clamp_min, clamp_max] */
        {
            int cur = 0;
            /* Reuse entity_get_int logic inline (avoid creating dependency on rule.c) */
            if (strcmp(e->field, "money") == 0) {
                cur = target->money;
            } else if (strcmp(e->field, "player_affinity") == 0) {
                cur = target->player_affinity;
            } else if (strcmp(e->field, "attr.appearance") == 0 ||
                       strcmp(e->field, "appearance") == 0) {
                cur = target->attr.appearance;
            } else if (strcmp(e->field, "attr.constitution") == 0 ||
                       strcmp(e->field, "constitution") == 0) {
                cur = target->attr.constitution;
            } else if (strcmp(e->field, "attr.intelligence") == 0 ||
                       strcmp(e->field, "intelligence") == 0) {
                cur = target->attr.intelligence;
            } else if (strcmp(e->field, "age") == 0) {
                cur = target->age;
            } else if (strcmp(e->field, "status") == 0) {
                cur = (int)target->status;
            } else {
                /* Check if it's a skill field: skill name matches a skill on the entity */
                int found = 0;
                for (int i = 0; i < target->skill_count; i++) {
                    if (strcmp(target->skills[i].name, e->field) == 0) {
                        cur = target->skills[i].level;
                        found = 1;
                        break;
                    }
                }
                if (!found) break; /* field not recognized, skip clamp */
            }
            /* Apply clamp */
            if (cur < e->clamp_min) {
                ChangeEntryFull set_e;
                memset(&set_e, 0, sizeof(set_e));
                set_e.type = CS_SET;
                safe_strcpy(set_e.entity_name, e->entity_name, sizeof(set_e.entity_name));
                safe_strcpy(set_e.field, e->field, sizeof(set_e.field));
                set_e.new_value = e->clamp_min;
                cs_apply_one(&set_e, target, events, tick);
            } else if (cur > e->clamp_max) {
                ChangeEntryFull set_e;
                memset(&set_e, 0, sizeof(set_e));
                set_e.type = CS_SET;
                safe_strcpy(set_e.entity_name, e->entity_name, sizeof(set_e.entity_name));
                safe_strcpy(set_e.field, e->field, sizeof(set_e.field));
                set_e.new_value = e->clamp_max;
                cs_apply_one(&set_e, target, events, tick);
            }
        }
        break;
    }
}

int cs_apply(const ChangeSetFull *cs, CharacterCard *player,
             CharacterCard *npcs, int npc_count,
             EventLog *events, long long tick)
{
    int applied = 0;
    for (int i = 0; i < cs->count; i++) {
        const ChangeEntryFull *e = &cs->entries[i];
        CharacterCard *target = cs_find_entity(e->entity_name, player, npcs, npc_count);
        if (target) {
            cs_apply_one(e, target, events, tick);
            applied++;
        }
    }
    return applied;
}

#include "emotion.h"
#include "character_card.h"
#include "environment.h"
#include "event.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════
   EmotionState
   ═══════════════════════════════════════════════════════════════ */

void emotion_init(EmotionState *e)
{
    if (!e) return;
    /* Start at neutral baseline with slight positive bias for happiness */
    memset(e, 0, sizeof(*e));
    e->happiness = 20;
    e->trust     = 10;
    e->anticipation = 10;
}

void emotion_update(EmotionState *e, const struct CharacterCard *npc,
                    const struct Environment *env, const struct EventLog *events)
{
    /* Phase 2 full implementation. For now: no-op to allow compilation
       with the new types. The stub preserves the interface for future use. */
    (void)e;
    (void)npc;
    (void)env;
    (void)events;
}

int emotion_export_state(const EmotionState *e, char *out, int out_size)
{
    if (!e || !out || out_size <= 0) return 0;
    return snprintf(out, out_size,
        "H:%d S:%d A:%d F:%d D:%d Su:%d T:%d An:%d",
        e->happiness, e->sadness, e->anger, e->fear,
        e->disgust, e->surprise, e->trust, e->anticipation);
}

/* ═══════════════════════════════════════════════════════════════
   NeedType
   ═══════════════════════════════════════════════════════════════ */

const char *need_type_str(NeedType t)
{
    switch (t) {
    case NEED_SURVIVAL:  return "SURVIVAL";
    case NEED_SAFETY:    return "SAFETY";
    case NEED_SOCIAL:    return "SOCIAL";
    case NEED_ESTEEM:    return "ESTEEM";
    case NEED_SELF_ACT:  return "SELF_ACT";
    default:             return "UNKNOWN";
    }
}

const char *need_type_cn(NeedType t)
{
    switch (t) {
    case NEED_SURVIVAL:  return "生存";
    case NEED_SAFETY:    return "安全";
    case NEED_SOCIAL:    return "社交";
    case NEED_ESTEEM:    return "尊重";
    case NEED_SELF_ACT:  return "自我实现";
    default:             return "未知";
    }
}

/* ═══════════════════════════════════════════════════════════════
   Need
   ═══════════════════════════════════════════════════════════════ */

void need_evaluate_all(Need needs[NEED_COUNT], const struct CharacterCard *npc,
                       const struct Environment *env)
{
    /* Phase 2 full implementation. For now: set sensible defaults. */
    (void)env;
    if (!needs || !npc) return;

    for (int i = 0; i < NEED_COUNT; i++) {
        needs[i].type = (NeedType)i;
        needs[i].urgency = 20;
        needs[i].satisfaction = 70;
        needs[i].target[0] = '\0';
    }

    /* Quick heuristic adjustments based on existing NPC state */
    if (npc->status == STATUS_HUNGRY) {
        needs[NEED_SURVIVAL].urgency = 70;
        needs[NEED_SURVIVAL].satisfaction = 20;
    }
    if (npc->money < 20) {
        needs[NEED_SAFETY].urgency = 50;
        needs[NEED_SAFETY].satisfaction = 30;
    }
}

const Need *need_select_top(const Need needs[NEED_COUNT])
{
    if (!needs) return NULL;
    const Need *best = NULL;
    int best_delta = -1;
    for (int i = 0; i < NEED_COUNT; i++) {
        int delta = needs[i].urgency - needs[i].satisfaction;
        if (delta > best_delta) {
            best_delta = delta;
            best = &needs[i];
        }
    }
    return (best_delta > 0) ? best : NULL;
}

int need_export_summary(const Need needs[NEED_COUNT], char *out, int out_size)
{
    if (!needs || !out || out_size <= 0) return 0;
    int pos = 0;
    for (int i = 0; i < NEED_COUNT; i++) {
        pos += snprintf(out + pos, out_size - pos,
            "%s(u=%d,s=%d) ",
            need_type_cn(needs[i].type),
            needs[i].urgency, needs[i].satisfaction);
        if (pos >= out_size - 1) break;
    }
    return pos;
}

/* ═══════════════════════════════════════════════════════════════
   RelationDimensions
   ═══════════════════════════════════════════════════════════════ */

void reldim_init(RelationDimensions *rd)
{
    if (rd) memset(rd, 0, sizeof(*rd));
}

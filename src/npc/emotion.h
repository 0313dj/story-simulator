#ifndef EMOTION_H
#define EMOTION_H

/* ═══════════════════════════════════════════════════════════════
   Emotion & Need System (USE v2.0, Phase 2 foundation)

   Based on:
   - OCC Model of Emotions (Ortony, Clore, Collins)
   - Maslow's Hierarchy of Needs (simplified 5-level)

   These are DATA MODEL DEFINITIONS only. The full behavior
   engine (emotion_update, need_evaluate_all, etc.) will be
   implemented in Phase 2.
   ═══════════════════════════════════════════════════════════════ */

#include <stdbool.h>

/* ── Forward declarations (struct only, no typedef to avoid conflicts) ── */
struct CharacterCard;
struct Environment;
struct EventLog;

/* ═══════════════════════════════════════════════════════════════
   EmotionState — 8 dimensions based on OCC model
   All values range [-100, 100].  0 = neutral.
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int happiness;      /* 愉悦度 */
    int sadness;        /* 悲伤 */
    int anger;          /* 愤怒 */
    int fear;           /* 恐惧 */
    int disgust;        /* 厌恶 */
    int surprise;       /* 惊讶 */
    int trust;          /* 信任 */
    int anticipation;   /* 期待 */
} EmotionState;

/* Initialize emotion to neutral baseline.
   personality_idx is a hash-based seed for slight variations. */
void emotion_init(EmotionState *e);

/* Update emotion based on environment, recent events, and NPC state.
   Called once per NPC per Tick.
   NOTE: full implementation in Phase 2. This is the interface. */
void emotion_update(EmotionState *e, const struct CharacterCard *npc,
                    const struct Environment *env, const struct EventLog *events);

/* Export emotion state for AI context (compact single-line format).
   Returns number of bytes written (excluding null terminator). */
int  emotion_export_state(const EmotionState *e, char *out, int out_size);

/* ═══════════════════════════════════════════════════════════════
   NeedType — Maslow's hierarchy, simplified to 5 levels
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    NEED_SURVIVAL,      /* 生存：饥饿、受伤、疲劳 */
    NEED_SAFETY,        /* 安全：住所、金钱、健康 */
    NEED_SOCIAL,        /* 社交：关系、归属、爱情 */
    NEED_ESTEEM,        /* 尊重：声望、成就、技能 */
    NEED_SELF_ACT,      /* 自我实现：目标、理想、成长 */
    NEED_COUNT
} NeedType;

/* Human-readable name for each need level */
const char *need_type_str(NeedType t);
const char *need_type_cn(NeedType t);

/* ═══════════════════════════════════════════════════════════════
   Need — a single need instance
   ═══════════════════════════════════════════════════════════════ */

#define NEED_MAX_TARGET_LEN 64

typedef struct {
    NeedType type;
    int urgency;        /* 0-100, higher = more urgent */
    int satisfaction;   /* 0-100, current satisfaction level */
    char target[NEED_MAX_TARGET_LEN];  /* e.g. "食物", "铁匠老王" */
} Need;

/* Evaluate all 5 needs for an NPC based on current state.
   NOTE: full implementation in Phase 2. */
void need_evaluate_all(Need needs[NEED_COUNT], const struct CharacterCard *npc,
                       const struct Environment *env);

/* Select the need with highest (urgency - satisfaction) delta.
   Returns NULL if all needs are satisfied. */
const Need *need_select_top(const Need needs[NEED_COUNT]);

/* Export needs summary for AI context. */
int  need_export_summary(const Need needs[NEED_COUNT], char *out, int out_size);

/* ═══════════════════════════════════════════════════════════════
   Goal (extended) — links a goal back to its source Need
   ═══════════════════════════════════════════════════════════════ */

#define GOAL_MAX_DESC_LEN  128
#define GOAL_MAX_TARGET_LEN 64

typedef struct {
    char description[GOAL_MAX_DESC_LEN];  /* e.g. "去市场买食物" */
    int  priority;                         /* 0-100 */
    NeedType source_need;                   /* which need spawned this goal */
    char target[GOAL_MAX_TARGET_LEN];      /* e.g. "市场", "铁匠" */
} GoalV2;

/* ═══════════════════════════════════════════════════════════════
   RelationDimensions — multi-faceted relationship (Phase 2)
   Extends the single "affinity" value with 7 dimensions.
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int friendship;     /* 友情  -100..100 */
    int trust;          /* 信任  -100..100 */
    int respect;        /* 尊重  -100..100 */
    int love;           /* 爱情  -100..100 */
    int fear;           /* 畏惧  -100..100 */
    int hate;           /* 仇恨  -100..100 */
    int obligation;     /* 义务/恩情 -100..100 */
} RelationDimensions;

/* Initialize to neutral (all zeros). */
void reldim_init(RelationDimensions *rd);

/* ═══════════════════════════════════════════════════════════════
   Emotion→Behavior rules (macros for Rule Engine integration)
   These will be used in Phase 2 to generate dynamic rules.
   ═══════════════════════════════════════════════════════════════ */

/* Example rules from the technical plan:
   IF anger > 50 AND trust < -30 THEN 拒绝交易/攻击倾向
   IF fear > 60 THEN 逃跑/躲避
   IF happiness > 70 AND trust > 50 THEN 主动帮助
*/

/* Check if an emotion exceeds a threshold */
#define EMOTION_IS(e, dim, threshold)  ((e).dim > (threshold))

/* Combined emotion checks */
#define EMOTION_IS_ANGRY(e)            EMOTION_IS(e, anger, 50)
#define EMOTION_IS_AFRAID(e)           EMOTION_IS(e, fear, 60)
#define EMOTION_IS_HAPPY(e)            EMOTION_IS(e, happiness, 70)
#define EMOTION_IS_TRUSTING(e)         EMOTION_IS(e, trust, 50)
#define EMOTION_IS_SAD(e)              EMOTION_IS(e, sadness, 50)

#endif

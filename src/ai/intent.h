#ifndef INTENT_H
#define INTENT_H

#include <stdbool.h>
#include "api.h"

/* ═══════════════════════════════════════════════════════════════
   Intent System — per USE Spec Chapter 9 (Layer 2)

   Three-level recognition:
     Level 1: Keyword pattern matching   (<1ms, ~70% accuracy)
     Level 2: Small model API (optional) (~200ms, ~90% accuracy)
     Level 3: Main model API             (~2s,  ~98% accuracy)

   The system tries Level 1 first; if confidence < threshold,
   falls back to Level 3. Level 2 is a placeholder for future
   lightweight model integration.
   ═══════════════════════════════════════════════════════════════ */

#define INTENT_MAX_TARGET_LEN   64
#define INTENT_MAX_PARAMS_LEN   512
#define INTENT_MIN_CONFIDENCE   0.5f   /* fall back to next level below this */

/* ═══════════════════════════════════════════════════════════════
   IntentType — per USE Spec Chapter 9
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    INTENT_MOVE,        /* 移动到某地点 */
    INTENT_ATTACK,      /* 攻击目标 */
    INTENT_TRADE,       /* 交易/买卖 */
    INTENT_TALK,        /* 对话/社交 */
    INTENT_REST,        /* 休息/恢复 */
    INTENT_SEARCH,      /* 搜索/调查 */
    INTENT_USE_ITEM,    /* 使用物品 */
    INTENT_CRAFT,       /* 制作/合成 */
    INTENT_OBSERVE,     /* 观察/打量 */
    INTENT_COMMAND,     /* 命令/指挥 */
    INTENT_UNKNOWN,     /* 未能识别 — fall through to general generation */
} IntentType;

/* ═══════════════════════════════════════════════════════════════
   IntentResult — recognition output
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    IntentType  type;
    char        target[INTENT_MAX_TARGET_LEN];    /* 目标对象/地点名 */
    char        parameters[INTENT_MAX_PARAMS_LEN]; /* 额外参数（原始输入） */
    float       confidence;                        /* 0.0 ~ 1.0 */
    int         level;                             /* 1/2/3 — which level succeeded */
} IntentResult;

/* ═══════════════════════════════════════════════════════════════
   Intent API
   ═══════════════════════════════════════════════════════════════ */

/* Get the Chinese name of an intent type */
const char *intent_type_str(IntentType t);

/* Get a short action verb for the intent type (for plan steps) */
const char *intent_action_verb(IntentType t);

/* ── Level 1: Keyword pattern matching ── */

/* Fast keyword-based intent recognition.
   Returns confidence >= 0.5 if a match was found, < 0.5 otherwise.
   Always fills the result struct with the best guess. */
float intent_recognize_level1(const char *user_input, const char *known_names,
                               IntentResult *result);

/* ── Level 3: Main model API ── */

/* Use the main AI model to recognise intent.
   Returns true on success, false on API error.
   result->level is set to 3 on success. */
bool intent_recognize_level3(ApiClient *api, const char *user_input,
                              const char *game_context, IntentResult *result);

/* ── Three-level cascade ── */

/* Full three-level intent recognition.
   Tries Level 1 first; if confidence < threshold, falls back to Level 3.
   The `known_names` parameter is a string of known entity/location names
   (comma-separated) used by Level 1 for target extraction.
   The `game_context` parameter is a brief game state summary for Level 3.
   Returns the final IntentResult with the level that succeeded. */
IntentResult intent_recognize(ApiClient *api, const char *user_input,
                               const char *known_names, const char *game_context);

#endif

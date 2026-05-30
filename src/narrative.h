#ifndef NARRATIVE_H
#define NARRATIVE_H

#include <stdbool.h>
#include "api.h"

/* ═══════════════════════════════════════════════════════════════
   Narrative Module — per USE Spec Chapter 10 (Layer 3)

   PURE TEXT GENERATION. This module MUST NOT modify any game state.
   Input:  WorldResult  (events + changes + context)
   Output: NarrativeText (text + style)

   Architecture constraint:
   - Receives all data via parameters (no global access)
   - Returns only text — never calls state-changing functions
   - Stateless: can be called multiple times without side effects
   ═══════════════════════════════════════════════════════════════ */

#define NARR_MAX_TEXT_LEN   4096
#define NARR_MAX_STYLE_LEN  32
#define NARR_MAX_CTX_LEN    4096
#define NARR_MAX_SUMMARY_LEN 1024

/* ═══════════════════════════════════════════════════════════════
   NarrativeStyle — output style tag
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    NSTYLE_AUTO,       /* AI decides based on context */
    NSTYLE_DRAMATIC,   /* 戏剧化 */
    NSTYLE_CASUAL,     /* 日常 */
    NSTYLE_COMBAT,     /* 战斗 */
    NSTYLE_MYSTERY,    /* 悬疑 */
    NSTYLE_ROMANCE,    /* 浪漫 */
    NSTYLE_HORROR,     /* 恐怖 */
    NSTYLE_HUMOR,      /* 幽默 */
} NarrativeStyle;

/* ═══════════════════════════════════════════════════════════════
   WorldResult — input to narrative generation
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    /* Summary of events that occurred (change descriptions) */
    char events_summary[NARR_MAX_SUMMARY_LEN];

    /* Game context snapshot (time, location, weather, present NPCs) */
    char context[NARR_MAX_CTX_LEN];

    /* The user's original input (for reference) */
    char user_input[512];

    /* Recognised intent (for tonal guidance) */
    char intent_type[32];      /* e.g. "TALK", "ATTACK", "MOVE" */
    char intent_target[64];    /* target entity/location */
    float intent_confidence;

    /* The execution plan steps that were completed */
    char plan_summary[512];    /* brief description of what was planned */

    /* Output style preference (NSTYLE_AUTO = AI decides) */
    NarrativeStyle style;
} WorldResult;

/* ═══════════════════════════════════════════════════════════════
   NarrativeText — output from narrative generation
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char text[NARR_MAX_TEXT_LEN];    /* narrative/dialogue text */
    char style[NARR_MAX_STYLE_LEN];  /* style tag (e.g. "dramatic", "casual") */
} NarrativeText;

/* ═══════════════════════════════════════════════════════════════
   Narrative API
   ═══════════════════════════════════════════════════════════════ */

/* Initialise an empty WorldResult */
void wr_init(WorldResult *wr);

/* Initialise an empty NarrativeText */
void nt_init(NarrativeText *nt);

/* Get the canonical narrative System Prompt.
   This is the authoritative prompt for all narrative generation.
   Returns a statically-allocated string (do not free). */
const char *narrative_get_system_prompt(void);

/* Get the style instruction string for a given style */
const char *narrative_style_str(NarrativeStyle s);

/* ── Narrative Generation ── */

/* Generate narrative text from a WorldResult.
   Calls the AI with the narrative System Prompt.
   The `game_state_snapshot` is the full game state text used for context.
   Returns true on success; NarrativeText is filled with the result. */
bool narrative_generate(ApiClient *api, const WorldResult *wr,
                         const char *game_state_snapshot,
                         NarrativeText *nt);

#endif

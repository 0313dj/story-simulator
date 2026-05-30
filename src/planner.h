#ifndef PLANNER_H
#define PLANNER_H

#include <stdbool.h>
#include "api.h"
#include "intent.h"

/* ═══════════════════════════════════════════════════════════════
   Planner — per USE Spec Chapter 9 (Layer 2)

   Takes an intent + game state and generates a step-by-step plan.
   The plan is AI-generated (main model API call).
   ═══════════════════════════════════════════════════════════════ */

#define PLAN_MAX_STEPS      16
#define PLAN_MAX_GOAL_LEN   256
#define PLAN_MAX_ACTION_LEN 64
#define PLAN_MAX_TARGET_LEN 64

/* ═══════════════════════════════════════════════════════════════
   PlanStep — a single step in a plan
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char action[PLAN_MAX_ACTION_LEN];      /* 动作描述（如 "与铁匠交谈"） */
    char target[PLAN_MAX_TARGET_LEN];      /* 目标对象/地点 */
    int  estimated_ticks;                  /* 预估耗时（分钟） */
    char action_type[32];                  /* 动作类型标签 */
} PlanStep;

/* ═══════════════════════════════════════════════════════════════
   Plan — the complete execution plan
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char   goal[PLAN_MAX_GOAL_LEN];        /* 目标描述 */
    PlanStep steps[PLAN_MAX_STEPS];        /* 步骤列表 */
    int    step_count;                     /* 步骤数 */
    int    current_step;                   /* 当前执行到的步骤 (0-based) */
    bool   complete;                       /* 是否已完成 */
} Plan;

/* ═══════════════════════════════════════════════════════════════
   Planner API
   ═══════════════════════════════════════════════════════════════ */

/* Initialise an empty plan */
void plan_init(Plan *plan);

/* Get a plan step as a readable string */
int plan_step_str(const PlanStep *step, char *out, int out_size);

/* Export the full plan as a readable string (for AI context) */
int plan_export(const Plan *plan, char *out, int out_size);

/* Generate a plan using the main AI model.
   Takes the user input, recognised intent, and game state snapshot.
   Returns true on success. The plan is filled with steps. */
bool planner_generate(ApiClient *api, const char *user_input,
                      const IntentResult *intent,
                      const char *game_state, Plan *plan);

#endif

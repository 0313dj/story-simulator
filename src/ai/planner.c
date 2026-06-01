#include "log.h"
#include "planner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Plan utilities
   ═══════════════════════════════════════════════════════════════ */

void plan_init(Plan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->current_step = 0;
    plan->complete = false;
}

int plan_step_str(const PlanStep *step, char *out, int out_size)
{
    return snprintf(out, out_size, "%s → %s (约%d分钟)",
        step->action, step->target[0] ? step->target : "—",
        step->estimated_ticks);
}

int plan_export(const Plan *plan, char *out, int out_size)
{
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos,
        "目标: %s\n步骤数: %d\n",
        plan->goal[0] ? plan->goal : "(未设定)",
        plan->step_count);

    for (int i = 0; i < plan->step_count; i++) {
        pos += snprintf(out + pos, out_size - pos,
            "  步骤%d: %s → %s (约%d分钟)\n",
            i + 1,
            plan->steps[i].action,
            plan->steps[i].target[0] ? plan->steps[i].target : "—",
            plan->steps[i].estimated_ticks);
    }

    if (plan->complete) {
        pos += snprintf(out + pos, out_size - pos, "(计划已完成)");
    } else if (plan->step_count > 0) {
        pos += snprintf(out + pos, out_size - pos,
            "当前步骤: %d/%d", plan->current_step + 1, plan->step_count);
    }

    return pos;
}

/* ═══════════════════════════════════════════════════════════════
   Parse PLAN tag from AI response
   Expected format:
     GOAL: <goal text>
     STEPS:
     1. <action> | <target> | <minutes>
     2. ...
   ═══════════════════════════════════════════════════════════════ */

static void plan_parse_response(const char *raw, Plan *plan)
{
    if (!raw || !*raw) return;

    /* Parse GOAL */
    {
        const char *p = strstr(raw, "GOAL:");
        if (!p) p = strstr(raw, "目标:");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\n' || *p == '\r') p++;
                const char *end = strchr(p, '\n');
                int len = end ? (int)(end - p) : (int)strlen(p);
                if (len > PLAN_MAX_GOAL_LEN - 1) len = PLAN_MAX_GOAL_LEN - 1;
                memcpy(plan->goal, p, len);
                plan->goal[len] = '\0';
                /* Trim trailing \r */
                char *cr = strchr(plan->goal, '\r');
                if (cr) *cr = '\0';
            }
        }
    }

    /* Parse STEPS */
    {
        const char *p = strstr(raw, "STEPS:");
        if (!p) p = strstr(raw, "步骤:");
        if (p) {
            p = strchr(p, '\n');
            if (p) {
                p++; /* skip newline after STEPS: */
                while (*p && plan->step_count < PLAN_MAX_STEPS) {
                    /* Skip leading whitespace */
                    while (*p == ' ' || *p == '\r' || *p == '\n') p++;
                    if (!*p) break;

                    /* Look for numbered step: "1. " or "1, " or "- " */
                    if ((*p >= '0' && *p <= '9') || *p == '-') {
                        /* Skip past "1. " or "- " */
                        if (*p >= '0' && *p <= '9') {
                            while (*p >= '0' && *p <= '9') p++;
                            if (*p == '.' || *p == ',' || *p == ')') p++;
                            while (*p == ' ') p++;
                        } else if (*p == '-') {
                            p++;
                            while (*p == ' ') p++;
                        }

                        PlanStep *step = &plan->steps[plan->step_count];
                        memset(step, 0, sizeof(*step));

                        /* Parse: action | target | minutes
                           or: action → target (约X分钟)
                           or: action (plain text without separators) */
                        const char *line_end = strchr(p, '\n');
                        if (!line_end) line_end = p + strlen(p);
                        char line[256];
                        int line_len = (int)(line_end - p);
                        if (line_len > (int)sizeof(line) - 1)
                            line_len = sizeof(line) - 1;
                        memcpy(line, p, line_len);
                        line[line_len] = '\0';

                        /* Trim \r */
                        char *cr = strchr(line, '\r');
                        if (cr) *cr = '\0';

                        /* Try | separator */
                        char *sep1 = strchr(line, '|');
                        char *arrow = strstr(line, "→");
                        char *cn_arrow = strstr(line, "->");

                        if (sep1) {
                            /* Format: action | target | minutes */
                            *sep1 = '\0';
                            char *act = line;
                            while (*act == ' ') act++;
                            char *act_end = act + strlen(act) - 1;
                            while (act_end > act && *act_end == ' ') *act_end-- = '\0';
                            safe_strcpy(step->action, act, PLAN_MAX_ACTION_LEN);

                            char *tgt = sep1 + 1;
                            while (*tgt == ' ') tgt++;
                            char *sep2 = strchr(tgt, '|');
                            if (sep2) {
                                *sep2 = '\0';
                                char *tgt_end = tgt + strlen(tgt) - 1;
                                while (tgt_end > tgt && *tgt_end == ' ') *tgt_end-- = '\0';
                                safe_strcpy(step->target, tgt, PLAN_MAX_TARGET_LEN);
                                step->estimated_ticks = atoi(sep2 + 1);
                            } else {
                                safe_strcpy(step->target, tgt, PLAN_MAX_TARGET_LEN);
                            }
                        } else if (arrow || cn_arrow) {
                            /* Format: action → target (约X分钟) or action -> target.
                               If both symbols appear, pick whichever comes first. */
                            int arrow_len;
                            char *arrow_pos;
                            if (arrow && cn_arrow) {
                                if (arrow < cn_arrow)
                                    { arrow_pos = arrow; arrow_len = 3; }
                                else
                                    { arrow_pos = cn_arrow; arrow_len = 2; }
                            } else if (arrow) {
                                arrow_pos = arrow; arrow_len = 3;
                            } else {
                                arrow_pos = cn_arrow; arrow_len = 2;
                            }
                            *arrow_pos = '\0';
                            char *act = line;
                            while (*act == ' ') act++;
                            char *act_end = act + strlen(act) - 1;
                            while (act_end > act && *act_end == ' ') *act_end-- = '\0';
                            safe_strcpy(step->action, act, PLAN_MAX_ACTION_LEN);

                            char *tgt = arrow_pos + arrow_len;
                            while (*tgt == ' ') tgt++;
                            /* Check for "(约X分钟)" suffix */
                            char *paren = strchr(tgt, '(');
                            if (paren) {
                                *(paren - 1) = '\0'; /* trim space before paren */
                                /* Extract minutes */
                                sscanf(paren, "(%*[^0-9]%d", &step->estimated_ticks);
                            }
                            char *tgt_end = tgt + strlen(tgt) - 1;
                            while (tgt_end > tgt && *tgt_end == ' ') *tgt_end-- = '\0';
                            safe_strcpy(step->target, tgt, PLAN_MAX_TARGET_LEN);
                        } else {
                            /* Plain text: use whole line as action */
                            safe_strcpy(step->action, line, PLAN_MAX_ACTION_LEN);
                        }

                        if (step->action[0]) {
                            plan->step_count++;
                        }

                        p = line_end;
                        if (*p) p++;
                    } else {
                        /* Not a step line — skip */
                        const char *nl = strchr(p, '\n');
                        p = nl ? nl + 1 : p + strlen(p);
                    }
                }
            }
        }
    }

    /* If GOAL wasn't found but we have steps, create a default goal */
    if (!plan->goal[0] && plan->step_count > 0) {
        snprintf(plan->goal, sizeof(plan->goal),
            "执行 %d 步计划", plan->step_count);
    }
}

/* ═══════════════════════════════════════════════════════════════
   AI-driven plan generation
   ═══════════════════════════════════════════════════════════════ */

bool planner_generate(ApiClient *api, const char *user_input,
                      const IntentResult *intent,
                      const char *game_state, Plan *plan)
{
    plan_init(plan);

    if (!api || !user_input || !user_input[0]) return false;

    const char *intent_str = intent ? intent_type_str(intent->type) : "UNKNOWN";
    const char *target_str = (intent && intent->target[0]) ? intent->target : "(未指定)";
    float conf = intent ? intent->confidence : 0.0f;

    const char *sys =
        "你是一个游戏计划生成器(Planner)。根据玩家意图和游戏状态，生成一个可实现的分步计划。\n\n"
        "输出格式（严格遵循）：\n"
        "GOAL: <目标文本，一段话描述玩家想要达成的目标>\n\n"
        "STEPS:\n"
        "1. <动作> | <对象> | <分钟数>\n"
        "2. <动作> | <对象> | <分钟数>\n"
        "...\n\n"
        "规则：\n"
        "- 步骤数控制在1-5步，复杂任务可到8步\n"
        "- 每步必须包含：动作描述 | 对象/地点 | 预估耗时(分钟)\n"
        "- 动作必须基于当前游戏状态中可用的信息\n"
        "- 考虑地点、时间、在场NPC等因素\n"
        "- 所有内容使用中文\n"
        "- 如果意图是UNKNOWN，请推断最合理的行动\n"
        "- 预估耗时需合理：对话1-5分钟，移动根据距离10-120分钟，交易5-15分钟，等等";

    char prompt[8192];
    snprintf(prompt, sizeof(prompt),
        "玩家输入: %s\n\n"
        "识别到的意图: %s (置信度: %.0f%%)\n"
        "意图目标: %s\n\n"
        "游戏状态:\n%s\n\n"
        "请生成计划。",
        user_input, intent_str, conf * 100.0f, target_str,
        game_state ? game_state : "(无)");

    char raw[4096];
    memset(raw, 0, sizeof(raw));
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 2048)) {
        return false;
    }

    plan_parse_response(raw, plan);

    /* If still no steps, create a single-step plan from the intent */
    if (plan->step_count == 0) {
        if (!plan->goal[0]) {
            snprintf(plan->goal, sizeof(plan->goal), "%s",
                user_input);
        }
        PlanStep *step = &plan->steps[0];
        snprintf(step->action, sizeof(step->action), "%s %s",
            intent_action_verb(intent ? intent->type : INTENT_UNKNOWN),
            target_str);
        if (intent && intent->target[0]) {
            safe_strcpy(step->target, intent->target, PLAN_MAX_TARGET_LEN);
        }
        step->estimated_ticks = 5; /* default 5 minutes */
        plan->step_count = 1;
    }

    return plan->step_count > 0;
}

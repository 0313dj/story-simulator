#include "narrative.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Initialisation
   ═══════════════════════════════════════════════════════════════ */

void wr_init(WorldResult *wr)
{
    memset(wr, 0, sizeof(*wr));
    wr->style = NSTYLE_AUTO;
}

void nt_init(NarrativeText *nt)
{
    memset(nt, 0, sizeof(*nt));
}

/* ═══════════════════════════════════════════════════════════════
   Style string
   ═══════════════════════════════════════════════════════════════ */

const char *narrative_style_str(NarrativeStyle s)
{
    switch (s) {
    case NSTYLE_AUTO:     return "auto";
    case NSTYLE_DRAMATIC: return "dramatic";
    case NSTYLE_CASUAL:   return "casual";
    case NSTYLE_COMBAT:   return "combat";
    case NSTYLE_MYSTERY:  return "mystery";
    case NSTYLE_ROMANCE:  return "romance";
    case NSTYLE_HORROR:   return "horror";
    case NSTYLE_HUMOR:    return "humor";
    default:              return "auto";
    }
}

/* ═══════════════════════════════════════════════════════════════
   Canonical Narrative System Prompt — moved from api.c (Phase 4)

   This prompt governs ALL narrative text generation.
   It defines the tone, style, and output format for the AI narrator.
   ═══════════════════════════════════════════════════════════════ */

const char *narrative_get_system_prompt(void)
{
    return
        "你是一个沉浸式角色扮演游戏的叙事引擎。你负责扮演游戏世界中的所有NPC角色，\n"
        "描述场景变化，根据玩家的行动做出合理的剧情裁决。\n"
        "所有地名、人名、对话必须使用中文，不得使用英文名。\n\n"
        "[对话格式]\n"
        "当NPC对玩家说话时，必须以\"NPC名: 对话内容\"的格式输出。\n"
        "若是场景描述、旁白或动作叙述，则直接叙述，不加角色名前缀。\n"
        "你可以在一次回复中交替使用NPC对话和场景描述。\n\n"
        "[行为准则]\n"
        "- 严格依据下方提供的游戏状态信息进行裁决，不得凭空编造\n"
        "- NPC的行为必须与其身份、性格、当前状态一致\n"
        "- 注意时间、天气对场景的影响（深夜不应出现热闹集市，暴雨天路人稀少）\n"
        "- 技能检定：玩家使用技能时，根据技能等级(0-100)判断成败。高等级(70+)大概率成功\n"
        "- NPC好感度变化应有理有据，一次交互变化不宜超过±10\n"
        "- 新地点的名称应与当前时代背景一致\n\n"
        "[叙事原则]\n"
        "- 叙事应生动、具体、有画面感\n"
        "- 根据场景氛围选择合适的叙事节奏（紧张场景短句快节奏，日常场景可细腻描写）\n"
        "- 对话应符合NPC的身份、性格、受教育程度\n"
        "- 五感描写：适当加入视觉、听觉、嗅觉、触觉、味觉细节\n"
        "- 留白：不要把所有信息都写出来，给玩家想象空间\n"
        "- 连续性：叙事应与之前的记忆和事件保持连贯\n\n"
        "[禁止事项]\n"
        "- 禁止代控玩家角色（不要说玩家做了什么决定或说了什么话）\n"
        "- 禁止修改游戏状态（此模块只负责文本输出）\n"
        "- 禁止输出任何标签格式（TEXT:, CHANGES:等）——只输出纯叙事文本\n"
        "- 禁止使用英文名\n\n"
        "─── 以下是当前游戏状态与事件信息，请据此生成叙事 ───";
}

/* ── Refinement prompt (shorter, for polishing existing text) ── */

const char *narrative_get_refinement_prompt(void)
{
    return
        "你是一个叙事润色引擎。请对以下游戏叙事进行润色，使其更生动、更有画面感。\n"
        "保持原有的信息完整和NPC对话内容，只改进文笔和细节描写。\n"
        "只输出润色后的文本，不要添加任何其他内容。";
}

/* ═══════════════════════════════════════════════════════════════
   Narrative Generation
   ═══════════════════════════════════════════════════════════════ */

bool narrative_generate(ApiClient *api, const WorldResult *wr,
                         const char *game_state_snapshot,
                         NarrativeText *nt)
{
    nt_init(nt);

    if (!api || !wr || !game_state_snapshot) return false;

    /* Build the narrative prompt with all available context */
    char prompt[24576];
    int pos = 0;

    /* User input */
    if (wr->user_input[0]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "玩家行动：%s\n\n", wr->user_input);
    }

    /* Intent context */
    if (wr->intent_type[0]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "玩家意图：%s (置信度: %.0f%%)",
            wr->intent_type, wr->intent_confidence * 100.0f);
        if (wr->intent_target[0]) {
            pos += snprintf(prompt + pos, sizeof(prompt) - pos,
                " → %s", wr->intent_target);
        }
        pos += snprintf(prompt + pos, sizeof(prompt) - pos, "\n");
    }

    /* Plan summary */
    if (wr->plan_summary[0]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "执行计划：%s\n", wr->plan_summary);
    }

    /* Events / changes summary */
    if (wr->events_summary[0]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\n发生的事件：\n%s\n", wr->events_summary);
    }

    /* Style instruction */
    if (wr->style != NSTYLE_AUTO) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\n叙事风格要求：%s\n", narrative_style_str(wr->style));
    }

    /* Game state snapshot */
    if (game_state_snapshot[0]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\n游戏状态：\n%s\n", game_state_snapshot);
    }

    /* Context */
    if (wr->context[0]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "\n场景上下文：\n%s\n", wr->context);
    }

    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
        "\n请生成叙事文本。");

    char raw[NARR_MAX_TEXT_LEN];
    memset(raw, 0, sizeof(raw));

    const char *sys = narrative_get_system_prompt();
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 2048)) {
        return false;
    }

    /* Extract clean narrative text (strip any leftover tag markers) */
    {
        char *p = raw;
        /* If AI accidentally prepended "TEXT:", remove it */
        if (strncmp(p, "TEXT:", 5) == 0) {
            p += 5;
            while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        }
        /* Trim trailing whitespace */
        char *end = p + strlen(p) - 1;
        while (end > p && (*end == '\n' || *end == '\r' || *end == ' '))
            *end-- = '\0';

        strncpy(nt->text, p, NARR_MAX_TEXT_LEN - 1);
        nt->text[NARR_MAX_TEXT_LEN - 1] = '\0';
    }

    /* Set style tag */
    if (wr->style != NSTYLE_AUTO) {
        strncpy(nt->style, narrative_style_str(wr->style), NARR_MAX_STYLE_LEN - 1);
    } else {
        strncpy(nt->style, "generated", NARR_MAX_STYLE_LEN - 1);
    }

    return nt->text[0] != '\0';
}

/* ── Narrative Refinement ── */

bool narrative_refine(ApiClient *api, const WorldResult *wr,
                       const char *current_text, NarrativeText *nt)
{
    nt_init(nt);

    if (!api || !current_text || !current_text[0]) return false;

    char prompt[16384];
    int pos = 0;

    /* Style context */
    if (wr && wr->style != NSTYLE_AUTO) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "叙事风格：%s\n\n", narrative_style_str(wr->style));
    }

    /* Context for refinement */
    if (wr && wr->context[0]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos,
            "场景背景：%s\n\n", wr->context);
    }

    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
        "原文：\n%s\n\n"
        "请润色以上叙事文本，使其更生动、更有画面感、更符合角色性格。"
        "保持原有的信息完整和NPC对话内容，只改进文笔和细节描写。"
        "只输出润色后的文本。", current_text);

    char raw[NARR_MAX_TEXT_LEN];
    memset(raw, 0, sizeof(raw));

    const char *sys = narrative_get_refinement_prompt();
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 1024)) {
        return false;
    }

    /* Clean output */
    char *p = raw;
    char *end = p + strlen(p) - 1;
    while (end > p && (*end == '\n' || *end == '\r' || *end == ' '))
        *end-- = '\0';

    strncpy(nt->text, p, NARR_MAX_TEXT_LEN - 1);
    nt->text[NARR_MAX_TEXT_LEN - 1] = '\0';

    if (wr && wr->style != NSTYLE_AUTO) {
        strncpy(nt->style, narrative_style_str(wr->style), NARR_MAX_STYLE_LEN - 1);
    } else {
        strncpy(nt->style, "refined", NARR_MAX_STYLE_LEN - 1);
    }

    return nt->text[0] != '\0';
}

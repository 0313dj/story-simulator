#include "log.h"
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
        "- NPC的行为和对话必须与其身份、性格、当前状态严格一致\n"
        "- NPC出现在与其身份相符的地点\n"
        "- 必须根据游戏中的当前时间决定场景氛围和NPC行为\n"
        "- 注意天气对场景的影响\n"
        "- 技能检定：玩家使用技能时，根据技能等级(0-100)判断成败。高等级(70+)大概率成功\n"
        "- NPC好感度变化应有理有据，一次交互变化不宜超过±10\n"
        "- 所有场景描写、NPC对话、事件发展必须严格符合当前时代背景\n"
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
        "- 禁止使用英文名\n"
        "- 如果用户输入以『(续前场景)』开头，表示当前地点环境已在上一轮叙事中详细\n"
        "  描写过（光线、气味、建筑、氛围等），你必须直接延续剧情，不得重复描写环境。\n"
        "  重点放在NPC对话、动作推进、玩家互动上，仅当场景发生明显变化时才用一两句话说明。\n\n"
        "─── 以下是当前游戏状态与事件信息，请据此生成叙事 ───";
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

    /* Build the narrative prompt with all available context.
       Bug #35: use dynamic allocation instead of fixed 24576-byte stack buffer
       to prevent truncation with large game state snapshots. */
    /* Estimate needed size: sum of all input sections + overhead */
    int est = 512; /* base overhead */
    if (wr->user_input[0])      est += (int)strlen(wr->user_input) + 64;
    if (wr->intent_type[0])     est += (int)strlen(wr->intent_type) + 128;
    if (wr->intent_target[0])   est += (int)strlen(wr->intent_target) + 32;
    if (wr->plan_summary[0])    est += (int)strlen(wr->plan_summary) + 64;
    if (wr->events_summary[0])  est += (int)strlen(wr->events_summary) + 64;
    if (wr->style != NSTYLE_AUTO) est += 128;
    if (game_state_snapshot[0]) est += (int)strlen(game_state_snapshot) + 64;
    if (wr->context[0])         est += (int)strlen(wr->context) + 64;

    /* Clamp to reasonable bounds: min 8KB, max 128KB */
    if (est < 8192) est = 8192;
    if (est > 131072) est = 131072;

    char *prompt = (char *)malloc(est);
    if (!prompt) {
        LOG_E("narrative: malloc(%d) failed for prompt buffer", est);
        return false;
    }
    int pos = 0;

    /* User input */
    if (wr->user_input[0]) {
        pos += snprintf(prompt + pos, est - pos,
            "玩家行动：%s\n\n", wr->user_input);
    }

    /* Intent context */
    if (wr->intent_type[0]) {
        pos += snprintf(prompt + pos, est - pos,
            "玩家意图：%s (置信度: %.0f%%)",
            wr->intent_type, wr->intent_confidence * 100.0f);
        if (wr->intent_target[0]) {
            pos += snprintf(prompt + pos, est - pos,
                " → %s", wr->intent_target);
        }
        pos += snprintf(prompt + pos, est - pos, "\n");
    }

    /* Plan summary */
    if (wr->plan_summary[0]) {
        pos += snprintf(prompt + pos, est - pos,
            "执行计划：%s\n", wr->plan_summary);
    }

    /* Events / changes summary */
    if (wr->events_summary[0]) {
        pos += snprintf(prompt + pos, est - pos,
            "\n发生的事件：\n%s\n", wr->events_summary);
    }

    /* Style instruction */
    if (wr->style != NSTYLE_AUTO) {
        pos += snprintf(prompt + pos, est - pos,
            "\n叙事风格要求：%s\n", narrative_style_str(wr->style));
    }

    /* Game state snapshot */
    if (game_state_snapshot[0]) {
        pos += snprintf(prompt + pos, est - pos,
            "\n游戏状态：\n%s\n", game_state_snapshot);
    }

    /* Context */
    if (wr->context[0]) {
        pos += snprintf(prompt + pos, est - pos,
            "\n场景上下文：\n%s\n", wr->context);
    }

    pos += snprintf(prompt + pos, est - pos,
        "\n请生成叙事文本。");

    if (pos >= est) {
        LOG_W("narrative: prompt truncated (%d >= %d)", pos, est);
    }

    char raw[NARR_MAX_TEXT_LEN];
    memset(raw, 0, sizeof(raw));

    const char *sys = narrative_get_system_prompt();
    bool ok = api_chat(api, sys, prompt, raw, sizeof(raw), 2048);
    free(prompt);
    if (!ok) {
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

        /* Bug #36: detect truncation of narrative text.
           NARR_MAX_TEXT_LEN (4096) may be insufficient for long AI responses. */
        int src_len = (int)strlen(p);
        if (src_len >= NARR_MAX_TEXT_LEN) {
            LOG_W("narrative: text truncated (%d bytes, max %d)", src_len, NARR_MAX_TEXT_LEN);
        }
        strncpy(nt->text, p, NARR_MAX_TEXT_LEN - 1);
        nt->text[NARR_MAX_TEXT_LEN - 1] = '\0';
    }

    /* Set style tag */
    if (wr->style != NSTYLE_AUTO) {
        safe_strcpy(nt->style, narrative_style_str(wr->style), NARR_MAX_STYLE_LEN);
    } else {
        safe_strcpy(nt->style, "generated", NARR_MAX_STYLE_LEN);
    }

    return nt->text[0] != '\0';
}

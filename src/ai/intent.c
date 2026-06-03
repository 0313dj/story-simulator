#include "intent.h"
#include "json.h"
#include "log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Intent string conversions
   ═══════════════════════════════════════════════════════════════ */

const char *intent_type_str(IntentType t)
{
    switch (t) {
    case INTENT_MOVE:      return "MOVE";
    case INTENT_ATTACK:    return "ATTACK";
    case INTENT_TRADE:     return "TRADE";
    case INTENT_TALK:      return "TALK";
    case INTENT_REST:      return "REST";
    case INTENT_SEARCH:    return "SEARCH";
    case INTENT_USE_ITEM:  return "USE_ITEM";
    case INTENT_CRAFT:     return "CRAFT";
    case INTENT_OBSERVE:   return "OBSERVE";
    case INTENT_COMMAND:   return "COMMAND";
    case INTENT_UNKNOWN:   return "UNKNOWN";
    default:               return "UNKNOWN";
    }
}

const char *intent_action_verb(IntentType t)
{
    switch (t) {
    case INTENT_MOVE:      return "移动到";
    case INTENT_ATTACK:    return "攻击";
    case INTENT_TRADE:     return "交易";
    case INTENT_TALK:      return "与...交谈";
    case INTENT_REST:      return "休息";
    case INTENT_SEARCH:    return "搜索";
    case INTENT_USE_ITEM:  return "使用";
    case INTENT_CRAFT:     return "制作";
    case INTENT_OBSERVE:   return "观察";
    case INTENT_COMMAND:   return "命令";
    case INTENT_UNKNOWN:   return "执行";
    default:               return "执行";
    }
}

/* ═══════════════════════════════════════════════════════════════
   Level 1: Keyword pattern matching
   ═══════════════════════════════════════════════════════════════ */

/* ── Keyword → Intent mapping ── */
typedef struct {
    const char *keywords[8];   /* up to 8 keyword patterns */
    int         kw_count;
    IntentType  intent;
} KeywordRule;

/* Each rule has a set of Chinese keyword patterns.
   The more keywords matched, the higher the confidence. */
static const KeywordRule g_keyword_rules[] = {
    /* MOVE */
    {{"去", "走", "前往", "移动", "出发", "离开", "旅行", "赶路"}, 8, INTENT_MOVE},
    /* ATTACK */
    {{"攻击", "打", "战斗", "杀", "干掉", "揍", "搏斗", "袭击"}, 8, INTENT_ATTACK},
    /* TRADE */
    {{"买", "卖", "交易", "换", "购买", "出售", "交换", "付钱"}, 8, INTENT_TRADE},
    /* TALK */
    {{"说", "聊", "问", "告诉", "对话", "交谈", "讲话", "打招呼"}, 8, INTENT_TALK},
    /* REST */
    {{"休息", "睡觉", "歇", "打盹", "小睡", "睡一觉"}, 6, INTENT_REST},
    /* SEARCH */
    {{"找", "搜索", "查看", "检查", "调查", "搜寻", "翻", "寻"}, 8, INTENT_SEARCH},
    /* USE_ITEM */
    {{"使用", "用", "饮用", "喝", "吃", "装备", "穿上", "拿起"}, 8, INTENT_USE_ITEM},
    /* CRAFT */
    {{"制作", "合成", "打造", "制造", "锻造", "做", "创造", "炼制"}, 8, INTENT_CRAFT},
    /* OBSERVE */
    {{"观察", "看", "打量", "端详", "审视", "查看", "环顾", "望"}, 8, INTENT_OBSERVE},
    /* COMMAND */
    {{"命令", "下令", "指挥", "派遣", "让", "叫", "吩咐", "指示"}, 8, INTENT_COMMAND},
};

#define NUM_KEYWORD_RULES \
    (sizeof(g_keyword_rules) / sizeof(g_keyword_rules[0]))

/* ── Check if a keyword appears as a standalone word in the input ── */
static bool keyword_matches(const char *input, const char *kw)
{
    if (!input || !kw || !*kw) return false;
    const char *p = input;
    int kw_len = (int)strlen(kw);

    /* For single-char keywords, be more strict to avoid false positives */
    if (kw_len <= 1) {
        /* Single-char keywords like "去", "走", "看", "用", "打", "说", "找",
           "买", "卖", "换", "歇", "喝", "吃", "做", "让", "叫", "望", "翻"
           — these are common and need context. We match them but assign
           lower confidence unless combined with a target. */
        while (*p) {
            /* Match: the single char appears anywhere */
            if (strncmp(p, kw, kw_len) == 0) {
                return true;
            }
            p++;
        }
        return false;
    }

    /* For multi-char keywords, match as substring */
    return strstr(input, kw) != NULL;
}

/* ── Extract target name from input by matching against known names ── */
static bool extract_target(const char *input, const char *known_names,
                           char *target, int target_size)
{
    if (!input || !known_names || !target) return false;

    /* Try each known name */
    char names_buf[1024];
    strncpy(names_buf, known_names, sizeof(names_buf) - 1);
    names_buf[sizeof(names_buf) - 1] = '\0';

    char *name = names_buf;
    /* Manual tokenization by comma */
    while (name && *name) {
        while (*name == ' ' || *name == ',') name++;
        if (!*name) break;

        char *comma = strchr(name, ',');
        int len = comma ? (int)(comma - name) : (int)strlen(name);
        if (len > 0) {
            char single[64];
            if (len >= (int)sizeof(single)) len = sizeof(single) - 1;
            memcpy(single, name, len);
            single[len] = '\0';

            if (strstr(input, single)) {
                strncpy(target, single, target_size - 1);
                target[target_size - 1] = '\0';
                return true;
            }
        }
        name = comma ? comma + 1 : NULL;
    }
    return false;
}

/* ── Main Level 1 recognizer ── */

float intent_recognize_level1(const char *user_input, const char *known_names,
                               IntentResult *result)
{
    memset(result, 0, sizeof(*result));
    result->type = INTENT_UNKNOWN;
    result->confidence = 0.0f;
    result->level = 1;

    if (!user_input || !*user_input) return 0.0f;

    /* Store original input in parameters */
    safe_strcpy(result->parameters, user_input, INTENT_MAX_PARAMS_LEN);

    /* Score each intent rule */
    int best_idx = -1;
    float best_score = 0.0f;

    for (int i = 0; i < (int)NUM_KEYWORD_RULES; i++) {
        const KeywordRule *rule = &g_keyword_rules[i];
        int matches = 0;

        for (int j = 0; j < rule->kw_count; j++) {
            if (keyword_matches(user_input, rule->keywords[j])) {
                matches++;
            }
        }

        if (matches > 0) {
            /* Score: base 0.5 + 0.1 per additional match, capped at 0.95 */
            float score = 0.5f + (matches - 1) * 0.1f;
            if (score > 0.95f) score = 0.95f;

            /* Penalty for single-char keyword matches without target */
            if (matches == 1 && strlen(rule->keywords[0]) <= 1) {
                score = 0.3f; /* low confidence for single-char matches */
            }

            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }
    }

    if (best_idx >= 0 && best_score >= 0.3f) {
        result->type = g_keyword_rules[best_idx].intent;
        result->confidence = best_score;

        /* Try to extract target */
        if (known_names) {
            extract_target(user_input, known_names,
                          result->target, INTENT_MAX_TARGET_LEN);
        }

        /* Boost confidence if target was found */
        if (result->target[0] && best_score < 0.8f) {
            result->confidence = best_score + 0.15f;
            if (result->confidence > 0.95f) result->confidence = 0.95f;
        }
    }

    return result->confidence;
}

/* ═══════════════════════════════════════════════════════════════
   Level 3: Main model API
   ═══════════════════════════════════════════════════════════════ */

bool intent_recognize_level3(ApiClient *api, const char *user_input,
                              const char *game_context, IntentResult *result)
{
    if (!api || !user_input) return false;

    memset(result, 0, sizeof(*result));
    result->level = 3;

    const char *sys =
        "你是一个游戏意图识别器。根据玩家输入和游戏上下文，识别玩家的意图类型。\n\n"
        "意图类型（11种）：\n"
        "- MOVE: 移动到某地点（去/前往/出发/旅行）\n"
        "- ATTACK: 攻击目标（攻击/战斗/杀/揍）\n"
        "- TRADE: 交易买卖（买/卖/交易/交换）\n"
        "- TALK: 对话社交（说/聊/问/交谈/打招呼）\n"
        "- REST: 休息恢复（休息/睡觉/歇息）\n"
        "- SEARCH: 搜索调查（找/搜索/检查/调查/寻找）\n"
        "- USE_ITEM: 使用物品（使用/用/喝/吃/装备）\n"
        "- CRAFT: 制作合成（制作/合成/打造/锻造）\n"
        "- OBSERVE: 观察打量（观察/看/打量/环顾）\n"
        "- COMMAND: 命令指挥（命令/下令/指挥/派遣）\n"
        "- UNKNOWN: 无法明确归类\n\n"
        "只返回如下JSON格式，不要任何额外文字：\n"
        "{\"intent\":\"TALK\",\"target\":\"铁匠老王\",\"confidence\":0.95}";

    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "玩家输入：%s\n\n"
        "游戏上下文：%s\n\n"
        "请识别意图。", user_input,
        game_context ? game_context : "(无)");

    char raw[512];
    memset(raw, 0, sizeof(raw));
    if (!api_chat(api, sys, prompt, raw, sizeof(raw), 128)) {
        return false;
    }

    /* Parse JSON response using the shared json.c utilities (fixes Bug #3:
       fragile hand-written parsing replaced with validated extraction). */
    char intent_str[32] = {0};
    char target_str[64] = {0};
    int conf_int = 0;  /* json_get_int reads int, we'll convert to float */
    float conf = 0.0f;

    json_get_str(raw, "intent", intent_str, sizeof(intent_str));
    json_get_str(raw, "target", target_str, sizeof(target_str));
    if (json_get_int(raw, "confidence", &conf_int)) {
        conf = (float)conf_int;
    } else {
        /* Try parsing as float via string extraction */
        char conf_str[32];
        if (json_get_str(raw, "confidence", conf_str, sizeof(conf_str))) {
            conf = (float)atof(conf_str);
        }
    }

    /* Map intent string to enum */
    if (strcmp(intent_str, "MOVE") == 0)      result->type = INTENT_MOVE;
    else if (strcmp(intent_str, "ATTACK") == 0) result->type = INTENT_ATTACK;
    else if (strcmp(intent_str, "TRADE") == 0)  result->type = INTENT_TRADE;
    else if (strcmp(intent_str, "TALK") == 0)   result->type = INTENT_TALK;
    else if (strcmp(intent_str, "REST") == 0)   result->type = INTENT_REST;
    else if (strcmp(intent_str, "SEARCH") == 0) result->type = INTENT_SEARCH;
    else if (strcmp(intent_str, "USE_ITEM") == 0) result->type = INTENT_USE_ITEM;
    else if (strcmp(intent_str, "CRAFT") == 0)  result->type = INTENT_CRAFT;
    else if (strcmp(intent_str, "OBSERVE") == 0) result->type = INTENT_OBSERVE;
    else if (strcmp(intent_str, "COMMAND") == 0) result->type = INTENT_COMMAND;
    else result->type = INTENT_UNKNOWN;

    if (target_str[0]) {
        safe_strcpy(result->target, target_str, INTENT_MAX_TARGET_LEN);
    }
    result->confidence = conf > 0.0f ? conf : 0.8f;
    if (result->confidence > 1.0f) result->confidence = 1.0f;
    safe_strcpy(result->parameters, user_input, INTENT_MAX_PARAMS_LEN);

    return true;
}

/* ═══════════════════════════════════════════════════════════════
   Three-level cascade
   ═══════════════════════════════════════════════════════════════ */

IntentResult intent_recognize(ApiClient *api, const char *user_input,
                               const char *known_names, const char *game_context)
{
    IntentResult result;
    memset(&result, 0, sizeof(result));

    /* Level 1: keyword matching */
    float conf = intent_recognize_level1(user_input, known_names, &result);
    LOG_I("Intent L1: type=%s conf=%.2f target=%.20s",
             intent_type_str(result.type), conf,
             result.target[0] ? result.target : "(none)");

    if (conf >= INTENT_MIN_CONFIDENCE) {
        /* Level 1 succeeded */
        LOG_I("Intent L1: SUFFICIENT (>=%.2f), skipping L3", INTENT_MIN_CONFIDENCE);
        return result;
    }

    /* Level 1 insufficient — try Level 3 (main model) */
    LOG_I("Intent L1: insufficient (%.2f < %.2f), falling back to L3...", conf, INTENT_MIN_CONFIDENCE);
    if (api) {
        IntentResult l3_result;
        if (intent_recognize_level3(api, user_input, game_context, &l3_result)) {
            LOG_I("Intent L3: type=%s conf=%.2f target=%.20s",
                     intent_type_str(l3_result.type), l3_result.confidence,
                     l3_result.target[0] ? l3_result.target : "(none)");
            return l3_result;
        }
        LOG_W("Intent L3: AI call FAILED, using L1 fallback");
    }

    /* Fall through: return Level 1 result even if low confidence */
    return result;
}

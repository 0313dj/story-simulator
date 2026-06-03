#include "character_card.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cc_init(CharacterCard *card, EntityType type)
{
    memset(card, 0, sizeof(*card));
    card->entity_type = type;
    /* Default TypeIDs — these can be overridden when loading from Registry */
    card->race_id       = TYPEID_NONE;
    card->profession_id = TYPEID_NONE;
    card->faction_id    = TYPEID_NONE;
    mem_init(&card->memory);
    emotion_init(&card->emotion);
}

void cc_set_attributes(CharacterCard *card, int appearance, int constitution, int intelligence)
{
    if (appearance   < 0) appearance   = 0;
    if (appearance   > 100) appearance   = 100;
    if (constitution < 0) constitution = 0;
    if (constitution > 100) constitution = 100;
    if (intelligence < 0) intelligence = 0;
    if (intelligence > 100) intelligence = 100;

    card->attr.appearance   = appearance;
    card->attr.constitution = constitution;
    card->attr.intelligence = intelligence;
}

bool cc_add_item(CharacterCard *card, const char *name, int quantity)
{
    if (card->item_count >= MAX_ITEMS) return false;

    /* 已有同名物品则叠加数量 */
    for (int i = 0; i < card->item_count; i++) {
        if (strcmp(card->items[i].name, name) == 0) {
            card->items[i].quantity += quantity;
            return true;
        }
    }

    safe_strcpy(card->items[card->item_count].name, name, MAX_ITEM_NAME_LEN);
    card->items[card->item_count].quantity = quantity;
    card->item_count++;
    return true;
}

bool cc_remove_item(CharacterCard *card, const char *name)
{
    for (int i = 0; i < card->item_count; i++) {
        if (strcmp(card->items[i].name, name) == 0) {
            for (int j = i; j < card->item_count - 1; j++) {
                card->items[j] = card->items[j + 1];
            }
            card->item_count--;
            return true;
        }
    }
    return false;
}

bool cc_add_skill(CharacterCard *card, const char *name, int level)
{
    if (card->skill_count >= MAX_SKILLS) return false;
    if (level < 0)   level = 0;
    if (level > 100) level = 100;

    /* 已有同名技能则覆盖等级 */
    for (int i = 0; i < card->skill_count; i++) {
        if (strcmp(card->skills[i].name, name) == 0) {
            card->skills[i].level = level;
            return true;
        }
    }

    safe_strcpy(card->skills[card->skill_count].name, name, MAX_SKILL_NAME_LEN);
    card->skills[card->skill_count].level = level;
    card->skill_count++;
    return true;
}

void cc_change_money(CharacterCard *card, int delta)
{
    card->money += delta;
    if (card->money < 0) card->money = 0;
}

bool cc_add_relation(CharacterCard *card, const char *target, RelationType type, int affinity)
{
    if (card->relation_count >= MAX_RELATIONS) return false;
    if (affinity < -100) affinity = -100;
    if (affinity >  100) affinity =  100;

    /* 已有同名关系则更新 */
    for (int i = 0; i < card->relation_count; i++) {
        if (strcmp(card->relations[i].target, target) == 0) {
            card->relations[i].type    = type;
            card->relations[i].affinity = affinity;
            return true;
        }
    }

    safe_strcpy(card->relations[card->relation_count].target, target, MAX_REL_TARGET_LEN);
    card->relations[card->relation_count].type    = type;
    card->relations[card->relation_count].affinity = affinity;
    card->relation_count++;
    return true;
}

bool cc_set_affinity(CharacterCard *card, const char *target, int delta)
{
    for (int i = 0; i < card->relation_count; i++) {
        if (strcmp(card->relations[i].target, target) == 0) {
            card->relations[i].affinity += delta;
            if (card->relations[i].affinity < -100) card->relations[i].affinity = -100;
            if (card->relations[i].affinity >  100) card->relations[i].affinity =  100;
            return true;
        }
    }
    return false;
}

const char *relation_type_str(RelationType t)
{
    switch (t) {
        case REL_PARENT:       return "父母";
        case REL_CHILD:        return "子女";
        case REL_SIBLING:      return "兄弟姐妹";
        case REL_SPOUSE:       return "配偶";
        case REL_LOVER:        return "恋人";
        case REL_EX:           return "前任";
        case REL_KIN:          return "亲戚";
        case REL_FRIEND:       return "朋友";
        case REL_BEST_FRIEND:  return "挚友";
        case REL_RIVAL:        return "对手";
        case REL_ENEMY:        return "仇敌";
        case REL_COLLEAGUE:    return "同事";
        case REL_MASTER:       return "师父";
        case REL_DISCIPLE:     return "徒弟";
        case REL_STRANGER:     return "陌生人";
        case REL_ACQUAINTANCE: return "熟人";
        default:               return "未知";
    }
}

/* 导出完整游戏状态为可读文本（发给AI用） */
int cc_export_state(const CharacterCard *card, char *out, int out_size, bool is_player)
{
    /* Bug #30: use a safety macro to prevent snprintf with negative/zero
       remaining size, which is undefined behavior. */
#define SAFE_APPEND(fmt, ...) do { \
    if (pos >= out_size) break; \
    int _w = snprintf(out + pos, out_size - pos, fmt, ##__VA_ARGS__); \
    if (_w < 0) break; \
    pos += _w; \
} while(0)

    int pos = 0;

    SAFE_APPEND(
        "=== 人物信息 ===\n"
        "姓名: %s\n年龄: %d\n性别: %s\n衣着: %s\n",
        card->name, card->age,
        card->gender[0] ? card->gender : "未设定",
        card->clothing);
    if (!is_player) {
        SAFE_APPEND("身份: %s\n", card->personality);
        if (card->home[0]) {
            SAFE_APPEND("住所: %s\n", card->home);
        }
    }

    const char *st;
    switch (card->status) {
        case STATUS_NORMAL:  st = "正常"; break;
        case STATUS_HUNGRY:  st = "饥饿"; break;
        case STATUS_TIRED:   st = "疲惫"; break;
        case STATUS_SICK:    st = "生病"; break;
        case STATUS_INJURED: st = "受伤"; break;
        case STATUS_EXCITED: st = "兴奋"; break;
        case STATUS_ANGRY:   st = "愤怒"; break;
        case STATUS_SAD:     st = "悲伤"; break;
        case STATUS_HAPPY:   st = "开心"; break;
        default: st = "正常"; break;
    }

    SAFE_APPEND(
        "状态: %s\n金钱: %d\n颜值: %d\n体质: %d\n智力: %d\n",
        st, card->money, card->attr.appearance,
        card->attr.constitution, card->attr.intelligence);

    if (card->skill_count > 0) {
        SAFE_APPEND("技能: ");
        for (int i = 0; i < card->skill_count; i++) {
            SAFE_APPEND("%s(Lv.%d) ",
                card->skills[i].name, card->skills[i].level);
        }
        SAFE_APPEND("\n");
    }

    if (card->item_count > 0) {
        SAFE_APPEND("持有物: ");
        for (int i = 0; i < card->item_count; i++) {
            SAFE_APPEND("%s×%d ",
                card->items[i].name, card->items[i].quantity);
        }
        SAFE_APPEND("\n");
    }

    if (card->relation_count > 0) {
        SAFE_APPEND("关系: ");
        for (int i = 0; i < card->relation_count; i++) {
            SAFE_APPEND("%s(%s,好感%d) ",
                card->relations[i].target,
                relation_type_str(card->relations[i].type),
                card->relations[i].affinity);
        }
        SAFE_APPEND("\n");
    }

    if (!is_player) {
        SAFE_APPEND(
            "与玩家好感度: %d  上次互动: %d-%02d-%02d\n",
            card->player_affinity,
            card->last_interaction.year, card->last_interaction.month,
            card->last_interaction.day);
    }

    if (card->memory.short_count > 0) {
        SAFE_APPEND("近期对话记忆: ");
        for (int i = 0; i < card->memory.short_count; i++) {
            SAFE_APPEND("%s; ",
                card->memory.short_term[i].content);
        }
        SAFE_APPEND("\n");
    }
    if (card->memory.long_count > 0) {
        SAFE_APPEND("近期事件记忆: ");
        for (int i = 0; i < card->memory.long_count; i++) {
            SAFE_APPEND("%s; ",
                card->memory.long_term[i].content);
        }
        SAFE_APPEND("\n");
    }
    if (card->memory.perm_count > 0) {
        SAFE_APPEND("重要记忆: ");
        for (int i = 0; i < card->memory.perm_count; i++) {
            SAFE_APPEND("%s; ",
                card->memory.permanent[i].content);
        }
        SAFE_APPEND("\n");
    }

#undef SAFE_APPEND
    return pos;
}

void cc_set_player_affinity(CharacterCard *card, int value)
{
    if (value < -100) value = -100;
    if (value >  100) value =  100;
    card->player_affinity = value;
}

void cc_touch_interaction(CharacterCard *card, const GameTime *now)
{
    /* Bug #29: prevent time reversal — only update if the new time is
       not earlier than the existing timestamp. A backwards time jump
       would make the staleness calculation incorrect. */
    if (!now) return;
    if (card->last_interaction.year == 0 ||
        now->year > card->last_interaction.year ||
        (now->year == card->last_interaction.year &&
         now->month > card->last_interaction.month) ||
        (now->year == card->last_interaction.year &&
         now->month == card->last_interaction.month &&
         now->day >= card->last_interaction.day)) {
        card->last_interaction = *now;
    }
}

bool cc_is_stale_npc(const CharacterCard *card, const GameTime *now)
{
    if (card->player_affinity >= 30) return false;

    /* 上次互动时间为0（从未互动），视为过期 */
    if (card->last_interaction.year == 0) return true;

    /* 精确计算距上次互动的天数 (epoch from year 1) */
    static const int md[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int y1 = card->last_interaction.year, m1 = card->last_interaction.month, d1_day = card->last_interaction.day;
    int y2 = now->year, m2 = now->month, d2 = now->day;

    /* days from epoch for date 1 */
    int days1 = (y1 - 1) * 365;
    for (int y = 1; y < y1; y++) { if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) days1++; }
    for (int m = 1; m < m1; m++) {
        days1 += md[m];
        if (m == 2 && (y1 % 4 == 0 && (y1 % 100 != 0 || y1 % 400 == 0))) days1++;
    }
    days1 += d1_day;

    /* days from epoch for date 2 */
    int days2 = (y2 - 1) * 365;
    for (int y = 1; y < y2; y++) { if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) days2++; }
    for (int m = 1; m < m2; m++) {
        days2 += md[m];
        if (m == 2 && (y2 % 4 == 0 && (y2 % 100 != 0 || y2 % 400 == 0))) days2++;
    }
    days2 += d2;

    return (days2 - days1) >= 180;
}


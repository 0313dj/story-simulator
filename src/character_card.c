#include "character_card.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cc_init(CharacterCard *card, EntityType type)
{
    memset(card, 0, sizeof(*card));
    card->entity_type = type;
    mem_init(&card->memory);
}

const char *entity_type_str(EntityType t)
{
    switch (t) {
    case ENTITY_PLAYER:       return "player";
    case ENTITY_CHARACTER:    return "character";
    case ENTITY_MONSTER:      return "monster";
    case ENTITY_FACTION:      return "faction";
    case ENTITY_ORGANIZATION: return "organization";
    case ENTITY_FLEET:        return "fleet";
    case ENTITY_CIVILIZATION: return "civilization";
    case ENTITY_DEITY:        return "deity";
    default:                  return "unknown";
    }
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

    strncpy(card->items[card->item_count].name, name, MAX_ITEM_NAME_LEN - 1);
    card->items[card->item_count].name[MAX_ITEM_NAME_LEN - 1] = '\0';
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

    strncpy(card->skills[card->skill_count].name, name, MAX_SKILL_NAME_LEN - 1);
    card->skills[card->skill_count].name[MAX_SKILL_NAME_LEN - 1] = '\0';
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

    strncpy(card->relations[card->relation_count].target, target, MAX_REL_TARGET_LEN - 1);
    card->relations[card->relation_count].target[MAX_REL_TARGET_LEN - 1] = '\0';
    card->relations[card->relation_count].type    = type;
    card->relations[card->relation_count].affinity = affinity;
    card->relation_count++;
    return true;
}

bool cc_remove_relation(CharacterCard *card, const char *target)
{
    for (int i = 0; i < card->relation_count; i++) {
        if (strcmp(card->relations[i].target, target) == 0) {
            for (int j = i; j < card->relation_count - 1; j++) {
                card->relations[j] = card->relations[j + 1];
            }
            card->relation_count--;
            return true;
        }
    }
    return false;
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

/* 解析变更字符串并应用 */
/* Bug #14 fix: avoid strtok (not thread-safe); use manual line splitting */
void cc_apply_changes(CharacterCard *card, const char *changes)
{
    if (!changes || !*changes) return;

    char buf[2048];
    strncpy(buf, changes, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *line = buf;
    while (line && *line) {
        /* Find end of current line */
        char *nl = strchr(line, '\n');
        if (nl) { *nl = '\0'; saveptr = nl + 1; }
        else saveptr = NULL;
        while (*line == ' ' || *line == '\r') line++;

        /* 跳过空行 */
        if (*line == '\0') { line = saveptr; continue; }

        /* 跳过明显是对话/叙述的行（含中文标点或引号） */
        if (strstr(line, "\xEF\xBC\x9A") ||  /* ： */
            strstr(line, "\xE3\x80\x82") ||  /* 。 */
            strstr(line, "\xEF\xBC\x8C") ||  /* ， */
            strstr(line, "\xEF\xBC\x81") ||  /* ！ */
            strstr(line, "\xEF\xBC\x9F") ||  /* ？ */
            strstr(line, "\xE3\x80\x8C") ||  /* 「 */
            strchr(line, '"')  ||
            strchr(line, '"')) {
            { line = saveptr; } /* Bug #14: manual split replaces strtok */ continue;
        }

        /* 格式: target.var+delta, target.var=value, var+delta, var=value */
        char *eq = strchr(line, '=');
        char *plus = strchr(line, '+');
        char *minus = strchr(line, '-');

        if (eq) {
            /* 赋值格式：变量=新值 */
            *eq = '\0';
            char *varname = line;
            char *value = eq + 1;

            /* 去除空格 */
            while (*varname == ' ') varname++;
            char *vname_end = varname + strlen(varname) - 1;
            while (vname_end > varname && *vname_end == ' ') *vname_end-- = '\0';
            while (*value == ' ') value++;

            /* 跳过空变量名 */
            if (!*varname) { { line = saveptr; } /* Bug #14: manual split replaces strtok */ continue; }

            int val = atoi(value);

            if (strcmp(varname, "name") == 0) {
                /* value是字符串不是数字，特殊处理 */
                strncpy(card->name, value, MAX_NAME_LEN - 1);
                card->name[MAX_NAME_LEN - 1] = '\0';
            } else if (strcmp(varname, "personality") == 0) {
                strncpy(card->personality, value, MAX_PERSONALITY_LEN - 1);
                card->personality[MAX_PERSONALITY_LEN - 1] = '\0';
            } else if (strcmp(varname, "clothing") == 0) {
                strncpy(card->clothing, value, MAX_CLOTHING_LEN - 1);
                card->clothing[MAX_CLOTHING_LEN - 1] = '\0';
            } else if (strcmp(varname, "money") == 0) {
                card->money = atoi(value);
            } else if (strcmp(varname, "player_affinity") == 0) {
                cc_set_player_affinity(card, atoi(value));
            } else if (strcmp(varname, "age") == 0) {
                card->age = atoi(value);
            } else if (strcmp(varname, "status") == 0) {
                if (strcmp(value, "HUNGRY") == 0) card->status = STATUS_HUNGRY;
                else if (strcmp(value, "TIRED") == 0) card->status = STATUS_TIRED;
                else if (strcmp(value, "SICK") == 0) card->status = STATUS_SICK;
                else if (strcmp(value, "INJURED") == 0) card->status = STATUS_INJURED;
                else if (strcmp(value, "EXCITED") == 0) card->status = STATUS_EXCITED;
                else if (strcmp(value, "ANGRY") == 0) card->status = STATUS_ANGRY;
                else if (strcmp(value, "SAD") == 0) card->status = STATUS_SAD;
                else if (strcmp(value, "HAPPY") == 0) card->status = STATUS_HAPPY;
                else card->status = STATUS_NORMAL;
            } else if (strcmp(varname, "attr.appearance") == 0 ||
                       strcmp(varname, "appearance") == 0) {
                card->attr.appearance = val;
            } else if (strcmp(varname, "attr.constitution") == 0 ||
                       strcmp(varname, "constitution") == 0) {
                card->attr.constitution = val;
            } else if (strcmp(varname, "attr.intelligence") == 0 ||
                       strcmp(varname, "intelligence") == 0) {
                card->attr.intelligence = val;
            } else {
                /* 可能是 人物.affinity=val 或 技能名=等级 */
                char *dot = strchr(varname, '.');
                if (dot) {
                    *dot = '\0';
                    char *target = varname;
                    char *field = dot + 1;
                    if (strcmp(field, "affinity") == 0 || strcmp(field, "好感度") == 0) {
                        /* Bug #15: create new relation if target not found */
                        bool found = false;
                        for (int i = 0; i < card->relation_count; i++) {
                            if (strcmp(card->relations[i].target, target) == 0) {
                                card->relations[i].affinity = val;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            cc_add_relation(card, target, REL_STRANGER, val);
                        }
                    }
                    /* unrecognized dotted field — ignore, don't fall through to skill */
                } else {
                    /* 纯变量名可能是技能名=等级 */
                    cc_add_skill(card, varname, val);
                }
            }
        } else if (plus || minus) {
            /* 加减格式：变量+delta 或 变量-delta */
            char *op;
            int sign;
            if (plus) { op = plus; sign = 1; }
            else      { op = minus; sign = -1; }

            *op = '\0';
            char *varname = line;
            /* 去除空格 */
            while (*varname == ' ') varname++;
            char *vname_end2 = varname + strlen(varname) - 1;
            while (vname_end2 > varname && *vname_end2 == ' ') *vname_end2-- = '\0';

            /* 跳过空变量名 */
            if (!*varname) { { line = saveptr; } /* Bug #14: manual split replaces strtok */ continue; }

            int delta = sign * atoi(op + 1);

            if (strcmp(varname, "money") == 0) {
                cc_change_money(card, delta);
            } else if (strcmp(varname, "player_affinity") == 0) {
                cc_set_player_affinity(card, card->player_affinity + delta);
            } else if (strcmp(varname, "attr.appearance") == 0 ||
                       strcmp(varname, "appearance") == 0) {
                cc_set_attributes(card,
                    card->attr.appearance + delta,
                    card->attr.constitution,
                    card->attr.intelligence);
            } else if (strcmp(varname, "attr.constitution") == 0 ||
                       strcmp(varname, "constitution") == 0) {
                cc_set_attributes(card,
                    card->attr.appearance,
                    card->attr.constitution + delta,
                    card->attr.intelligence);
            } else if (strcmp(varname, "attr.intelligence") == 0 ||
                       strcmp(varname, "intelligence") == 0) {
                cc_set_attributes(card,
                    card->attr.appearance,
                    card->attr.constitution,
                    card->attr.intelligence + delta);
            } else {
                /* 可能是 人物.affinity+delta */
                char *dot = strchr(varname, '.');
                if (dot) {
                    *dot = '\0';
                    char *target = varname;
                    char *field = dot + 1;
                    if (strcmp(field, "affinity") == 0 || strcmp(field, "好感度") == 0) {
                        cc_set_affinity(card, target, delta);
                    }
                } else {
                    /* 可能是技能名+delta */
                    bool found_skill = false;
                    for (int i = 0; i < card->skill_count; i++) {
                        if (strcmp(card->skills[i].name, varname) == 0) {
                            cc_add_skill(card, varname, card->skills[i].level + delta);
                            found_skill = true;
                            break;
                        }
                    }
                    /* Bug #16: create new skill if not found */
                    if (!found_skill) {
                        cc_add_skill(card, varname, delta > 0 ? delta : 0);
                    }
                }
            }
        }

        { line = saveptr; } /* Bug #14: manual split replaces strtok */
    }
}

/* 导出完整游戏状态为可读文本（发给AI用） */
int cc_export_state(const CharacterCard *card, char *out, int out_size, bool is_player)
{
    int pos = 0;

    pos += snprintf(out + pos, out_size - pos,
        "=== 人物信息 ===\n"
        "姓名: %s\n年龄: %d\n衣着: %s\n",
        card->name, card->age, card->clothing);
    if (!is_player) {
        pos += snprintf(out + pos, out_size - pos,
            "身份: %s\n", card->personality);
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

    pos += snprintf(out + pos, out_size - pos,
        "状态: %s\n金钱: %d\n颜值: %d\n体质: %d\n智力: %d\n",
        st, card->money, card->attr.appearance,
        card->attr.constitution, card->attr.intelligence);

    if (card->skill_count > 0) {
        pos += snprintf(out + pos, out_size - pos, "技能: ");
        for (int i = 0; i < card->skill_count; i++) {
            pos += snprintf(out + pos, out_size - pos, "%s(Lv.%d) ",
                card->skills[i].name, card->skills[i].level);
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }

    if (card->item_count > 0) {
        pos += snprintf(out + pos, out_size - pos, "持有物: ");
        for (int i = 0; i < card->item_count; i++) {
            pos += snprintf(out + pos, out_size - pos, "%s×%d ",
                card->items[i].name, card->items[i].quantity);
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }

    if (card->relation_count > 0) {
        pos += snprintf(out + pos, out_size - pos, "关系: ");
        for (int i = 0; i < card->relation_count; i++) {
            pos += snprintf(out + pos, out_size - pos, "%s(%s,好感%d) ",
                card->relations[i].target,
                relation_type_str(card->relations[i].type),
                card->relations[i].affinity);
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }

    if (!is_player) {
        pos += snprintf(out + pos, out_size - pos,
            "与玩家好感度: %d  上次互动: %d-%02d-%02d\n",
            card->player_affinity,
            card->last_interaction.year, card->last_interaction.month,
            card->last_interaction.day);
    }

    /* 记忆导出 */
    if (card->memory.short_count > 0) {
        pos += snprintf(out + pos, out_size - pos, "近期对话记忆: ");
        for (int i = 0; i < card->memory.short_count; i++) {
            pos += snprintf(out + pos, out_size - pos, "%s; ",
                card->memory.short_term[i].content);
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }
    if (card->memory.long_count > 0) {
        pos += snprintf(out + pos, out_size - pos, "近期事件记忆: ");
        for (int i = 0; i < card->memory.long_count; i++) {
            pos += snprintf(out + pos, out_size - pos, "%s; ",
                card->memory.long_term[i].content);
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }
    if (card->memory.perm_count > 0) {
        pos += snprintf(out + pos, out_size - pos, "重要记忆: ");
        for (int i = 0; i < card->memory.perm_count; i++) {
            pos += snprintf(out + pos, out_size - pos, "%s; ",
                card->memory.permanent[i].content);
        }
        pos += snprintf(out + pos, out_size - pos, "\n");
    }

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
    card->last_interaction = *now;
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

static const char *status_str(StatusType s)
{
    switch (s) {
        case STATUS_NORMAL:   return "正常";
        case STATUS_HUNGRY:   return "饥饿";
        case STATUS_TIRED:    return "疲惫";
        case STATUS_SICK:     return "生病";
        case STATUS_INJURED:  return "受伤";
        case STATUS_EXCITED:  return "兴奋";
        case STATUS_ANGRY:    return "愤怒";
        case STATUS_SAD:      return "悲伤";
        case STATUS_HAPPY:    return "开心";
        default:              return "未知";
    }
}

void cc_print(const CharacterCard *card)
{
    printf("══════════════════════════════════════\n");
    printf("  人物卡\n");
    printf("══════════════════════════════════════\n");
    printf("  姓名:   %s\n", card->name);
    printf("  年龄:   %d\n", card->age);
    printf("  身份:   %s\n", card->personality);
    printf("  衣着:   %s\n", card->clothing);
    printf("  状态:   %s\n", status_str(card->status));
    printf("  金钱:   %d G\n", card->money);
    printf("──────────────────────────────────────\n");
    printf("  属性\n");
    printf("  颜值:   %d/100\n", card->attr.appearance);
    printf("  体质:   %d/100\n", card->attr.constitution);
    printf("  智力:   %d/100\n", card->attr.intelligence);
    printf("──────────────────────────────────────\n");
    printf("  持有物 (%d):\n", card->item_count);
    for (int i = 0; i < card->item_count; i++) {
        printf("    · %s ×%d\n", card->items[i].name, card->items[i].quantity);
    }
    printf("──────────────────────────────────────\n");
    printf("  技能 (%d):\n", card->skill_count);
    for (int i = 0; i < card->skill_count; i++) {
        printf("    · %s  Lv.%d\n", card->skills[i].name, card->skills[i].level);
    }
    printf("──────────────────────────────────────\n");
    printf("  人物关系 (%d):\n", card->relation_count);
    for (int i = 0; i < card->relation_count; i++) {
        const Relationship *r = &card->relations[i];
        printf("    · %-12s [%s]  好感度: %+d\n",
            r->target, relation_type_str(r->type), r->affinity);
    }
    if (card->player_affinity != 0 || card->last_interaction.year != 0) {
        printf("  玩家好感度: %+d  上次互动: %d-%02d-%02d\n",
            card->player_affinity,
            card->last_interaction.year, card->last_interaction.month,
            card->last_interaction.day);
    }
    printf("──────────────────────────────────────\n");
    mem_print(&card->memory);
    printf("══════════════════════════════════════\n");
}

#ifndef CHARACTER_CARD_H
#define CHARACTER_CARD_H

#include <stdbool.h>
#include "memory.h"

/* Forward declaration for NPC autonomous behavior (Stage 3).
   Full definition in npc_brain.h — included by .c files that need it. */
typedef struct NpcBrain NpcBrain;

/* ═══════════════════════════════════════════════════════════════
   EntityType — per USE Spec Chapter 4
   ═══════════════════════════════════════════════════════════════ */
typedef enum {
    ENTITY_PLAYER,        /* 玩家 */
    ENTITY_CHARACTER,     /* NPC角色 */
    ENTITY_MONSTER,       /* 怪物 */
    ENTITY_FACTION,       /* 派系 */
    ENTITY_ORGANIZATION,  /* 组织 */
    ENTITY_FLEET,         /* 舰队 */
    ENTITY_CIVILIZATION,  /* 文明 */
    ENTITY_DEITY,         /* 神祇 */
} EntityType;

#define MAX_NAME_LEN        64
#define MAX_PERSONALITY_LEN 256
#define MAX_CLOTHING_LEN    128
#define MAX_ITEM_NAME_LEN   32
#define MAX_STATUS_LEN      64
#define MAX_SKILL_NAME_LEN  32
#define MAX_SKILLS          32
#define MAX_ITEMS           16
#define MAX_RELATIONS       32
#define MAX_REL_TARGET_LEN  64

/* 单项技能 */
typedef struct {
    char name[MAX_SKILL_NAME_LEN];
    int  level;       /* 技能等级 0-100 */
} Skill;

/* 三项基础数值 */
typedef struct {
    int appearance;    /* 颜值 0-100 */
    int constitution;  /* 体质 0-100 */
    int intelligence;  /* 智力 0-100 */
} Attributes;

/* 关系类型 */
typedef enum {
    REL_PARENT,       /* 父母 */
    REL_CHILD,        /* 子女 */
    REL_SIBLING,      /* 兄弟姐妹 */
    REL_SPOUSE,       /* 配偶 */
    REL_LOVER,        /* 恋人 */
    REL_EX,            /* 前任 */
    REL_KIN,           /* 亲戚 */
    REL_FRIEND,        /* 朋友 */
    REL_BEST_FRIEND,   /* 挚友 */
    REL_RIVAL,         /* 对手 */
    REL_ENEMY,         /* 仇敌 */
    REL_COLLEAGUE,     /* 同事/同僚 */
    REL_MASTER,        /* 师父/上级 */
    REL_DISCIPLE,      /* 徒弟/下级 */
    REL_STRANGER,      /* 陌生人 */
    REL_ACQUAINTANCE,  /* 熟人 */
} RelationType;

/* 人物关系 */
typedef struct {
    char         target[MAX_REL_TARGET_LEN];  /* 对方姓名 */
    RelationType type;                        /* 关系类型 */
    int          affinity;                   /* 好感度 -100~100 */
} Relationship;

/* 持有物 */
typedef struct {
    char name[MAX_ITEM_NAME_LEN];
    int  quantity;
} Item;

/* 角色状态 */
typedef enum {
    STATUS_NORMAL,
    STATUS_HUNGRY,
    STATUS_TIRED,
    STATUS_SICK,
    STATUS_INJURED,
    STATUS_EXCITED,
    STATUS_ANGRY,
    STATUS_SAD,
    STATUS_HAPPY,
} StatusType;

/* 角色卡 */
typedef struct {
    EntityType  entity_type;      /* 实体类型 */
    char        name[MAX_NAME_LEN];
    int         age;
    char        personality[MAX_PERSONALITY_LEN];  /* 性格身份 */
    char        clothing[MAX_CLOTHING_LEN];         /* 衣着 */
    Item        items[MAX_ITEMS];                   /* 持有物 */
    int         item_count;
    StatusType  status;                             /* 当前状态 */
    int         money;                              /* 金钱 */
    Attributes  attr;                               /* 颜值/体质/智力 */
    Skill         skills[MAX_SKILLS];               /* 技能列表 */
    int           skill_count;
    Relationship  relations[MAX_RELATIONS];         /* 人物关系 */
    int           relation_count;
    int           player_affinity;                   /* 与玩家的好感度 -100~100 */
    GameTime      last_interaction;                  /* 上次与玩家互动的时间 */
    MemoryStore   memory;                            /* 记忆库 */
    NpcBrain     *brain;                            /* NPC自主行为 (Stage 3), NULL for player */
} CharacterCard;

/* 初始化角色卡 */
void cc_init(CharacterCard *card, EntityType type);

/* 实体类型名 */
const char *entity_type_str(EntityType t);

/* 设置基础属性 */
void cc_set_attributes(CharacterCard *card, int appearance, int constitution, int intelligence);

/* 添加持有物 */
bool cc_add_item(CharacterCard *card, const char *name, int quantity);

/* 移除持有物 */
bool cc_remove_item(CharacterCard *card, const char *name);

/* 添加技能 */
bool cc_add_skill(CharacterCard *card, const char *name, int level);

/* 修改金钱 */
void cc_change_money(CharacterCard *card, int delta);

/* 添加人物关系 */
bool cc_add_relation(CharacterCard *card, const char *target, RelationType type, int affinity);

/* 移除人物关系 */
bool cc_remove_relation(CharacterCard *card, const char *target);

/* 修改好感度 */
bool cc_set_affinity(CharacterCard *card, const char *target, int delta);

/* 关系类型名 */
const char *relation_type_str(RelationType t);

/* 应用AI返回的变量变更。
   @deprecated Use rule_engine_apply_changes() from rule.h instead.
   This function is retained for backward compatibility but new code
   should route all changes through the Rule Engine (Phase 1+). */
void cc_apply_changes(CharacterCard *card, const char *changes);

/* 导出角色卡状态为可读字符串。is_player=true 时跳过性格字段 */
int cc_export_state(const CharacterCard *card, char *out, int out_size, bool is_player);

/* 修改玩家好感度 */
void cc_set_player_affinity(CharacterCard *card, int value);

/* 记录与玩家的互动时间 */
void cc_touch_interaction(CharacterCard *card, const GameTime *now);

/* 清理过期NPC：好感度<30且超过6个月(180天)未互动则返回true */
bool cc_is_stale_npc(const CharacterCard *card, const GameTime *now);

/* 打印角色卡信息 */
void cc_print(const CharacterCard *card);

#endif

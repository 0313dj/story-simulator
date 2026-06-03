#include "npc_brain.h"
#include "character_card.h"
#include "worldstate.h"
#include "event.h"
#include "changeset.h"
#include "json.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Internal helpers
   ═══════════════════════════════════════════════════════════════ */

/* Simple deterministic hash from string — used for personality seeding */
static unsigned int name_hash(const char *s)
{
    unsigned int h = 5381;
    while (*s) { h = ((h << 5) + h) + (unsigned char)*s; s++; }
    return h;
}

/* Clamp int to [lo, hi] */
static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Return a random int in [lo, hi] using name_hash for determinism */
static int seeded_rand(unsigned int *seed, int lo, int hi)
{
    *seed = (*seed * 1103515245U + 12345U) & 0x7fffffffU;
    return lo + (int)(*seed % (unsigned int)(hi - lo + 1));
}

/* ═══════════════════════════════════════════════════════════════
   Goal type name
   ═══════════════════════════════════════════════════════════════ */

const char *goal_type_str(GoalType t)
{
    switch (t) {
    case GOAL_IDLE:       return "IDLE";
    case GOAL_SOCIALIZE:  return "SOCIALIZE";
    case GOAL_TRADE:      return "TRADE";
    case GOAL_REST:       return "REST";
    case GOAL_EAT:        return "EAT";
    case GOAL_EXPLORE:    return "EXPLORE";
    case GOAL_WORK:       return "WORK";
    case GOAL_TRAIN:      return "TRAIN";
    case GOAL_CRAFT:      return "CRAFT";
    case GOAL_DEFEND:     return "DEFEND";
    case GOAL_ATTACK:     return "ATTACK";
    case GOAL_HEAL:       return "HEAL";
    case GOAL_WANDER:     return "WANDER";
    case GOAL_OBSERVE:    return "OBSERVE";
    default:              return "UNKNOWN";
    }
}

/* Chinese name for display */
static const char *goal_type_cn(GoalType t)
{
    switch (t) {
    case GOAL_IDLE:       return "空闲";
    case GOAL_SOCIALIZE:  return "社交";
    case GOAL_TRADE:      return "交易";
    case GOAL_REST:       return "休息";
    case GOAL_EAT:        return "进食";
    case GOAL_EXPLORE:    return "探索";
    case GOAL_WORK:       return "工作";
    case GOAL_TRAIN:      return "训练";
    case GOAL_CRAFT:      return "制作";
    case GOAL_DEFEND:     return "防御";
    case GOAL_ATTACK:     return "攻击";
    case GOAL_HEAL:       return "治疗";
    case GOAL_WANDER:     return "闲逛";
    case GOAL_OBSERVE:    return "观察";
    default:              return "未知";
    }
}

/* ═══════════════════════════════════════════════════════════════
   Init
   ═══════════════════════════════════════════════════════════════ */

void nb_init(NpcBrain *brain, long long current_tick)
{
    memset(brain, 0, sizeof(*brain));
    brain->active = true;

    /* Stagger initial think times: random offset within NB_THINK_COOLDOWN_MIN */
    unsigned int seed = (unsigned int)(current_tick ^ 0xDEADBEEF);
    brain->next_think_tick = current_tick + seeded_rand(&seed,
        NB_THINK_COOLDOWN_MIN / 2, NB_THINK_COOLDOWN_MIN);
    brain->last_think_tick = 0;
    brain->idle_rounds = 0;

    plan_init(&brain->current_plan);
}

/* ═══════════════════════════════════════════════════════════════
   Personality generation
   ═══════════════════════════════════════════════════════════════ */

void nb_personality_from_name(NpcBrain *brain, const char *name)
{
    unsigned int seed = name_hash(name);

    brain->personality.aggressiveness  = seeded_rand(&seed, 5, 70);
    brain->personality.sociability     = seeded_rand(&seed, 10, 90);
    brain->personality.curiosity       = seeded_rand(&seed, 10, 85);
    brain->personality.industriousness = seeded_rand(&seed, 10, 90);
    brain->personality.bravery         = seeded_rand(&seed, 5, 80);
}

void nb_generate_personality(NpcBrain *brain, const CharacterCard *npc)
{
    /* Start with name-based seed for determinism */
    nb_personality_from_name(brain, npc->name);

    /* Blend with NPC attributes */
    Personality *p = &brain->personality;

    /* Intelligence → curiosity boost */
    p->curiosity = clamp_int(p->curiosity + (npc->attr.intelligence - 50) / 5, 0, 100);

    /* Constitution → bravery boost */
    p->bravery = clamp_int(p->bravery + (npc->attr.constitution - 50) / 5, 0, 100);

    /* High player_affinity → sociability boost toward player */
    if (npc->player_affinity > 30)
        p->sociability = clamp_int(p->sociability + 10, 0, 100);
    else if (npc->player_affinity < -30)
        p->aggressiveness = clamp_int(p->aggressiveness + 10, 0, 100);

    /* Wealthy NPCs are less industrious (tend toward leisure) */
    if (npc->money > 500)
        p->industriousness = clamp_int(p->industriousness - 10, 0, 100);

    /* Many relations → sociability boost */
    if (npc->relation_count > 5)
        p->sociability = clamp_int(p->sociability + 15, 0, 100);

    /* Combat skills → aggressiveness boost */
    for (int i = 0; i < npc->skill_count; i++) {
        if (strstr(npc->skills[i].name, "战斗") ||
            strstr(npc->skills[i].name, "剑") ||
            strstr(npc->skills[i].name, "弓") ||
            strstr(npc->skills[i].name, "格斗")) {
            p->aggressiveness = clamp_int(p->aggressiveness + 10, 0, 100);
            p->bravery = clamp_int(p->bravery + 10, 0, 100);
        }
        if (strstr(npc->skills[i].name, "锻造") ||
            strstr(npc->skills[i].name, "炼金") ||
            strstr(npc->skills[i].name, "工匠")) {
            p->industriousness = clamp_int(p->industriousness + 10, 0, 100);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   Goal management
   ═══════════════════════════════════════════════════════════════ */

bool nb_add_goal(NpcBrain *brain, GoalType type, const char *target,
                 const char *params, int priority, long long tick, long long expiry)
{
    if (brain->goal_count >= NB_MAX_GOALS) return false;

    /* Find insertion point (keep sorted by priority descending) */
    int insert_at = brain->goal_count;
    for (int i = 0; i < brain->goal_count; i++) {
        if (priority > brain->goals[i].priority) {
            insert_at = i;
            break;
        }
    }

    /* Shift down */
    for (int i = brain->goal_count; i > insert_at; i--)
        brain->goals[i] = brain->goals[i - 1];

    /* Insert */
    Goal *g = &brain->goals[insert_at];
    memset(g, 0, sizeof(*g));
    g->type = type;
    if (target)  safe_strcpy(g->target, target, sizeof(g->target));
    if (params)  safe_strcpy(g->params, params, sizeof(g->params));
    g->priority = clamp_int(priority, 0, 100);
    g->created_tick = tick;
    g->expiry_tick  = expiry;
    g->complete = false;

    brain->goal_count++;
    return true;
}

bool nb_remove_goal(NpcBrain *brain, int idx)
{
    if (idx < 0 || idx >= brain->goal_count) return false;
    for (int i = idx; i < brain->goal_count - 1; i++)
        brain->goals[i] = brain->goals[i + 1];
    brain->goal_count--;
    return true;
}

void nb_clear_goals(NpcBrain *brain)
{
    brain->goal_count = 0;
}

const Goal* nb_top_goal(const NpcBrain *brain)
{
    if (brain->goal_count <= 0) return NULL;
    return &brain->goals[0];
}

void nb_complete_top_goal(NpcBrain *brain)
{
    if (brain->goal_count > 0) {
        brain->goals[0].complete = true;
        nb_remove_goal(brain, 0);
    }
}

bool nb_needs_new_goal(const NpcBrain *brain, long long current_tick)
{
    /* No goals at all */
    if (brain->goal_count == 0) return true;

    /* Top goal expired */
    const Goal *top = &brain->goals[0];
    if (top->expiry_tick > 0 && current_tick >= top->expiry_tick) return true;

    /* Too many idle rounds */
    if (brain->idle_rounds > 0 && brain->idle_rounds >= 3) return true;

    return false;
}

/* ═══════════════════════════════════════════════════════════════
   Goal selection — weighted random based on personality + state
   ═══════════════════════════════════════════════════════════════ */

GoalType nb_select_goal(const NpcBrain *brain, const CharacterCard *npc,
                        const WorldState *ws)
{
    (void)ws;  /* reserved for future: location-based goals */

    const Personality *p = &brain->personality;
    unsigned int seed = name_hash(npc->name) ^ (unsigned int)brain->last_think_tick;

    /* Check for status-driven goals first (these override random selection) */
    if (npc->status == STATUS_TIRED && seeded_rand(&seed, 0, 100) < 70)
        return GOAL_REST;
    if (npc->status == STATUS_HUNGRY && seeded_rand(&seed, 0, 100) < 80)
        return GOAL_EAT;
    if ((npc->status == STATUS_SICK || npc->status == STATUS_INJURED)
        && seeded_rand(&seed, 0, 100) < 85)
        return GOAL_HEAL;

    /* Build weighted table */
    typedef struct { GoalType type; int weight; } WeightedGoal;
    WeightedGoal table[16];
    int count = 0;
    int total_weight = 0;

    #define ADD_GOAL(t, w) do { \
        if (count < 16) { table[count].type = (t); table[count].weight = (w); total_weight += (w); count++; } \
    } while(0)

    ADD_GOAL(GOAL_SOCIALIZE, 10 + p->sociability / 2);
    ADD_GOAL(GOAL_TRADE,      5 + p->sociability / 3 + p->industriousness / 5);
    ADD_GOAL(GOAL_WORK,      10 + p->industriousness / 2);
    ADD_GOAL(GOAL_TRAIN,      5 + p->industriousness / 3 + p->bravery / 5);
    ADD_GOAL(GOAL_CRAFT,      3 + p->industriousness / 3);
    ADD_GOAL(GOAL_EXPLORE,    5 + p->curiosity / 2);
    ADD_GOAL(GOAL_WANDER,     5 + p->curiosity / 3);
    ADD_GOAL(GOAL_OBSERVE,    5 + p->curiosity / 2);

    /* Aggressive goals only for aggressive NPCs */
    if (p->aggressiveness > 40) {
        ADD_GOAL(GOAL_ATTACK, (p->aggressiveness - 30) / 2);
    }

    /* Hungry NPCs more likely to eat even if not yet HUNGRY status */
    if (npc->money < 20)
        ADD_GOAL(GOAL_WORK, 15);  /* poor NPCs work more */

    #undef ADD_GOAL

    if (total_weight <= 0) return GOAL_IDLE;

    /* Weighted random pick */
    int roll = seeded_rand(&seed, 0, total_weight - 1);
    int cumulative = 0;
    for (int i = 0; i < count; i++) {
        cumulative += table[i].weight;
        if (roll < cumulative) return table[i].type;
    }

    return GOAL_IDLE;
}

/* ═══════════════════════════════════════════════════════════════
   Simple plan generation (deterministic, no AI needed)
   ═══════════════════════════════════════════════════════════════ */

bool nb_generate_plan_simple(NpcBrain *brain, const Goal *goal,
                             const CharacterCard *npc, const WorldState *ws)
{
    (void)ws;
    plan_init(&brain->current_plan);

    switch (goal->type) {
    case GOAL_REST:
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "休息恢复体力");
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "休息");
        snprintf(brain->current_plan.steps[0].target, PLAN_MAX_TARGET_LEN,
            "%s", npc->name);
        brain->current_plan.steps[0].estimated_ticks = 60;  /* 1 hour rest */
        brain->current_plan.step_count = 1;
        return true;

    case GOAL_EAT:
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "找点吃的");
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "进食");
        snprintf(brain->current_plan.steps[0].target, PLAN_MAX_TARGET_LEN,
            "%s", npc->name);
        brain->current_plan.steps[0].estimated_ticks = 30;  /* 30 min meal */
        brain->current_plan.step_count = 1;
        return true;

    case GOAL_HEAL:
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "治疗伤势或疾病");
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "治疗");
        snprintf(brain->current_plan.steps[0].target, PLAN_MAX_TARGET_LEN,
            "%s", npc->name);
        brain->current_plan.steps[0].estimated_ticks = 120; /* 2 hours */
        brain->current_plan.step_count = 1;
        return true;

    case GOAL_WANDER:
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "四处走走");
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "闲逛");
        brain->current_plan.steps[0].estimated_ticks = 45;  /* 45 min walk */
        brain->current_plan.step_count = 1;
        return true;

    case GOAL_OBSERVE:
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "观察周围环境");
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "观察");
        brain->current_plan.steps[0].estimated_ticks = 15;
        brain->current_plan.step_count = 1;
        return true;

    case GOAL_WORK:
        /* Simple work: earn some money */
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "工作赚取收入");
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "工作");
        snprintf(brain->current_plan.steps[0].target, PLAN_MAX_TARGET_LEN,
            "%s", npc->name);
        brain->current_plan.steps[0].estimated_ticks = 240; /* 4 hours */
        brain->current_plan.step_count = 1;
        return true;

    default:
        /* Complex goals need AI */
        return false;
    }
}

/* ═══════════════════════════════════════════════════════════════
   AI-assisted plan generation for complex goals
   ═══════════════════════════════════════════════════════════════ */

bool nb_generate_plan_ai(NpcBrain *brain, const Goal *goal,
                          const CharacterCard *npc, const WorldState *ws,
                          ApiClient *api)
{
    (void)ws;  /* reserved for future: location-aware planning */
    if (!api) {
        /* No API available — fall back to simple single-step plan */
        plan_init(&brain->current_plan);
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "%s", goal_type_cn(goal->type));
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "%s", goal_type_cn(goal->type));
        if (goal->target[0])
            snprintf(brain->current_plan.steps[0].target, PLAN_MAX_TARGET_LEN,
                "%s", goal->target);
        brain->current_plan.steps[0].estimated_ticks = 60;
        brain->current_plan.step_count = 1;
        return true;
    }

    /* Build a compact game state for the AI */
    char state_buf[2048];
    int sp = 0;
    sp += snprintf(state_buf + sp, sizeof(state_buf) - sp,
        "NPC: %s\n", npc->name);
    sp += snprintf(state_buf + sp, sizeof(state_buf) - sp,
        "身份: %s  年龄: %d  金钱: %d  状态: ",
        npc->personality, npc->age, npc->money);
    switch (npc->status) {
    case STATUS_NORMAL:  sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "正常"); break;
    case STATUS_HUNGRY:  sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "饥饿"); break;
    case STATUS_TIRED:   sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "疲惫"); break;
    case STATUS_SICK:    sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "生病"); break;
    case STATUS_INJURED: sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "受伤"); break;
    default:             sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "其他"); break;
    }
    sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "\n");
    sp += snprintf(state_buf + sp, sizeof(state_buf) - sp,
        "目标: %s", goal_type_cn(goal->type));
    if (goal->target[0])
        sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, " → %s", goal->target);
    sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "\n");

    if (npc->skill_count > 0) {
        sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "技能: ");
        for (int i = 0; i < npc->skill_count && i < 5; i++)
            sp += snprintf(state_buf + sp, sizeof(state_buf) - sp,
                "%s(Lv.%d) ", npc->skills[i].name, npc->skills[i].level);
        sp += snprintf(state_buf + sp, sizeof(state_buf) - sp, "\n");
    }

    /* Simple system prompt for NPC plan generation */
    const char *sys =
        "你是一个NPC行为规划器。根据NPC的身份、状态和目标，"
        "生成1-3个简洁的行动步骤。\n"
        "输出格式：每行一个步骤，格式为\"动作描述|目标对象|预估分钟数\"\n"
        "例如：\n"
        "去市场买菜|市场|30\n"
        "与铁匠交谈|铁匠|15\n"
        "回家做饭|家|60\n"
        "只输出步骤，不要输出其他内容。";

    char response[1024];
    bool ok = api_chat(api, sys, state_buf, response, sizeof(response), 256);
    if (!ok || !response[0]) {
        /* Fallback: single step */
        plan_init(&brain->current_plan);
        snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
            "%s", goal_type_cn(goal->type));
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "%s", goal_type_cn(goal->type));
        if (goal->target[0])
            snprintf(brain->current_plan.steps[0].target, PLAN_MAX_TARGET_LEN,
                "%s", goal->target);
        brain->current_plan.steps[0].estimated_ticks = 60;
        brain->current_plan.step_count = 1;
        return true;
    }

    /* Parse AI response into plan steps */
    plan_init(&brain->current_plan);
    snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
        "%s", goal_type_cn(goal->type));

    char *line = response;
    int step_idx = 0;
    while (line && *line && step_idx < PLAN_MAX_STEPS) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        while (*line == ' ' || *line == '\r') line++;
        if (!*line) { line = nl ? nl + 1 : NULL; continue; }

        /* Parse "动作|目标|分钟" */
        char *p1 = strchr(line, '|');
        char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        if (p1 && p2) {
            *p1 = '\0'; *p2 = '\0';
            safe_strcpy(brain->current_plan.steps[step_idx].action, line,
                    PLAN_MAX_ACTION_LEN);
            safe_strcpy(brain->current_plan.steps[step_idx].target, p1 + 1,
                    PLAN_MAX_TARGET_LEN);
            brain->current_plan.steps[step_idx].estimated_ticks = atoi(p2 + 1);
            if (brain->current_plan.steps[step_idx].estimated_ticks <= 0)
                brain->current_plan.steps[step_idx].estimated_ticks = 30;
        } else {
            /* No pipe: treat whole line as action */
            safe_strcpy(brain->current_plan.steps[step_idx].action, line,
                    PLAN_MAX_ACTION_LEN);
            brain->current_plan.steps[step_idx].estimated_ticks = 30;
        }
        step_idx++;
        line = nl ? nl + 1 : NULL;
    }
    brain->current_plan.step_count = step_idx > 0 ? step_idx : 1;

    /* If nothing parsed, fallback single step */
    if (brain->current_plan.step_count == 0) {
        snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
            "%s", goal_type_cn(goal->type));
        brain->current_plan.steps[0].estimated_ticks = 60;
        brain->current_plan.step_count = 1;
    }

    return true;
}

/* ═══════════════════════════════════════════════════════════════
   ActionProposal generation from current plan step
   ═══════════════════════════════════════════════════════════════ */

bool nb_generate_proposal(const NpcBrain *brain, const CharacterCard *npc,
                          const WorldState *ws, ActionProposal *ap)
{
    (void)ws;
    ap_init(ap);

    if (brain->current_plan.step_count <= 0) return false;

    const Goal *goal = nb_top_goal(brain);
    if (!goal || goal->complete) return false;

    GoalType gt = goal->type;
    const char *npc_name = npc->name;

    /* Bug #11 fix: use name_hash instead of casting char* to unsigned int*
       (which violates strict aliasing and may be misaligned). */
    unsigned int rseed = name_hash(npc->name);

    switch (gt) {
    case GOAL_REST:
        if (npc->status == STATUS_TIRED) {
            ap_add_status_set(ap, npc_name, STATUS_NORMAL);
            return true;
        }
        return false;  /* nothing to do */

    case GOAL_EAT:
        if (npc->status == STATUS_HUNGRY) {
            ap_add_status_set(ap, npc_name, STATUS_NORMAL);
            /* Spend some money on food */
            ap_add_int_change(ap, npc_name, "money", -seeded_rand(
                &rseed, 5, 15));
            return true;
        }
        return false;

    case GOAL_HEAL:
        if (npc->status == STATUS_SICK || npc->status == STATUS_INJURED) {
            ap_add_status_set(ap, npc_name, STATUS_NORMAL);
            /* Medical expenses */
            ap_add_int_change(ap, npc_name, "money", -seeded_rand(
                &rseed, 20, 50));
            return true;
        }
        return false;

    case GOAL_WORK:
        /* Earn money */
        ap_add_int_change(ap, npc_name, "money", seeded_rand(
            &rseed, 10, 50));
        return true;

    case GOAL_TRAIN: {
        /* Improve a random skill */
        if (npc->skill_count > 0) {
            int si = seeded_rand(&rseed, 0, npc->skill_count - 1);
            ap_add_int_change(ap, npc_name, npc->skills[si].name,
                seeded_rand(&rseed, 1, 5));
        }
        return true;
    }

    case GOAL_SOCIALIZE:
        /* If target specified, improve relationship */
        if (goal->target[0]) {
            /* Check if target is the player */
            ap_add_int_change(ap, goal->target, "player_affinity",
                seeded_rand(&rseed, 1, 5));
        }
        return true;

    case GOAL_TRADE:
        /* Buy or sell — simple: adjust money */
        ap_add_int_change(ap, npc_name, "money", seeded_rand(
            &rseed, -30, 30));
        return true;

    case GOAL_WANDER:
    case GOAL_EXPLORE:
    case GOAL_OBSERVE:
    case GOAL_IDLE:
        /* Bug #10 fix: these goals produce no state changes, so return false
           to prevent the goal loop from advancing without effect.
           The plan step is still recorded as an event for narrative purposes. */
        return false;

    case GOAL_DEFEND:
    case GOAL_ATTACK: {
        /* Aggressive actions — could injure NPC or affect status */
        unsigned int rseed = name_hash(npc->name);
        ap_add_int_change(ap, npc_name, "money", seeded_rand(
            &rseed, -20, 5));
        return true;
    }

    case GOAL_CRAFT: {
        /* Spend money, potential to gain skill */
        unsigned int rseed = name_hash(npc->name);
        ap_add_int_change(ap, npc_name, "money", -seeded_rand(
            &rseed, 10, 40));
        return true;
    }

    default:
        return false;
    }
}

/* ═══════════════════════════════════════════════════════════════
   Main tick function
   ═══════════════════════════════════════════════════════════════ */

char* nb_tick(NpcBrain *brain, CharacterCard *npc, CharacterCard *player,
              RuleEngine *re, WorldState *ws, EventLog *events, ApiClient *api)
{
    if (!brain->active) return NULL;
    if (ws->tick < brain->next_think_tick) return NULL;

    long long current_tick = ws->tick;
    brain->last_think_tick = current_tick;

    /* ── Step 1: Check if new goal needed ── */
    if (nb_needs_new_goal(brain, current_tick)) {
        /* Clear expired goals */
        int i = 0;
        while (i < brain->goal_count) {
            if (brain->goals[i].expiry_tick > 0 &&
                current_tick >= brain->goals[i].expiry_tick) {
                LOG_D("NPC Brain: %s expired goal #%d (%s)", npc->name, i,
                         goal_type_str(brain->goals[i].type));
                nb_remove_goal(brain, i);
            } else {
                i++;
            }
        }

        /* Select new goal if still empty */
        if (brain->goal_count == 0) {
            GoalType gt = nb_select_goal(brain, npc, ws);
            if (gt != GOAL_IDLE) {
                /* Set expiry: most goals last a few days */
                long long expiry = current_tick + 2880; /* ~2 days in ticks */
                char params[64] = "";
                nb_add_goal(brain, gt, "", params, 50, current_tick, expiry);
                LOG_D("NPC Brain: %s new goal=%s", npc->name, goal_type_str(gt));
                brain->idle_rounds = 0;
            } else {
                LOG_D("NPC Brain: %s -> IDLE (round %d)", npc->name, brain->idle_rounds + 1);
                brain->idle_rounds++;
            }
        }
    }

    const Goal *goal = nb_top_goal(brain);
    if (!goal) {
        /* No goal — schedule next think and return */
        nb_schedule_next_think(brain, current_tick);
        return NULL;
    }

    /* ── Step 2: Generate plan if needed ── */
    if (brain->current_plan.step_count == 0 ||
        brain->current_plan.complete) {
        bool plan_ok = nb_generate_plan_simple(brain, goal, npc, ws);
        if (!plan_ok && api) {
            /* Try AI-assisted plan for complex goals */
            nb_generate_plan_ai(brain, goal, npc, ws, api);
        } else if (!plan_ok) {
            /* No API: single-step fallback */
            plan_init(&brain->current_plan);
            snprintf(brain->current_plan.goal, sizeof(brain->current_plan.goal),
                "%s", goal_type_cn(goal->type));
            snprintf(brain->current_plan.steps[0].action, PLAN_MAX_ACTION_LEN,
                "%s", goal_type_cn(goal->type));
            brain->current_plan.steps[0].estimated_ticks = 60;
            brain->current_plan.step_count = 1;
        }
        brain->current_plan.current_step = 0;
        brain->current_plan.complete = false;
    }

    /* ── Step 3: Generate ActionProposal ── */
    ActionProposal ap;
    ap_init(&ap);
    bool has_proposal = nb_generate_proposal(brain, npc, ws, &ap);

    /* ── Step 4: Process proposal through Rule Engine ── */
    int rules_fired = 0;
    if (has_proposal && ap.count > 0) {
        ChangeSetFull cs;
        cs_full_init(&cs);

        rules_fired = re_process_proposal(re, &ap, player, npc, 1, ws,
                                           &cs, events, current_tick);

        /* Apply the validated ChangeSet */
        if (cs.count > 0) {
            cs_apply(&cs, player, npc, 1, events, current_tick);
        }
    }

    /* ── Step 5: Record event ── */
    if (has_proposal) {
        event_push(events, current_tick, -1, -1, EVENT_ACTION,
            "{\"npc\":\"%s\",\"goal\":\"%s\",\"step\":%d}",
            npc->name, goal_type_str(goal->type),
            brain->current_plan.current_step);
    }

    /* ── Step 6: Advance plan (only if proposal was generated) ── */
    if (has_proposal) {
        brain->current_plan.current_step++;
        if (brain->current_plan.current_step >= brain->current_plan.step_count) {
            brain->current_plan.complete = true;
            nb_complete_top_goal(brain);
        }
    } else {
        /* Goal conditions not met (e.g., was TIRED but already fixed).
           Discard this goal so NPC can pick a new one next time. */
        nb_complete_top_goal(brain);
    }

    /* ── Step 7: Schedule next think ── */
    nb_schedule_next_think(brain, current_tick);

    /* ── Step 8: Build return summary ── */
    if (!has_proposal) {
        return NULL;
    }

    JsonBuf j; jb_init(&j); jb_obj_open(&j);
    jb_kv_str(&j, "npcName", npc->name);
    jb_kv_str(&j, "goalType", goal_type_str(goal->type));
    if (brain->current_plan.steps[brain->current_plan.current_step > 0 ?
        brain->current_plan.current_step - 1 : 0].action[0]) {
        jb_kv_str(&j, "action",
            brain->current_plan.steps[brain->current_plan.current_step > 0 ?
            brain->current_plan.current_step - 1 : 0].action);
    }
    if (rules_fired > 0) jb_kv_int(&j, "rulesFired", rules_fired);
    jb_obj_close(&j);
    return jb_detach(&j);
}

/* ═══════════════════════════════════════════════════════════════
   Scheduling
   ═══════════════════════════════════════════════════════════════ */

void nb_schedule_next_think(NpcBrain *brain, long long current_tick)
{
    unsigned int seed = name_hash((const char*)&brain->personality)
                      ^ (unsigned int)current_tick;
    int cooldown = seeded_rand(&seed, NB_THINK_COOLDOWN_MIN,
                               NB_THINK_COOLDOWN_MAX);
    brain->next_think_tick = current_tick + cooldown;
}

/* ═══════════════════════════════════════════════════════════════
   Export
   ═══════════════════════════════════════════════════════════════ */

int nb_export_state(const NpcBrain *brain, const CharacterCard *npc,
                    char *out, int out_size)
{
    int pos = 0;
    pos += snprintf(out + pos, out_size - pos,
        "NPC: %s  当前目标: ", npc->name);

    const Goal *top = (brain->goal_count > 0) ? &brain->goals[0] : NULL;
    if (top && !top->complete) {
        pos += snprintf(out + pos, out_size - pos,
            "%s", goal_type_cn(top->type));
        if (top->target[0])
            pos += snprintf(out + pos, out_size - pos, " → %s", top->target);
    } else {
        pos += snprintf(out + pos, out_size - pos, "无");
    }

    pos += snprintf(out + pos, out_size - pos,
        "  下次思考: tick %lld\n", brain->next_think_tick);

    return pos;
}

int nb_export_personality(const NpcBrain *brain, char *out, int out_size)
{
    const Personality *p = &brain->personality;
    return snprintf(out, out_size,
        "攻击性:%d 社交性:%d 好奇心:%d 勤劳度:%d 勇气:%d",
        p->aggressiveness, p->sociability, p->curiosity,
        p->industriousness, p->bravery);
}

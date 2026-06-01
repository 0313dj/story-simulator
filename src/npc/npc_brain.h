#ifndef NPC_BRAIN_H
#define NPC_BRAIN_H

#include <stdbool.h>
#include "rule.h"
#include "planner.h"
#include "api.h"

/* ═══════════════════════════════════════════════════════════════
   NPC Brain — per USE Spec Chapter 9 (Layer 2 Extension)

   Gives NPCs autonomous Goal → Plan → ActionProposal behavior.
   Each NPC with a NpcBrain periodically "thinks" on a throttled
   tick schedule, generating proposals that flow through the
   standard Rule Engine pipeline.

   Behavior chain:
     Goal(目标) → Plan(计划) → ActionProposal(提案) → RuleEngine → ChangeSet
   ═══════════════════════════════════════════════════════════════ */

#define NB_MAX_GOALS          8
#define NB_MAX_GOAL_PARAMS   128
#define NB_THINK_COOLDOWN_MIN 60     /* minimum ticks between thoughts (1 hour) */
#define NB_THINK_COOLDOWN_MAX 1440   /* maximum ticks between thoughts (1 day) */
#define NB_IDLE_TICKS         4320   /* ticks before an idle NPC generates a new goal (~3 days) */

/* ═══════════════════════════════════════════════════════════════
   Personality — drives NPC goal selection and behavior
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int aggressiveness;    /* 0-100: tendency toward ATTACK, DEFEND, CONFRONT */
    int sociability;       /* 0-100: tendency toward TALK, TRADE, SOCIALIZE */
    int curiosity;         /* 0-100: tendency toward EXPLORE, SEARCH, OBSERVE */
    int industriousness;   /* 0-100: tendency toward WORK, CRAFT, TRAIN */
    int bravery;           /* 0-100: willingness to take risks, confront danger */
} Personality;

/* ═══════════════════════════════════════════════════════════════
   GoalType — what the NPC wants to achieve
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    GOAL_IDLE,          /* 空闲 — no active goal */
    GOAL_SOCIALIZE,     /* 社交 — talk to someone */
    GOAL_TRADE,         /* 交易 — buy/sell items */
    GOAL_REST,          /* 休息 — recover from tired/injured status */
    GOAL_EAT,           /* 进食 — recover from hungry status */
    GOAL_EXPLORE,       /* 探索 — move to a new location */
    GOAL_WORK,          /* 工作 — earn money */
    GOAL_TRAIN,         /* 训练 — improve a skill */
    GOAL_CRAFT,         /* 制作 — create an item */
    GOAL_DEFEND,        /* 防御 — protect self or ally */
    GOAL_ATTACK,        /* 攻击 — confront an enemy */
    GOAL_HEAL,          /* 治疗 — recover from sick/injured */
    GOAL_WANDER,        /* 闲逛 — move aimlessly */
    GOAL_OBSERVE,       /* 观察 — watch surroundings */
} GoalType;

/* ═══════════════════════════════════════════════════════════════
   Goal — a single objective the NPC is working toward
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    GoalType type;
    char     target[64];              /* target entity/location name */
    char     params[NB_MAX_GOAL_PARAMS]; /* extra parameters */
    int      priority;                /* 0-100, higher = more urgent */
    long long created_tick;           /* when this goal was created */
    long long expiry_tick;            /* when this goal expires (-1 = no expiry) */
    bool     complete;                /* goal achieved */
} Goal;

/* ═══════════════════════════════════════════════════════════════
   NpcBrain — autonomous behavior controller for one NPC
   ═══════════════════════════════════════════════════════════════ */

struct NpcBrain {
    Goal      goals[NB_MAX_GOALS];    /* goal queue (sorted by priority) */
    int       goal_count;
    Plan      current_plan;            /* active execution plan */
    long long next_think_tick;        /* next tick when this NPC should think */
    long long last_think_tick;        /* last tick when thinking occurred */
    Personality personality;          /* drives behavior preferences */

    /* ── Runtime state ── */
    int       idle_rounds;            /* consecutive rounds with no goal */
    bool      active;                 /* whether brain is enabled for this NPC */
};

/* ═══════════════════════════════════════════════════════════════
   NPC Brain API
   ═══════════════════════════════════════════════════════════════ */

/* Initialize an NpcBrain with default (neutral) personality.
   Sets next_think_tick to a randomized future value so NPCs
   don't all think on the same tick. */
void nb_init(NpcBrain *brain, long long current_tick);

/* Generate a random personality influenced by the NPC's existing
   character card traits (attributes, skills, relations). */
void nb_generate_personality(NpcBrain *brain, const CharacterCard *npc);

/* Generate a deterministic personality seed from the NPC's name.
   Same name always produces the same personality. */
void nb_personality_from_name(NpcBrain *brain, const char *name);

/* ── Goal management ── */

/* Add a goal to the brain (inserts sorted by priority). Returns false if full. */
bool nb_add_goal(NpcBrain *brain, GoalType type, const char *target,
                 const char *params, int priority, long long tick, long long expiry);

/* Remove a goal by index. Returns true if removed. */
bool nb_remove_goal(NpcBrain *brain, int idx);

/* Clear all goals */
void nb_clear_goals(NpcBrain *brain);

/* Get the highest-priority incomplete goal, or NULL if none. */
const Goal* nb_top_goal(const NpcBrain *brain);

/* Mark the top goal as complete and remove it. */
void nb_complete_top_goal(NpcBrain *brain);

/* ── Goal selection ── */

/* Select a new goal based on personality + NPC state.
   Uses weighted random selection where personality traits
   bias the probability of each goal type.
   Returns the selected goal type. */
GoalType nb_select_goal(const NpcBrain *brain, const CharacterCard *npc,
                        const WorldState *ws);

/* Check if the NPC needs a new goal (current goals empty/expired,
   or idle_rounds exceeded threshold). Returns true if new goal needed. */
bool nb_needs_new_goal(const NpcBrain *brain, long long current_tick);

/* ── Plan generation ── */

/* Generate a simple plan for a goal. For simple goals (REST, EAT, WANDER),
   the plan is deterministic and requires no AI call. For complex goals
   (SOCIALIZE, TRADE, EXPLORE, WORK, TRAIN), returns false to indicate
   an AI-assisted plan is needed. */
bool nb_generate_plan_simple(NpcBrain *brain, const Goal *goal,
                             const CharacterCard *npc, const WorldState *ws);

/* Generate a plan using the AI model for complex goals.
   Returns true on success; the brain's current_plan is filled.
   Falls back to a simple single-step plan on API failure. */
bool nb_generate_plan_ai(NpcBrain *brain, const Goal *goal,
                          const CharacterCard *npc, const WorldState *ws,
                          ApiClient *api);

/* ── Action generation ── */

/* Convert the current plan step into an ActionProposal for the Rule Engine.
   Returns true if a proposal was generated. For simple actions, generates
   deterministic proposals; for complex actions, assembles from the plan. */
bool nb_generate_proposal(const NpcBrain *brain, const CharacterCard *npc,
                          const WorldState *ws, ActionProposal *ap);

/* ── Main tick function ── */

/* Process one NPC's brain for the current tick.
   Returns a malloc'd JSON string with a summary of what happened
   (intended for frontend display), or NULL if the NPC didn't act.
   The caller must free the returned string.

   This is the main entry point: it checks if the NPC should think,
   selects goals, generates plans, creates ActionProposals, processes
   them through the Rule Engine, and applies changes.

   Parameters:
     brain   - the NPC's brain (modified in-place)
     npc     - the NPC's character card (modified in-place)
     player  - the player's character card (may be modified, e.g. affinity)
     re      - the Rule Engine for proposal validation
     ws      - the WorldState for context
     events  - EventLog for recording actions
     api     - ApiClient for AI-assisted planning (can be NULL for simple only)

   Returns: malloc'd JSON string with {npcName, goalType, action, changes}
            or NULL if no action was taken. */
char* nb_tick(NpcBrain *brain, CharacterCard *npc, CharacterCard *player,
              RuleEngine *re, WorldState *ws, EventLog *events, ApiClient *api);

/* ── Utility ── */

/* Get a human-readable name for a GoalType */
const char *goal_type_str(GoalType t);

/* Export the brain's current state as a readable summary (for AI context) */
int nb_export_state(const NpcBrain *brain, const CharacterCard *npc,
                    char *out, int out_size);

/* Get a short description of the personality */
int nb_export_personality(const NpcBrain *brain, char *out, int out_size);

/* Set next think tick to a randomized cooldown from current tick */
void nb_schedule_next_think(NpcBrain *brain, long long current_tick);

#endif

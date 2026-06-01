#ifndef RULE_H
#define RULE_H

#include <stdbool.h>
#include "character_card.h"
#include "worldstate.h"
#include "event.h"
#include "changeset.h"

/* ═══════════════════════════════════════════════════════════════
   Rule Engine — per USE Spec Chapter 8 (Layer 1)

   All state changes MUST go through rule validation.
   Rules are evaluated post-change: after a proposed change, rules
   fire cascading effects if conditions are met.
   ═══════════════════════════════════════════════════════════════ */

#define RE_MAX_RULES          128
#define RE_MAX_EFFECTS         8
#define RE_MAX_CONDITION_LEN  256
#define RE_MAX_TARGET_LEN      64
#define RE_MAX_VALUE_LEN       64

/* ═══════════════════════════════════════════════════════════════
   Effect — what happens when a rule fires
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    EFFECT_SET_INT,       /* set an int field to a specific value */
    EFFECT_ADD_INT,       /* add a delta to an int field */
    EFFECT_SET_STATUS,    /* set a StatusType enum */
    EFFECT_SET_STRING,    /* set a string field */
    EFFECT_CLAMP,         /* clamp a field to min/max range */
} EffectType;

typedef struct {
    EffectType type;
    char target[RE_MAX_TARGET_LEN];  /* field name (e.g., "money", "status") */
    int  int_value;                  /* value for SET/ADD/CLAMP */
    int  int_value2;                 /* second value for CLAMP (max) */
    char str_value[RE_MAX_VALUE_LEN];/* value for SET_STRING */
} RuleEffect;

/* ═══════════════════════════════════════════════════════════════
   Rule — condition + cascading effects
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int         id;
    int         priority;                        /* higher = evaluated first */
    char        condition[RE_MAX_CONDITION_LEN]; /* DSL expression */
    RuleEffect  effects[RE_MAX_EFFECTS];
    int         effect_count;
    bool        enabled;
} Rule;

/* ═══════════════════════════════════════════════════════════════
   RuleEngine — manages the rule table
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    Rule  rules[RE_MAX_RULES];
    int   count;
    int   next_id;
} RuleEngine;

/* ═══════════════════════════════════════════════════════════════
   ActionProposal — AI-generated change proposal (JSON format)
   Parsed from AI output before being fed to Rule Engine.
   ═══════════════════════════════════════════════════════════════ */

#define AP_MAX_ACTIONS 32

typedef enum {
    AP_CHANGE_INT,       /* modify an integer field by delta */
    AP_SET_INT,          /* set an integer field to a value */
    AP_SET_STR,          /* set a string field */
    AP_SET_STATUS,       /* set a status enum */
} ActionType;

typedef struct {
    ActionType type;
    char entity[64];     /* target entity name */
    char field[64];      /* field name */
    int  delta;          /* delta (for AP_CHANGE_INT) */
    int  value;          /* value (for AP_SET_INT, AP_SET_STATUS) */
    char str_value[128]; /* value (for AP_SET_STR) */
} ActionProposalEntry;

typedef struct {
    ActionProposalEntry actions[AP_MAX_ACTIONS];
    int count;
} ActionProposal;

/* ═══════════════════════════════════════════════════════════════
   Rule Engine API
   ═══════════════════════════════════════════════════════════════ */

/* Initialise RuleEngine */
void re_init(RuleEngine *re);

/* Register a rule. Returns rule id, or -1 if full. */
int re_register(RuleEngine *re, int priority, const char *condition,
                const RuleEffect *effects, int effect_count);

/* Register built-in rules that ship with the engine */
void re_register_builtins(RuleEngine *re);

/* ── Condition evaluation ── */

/* Evaluate a single rule's condition against an entity and world state.
   Returns true if the condition matches (rule should fire). */
bool re_evaluate_condition(const Rule *rule, const CharacterCard *entity,
                           const WorldState *ws);

/* Evaluate all enabled rules against an entity.
   Fills hit_ids with matching rule IDs (max max_hits).
   Returns number of rules that matched. */
int re_evaluate_all(const RuleEngine *re, const CharacterCard *entity,
                    const WorldState *ws, int *hit_ids, int max_hits);

/* ── Effect application ── */

/* Apply a rule's effects to an entity, recording events.
   The entity is modified in-place. */
void re_apply_effects(const Rule *rule, CharacterCard *entity,
                      EventLog *events, long long tick);

/* ── ActionProposal processing ── */

/* Initialise an empty ActionProposal */
void ap_init(ActionProposal *ap);

/* Add an action to the proposal */
bool ap_add_int_change(ActionProposal *ap, const char *entity,
                       const char *field, int delta);
bool ap_add_int_set(ActionProposal *ap, const char *entity,
                    const char *field, int value);
bool ap_add_str_set(ActionProposal *ap, const char *entity,
                    const char *field, const char *value);
bool ap_add_status_set(ActionProposal *ap, const char *entity, int status_code);

/* Parse a legacy CHANGES text block into an ActionProposal.
   Returns number of actions parsed. */
int ap_parse_changes_text(const char *changes_text, ActionProposal *ap);

/* ── The main pipeline: Proposal → Rule Engine → ChangeSet → Apply ── */

/* Process an ActionProposal through the Rule Engine:
   1. Parse each action into a ChangeSet
   2. Evaluate rules against affected entities
   3. Apply triggered rule effects to ChangeSet
   4. Return the validated ChangeSet (caller applies it)
   Returns number of rules that fired. */
int re_process_proposal(const RuleEngine *re, const ActionProposal *ap,
                        CharacterCard *player, CharacterCard *npcs,
                        int npc_count, const WorldState *ws,
                        ChangeSetFull *out_cs, EventLog *events, long long tick);

#endif

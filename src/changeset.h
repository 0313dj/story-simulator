#ifndef CHANGESET_H
#define CHANGESET_H

#include <stdbool.h>
#include "character_card.h"
#include "event.h"

/* ═══════════════════════════════════════════════════════════════
   ChangeSet — per USE Spec Chapter 8
   The validated, structured output of the Rule Engine.
   ═══════════════════════════════════════════════════════════════ */

#define CS_MAX_CHANGES 64

/* ── Change type ── */
typedef enum {
    CS_VARIABLE,       /* entity variable delta change (e.g., money+50) */
    CS_SET,            /* entity field set to absolute value (e.g., status=NORMAL) */
    CS_RELATION,       /* relationship change (affinity delta) */
    CS_STATUS,         /* status enum change */
    CS_CLAMP,          /* clamp a field to [min, max] range */
} ChangeType;

/* ── A single atomic change ── */
typedef struct {
    ChangeType type;
    char entity_name[64];   /* target entity name ("player" or NPC name) */
    char field[64];         /* field name (e.g., "money", "attr.appearance", "player_affinity") */
    int  delta;             /* delta value (for CS_VARIABLE, CS_RELATION) */
    int  new_value;         /* new value (for CS_SET) */
    int  status_code;       /* status enum int (for CS_STATUS) */
} ChangeEntry;

/* ── ChangeSet: a collection of validated changes ── */
typedef struct {
    ChangeEntry entries[CS_MAX_CHANGES];
    int count;
} ChangeSet;

/* ═══════════════════════════════════════════════════════════════
   ChangeSet API
   ═══════════════════════════════════════════════════════════════ */

/* Initialise an empty ChangeSet */
void cs_init(ChangeSet *cs);

/* Add a variable delta change (e.g., money+50 or money-30) */
bool cs_add_delta(ChangeSet *cs, const char *entity, const char *field, int delta);

/* Add a set-to-value change (e.g., status=NORMAL) */
bool cs_add_set(ChangeSet *cs, const char *entity, const char *field, int new_value);

/* Add a status enum change */
bool cs_add_status(ChangeSet *cs, const char *entity, int status_code);

/* Add a string set change (e.g., name/field=string_value).
   The string is passed directly; caller must ensure it's already the final value. */
bool cs_add_set_str(ChangeSet *cs, const char *entity, const char *field,
                    const char *str_value, int str_value_len);

/* ── Internal field name for string values ── */
#define CS_STR_VALUE_LEN 128
typedef struct {
    ChangeType type;
    char entity_name[64];
    char field[64];
    int  delta;
    int  new_value;
    int  status_code;
    int  clamp_min;                      /* for CS_CLAMP: lower bound */
    int  clamp_max;                      /* for CS_CLAMP: upper bound */
    char str_value[CS_STR_VALUE_LEN];   /* for string set operations */
} ChangeEntryFull;

/* Full ChangeSet with string value support */
typedef struct {
    ChangeEntryFull entries[CS_MAX_CHANGES];
    int count;
} ChangeSetFull;

void cs_full_init(ChangeSetFull *cs);
bool cs_full_add_delta(ChangeSetFull *cs, const char *entity, const char *field, int delta);
bool cs_full_add_set_int(ChangeSetFull *cs, const char *entity, const char *field, int new_value);
bool cs_full_add_status(ChangeSetFull *cs, const char *entity, int status_code);
bool cs_full_add_set_str(ChangeSetFull *cs, const char *entity, const char *field,
                         const char *str_value);

/* Add a clamp operation (clamp field value to [min_val, max_val] range) */
bool cs_full_add_clamp(ChangeSetFull *cs, const char *entity, const char *field,
                       int min_val, int max_val);

/* ── Apply ChangeSet to game entities ── */

/* Apply a ChangeSetFull to the player and NPC array.
   Returns number of changes successfully applied. */
int cs_apply(const ChangeSetFull *cs, CharacterCard *player,
             CharacterCard *npcs, int npc_count,
             EventLog *events, long long tick);

/* Find an entity by name: returns pointer to CharacterCard or NULL.
   Checks player first, then npcs[0..npc_count-1]. */
CharacterCard* cs_find_entity(const char *name, CharacterCard *player,
                               CharacterCard *npcs, int npc_count);

#endif

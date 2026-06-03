#ifndef TYPEID_H
#define TYPEID_H

#include <stdint.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════
   TypeID — compact runtime identifier for game content types

   A TypeID is a 64-bit FNV-1a hash of a namespaced string like
   "race_void_elf" or "item_mana_potion". This enables fast integer
   comparisons while keeping the human-readable string for debugging.

   Design:
   - Namespaced naming: "race_human", "item_sword", "skill_fireball"
   - AI-generated content uses prefix "ai_" for collision avoidance
   - 64-bit hash has negligible collision probability for game use
   ═══════════════════════════════════════════════════════════════ */

typedef uint64_t TypeID;

/* Sentinel for unset / invalid TypeID */
#define TYPEID_NONE ((TypeID)0)

/* Compute TypeID from a namespaced string (e.g., "race_human").
   Returns TYPEID_NONE (0) for NULL or empty input. */
TypeID typeid_from_string(const char *str);

/* Compare two TypeIDs for equality */
static inline bool typeid_equals(TypeID a, TypeID b) { return a == b; }

/* Check if a TypeID is valid (non-zero) */
static inline bool typeid_valid(TypeID id) { return id != TYPEID_NONE; }

#endif

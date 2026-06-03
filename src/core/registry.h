#ifndef REGISTRY_H
#define REGISTRY_H

#include "typeid.h"
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════
   Registry — central type definition store

   Maps TypeID → Definition for all game content types (races, items,
   skills, factions, locations, events, professions, worlds).

   Each Definition is keyed by a unique TypeID (hash of its namespaced
   string identifier) and carries metadata about its source mod, version,
   and a JSON blob of attributes.

   This is the foundation of the Data-Driven Simulation Engine — all
   hardcoded type enums will eventually be replaced by Registry lookups.
   ═══════════════════════════════════════════════════════════════ */

#define REG_MAX_DEFS    1024
#define REG_MAX_NAME    128
#define REG_MAX_MODID    64
#define REG_BUCKETS      256

/* ── Definition — one registered game content type ── */
typedef struct {
    TypeID  id;                     /* hash of "race_human", etc. */
    char    name[REG_MAX_NAME];     /* display name (UTF-8) */
    char    mod_id[REG_MAX_MODID];  /* source mod identifier */
    char   *json_data;              /* malloc'd JSON attribute blob */
    int     version;                /* definition version number */
} Definition;

/* ── Registry — hash table of Definition pointers ── */
typedef struct {
    Definition **buckets[REG_BUCKETS];  /* array of Definition* per bucket */
    int          bucket_sizes[REG_BUCKETS];
    int          bucket_caps[REG_BUCKETS];
    int          total_count;
} Registry;

/* Initialise an empty Registry */
void registry_init(Registry *reg);

/* Register a new definition. Returns false if the TypeID already
   exists or allocation fails. The json_data string is copied. */
bool registry_register(Registry *reg, TypeID id, const char *name,
                       const char *mod_id, const char *json_data, int version);

/* Look up a definition by TypeID. Returns NULL if not found. */
const Definition *registry_get(const Registry *reg, TypeID id);

/* Look up a definition by name. Returns NULL if not found. */
const Definition *registry_get_by_name(const Registry *reg, const char *name);

/* Remove a definition by TypeID. Frees internal storage. */
bool registry_unregister(Registry *reg, TypeID id);

/* Free all definitions and reset the Registry */
void registry_destroy(Registry *reg);

/* Get the total number of registered definitions */
int registry_count(const Registry *reg);

#endif

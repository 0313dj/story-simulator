#ifndef MOD_LOADER_H
#define MOD_LOADER_H

#include "registry.h"
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════
   Mod Loader — discovers and loads mod packages

   A Mod is a directory under the mods/ root containing:
     mod.json              — manifest (id, name, version, dependencies)
     definitions/          — game content definitions
       races/   (*.json)
       items/   (*.json)
       skills/  (*.json)
       factions/(*.json)
       events/  (*.json)
       locations/(*.json)
     rules/                — custom rule engine rules (optional)
       custom_rules.json
     scripts/              — Lua init scripts (optional, future)

   Mods are loaded in topological order based on dependency graph.
   AI-generated mods use the "ai_" prefix for their IDs to avoid
   collisions with built-in and user-created content.
   ═══════════════════════════════════════════════════════════════ */

#define MOD_MAX_ID       64
#define MOD_MAX_NAME    128
#define MOD_MAX_VERSION  16
#define MOD_MAX_DEPS     16

/* ── Mod manifest parsed from mod.json ── */
typedef struct {
    char id[MOD_MAX_ID];
    char name[MOD_MAX_NAME];
    char version[MOD_MAX_VERSION];
    char dependencies[MOD_MAX_DEPS][MOD_MAX_ID];
    int  dep_count;
} ModManifest;

/* ── Mod Loader ── */

/* Scan a mods root directory, discover all valid mod packages,
   and register their definitions into the given Registry.
   Returns the number of mods loaded, or -1 on error. */
int mod_scan_directory(const char *mods_root, Registry *reg);

/* Load a single mod by its directory path. Validates the manifest,
   parses all definitions, and registers them. Returns true on success. */
bool mod_load(const char *mod_path, Registry *reg);

/* Validate a mod manifest. Returns true if all required fields
   are present and the dependency list is well-formed. */
bool mod_validate_manifest(const ModManifest *m);

/* Unload a mod: removes all definitions with the given mod_id
   from the Registry. Returns the number of definitions removed. */
int mod_unload(const char *mod_id, Registry *reg);

/* Parse a mod.json file into a ModManifest struct.
   Returns true on success. */
bool mod_parse_manifest(const char *json_text, ModManifest *out);

#endif

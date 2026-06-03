#ifndef BACKEND_INTERNAL_H
#define BACKEND_INTERNAL_H

/* ═══════════════════════════════════════════════════════════════
   Internal header shared across backend_*.c modules.
   Exposes the global game context and functions that were
   previously file-static in backend.c.
   ═══════════════════════════════════════════════════════════════ */

#include "context.h"
#include "character_card.h"
#include "environment.h"
#include "api.h"
#include "map.h"
#include "npc.h"
#include "json.h"
#include "log.h"
#include "worldstate.h"
#include "event.h"
#include "rule.h"
#include "changeset.h"
#include "npc_brain.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ── Global game context (defined in backend.c) ── */
extern GameContext g_ctx;

/* ── State JSON building (backend_state.c) ── */
void state_player_json(JsonBuf *j);
void state_location_json(JsonBuf *j);
void state_environment_json(JsonBuf *j);
void state_npcs_json(JsonBuf *j);
bool npc_card_valid(const CharacterCard *c);
char *build_state_json(void);

/* ── Save/load (backend_save.c) ── */
bool validate_save_name(const char *name);
char *ok_json(const char *data_key, const char *data_json);
char *err_json(const char *msg);
bool write_json_file(const char *path, const char *json);
bool save_to_file(const char *name);
const char *extract_sub_obj(const char *json, const char *key);
int  json_parse_array(const char *json, const char *key,
                      void (*cb)(const char *obj, void *ud, int idx),
                      void *ud);
void parse_skill_cb(const char *obj, void *ud, int idx);
void parse_item_cb(const char *obj, void *ud, int idx);
void parse_relation_cb(const char *obj, void *ud, int idx);
void parse_present_npc_cb(const char *obj, void *ud, int idx);
void parse_npc_obj(const char *obj, void *ud, int idx);
char *slurp_file(const char *path);
void parse_player_scope(const char *scope);
bool load_from_file(const char *name);
int  next_save_number(void);

/* ── NPC helpers ── */
void npc_brains_init(void);
void npc_brains_free(void);
char *npc_brains_tick_all(void);
bool name_in_list(const char *name, const char *list);
bool npc_is_present(const char *name);
int  npc_compact_line(const CharacterCard *c, char *out, int out_size);
int  npc_full_entry(const CharacterCard *c, char *out, int out_size);
void cc_free_internals(CharacterCard *cc);
int  find_oldest_npc(void);

/* ── Response processing ── */
void apply_response(struct FullResponse *resp);

/* ── Variable helpers ── */
void build_var_catalog(char *out, int out_size);
void extract_var_values(const char *var_names, char *out, int out_size);

/* ── Sync ── */
void ws_sync_from_globals(void);

/* ── Misc ── */
#if !defined(strtok_s) && !defined(_MSC_VER)
#define strtok_s(s, d, c) strtok_r(s, d, c)
#endif

#endif /* BACKEND_INTERNAL_H */
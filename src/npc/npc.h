#ifndef NPC_H
#define NPC_H

#include <stdbool.h>
#include "character_card.h"
#include "environment.h"
#include "json.h"

/* Bug #51: MAX_NPC_CARDS defined here so all files use the same value */
#define MAX_NPC_CARDS   25
#define MAX_TEMP_NPCS   32
#define MAX_NPC_NAME    64
#define MAX_NPC_DESC    256
#define MAX_NPC_LOC     128

/* ── Per-location temp NPC cache ── */
#define MAX_CACHED_LOCS     32
#define MAX_NPC_PER_CACHE   8

/* 临时NPC（无角色卡，地点/事件触发时由AI创建） */
typedef struct {
    char name[MAX_NPC_NAME];
    char description[MAX_NPC_DESC];
    char location[MAX_NPC_LOC];   /* 出现地点 */
} TempNpc;

/* Per-location cache slot */
typedef struct {
    char     location[MAX_NPC_LOC];   /* "area/district/spot" key */
    TempNpc  temps[MAX_NPC_PER_CACHE];
    int      temp_count;
} LocTempCacheSlot;

/* 有角色卡的NPC生成决策 */
typedef struct {
    char name[MAX_NPC_NAME];
    char location[MAX_NPC_LOC];   /* 出现地点 */
    bool present;                  /* 当前是否在场 */
    int  category;                /* 0=一定生成 1=可能(20%) 2=不生成 */
    int  player_affinity;         /* 与玩家好感度 */
} NpcSpawn;

/* NPC管理器 */
typedef struct NpcManager {
    TempNpc  temps[MAX_TEMP_NPCS];
    int      temp_count;

    NpcSpawn spawns[MAX_TEMP_NPCS];
    int      spawn_count;

    /* Per-location temp NPC cache — survives location changes */
    LocTempCacheSlot loc_cache[MAX_CACHED_LOCS];
    int              loc_cache_count;
} NpcManager;

/* 初始化 */
void npc_init(NpcManager *mgr);

/* 清空临时NPC */
void npc_clear_temps(NpcManager *mgr);

/* 添加临时NPC */
bool npc_add_temp(NpcManager *mgr, const char *name, const char *desc, const char *loc);

/* 设置生成决策列表 */
void npc_set_spawns(NpcManager *mgr, const NpcSpawn *list, int count);

/* 执行生成判定：MUST直接在场，MAYBE按20%骰子，NEVER清除 */
void npc_resolve_spawns(NpcManager *mgr);

/* 获取当前在场的NPC数量（含临时+角色卡） */
int npc_present_count(const NpcManager *mgr);

/* 格式化在场NPC列表为文本（发给AI用） */
int npc_export_present(const NpcManager *mgr, char *out, int out_size);

/* 解析AI返回的NPC_SPAWN行 */
bool npc_parse_spawn_line(const char *line, NpcSpawn *sp);

/* 解析AI返回的NPC_TEMP行 */
bool npc_parse_temp_line(const char *line, TempNpc *tp);

/* 解析AI返回的NPC_CREATE文本，填充一张完整角色卡。成功返回true */
bool npc_parse_create(const char *text, CharacterCard *card);

/* ── Per-location temp NPC cache ── */

/* Build location key "area/district/spot" from a Location */
void npc_make_location_key(const Location *loc, char *out, int out_size);

/* Save active temps into cache for the given location.
   Creates or overwrites slot; FIFO-evicts if at MAX_CACHED_LOCS. */
void npc_cache_temps(NpcManager *mgr, const char *location_key);

/* Restore cached temps for location into active temps array.
   No-op if no cache entry exists (first visit). */
void npc_restore_temps(NpcManager *mgr, const char *location_key);

/* Serialize entire location cache as a JSON array (writes into JsonBuf) */
void npc_cache_to_json(const NpcManager *mgr, JsonBuf *j);

/* Parse location cache from state JSON. Returns number of slots loaded. */
int npc_parse_cache_from_json(NpcManager *mgr, const char *json);

#endif

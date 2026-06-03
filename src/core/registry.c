#include "registry.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/* ── Internal: hash a TypeID to a bucket index ── */
static int bucket_index(TypeID id)
{
    /* Mix the 64-bit hash into a bucket index */
    uint64_t h = id;
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    return (int)(h % REG_BUCKETS);
}

void registry_init(Registry *reg)
{
    if (!reg) return;
    memset(reg, 0, sizeof(*reg));
}

bool registry_register(Registry *reg, TypeID id, const char *name,
                       const char *mod_id, const char *json_data, int version)
{
    if (!reg || !typeid_valid(id) || !name) return false;
    if (reg->total_count >= REG_MAX_DEFS) {
        LOG_W("Registry: full (%d max)", REG_MAX_DEFS);
        return false;
    }

    /* Check for duplicate TypeID */
    if (registry_get(reg, id)) {
        LOG_W("Registry: duplicate TypeID %llu", (unsigned long long)id);
        return false;
    }

    int bi = bucket_index(id);
    int cap = reg->bucket_caps[bi];
    int sz  = reg->bucket_sizes[bi];

    /* Grow bucket if needed */
    if (sz >= cap) {
        int new_cap = cap ? cap * 2 : 4;
        Definition **new_bucket = realloc(reg->buckets[bi],
                                          (size_t)new_cap * sizeof(Definition *));
        if (!new_bucket) return false;
        reg->buckets[bi] = new_bucket;
        reg->bucket_caps[bi] = new_cap;
    }

    /* Allocate and populate the Definition */
    Definition *def = malloc(sizeof(Definition));
    if (!def) return false;
    memset(def, 0, sizeof(*def));

    def->id = id;
    safe_strcpy(def->name, name, REG_MAX_NAME);
    safe_strcpy(def->mod_id, mod_id ? mod_id : "builtin", REG_MAX_MODID);
    if (json_data) {
        def->json_data = malloc(strlen(json_data) + 1);
        if (def->json_data)
            strcpy(def->json_data, json_data);
    }
    def->version = version;

    reg->buckets[bi][sz] = def;
    reg->bucket_sizes[bi] = sz + 1;
    reg->total_count++;

    return true;
}

const Definition *registry_get(const Registry *reg, TypeID id)
{
    if (!reg || !typeid_valid(id)) return NULL;

    int bi = bucket_index(id);
    int sz = reg->bucket_sizes[bi];
    for (int i = 0; i < sz; i++) {
        if (reg->buckets[bi][i]->id == id)
            return reg->buckets[bi][i];
    }
    return NULL;
}

const Definition *registry_get_by_name(const Registry *reg, const char *name)
{
    if (!reg || !name) return NULL;

    for (int b = 0; b < REG_BUCKETS; b++) {
        int sz = reg->bucket_sizes[b];
        for (int i = 0; i < sz; i++) {
            if (strcmp(reg->buckets[b][i]->name, name) == 0)
                return reg->buckets[b][i];
        }
    }
    return NULL;
}

bool registry_unregister(Registry *reg, TypeID id)
{
    if (!reg || !typeid_valid(id)) return false;

    int bi = bucket_index(id);
    int sz = reg->bucket_sizes[bi];
    for (int i = 0; i < sz; i++) {
        if (reg->buckets[bi][i]->id == id) {
            Definition *def = reg->buckets[bi][i];
            free(def->json_data);
            free(def);
            /* Compact the bucket */
            if (i < sz - 1)
                memmove(&reg->buckets[bi][i], &reg->buckets[bi][i + 1],
                        (size_t)(sz - i - 1) * sizeof(Definition *));
            reg->bucket_sizes[bi] = sz - 1;
            reg->total_count--;
            return true;
        }
    }
    return false;
}

void registry_destroy(Registry *reg)
{
    if (!reg) return;

    for (int b = 0; b < REG_BUCKETS; b++) {
        int sz = reg->bucket_sizes[b];
        for (int i = 0; i < sz; i++) {
            Definition *def = reg->buckets[b][i];
            free(def->json_data);
            free(def);
        }
        free(reg->buckets[b]);
        reg->buckets[b] = NULL;
        reg->bucket_sizes[b] = 0;
        reg->bucket_caps[b] = 0;
    }
    reg->total_count = 0;
}

int registry_count(const Registry *reg)
{
    return reg ? reg->total_count : 0;
}

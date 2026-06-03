#include "typeid.h"
#include <string.h>

/* FNV-1a 64-bit constants */
#define FNV_OFFSET UINT64_C(0xcbf29ce484222325)
#define FNV_PRIME  UINT64_C(0x00000100000001b3)

TypeID typeid_from_string(const char *str)
{
    if (!str || !*str) return TYPEID_NONE;

    uint64_t hash = FNV_OFFSET;
    while (*str) {
        hash ^= (uint64_t)(unsigned char)*str;
        hash *= FNV_PRIME;
        str++;
    }
    return hash;
}

#include "mod_loader.h"
#include "json.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ═══════════════════════════════════════════════════════════════
   Manifest parsing
   ═══════════════════════════════════════════════════════════════ */

bool mod_parse_manifest(const char *json_text, ModManifest *out)
{
    if (!json_text || !out) return false;
    memset(out, 0, sizeof(*out));

    json_get_str(json_text, "id",      out->id,      MOD_MAX_ID);
    json_get_str(json_text, "name",    out->name,    MOD_MAX_NAME);
    json_get_str(json_text, "version", out->version, MOD_MAX_VERSION);

    /* Parse dependencies array (simple comma-separated approach) */
    char deps_raw[512] = {0};
    json_get_str(json_text, "dependencies", deps_raw, sizeof(deps_raw));
    if (deps_raw[0]) {
        /* deps_raw might be a JSON array like '["a","b"]' or already just names */
        char *p = deps_raw;
        /* Strip brackets if present */
        while (*p == '[' || *p == ']' || *p == '"' || *p == ' ') p++;
        char *token = p;
        while (token && *token && out->dep_count < MOD_MAX_DEPS) {
            char *comma = strchr(token, ',');
            if (comma) *comma = '\0';
            /* Strip quotes and whitespace */
            while (*token == '"' || *token == ' ') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && (*end == '"' || *end == ' ')) *end-- = '\0';
            if (*token) {
                safe_strcpy(out->dependencies[out->dep_count], token, MOD_MAX_ID);
                out->dep_count++;
            }
            token = comma ? comma + 1 : NULL;
        }
    }

    return out->id[0] != '\0';
}

bool mod_validate_manifest(const ModManifest *m)
{
    if (!m || !m->id[0]) return false;
    if (!m->version[0]) return false;
    /* Dependencies are optional; no circular check here (done at load time) */
    return true;
}

/* ═══════════════════════════════════════════════════════════════
   Single mod loading
   ═══════════════════════════════════════════════════════════════ */

/* Read an entire file into a malloc'd string. Returns NULL on failure. */
static char *slurp_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1048576) { fclose(fp); return NULL; } /* max 1MB */

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }

    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[rd] = '\0';
    return buf;
}

/* Parse a single definition JSON file and register it */
static bool load_definition_file(const char *filepath, const char *mod_id,
                                  Registry *reg)
{
    char *json = slurp_file(filepath);
    if (!json) return false;

    /* Extract required fields */
    char def_id_str[128] = {0};
    char def_name[128] = {0};
    int  version = 1;

    json_get_str(json, "id",      def_id_str, sizeof(def_id_str));
    json_get_str(json, "name",    def_name,    sizeof(def_name));
    json_get_int(json, "version", &version);

    if (!def_id_str[0]) {
        LOG_W("mod_loader: %s missing 'id' field, skipping", filepath);
        free(json);
        return false;
    }

    TypeID tid = typeid_from_string(def_id_str);
    if (!typeid_valid(tid)) {
        free(json);
        return false;
    }

    bool ok = registry_register(reg, tid, def_name[0] ? def_name : def_id_str,
                                mod_id, json, version);
    if (!ok)
        LOG_W("mod_loader: failed to register %s from %s", def_id_str, filepath);

    free(json);
    return ok;
}

/* Scan a directory for .json files and load them as definitions */
static int load_definitions_dir(const char *dir_path, const char *mod_id,
                                Registry *reg)
{
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.json", dir_path);

    WIN32_FIND_DATAW fd;
    wchar_t wpattern[512];
    MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wpattern, 512);

    HANDLE h = FindFirstFileW(wpattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int loaded = 0;
    do {
        char fname[256];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, fname, 256, NULL, NULL);
        char fpath[768];
        snprintf(fpath, sizeof(fpath), "%s\\%s", dir_path, fname);

        if (load_definition_file(fpath, mod_id, reg))
            loaded++;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return loaded;
}

bool mod_load(const char *mod_path, Registry *reg)
{
    if (!mod_path || !reg) return false;

    /* Read mod.json */
    char manifest_path[768];
    snprintf(manifest_path, sizeof(manifest_path), "%s\\mod.json", mod_path);

    char *json = slurp_file(manifest_path);
    if (!json) {
        LOG_W("mod_loader: %s not found", manifest_path);
        return false;
    }

    ModManifest mf;
    if (!mod_parse_manifest(json, &mf)) {
        LOG_W("mod_loader: invalid manifest in %s", manifest_path);
        free(json);
        return false;
    }
    free(json);

    if (!mod_validate_manifest(&mf)) {
        LOG_W("mod_loader: manifest validation failed for %s", mf.id);
        return false;
    }

    LOG_I("mod_loader: loading mod '%s' v%s from %s", mf.name, mf.version, mod_path);

    /* Load definition subdirectories */
    int total = 0;
    const char *def_types[] = {"races", "items", "skills", "factions",
                                "events", "locations", "professions", NULL};
    for (int i = 0; def_types[i]; i++) {
        char dir[768];
        snprintf(dir, sizeof(dir), "%s\\definitions\\%s", mod_path, def_types[i]);
        int n = load_definitions_dir(dir, mf.id, reg);
        if (n > 0)
            LOG_I("mod_loader:   %s: %d definition(s)", def_types[i], n);
        total += n;
    }

    LOG_I("mod_loader: loaded %d total definitions for mod '%s'", total, mf.id);
    return true;
}

/* ═══════════════════════════════════════════════════════════════
   Directory scanning
   ═══════════════════════════════════════════════════════════════ */

int mod_scan_directory(const char *mods_root, Registry *reg)
{
    if (!mods_root || !reg) return -1;

    /* Check if mods_root exists */
    DWORD attr = GetFileAttributesA(mods_root);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        LOG_I("mod_loader: mods directory '%s' not found, skipping", mods_root);
        return 0;
    }

    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", mods_root);

    WIN32_FIND_DATAW fd;
    wchar_t wpattern[512];
    MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wpattern, 512);

    HANDLE h = FindFirstFileW(wpattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int loaded = 0;
    do {
        /* Skip . and .. */
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        /* Only process directories */
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;

        char mod_name[256];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, mod_name, 256, NULL, NULL);

        char mod_path[768];
        snprintf(mod_path, sizeof(mod_path), "%s\\%s", mods_root, mod_name);

        if (mod_load(mod_path, reg))
            loaded++;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    LOG_I("mod_loader: %d mod(s) loaded", loaded);
    return loaded;
}

/* ═══════════════════════════════════════════════════════════════
   Mod unloading
   ═══════════════════════════════════════════════════════════════ */

int mod_unload(const char *mod_id, Registry *reg)
{
    if (!mod_id || !reg) return 0;

    int removed = 0;
    /* Walk all definitions, collect TypeIDs for this mod, then unregister */
    /* Since registry_unregister modifies the bucket during iteration,
       we collect IDs first. */
    TypeID to_remove[REG_MAX_DEFS];
    int count = 0;

    for (int b = 0; b < REG_BUCKETS && count < REG_MAX_DEFS; b++) {
        int sz = reg->bucket_sizes[b];
        for (int i = 0; i < sz; i++) {
            if (strcmp(reg->buckets[b][i]->mod_id, mod_id) == 0) {
                to_remove[count++] = reg->buckets[b][i]->id;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        if (registry_unregister(reg, to_remove[i]))
            removed++;
    }

    LOG_I("mod_loader: unloaded %d definitions for mod '%s'", removed, mod_id);
    return removed;
}

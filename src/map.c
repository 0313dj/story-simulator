#include "map.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 坐标缩放：坐标单位 → 公里。坐标范围0-999，对应约0-500km */
#define COORD_SCALE  0.5

void map_init(GameMap *map)
{
    memset(map, 0, sizeof(*map));
    map->zoom = 2;
    for (int i = 0; i < 3; i++) {
        map->parent_idx[i] = -1;
        map->selected[i]   = -1;
    }
    strcpy(map->levels[2].level_name, "大地点");
    strcpy(map->levels[1].level_name, "小地点");
    strcpy(map->levels[0].level_name, "具体地点");
}

double map_distance(const MapPoint *a, const MapPoint *b, double scale)
{
    double dx = (double)(a->x - b->x);
    double dy = (double)(a->y - b->y);
    return sqrt(dx * dx + dy * dy) * scale;
}

double map_selected_distance(const GameMap *map, int from_idx, int to_idx)
{
    const MapLevel *level = &map->levels[map->zoom];
    if (from_idx < 0 || from_idx >= level->count) return 0.0;
    if (to_idx   < 0 || to_idx   >= level->count) return 0.0;
    return map_distance(&level->points[from_idx], &level->points[to_idx], COORD_SCALE);
}

void map_zoom_to(GameMap *map, int level)
{
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    map->zoom = level;
}

void map_select(GameMap *map, int index)
{
    MapLevel *level = &map->levels[map->zoom];
    if (index < -1 || index >= level->count) return;
    map->selected[map->zoom] = index;
}

const char *map_current_level_name(const GameMap *map)
{
    return map->levels[map->zoom].level_name;
}

const MapLevel *map_current_level(const GameMap *map)
{
    return &map->levels[map->zoom];
}

bool map_locate(GameMap *map, const char *area, const char *district, const char *spot)
{
    /* 在 L3 中找 area */
    MapLevel *l3 = &map->levels[2];
    for (int i = 0; i < l3->count; i++) {
        if (strcmp(l3->points[i].name, area) == 0) {
            map->parent_idx[2] = i;
            map->selected[2]   = i;
            break;
        }
    }
    /* 在 L2 中找 district */
    MapLevel *l2 = &map->levels[1];
    for (int i = 0; i < l2->count; i++) {
        if (strcmp(l2->points[i].name, district) == 0) {
            map->parent_idx[1] = i;
            map->selected[1]   = i;
            break;
        }
    }
    /* 在 L1 中找 spot */
    MapLevel *l1 = &map->levels[0];
    for (int i = 0; i < l1->count; i++) {
        if (strcmp(l1->points[i].name, spot) == 0) {
            map->parent_idx[0] = i;
            map->selected[0]   = i;
            break;
        }
    }
    return true;
}

/* 在某级地图中查找点，返回索引，-1=未找到 */
static int find_point(const MapLevel *level, const char *name)
{
    for (int i = 0; i < level->count; i++) {
        if (strcmp(level->points[i].name, name) == 0) return i;
    }
    return -1;
}

/* 自由散布坐标（L3用） */
static void gen_coords_free(const MapLevel *level, int *x, int *y, int min_dist)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        int cx = rand() % 900 + 50;
        int cy = rand() % 900 + 50;
        bool ok = true;
        for (int i = 0; i < level->count; i++) {
            int dx = abs(cx - level->points[i].x);
            int dy = abs(cy - level->points[i].y);
            if (dx * dx + dy * dy < min_dist * min_dist) { ok = false; break; }
        }
        if (ok || level->count == 0) { *x = cx; *y = cy; return; }
    }
    *x = rand() % 900 + 50;
    *y = rand() % 900 + 50;
}

/* 聚类在父点附近的坐标（L2/L1用） */
static void gen_coords_near(const MapLevel *level, int *x, int *y,
    int px, int py, int radius, int min_dist)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        int cx = px + (rand() % (radius * 2)) - radius;
        int cy = py + (rand() % (radius * 2)) - radius;
        if (cx < 10) cx = 10; if (cx > 990) cx = 990;
        if (cy < 10) cy = 10; if (cy > 990) cy = 990;
        bool ok = true;
        for (int i = 0; i < level->count; i++) {
            int dx = abs(cx - level->points[i].x);
            int dy = abs(cy - level->points[i].y);
            if (dx * dx + dy * dy < min_dist * min_dist) { ok = false; break; }
        }
        if (ok || level->count == 0) { *x = cx; *y = cy; return; }
    }
    *x = px + (rand() % (radius * 2)) - radius;
    *y = py + (rand() % (radius * 2)) - radius;
    if (*x < 10) *x = 10; if (*x > 990) *x = 990;
    if (*y < 10) *y = 10; if (*y > 990) *y = 990;
}

/* 添加点：自由散布（L3） */
static bool add_point_free(MapLevel *level, const char *name, int min_dist)
{
    if (level->count >= MAX_MAP_POINTS) return false;
    int idx = find_point(level, name);
    if (idx >= 0) return true;

    MapPoint *p = &level->points[level->count];
    strncpy(p->name, name, MAP_NAME_LEN - 1);
    p->name[MAP_NAME_LEN - 1] = '\0';
    gen_coords_free(level, &p->x, &p->y, min_dist);
    level->count++;
    return true;
}

/* 添加点：聚类在父坐标附近（L2/L1） */
static bool add_point_near(MapLevel *level, const char *name,
    int px, int py, int radius, int min_dist)
{
    if (level->count >= MAX_MAP_POINTS) return false;
    int idx = find_point(level, name);
    if (idx >= 0) return true;

    MapPoint *p = &level->points[level->count];
    strncpy(p->name, name, MAP_NAME_LEN - 1);
    p->name[MAP_NAME_LEN - 1] = '\0';
    gen_coords_near(level, &p->x, &p->y, px, py, radius, min_dist);
    level->count++;
    return true;
}

bool map_location_exists(const GameMap *map,
    const char *area, const char *district, const char *spot)
{
    bool has_area = !area    || !area[0]    || find_point(&map->levels[2], area)     >= 0;
    bool has_dist = !district || !district[0] || find_point(&map->levels[1], district) >= 0;
    bool has_spot = !spot    || !spot[0]    || find_point(&map->levels[0], spot)     >= 0;
    return has_area && has_dist && has_spot;
}

void map_ensure_location(GameMap *map,
    const char *area, const char *district, const char *spot)
{
    MapLevel *l3 = &map->levels[2];
    MapLevel *l2 = &map->levels[1];
    MapLevel *l1 = &map->levels[0];

    /* 1. 确保L3存在，获取其坐标 */
    int l3x = 500, l3y = 500; /* 兜底 */
    if (area && area[0]) {
        add_point_free(l3, area, 150);
        int idx = find_point(l3, area);
        if (idx >= 0) { l3x = l3->points[idx].x; l3y = l3->points[idx].y; }
    }

    /* 2. 确保L2存在，聚类在L3点附近 */
    int l2x = l3x, l2y = l3y;
    if (district && district[0]) {
        add_point_near(l2, district, l3x, l3y, 40, 15);
        int idx = find_point(l2, district);
        if (idx >= 0) { l2x = l2->points[idx].x; l2y = l2->points[idx].y; }
    }

    /* 3. 确保L1存在，聚类在L2点附近 */
    if (spot && spot[0]) {
        add_point_near(l1, spot, l2x, l2y, 40, 10);
    }
}

void map_import_json(GameMap *map, const char *json)
{
    /* Clear existing map data first */
    for (int lv = 0; lv < 3; lv++) {
        map->levels[lv].count = 0;
        map->parent_idx[lv] = -1;
        map->selected[lv] = -1;
    }

    if (!json || !json[0]) return;

    /* Parse JSON array of {name, x, y, level} objects */
    const char *p = json;
    /* Find opening '[' */
    while (*p && *p != '[') p++;
    if (!*p) return;
    p++;

    while (*p) {
        /* Skip whitespace and commas */
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',') p++;
        if (*p == ']' || *p == '\0') break;
        if (*p != '{') { p++; continue; }

        char name[MAP_NAME_LEN] = {0};
        int x = -1, y = -1, level = -1;

        /* Extract fields from the JSON object */
        const char *obj_end = strchr(p, '}');
        if (!obj_end) break;

        /* Find "name" */
        const char *nk = strstr(p, "\"name\"");
        if (nk && nk < obj_end) {
            const char *nv = strchr(nk + 6, ':');
            if (nv && nv < obj_end) {
                nv = strchr(nv, '"');
                if (nv && nv < obj_end) {
                    nv++;
                    const char *ne = strchr(nv, '"');
                    if (ne && ne < obj_end) {
                        int len = (int)(ne - nv);
                        if (len >= MAP_NAME_LEN) len = MAP_NAME_LEN - 1;
                        /* Bug #36: ensure null termination after memcpy */
                        if (len > 0) memcpy(name, nv, len);
                        name[len] = '\0';
                    }
                }
            }
        }
        /* Find "x" — Bug #35: bounds-check all strstr/strchr results */
        const char *xk = strstr(p, "\"x\"");
        if (xk && xk < obj_end) {
            const char *xv = strchr(xk + 2, ':');
            if (xv && xv < obj_end) {
                const char *pv = xv + 1;
                if (pv < obj_end) x = atoi(pv);
            }
        }
        /* Find "y" */
        const char *yk = strstr(p, "\"y\"");
        if (yk && yk < obj_end) {
            const char *yv = strchr(yk + 2, ':');
            if (yv && yv < obj_end) {
                const char *pv = yv + 1;
                if (pv < obj_end) y = atoi(pv);
            }
        }
        /* Find "level" */
        const char *lk = strstr(p, "\"level\"");
        if (lk && lk < obj_end) {
            const char *lv_v = strchr(lk + 7, ':');
            if (lv_v && lv_v < obj_end) {
                const char *pv = lv_v + 1;
                if (pv < obj_end) level = atoi(pv);
            }
        }

        if (name[0] && x >= 0 && y >= 0 && level >= 0 && level < 3) {
            MapLevel *ml = &map->levels[level];
            if (ml->count < MAX_MAP_POINTS) {
                strncpy(ml->points[ml->count].name, name, MAP_NAME_LEN - 1);
                ml->points[ml->count].name[MAP_NAME_LEN - 1] = '\0';
                ml->points[ml->count].x = x;
                ml->points[ml->count].y = y;
                ml->count++;
            }
        }

        p = obj_end + 1;
    }
}

int map_export_json(const GameMap *map, char *out, int out_size)
{
    int p = 0;
    p += snprintf(out + p, out_size - p, "[");
    for (int lv = 2; lv >= 0; lv--) {
        const MapLevel *level = &map->levels[lv];
        for (int i = 0; i < level->count; i++) {
            if (p > 1) p += snprintf(out + p, out_size - p, ",");
            p += snprintf(out + p, out_size - p,
                "{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"level\":%d}",
                level->points[i].name,
                level->points[i].x,
                level->points[i].y, lv);
        }
    }
    p += snprintf(out + p, out_size - p, "]");
    return p;
}

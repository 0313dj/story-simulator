#ifndef MAP_H
#define MAP_H

#include <stdbool.h>

#define MAX_MAP_POINTS  32
#define MAX_MAP_LEVELS   3
#define MAP_NAME_LEN    64

/* 单个地图点 */
typedef struct {
    char name[MAP_NAME_LEN];
    int  x, y;          /* 该级地图上的坐标 (0-999) */
} MapPoint;

/* 一级地图 */
typedef struct {
    MapPoint points[MAX_MAP_POINTS];
    int      count;
    char     level_name[32];  /* "具体地点" / "小地点" / "大地点" */
} MapLevel;

/* 完整地图：三级联动 */
typedef struct {
    MapLevel levels[3];       /* [0]=具体地点(L1) [1]=小地点(L2) [2]=大地点(L3) */
    int      parent_idx[3];   /* 上级地图中被选中的点索引 (-1=未选) */
    int      selected[3];     /* 当前级被选中的点索引 (-1=未选) */
    int      zoom;            /* 当前缩放级 0=最近(L1) 1=中(L2) 2=最远(L3) */
} GameMap;

/* 初始化默认地图 */
void map_init(GameMap *map);

/* 根据三级名字定位并设置地图状态 */
bool map_locate(GameMap *map, const char *area, const char *district, const char *spot);

/* 检查三级地点是否全部已在地图中 */
bool map_location_exists(const GameMap *map,
    const char *area, const char *district, const char *spot);

/* 确保地图中存在指定地点，不存在则创建合理随机坐标 */
void map_ensure_location(GameMap *map,
    const char *area, const char *district, const char *spot);

/* 导出地图点为JSON数组 */
int map_export_json(const GameMap *map, char *out, int out_size);

/* 从JSON数组导入地图点（会先清空已有地图数据） */
void map_import_json(GameMap *map, const char *json);

/* 从指定级别中删除一个地图点。返回true表示成功 */
bool map_remove_point(GameMap *map, int level, const char *name);

/* 检查指定级别是否存在该名称的地图点 */
bool map_has_point(const GameMap *map, int level, const char *name);

/* 推开所有层级中距离过近的点（AI生成坐标可能拥挤） */
void map_adjust_crowding(GameMap *map);

/* 重命名指定层级的地图点。返回true=成功，false=名称重复或不存在 */
bool map_rename_point(GameMap *map, int level, const char *old_name, const char *new_name);

#endif

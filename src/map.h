#ifndef MAP_H
#define MAP_H

#include <stdbool.h>

#define MAX_MAP_POINTS  32
#define MAX_MAP_LEVELS   3
#define MAP_NAME_LEN    32

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

/* 旅行请求 */
typedef struct {
    char from[MAP_NAME_LEN];  /* 出发地名 */
    char to[MAP_NAME_LEN];    /* 目的地名 */
    double distance_km;       /* 直线距离(公里) */
    char method[32];          /* 交通方式 */
} TravelRequest;

/* 初始化默认地图 */
void map_init(GameMap *map);

/* 计算两点间的欧几里得距离（坐标单位→公里，比例尺可调） */
double map_distance(const MapPoint *a, const MapPoint *b, double scale);

/* 当前缩放级下选中点的距离 */
double map_selected_distance(const GameMap *map, int from_idx, int to_idx);

/* 切换缩放级别 */
void map_zoom_to(GameMap *map, int level);

/* 在当前级选择一个点 */
void map_select(GameMap *map, int index);

/* 获取当前级名称 */
const char *map_current_level_name(const GameMap *map);

/* 获取当前级所有点 */
const MapLevel *map_current_level(const GameMap *map);

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

#endif

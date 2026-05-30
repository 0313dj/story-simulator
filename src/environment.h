#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <stdbool.h>

#define MAX_AREA_LEN    32  /* 大地点 */
#define MAX_DISTRICT_LEN 32  /* 小地点 */
#define MAX_SPOT_LEN    32  /* 具体地点 */
#define MAX_ERA_LEN     64  /* 时代环境 */

/* 天气 */
typedef enum {
    WEATHER_SUNNY,
    WEATHER_CLOUDY,
    WEATHER_OVERCAST,
    WEATHER_LIGHT_RAIN,
    WEATHER_HEAVY_RAIN,
    WEATHER_THUNDERSTORM,
    WEATHER_SNOW,
    WEATHER_BLIZZARD,
    WEATHER_FOG,
    WEATHER_WINDY,
    WEATHER_SANDSTORM,
    WEATHER_COUNT  /* Bug #52: sentinel for weather enum size */
} Weather;

/* 星期 */
typedef enum {
    WEEKDAY_MON,
    WEEKDAY_TUE,
    WEEKDAY_WED,
    WEEKDAY_THU,
    WEEKDAY_FRI,
    WEEKDAY_SAT,
    WEEKDAY_SUN,
} Weekday;

/* 时间 */
typedef struct {
    int     year;
    int     month;   /* 1-12 */
    int     day;     /* 1-31 */
    int     hour;    /* 0-23 */
    int     minute;  /* 0-59 */
    Weekday weekday;
} GameTime;

/* 地点（三级） */
typedef struct {
    char area[MAX_AREA_LEN];           /* 大地点：城市/王国/大陆 */
    char district[MAX_DISTRICT_LEN];   /* 小地点：城区/街道 */
    char spot[MAX_SPOT_LEN];           /* 具体地点：建筑/房间 */
} Location;

/* 环境总成 */
typedef struct {
    Weather   weather;
    GameTime  time;
    Location  location;
    char      era[MAX_ERA_LEN];        /* 时代环境：中世纪/赛博朋克/现代 等 */
} Environment;

/* 初始化 */
void env_init(Environment *env);

/* 时间推进（分钟） */
void env_advance_time(Environment *env, int minutes);

/* 设置天气 */
void env_set_weather(Environment *env, Weather w);

/* 设置地点 */
void env_set_location(Environment *env,
    const char *area, const char *district, const char *spot);

/* 设置时代 */
void env_set_era(Environment *env, const char *era);

/* 根据年月日自动推算星期（Zeller公式） */
Weekday env_calc_weekday(int year, int month, int day);

/* 打印环境信息 */
void env_print(const Environment *env);

/* 天气名/星期名 */
const char *weather_str(Weather w);
const char *weekday_str(Weekday w);

/* 根据天气数字(0-10)设置天气 */
void env_set_weather_int(Environment *env, int w);

/* 解析 "大地点/小地点/具体地点" 格式的地点字符串并设置 */
void env_parse_location(Environment *env, const char *loc_str);

/* 导出环境状态为可读文本（发给AI） */
int env_export_state(const Environment *env, char *out, int out_size);

#endif

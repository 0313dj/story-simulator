#include "environment.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 每月天数，2月暂按28天 */
static const int days_in_month[] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static bool is_leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int month_days(int year, int month)
{
    if (month == 2 && is_leap_year(year)) return 29;
    return days_in_month[month];
}

void env_init(Environment *env)
{
    memset(env, 0, sizeof(*env));
    env->time.day = 1;
    env->time.month = 1;
    env->time.year = 1;
    env->time.weekday = WEEKDAY_MON;
}

/* Zeller公式计算星期（0=日,1-6=一至六） */
Weekday env_calc_weekday(int year, int month, int day)
{
    if (month <= 2) { month += 12; year--; }
    int y = year, m = month, d = day;
    int h = (d + 13 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    /* Bug #21: fix negative modulo in C — ensure non-negative result */
    if (h < 0) h += 7;
    /* h: 0=六,1=日,2=一,...6=五  → 映射回 Weekday */
    int w = (h + 6) % 7; /* 0=日,1=一,...,6=六 */
    /* 转为我们的枚举：MON=0,...,SUN=6 */
    return (Weekday)((w + 6) % 7);
}

void env_advance_time(Environment *env, int minutes)
{
    if (minutes <= 0) return;

    int total = env->time.minute + minutes;
    env->time.minute = total % 60;
    int carry_h = total / 60;

    if (carry_h > 0) {
        int total_h = env->time.hour + carry_h;
        env->time.hour = total_h % 24;
        int carry_d = total_h / 24;

        /* Bug #22: efficient day advancement (avoid O(days) loop) */
        while (carry_d > 0) {
            int max_d = month_days(env->time.year, env->time.month);
            int remaining = max_d - env->time.day;
            if (carry_d > remaining) {
                /* Skip to next month */
                carry_d -= remaining + 1;
                env->time.day = 1;
                env->time.month++;
                if (env->time.month > 12) {
                    env->time.month = 1;
                    env->time.year++;
                }
            } else {
                env->time.day += carry_d;
                carry_d = 0;
            }
        }
    }

    env->time.weekday = env_calc_weekday(env->time.year, env->time.month, env->time.day);
}

void env_set_weather(Environment *env, Weather w)
{
    env->weather = w;
}

void env_set_location(Environment *env,
    const char *area, const char *district, const char *spot)
{
    strncpy(env->location.area,     area,     MAX_AREA_LEN - 1);
    strncpy(env->location.district, district, MAX_DISTRICT_LEN - 1);
    strncpy(env->location.spot,     spot,     MAX_SPOT_LEN - 1);
    env->location.area[MAX_AREA_LEN - 1]         = '\0';
    env->location.district[MAX_DISTRICT_LEN - 1] = '\0';
    env->location.spot[MAX_SPOT_LEN - 1]         = '\0';
}

void env_set_era(Environment *env, const char *era)
{
    strncpy(env->era, era, MAX_ERA_LEN - 1);
    env->era[MAX_ERA_LEN - 1] = '\0';
}

void env_set_weather_int(Environment *env, int w)
{
    if (w < 0 || w > 10) return;
    env->weather = (Weather)w;
}

void env_parse_location(Environment *env, const char *loc_str)
{
    if (!loc_str || !*loc_str) return;

    char buf[256];
    strncpy(buf, loc_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *s1 = buf;
    char *s2 = strchr(buf, '/');
    char *s3 = s2 ? strchr(s2 + 1, '/') : NULL;

    if (s2) *s2 = '\0';
    if (s3) *s3 = '\0';

    if (s1 && *s1) {
        strncpy(env->location.area, s1, MAX_AREA_LEN - 1);
        env->location.area[MAX_AREA_LEN - 1] = '\0';
    }
    if (s3) {
        /* 三段: area/district/spot */
        if (s2 && *(s2 + 1)) {
            strncpy(env->location.district, s2 + 1, MAX_DISTRICT_LEN - 1);
            env->location.district[MAX_DISTRICT_LEN - 1] = '\0';
        }
        if (*(s3 + 1)) {
            strncpy(env->location.spot, s3 + 1, MAX_SPOT_LEN - 1);
            env->location.spot[MAX_SPOT_LEN - 1] = '\0';
        }
    } else if (s2) {
        /* 两段: area/spot（district 留空） */
        if (*(s2 + 1)) {
            strncpy(env->location.spot, s2 + 1, MAX_SPOT_LEN - 1);
            env->location.spot[MAX_SPOT_LEN - 1] = '\0';
        }
        env->location.district[0] = '\0';
    }
}

int env_export_state(const Environment *env, char *out, int out_size)
{
    return snprintf(out, out_size,
        "时代: %s\n"
        "时间: %d年%02d月%02d日 %s %02d:%02d\n"
        "天气: %s (数字=%d)\n"
        "地点: %s / %s / %s\n",
        env->era,
        env->time.year, env->time.month, env->time.day,
        weekday_str(env->time.weekday),
        env->time.hour, env->time.minute,
        weather_str(env->weather), (int)env->weather,
        env->location.area, env->location.district, env->location.spot);
}

const char *weather_str(Weather w)
{
    switch (w) {
        case WEATHER_SUNNY:        return "晴";
        case WEATHER_CLOUDY:       return "多云";
        case WEATHER_OVERCAST:     return "阴天";
        case WEATHER_LIGHT_RAIN:   return "小雨";
        case WEATHER_HEAVY_RAIN:   return "大雨";
        case WEATHER_THUNDERSTORM: return "雷暴";
        case WEATHER_SNOW:         return "雪";
        case WEATHER_BLIZZARD:     return "暴风雪";
        case WEATHER_FOG:          return "雾";
        case WEATHER_WINDY:        return "大风";
        case WEATHER_SANDSTORM:    return "沙尘暴";
        default:                   return "未知";
    }
}

const char *weekday_str(Weekday w)
{
    switch (w) {
        case WEEKDAY_MON: return "星期一";
        case WEEKDAY_TUE: return "星期二";
        case WEEKDAY_WED: return "星期三";
        case WEEKDAY_THU: return "星期四";
        case WEEKDAY_FRI: return "星期五";
        case WEEKDAY_SAT: return "星期六";
        case WEEKDAY_SUN: return "星期日";
        default:          return "未知";
    }
}

void env_print(const Environment *env)
{
    printf("══════════════════════════════════════\n");
    printf("  环境信息\n");
    printf("══════════════════════════════════════\n");
    printf("  时代:   %s\n", env->era);
    printf("  时间:   %d年%02d月%02d日  %s  %02d:%02d\n",
        env->time.year, env->time.month, env->time.day,
        weekday_str(env->time.weekday),
        env->time.hour, env->time.minute);
    printf("  天气:   %s\n", weather_str(env->weather));
    printf("──────────────────────────────────────\n");
    printf("  地点\n");
    printf("  大地点: %s\n", env->location.area);
    printf("  小地点: %s\n", env->location.district);
    printf("  具体:   %s\n", env->location.spot);
    printf("══════════════════════════════════════\n");
}

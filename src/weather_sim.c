#include "weather_sim.h"
#include <stdio.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Season helpers
   ═══════════════════════════════════════════════════════════════ */

int weather_sim_season(int month)
{
    /* Meteorological seasons (Northern Hemisphere) */
    if (month >= 3 && month <= 5)  return 0;  /* spring */
    if (month >= 6 && month <= 8)  return 1;  /* summer */
    if (month >= 9 && month <= 11) return 2;  /* autumn */
    return 3;                                   /* winter */
}

const char *weather_sim_season_str(int month)
{
    switch (weather_sim_season(month)) {
    case 0: return "春";
    case 1: return "夏";
    case 2: return "秋";
    case 3: return "冬";
    default: return "?";
    }
}

/* ═══════════════════════════════════════════════════════════════
   Weather transition tables
   ═══════════════════════════════════════════════════════════════ */

/* Probability weights for next weather given current weather + season.
   Weights are relative; higher = more likely.
   Index: [current_weather][season][next_weather] */

#define WS_COUNT  11  /* WEATHER_COUNT */

static int weather_transition(int current, int season, unsigned int seed)
{
    /* ── Base transition probabilities ──
       Most weather stays the same; when it changes, it follows
       natural patterns (sunny↔cloudy↔rain, not sunny→snow in summer) */

    /* Transition table: for each current weather → list of {next, weight} */
    typedef struct { int next; int weight; } Trans;
    static const Trans sunny_trans[] = {
        {WEATHER_SUNNY,     40},
        {WEATHER_CLOUDY,    30},
        {WEATHER_WINDY,     15},
        {WEATHER_FOG,       10},
        {WEATHER_LIGHT_RAIN, 5},
    };
    static const Trans cloudy_trans[] = {
        {WEATHER_CLOUDY,      35},
        {WEATHER_SUNNY,       25},
        {WEATHER_OVERCAST,    20},
        {WEATHER_LIGHT_RAIN,  10},
        {WEATHER_WINDY,       10},
    };
    static const Trans overcast_trans[] = {
        {WEATHER_OVERCAST,    30},
        {WEATHER_LIGHT_RAIN,  25},
        {WEATHER_HEAVY_RAIN,  15},
        {WEATHER_CLOUDY,      15},
        {WEATHER_FOG,         10},
        {WEATHER_THUNDERSTORM, 5},
    };
    static const Trans light_rain_trans[] = {
        {WEATHER_LIGHT_RAIN,  35},
        {WEATHER_OVERCAST,    25},
        {WEATHER_HEAVY_RAIN,  15},
        {WEATHER_CLOUDY,      15},
        {WEATHER_FOG,         10},
    };
    static const Trans heavy_rain_trans[] = {
        {WEATHER_HEAVY_RAIN,  30},
        {WEATHER_LIGHT_RAIN,  30},
        {WEATHER_THUNDERSTORM,20},
        {WEATHER_OVERCAST,    20},
    };
    static const Trans thunder_trans[] = {
        {WEATHER_THUNDERSTORM,25},
        {WEATHER_HEAVY_RAIN,  30},
        {WEATHER_LIGHT_RAIN,  25},
        {WEATHER_OVERCAST,    20},
    };
    static const Trans snow_trans[] = {
        {WEATHER_SNOW,        40},
        {WEATHER_CLOUDY,      25},
        {WEATHER_OVERCAST,    20},
        {WEATHER_BLIZZARD,    10},
        {WEATHER_SUNNY,        5},
    };
    static const Trans blizzard_trans[] = {
        {WEATHER_BLIZZARD,    30},
        {WEATHER_SNOW,        35},
        {WEATHER_OVERCAST,    20},
        {WEATHER_CLOUDY,      15},
    };
    static const Trans fog_trans[] = {
        {WEATHER_FOG,         40},
        {WEATHER_CLOUDY,      25},
        {WEATHER_SUNNY,       20},
        {WEATHER_OVERCAST,    15},
    };
    static const Trans windy_trans[] = {
        {WEATHER_WINDY,       35},
        {WEATHER_CLOUDY,      25},
        {WEATHER_SUNNY,       20},
        {WEATHER_SANDSTORM,   10},
        {WEATHER_OVERCAST,    10},
    };
    static const Trans sandstorm_trans[] = {
        {WEATHER_SANDSTORM,   30},
        {WEATHER_WINDY,       35},
        {WEATHER_CLOUDY,      20},
        {WEATHER_SUNNY,       15},
    };

    static const Trans *tables[WS_COUNT] = {
        sunny_trans,      /* SUNNY */
        cloudy_trans,     /* CLOUDY */
        overcast_trans,   /* OVERCAST */
        light_rain_trans, /* LIGHT_RAIN */
        heavy_rain_trans, /* HEAVY_RAIN */
        thunder_trans,    /* THUNDERSTORM */
        snow_trans,       /* SNOW */
        blizzard_trans,   /* BLIZZARD */
        fog_trans,        /* FOG */
        windy_trans,      /* WINDY */
        sandstorm_trans,  /* SANDSTORM */
    };
    static const int table_sizes[WS_COUNT] = {5,5,6,5,4,4,5,4,4,5,4};

    if (current < 0 || current >= WS_COUNT) return WEATHER_SUNNY;

    const Trans *table = tables[current];
    int tsize = table_sizes[current];

    /* ── Seasonal modifiers ── */
    int total = 0;
    int weights[16];  /* max table size */

    for (int i = 0; i < tsize; i++) {
        int w = table[i].weight;

        /* Summer boosts for hot weather */
        if (season == 1) {  /* summer */
            if (table[i].next == WEATHER_SUNNY)    w += 15;
            if (table[i].next == WEATHER_THUNDERSTORM) w += 10;
            if (table[i].next == WEATHER_SNOW || table[i].next == WEATHER_BLIZZARD) w = 0;
        }
        /* Winter boosts for cold weather */
        if (season == 3) {  /* winter */
            if (table[i].next == WEATHER_SNOW || table[i].next == WEATHER_BLIZZARD) w += 20;
            if (table[i].next == WEATHER_SUNNY) w -= 10;
        }
        /* Spring: more rain */
        if (season == 0) {
            if (table[i].next == WEATHER_LIGHT_RAIN || table[i].next == WEATHER_HEAVY_RAIN) w += 10;
        }
        /* Autumn: more wind, fog */
        if (season == 2) {
            if (table[i].next == WEATHER_WINDY) w += 10;
            if (table[i].next == WEATHER_FOG)   w += 10;
        }

        if (w < 0) w = 0;
        weights[i] = w;
        total += w;
    }

    if (total <= 0) return WEATHER_SUNNY;

    /* Weighted random pick */
    unsigned int roll = seed % (unsigned int)total;
    int cumulative = 0;
    for (int i = 0; i < tsize; i++) {
        cumulative += weights[i];
        if ((int)roll < cumulative) return table[i].next;
    }

    return table[tsize - 1].next;  /* fallback */
}

/* ═══════════════════════════════════════════════════════════════
   Tick
   ═══════════════════════════════════════════════════════════════ */

void weather_sim_tick(WorldState *ws, EventLog *events, long long tick)
{
    Environment *env = &ws->calendar;
    int month = env->time.month;
    int season = weather_sim_season(month);
    Weather current = env->weather;

    /* Hash for deterministic randomness */
    unsigned int seed = (unsigned int)(tick & 0xFFFFFFFFU);
    seed = (seed * 1103515245U + 12345U) & 0x7fffffffU;

    /* 70% chance weather stays the same (already captured in transition weights),
       but compute transition anyway */
    Weather next = (Weather)weather_transition((int)current, season, seed);

    if (next != current) {
        env_set_weather(env, next);
        event_push(events, tick, -1, -1, EVENT_WORLD_TICK,
            "{\"subsystem\":\"weather\",\"from\":\"%s\",\"to\":\"%s\","
            "\"season\":\"%s\"}",
            weather_str(current), weather_str(next),
            weather_sim_season_str(month));
    }
}

/* ═══════════════════════════════════════════════════════════════
   Export
   ═══════════════════════════════════════════════════════════════ */

int weather_sim_export(const WorldState *ws, char *out, int out_size)
{
    const Environment *env = &ws->calendar;
    return snprintf(out, out_size,
        "季节: %s  天气: %s  温度倾向: %s",
        weather_sim_season_str(env->time.month),
        weather_str(env->weather),
        weather_sim_season(env->time.month) <= 1 ? "偏暖" : "偏冷");
}

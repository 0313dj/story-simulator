#ifndef WEATHER_SIM_H
#define WEATHER_SIM_H

#include "environment.h"
#include "worldstate.h"
#include "event.h"

/* ═══════════════════════════════════════════════════════════════
   WeatherSim — per USE Spec Chapter 9 (Layer 2 Extension)

   Natural weather progression beyond random assignment:
   - Seasonal patterns (winter→snow, summer→sunny, spring→rain)
   - Realistic transitions (sunny→cloudy→rain, not sunny→snow)
   - Small chance of extreme weather events
   - Respects era/environment context

   Extends the existing environment.c Weather system.
   Schedule: every 60 ticks (1 game hour)
   ═══════════════════════════════════════════════════════════════ */

#define WEATHER_TICK_INTERVAL  60  /* hourly */

/* ── API ── */

/* Run one weather tick.
   Evaluates current weather, season, and time of day to
   determine a natural weather progression. Modifies ws->calendar.weather.
   Records WEATHER_CHANGE events on transition. */
void weather_sim_tick(WorldState *ws, EventLog *events, long long tick);

/* Get the season for a given month (0=spring, 1=summer, 2=autumn, 3=winter) */
int weather_sim_season(int month);

/* Get a descriptive season name in Chinese */
const char *weather_sim_season_str(int month);

/* Export weather forecast for AI context */
int weather_sim_export(const WorldState *ws, char *out, int out_size);

#endif

#ifndef WORLD_DIRECTOR_H
#define WORLD_DIRECTOR_H

#include "worldstate.h"
#include "event.h"

/* ═══════════════════════════════════════════════════════════════
   World Director — per USE Spec Chapter 9 (Layer 2 Extension)

   Central scheduler for background simulation subsystems.
   Orchestrates EconomySim, FactionSim, WeatherSim, and PopulationSim
   at their respective tick intervals.

   The World Director runs as part of the main game loop, triggered
   whenever the world tick advances enough for any subsystem.

   All subsystems operate on WorldState (reading/writing world
   variables) and record events via EventLog. They never directly
   modify entities — only aggregate world variables.
   ═══════════════════════════════════════════════════════════════ */

/* ── World variable names for director state ── */
#define WD_VAR_LAST_WEATHER    "wd_last_weather_tick"
#define WD_VAR_LAST_ECONOMY    "wd_last_economy_tick"
#define WD_VAR_LAST_FACTION    "wd_last_faction_tick"
#define WD_VAR_LAST_POP        "wd_last_population_tick"

/* ── API ── */

/* Initialize all simulation subsystems.
   Creates default world variables for economy, population, etc.
   Called once when a new world is created. */
void wd_init(WorldState *ws, long long current_tick);

/* Run one World Director tick.
   Checks each subsystem's interval and runs those that are due.
   Returns number of subsystems that ran.

   Call this from the main game loop after time advances. */
int wd_tick(WorldState *ws, EventLog *events, long long current_tick);

/* Export a combined summary of all simulation state (for AI context). */
int wd_export(const WorldState *ws, char *out, int out_size);

#endif

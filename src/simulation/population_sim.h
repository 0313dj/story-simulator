#ifndef POPULATION_SIM_H
#define POPULATION_SIM_H

#include "worldstate.h"
#include "event.h"

/* ═══════════════════════════════════════════════════════════════
   PopulationSim — per USE Spec Chapter 9 (Layer 2 Extension)

   Simulates demographic changes in the background:
   - Total population adjusts based on birth/death rates
   - Prosperity affects birth rate positively, death rate negatively
   - Migration between locations
   - NPC aging and lifecycle events

   Schedule: every 43200 ticks (1 game month)
   ═══════════════════════════════════════════════════════════════ */

#define POP_TICK_INTERVAL  43200  /* monthly */

/* ── World variable names ── */
#define POP_VAR_TOTAL      "pop_total"
#define POP_VAR_BIRTH_RATE  "pop_birth_rate"
#define POP_VAR_DEATH_RATE  "pop_death_rate"
#define POP_VAR_MIGRATION   "pop_migration"
#define POP_VAR_GROWTH      "pop_growth"

/* ── API ── */

/* Run one population tick.
   Adjusts population, birth/death rates based on prosperity.
   Records POPULATION_TICK events for significant changes. */
void pop_sim_tick(WorldState *ws, EventLog *events, long long tick);

/* Initialize default population variables in WorldState.
   Called once when a new world is created. */
void pop_sim_init(WorldState *ws);

/* Export a readable summary of demographic state (for AI context). */
int pop_sim_export(const WorldState *ws, char *out, int out_size);

#endif

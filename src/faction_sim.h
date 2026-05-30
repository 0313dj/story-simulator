#ifndef FACTION_SIM_H
#define FACTION_SIM_H

#include "worldstate.h"
#include "event.h"

/* ═══════════════════════════════════════════════════════════════
   FactionSim — per USE Spec Chapter 9 (Layer 2 Extension)

   Simulates faction dynamics in the background:
   - Faction power levels shift based on resources and conflicts
   - Inter-faction relationships drift over time
   - Small chance of new conflicts or peace resolutions
   - All state stored as WorldState variables

   Schedule: every 10080 ticks (1 game week)
   ═══════════════════════════════════════════════════════════════ */

#define FACTION_TICK_INTERVAL  10080  /* weekly */

#define FACTION_MAX_FACTIONS  16

/* ── World variable prefixes ── */
#define FAC_VAR_PREFIX_POWER    "fac_power_"
#define FAC_VAR_PREFIX_RELATION "fac_rel_"
#define FAC_VAR_ACTIVE_CONFLICTS "fac_active_conflicts"

/* ── API ── */

/* Run one faction tick.
   Adjusts faction power, inter-faction relations, and conflict states.
   Records FACTION_TICK events for significant changes. */
void faction_sim_tick(WorldState *ws, EventLog *events, long long tick);

/* Initialize default faction variables in the WorldState.
   Discovers faction-type entities and creates tracking variables. */
void faction_sim_init(WorldState *ws);

/* Export a readable summary of current faction state (for AI context). */
int faction_sim_export(const WorldState *ws, char *out, int out_size);

#endif

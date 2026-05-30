#ifndef ECONOMY_SIM_H
#define ECONOMY_SIM_H

#include "worldstate.h"
#include "event.h"

/* ═══════════════════════════════════════════════════════════════
   EconomySim — per USE Spec Chapter 9 (Layer 2 Extension)

   Simulates background economic activity:
   - Price levels (inflation) adjust over time
   - Prosperity fluctuates with random walk + seasonality
   - Trade volume varies by location activity
   - All state stored as WorldState variables

   Schedule: every 1440 ticks (1 game day)
   ═══════════════════════════════════════════════════════════════ */

#define ECON_TICK_INTERVAL  1440   /* daily */

/* ── World variable names used by EconomySim ── */
#define ECON_VAR_INFLATION    "econ_inflation"
#define ECON_VAR_PROSPERITY   "econ_prosperity"
#define ECON_VAR_TRADE_VOL    "econ_trade_volume"
#define ECON_VAR_PRICE_LEVEL  "econ_price_level"
#define ECON_VAR_SUPPLY       "econ_supply"
#define ECON_VAR_DEMAND       "econ_demand"

/* ── API ── */

/* Run one economy tick.
   Adjusts prosperity, inflation, trade volume based on current state.
   Records ECONOMY_TICK events for significant changes. */
void econ_sim_tick(WorldState *ws, EventLog *events, long long tick);

/* Initialize default economy variables in the WorldState.
   Called once when a new world is created. */
void econ_sim_init(WorldState *ws);

/* Export a readable summary of current economic state (for AI context). */
int econ_sim_export(const WorldState *ws, char *out, int out_size);

#endif

#include "economy_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Internal helpers
   ═══════════════════════════════════════════════════════════════ */

/* Simple hash for deterministic variation */
static unsigned int hash_tick(long long tick)
{
    unsigned int h = (unsigned int)(tick & 0xFFFFFFFFU);
    h = (h * 1103515245U + 12345U) & 0x7fffffffU;
    return h;
}

static int clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

/* Get or create an int variable */
static int get_int_var(const WorldState *ws, const char *name, int default_val)
{
    const WorldVariable *v = NULL;
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0) {
            v = &ws->variables[i];
            break;
        }
    }
    if (!v) return default_val;
    return v->int_val;
}

/* Set an int variable (creates if missing) */
static void set_int_var(WorldState *ws, const char *name, int val)
{
    WorldVariable *v = NULL;
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0) {
            v = &ws->variables[i];
            break;
        }
    }
    if (!v) {
        if (ws->variable_count >= WS_MAX_VARIABLES) return;
        v = &ws->variables[ws->variable_count++];
        memset(v, 0, sizeof(*v));
        v->id = ws->variable_count - 1;
        strncpy(v->name, name, sizeof(v->name) - 1);
        v->type = VAR_INT;
    }
    v->int_val = val;
    v->type = VAR_INT;
}

/* ═══════════════════════════════════════════════════════════════
   Initialization
   ═══════════════════════════════════════════════════════════════ */

void econ_sim_init(WorldState *ws)
{
    set_int_var(ws, ECON_VAR_INFLATION,  10);
    set_int_var(ws, ECON_VAR_PROSPERITY, 50);
    set_int_var(ws, ECON_VAR_TRADE_VOL,  100);
    set_int_var(ws, ECON_VAR_PRICE_LEVEL, 50);
    set_int_var(ws, ECON_VAR_SUPPLY,     50);
    set_int_var(ws, ECON_VAR_DEMAND,     50);
}

/* ═══════════════════════════════════════════════════════════════
   Tick
   ═══════════════════════════════════════════════════════════════ */

void econ_sim_tick(WorldState *ws, EventLog *events, long long tick)
{
    (void)events;

    /* Read current values */
    int inflation  = get_int_var(ws, ECON_VAR_INFLATION, 10);
    int prosperity = get_int_var(ws, ECON_VAR_PROSPERITY, 50);
    int trade_vol  = get_int_var(ws, ECON_VAR_TRADE_VOL, 100);
    int price_lvl  = get_int_var(ws, ECON_VAR_PRICE_LEVEL, 50);
    int supply     = get_int_var(ws, ECON_VAR_SUPPLY, 50);
    int demand     = get_int_var(ws, ECON_VAR_DEMAND, 50);

    unsigned int seed = hash_tick(tick);

    /* ── Prosperity: random walk with mean reversion toward 50 ── */
    {
        int drift = (50 - prosperity) / 20;  /* mean reversion */
        int noise = (int)(seed % 7) - 3;      /* -3 to +3 */
        prosperity = clamp100(prosperity + drift + noise);
    }

    /* ── Supply/Demand: mean-reverting around prosperity ── */
    {
        int s_drift = (prosperity - supply) / 10;
        supply = clamp100(supply + s_drift + (int)((seed >> 4) % 5) - 2);
        int d_drift = (50 - demand) / 15;
        demand = clamp100(demand + d_drift + (int)((seed >> 8) % 5) - 2);
    }

    /* ── Price level: supply vs demand pressure ── */
    {
        int pressure = (demand - supply) / 5;
        int p_drift = (50 - price_lvl) / 20;
        price_lvl = clamp100(price_lvl + pressure + p_drift);
    }

    /* ── Inflation: affected by price level and prosperity ── */
    {
        int inf_drift = (price_lvl - 50) / 10;
        int inf_mean  = (10 - inflation) / 5;
        inflation = clamp100(inflation + inf_drift + inf_mean);
    }

    /* ── Trade volume: affected by prosperity ── */
    {
        int tv_drift = (prosperity - 50) / 5;
        trade_vol = clamp100(trade_vol + tv_drift + (int)((seed >> 12) % 5) - 2);
    }

    /* Write back */
    set_int_var(ws, ECON_VAR_INFLATION,  inflation);
    set_int_var(ws, ECON_VAR_PROSPERITY, prosperity);
    set_int_var(ws, ECON_VAR_TRADE_VOL,  trade_vol);
    set_int_var(ws, ECON_VAR_PRICE_LEVEL, price_lvl);
    set_int_var(ws, ECON_VAR_SUPPLY,      supply);
    set_int_var(ws, ECON_VAR_DEMAND,      demand);

    /* Log significant changes */
    if (abs(prosperity - 50) > 30) {
        event_push(events, tick, -1, -1, EVENT_WORLD_TICK,
            "{\"subsystem\":\"economy\",\"prosperity\":%d,\"inflation\":%d,"
            "\"trade\":%d}", prosperity, inflation, trade_vol);
    }
}

/* ═══════════════════════════════════════════════════════════════
   Export
   ═══════════════════════════════════════════════════════════════ */

int econ_sim_export(const WorldState *ws, char *out, int out_size)
{
    int infl  = get_int_var(ws, ECON_VAR_INFLATION, 10);
    int prosp = get_int_var(ws, ECON_VAR_PROSPERITY, 50);
    int trade = get_int_var(ws, ECON_VAR_TRADE_VOL, 100);
    int price = get_int_var(ws, ECON_VAR_PRICE_LEVEL, 50);

    return snprintf(out, out_size,
        "经济状况: 繁荣度:%d/100  通胀率:%d%%  贸易量:%d  物价:%d/100",
        prosp, infl, trade, price);
}

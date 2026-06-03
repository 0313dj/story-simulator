#include "log.h"
#include "population_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Internal helpers
   ═══════════════════════════════════════════════════════════════ */

static unsigned int hash_tick(long long tick)
{
    unsigned int h = (unsigned int)(tick & 0xFFFFFFFFU);
    h = (h * 1103515245U + 12345U) & 0x7fffffffU;
    return h;
}

static int get_int_var(const WorldState *ws, const char *name, int default_val)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0)
            return ws->variables[i].int_val;
    }
    return default_val;
}

static float get_float_var(const WorldState *ws, const char *name, float default_val)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0)
            return ws->variables[i].float_val;
    }
    return default_val;
}

static void set_int_var(WorldState *ws, const char *name, int val)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0) {
            ws->variables[i].int_val = val;
            ws->variables[i].type = VAR_INT;
            return;
        }
    }
    if (ws->variable_count >= WS_MAX_VARIABLES) return;
    WorldVariable *v = &ws->variables[ws->variable_count++];
    memset(v, 0, sizeof(*v));
    v->id = ws->variable_count - 1;
    safe_strcpy(v->name, name, sizeof(v->name));
    v->type = VAR_INT;
    v->int_val = val;
}

static void set_float_var(WorldState *ws, const char *name, float val)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, name) == 0) {
            ws->variables[i].float_val = val;
            ws->variables[i].type = VAR_FLOAT;
            return;
        }
    }
    if (ws->variable_count >= WS_MAX_VARIABLES) return;
    WorldVariable *v = &ws->variables[ws->variable_count++];
    memset(v, 0, sizeof(*v));
    v->id = ws->variable_count - 1;
    safe_strcpy(v->name, name, sizeof(v->name));
    v->type = VAR_FLOAT;
    v->float_val = val;
}

/* ── Look up prosperity from economy variables ── */
static int get_prosperity(const WorldState *ws)
{
    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, "econ_prosperity") == 0)
            return ws->variables[i].int_val;
    }
    return 50;
}

/* ═══════════════════════════════════════════════════════════════
   Initialization
   ═══════════════════════════════════════════════════════════════ */

void pop_sim_init(WorldState *ws)
{
    /* Default values for a medieval-ish fantasy setting */
    set_int_var(ws, POP_VAR_TOTAL, 10000);     /* 10,000 total population */
    set_float_var(ws, POP_VAR_BIRTH_RATE, 0.02f);   /* 2% per year → ~0.17% per month */
    set_float_var(ws, POP_VAR_DEATH_RATE, 0.015f);  /* 1.5% per year */
    set_int_var(ws, POP_VAR_MIGRATION, 0);
    set_int_var(ws, POP_VAR_GROWTH, 0);
}

/* ═══════════════════════════════════════════════════════════════
   Tick
   ═══════════════════════════════════════════════════════════════ */

void pop_sim_tick(WorldState *ws, EventLog *events, long long tick)
{
    int population  = get_int_var(ws, POP_VAR_TOTAL, 10000);
    float birth_rate = get_float_var(ws, POP_VAR_BIRTH_RATE, 0.02f);
    float death_rate = get_float_var(ws, POP_VAR_DEATH_RATE, 0.015f);
    int migration   = get_int_var(ws, POP_VAR_MIGRATION, 0);
    int prosperity  = get_prosperity(ws);

    unsigned int seed = hash_tick(tick);

    /* ── Prosperity effects ──
       High prosperity → higher birth rate, lower death rate
       Low prosperity → lower birth rate, higher death rate */
    float prosp_factor = (float)(prosperity - 50) / 100.0f;  /* -0.5 to +0.5 */

    /* Monthly rates (annual / 12) */
    float monthly_birth = (birth_rate / 12.0f) + prosp_factor * 0.002f;
    float monthly_death = (death_rate / 12.0f) - prosp_factor * 0.001f;

    if (monthly_birth < 0.0f) monthly_birth = 0.0f;
    if (monthly_death < 0.0f) monthly_death = 0.0f;

    /* ── Calculate population change ── */
    int births   = (int)((float)population * monthly_birth);
    int deaths   = (int)((float)population * monthly_death);
    int mig_noise = (int)(seed % 21) - 10 + (prosperity - 50) / 5;  /* -10 to +10 + prosperity */
    int net_change = births - deaths + mig_noise;

    population += net_change;
    if (population < 100) population = 100;  /* floor */
    if (population > 100000000) population = 100000000;  /* ceiling */

    migration = mig_noise;

    /* ── Adjust rates dynamically ── */
    {
        /* Birth rate drifts with prosperity */
        float br_drift = (prosperity - 50) * 0.00005f;
        birth_rate += br_drift;
        if (birth_rate < 0.005f) birth_rate = 0.005f;
        if (birth_rate > 0.05f)  birth_rate = 0.05f;

        /* Death rate inversely correlated with prosperity */
        float dr_drift = (50 - prosperity) * 0.00003f;
        death_rate += dr_drift;
        if (death_rate < 0.005f) death_rate = 0.005f;
        if (death_rate > 0.05f)  death_rate = 0.05f;
    }

    /* Write back */
    set_int_var(ws, POP_VAR_TOTAL, population);
    set_float_var(ws, POP_VAR_BIRTH_RATE, birth_rate);
    set_float_var(ws, POP_VAR_DEATH_RATE, death_rate);
    set_int_var(ws, POP_VAR_MIGRATION, migration);
    set_int_var(ws, POP_VAR_GROWTH, net_change);

    LOG_I("Population tick: total=%d births=%d deaths=%d migration=%d",
          population, births, deaths, mig_noise);

    /* Log significant changes */
    if (abs(net_change) > 50) {
        event_push(events, tick, -1, -1, EVENT_WORLD_TICK,
            "{\"subsystem\":\"population\",\"total\":%d,\"births\":%d,"
            "\"deaths\":%d,\"migration\":%d}",
            population, births, deaths, mig_noise);
    }
}

/* ═══════════════════════════════════════════════════════════════
   Export
   ═══════════════════════════════════════════════════════════════ */

int pop_sim_export(const WorldState *ws, char *out, int out_size)
{
    int pop      = get_int_var(ws, POP_VAR_TOTAL, 10000);
    float br     = get_float_var(ws, POP_VAR_BIRTH_RATE, 0.02f);
    float dr     = get_float_var(ws, POP_VAR_DEATH_RATE, 0.015f);
    int growth   = get_int_var(ws, POP_VAR_GROWTH, 0);

    return snprintf(out, out_size,
        "人口: %d  出生率:%.1f%%  死亡率:%.1f%%  月增长:%+d",
        pop, br * 100.0f, dr * 100.0f, growth);
}

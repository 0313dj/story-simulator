#include "context.h"
#include "log.h"
#include <string.h>

void ctx_init(GameContext *ctx)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));

    InitializeCriticalSection(&ctx->lock);

    /* Init subsystems */
    cc_init(&ctx->player, ENTITY_PLAYER);
    map_init(&ctx->map);
    env_init(&ctx->env);
    ws_init(&ctx->ws);
    event_init(&ctx->events);
    npc_init(&ctx->npc_mgr);
    re_init(&ctx->rule_engine);
    registry_init(&ctx->registry);

    ctx->npc_count = 0;
    ctx->api_ready = false;
    ctx->world_ready = false;
    ctx->chat_count = 0;
    ctx->saves_root[0] = '\0';

    LOG_I("GameContext initialized");
}

void ctx_destroy(GameContext *ctx)
{
    if (!ctx) return;

    /* Bug #19: prevent double-destroy with a flag. */
    static bool destroyed = false;
    if (destroyed) return;
    destroyed = true;

    /* Free NPC brains */
    for (int i = 0; i < ctx->npc_count; i++) {
        if (ctx->npcs[i].brain) {
            free(ctx->npcs[i].brain);
            ctx->npcs[i].brain = NULL;
        }
    }

    /* Free memory stores */
    for (int i = 0; i < ctx->npc_count; i++)
        mem_free(&ctx->npcs[i].memory);
    mem_free(&ctx->player.memory);

    /* Free Registry */
    registry_destroy(&ctx->registry);

    DeleteCriticalSection(&ctx->lock);
    LOG_I("GameContext destroyed");
}

#ifndef API_H
#define API_H

#include <stdbool.h>

#define MAX_RESPONSE_LEN   16384
#define MAX_ENDPOINT_LEN   256
#define MAX_KEY_LEN        256
#define MAX_MODEL_LEN      64

/* 第一步返回：AI选中的相关变量名 */
typedef struct {
    char var_names[1024];    /* 逗号分隔的变量名 */
    char char_names[512];    /* 逗号分隔的相关人物名 */
    char raw[4096];          /* 原始响应 */
} VarSelectResult;

/* 第二步返回：AI生成的叙事+所有变更 */
typedef struct {
    char text[4096];         /* 叙事/对话文本 */
    char changes[2048];      /* 变量变更指令 */
    int  time_advance;       /* 时间推进(分钟)，-1不变 */
    int  weather;            /* 天气枚举值，-1不变 */
    char location[256];      /* 新地点 "大地点/小地点/具体地点"，空不变 */
    char mem_short[512];     /* 新增短期记忆 */
    char mem_long[512];      /* 新增长期记忆 */
    char mem_permanent[512]; /* 新增永久记忆 */
    char mem_chars[256];     /* 记忆关联人物 */
    char mem_summary[64];    /* 事件总结(≤10字) → 长期记忆 */
    char npc_temp[1024];     /* 临时NPC: name|desc 每行一个 */
    char npc_spawn[2048];    /* 生成决策: name|MUST/MAYBE/NEVER|location 每行一个 */
    char npc_create[4096];   /* 新建角色卡: key=value 每行一个 */
    /* ── USE Architecture: ActionProposal (Phase 1+ Rule Engine) ── */
    char action_proposal[4096]; /* JSON ActionProposal for Rule Engine */
    char raw[MAX_RESPONSE_LEN];  /* AI原始响应 */
} FullResponse;

/* API客户端 */
typedef struct {
    char endpoint[MAX_ENDPOINT_LEN];
    char api_key[MAX_KEY_LEN];
    char model[MAX_MODEL_LEN];
    char last_error[512];
    /* Token usage tracking (cumulative across all calls) */
    long long total_prompt_tokens;
    long long total_completion_tokens;
    long long total_tokens;
    int          api_call_count;
} ApiClient;

/* 初始化 */
void api_init(ApiClient *api, const char *endpoint, const char *key, const char *model);
bool api_chat(ApiClient *api, const char *system_prompt,
              const char *user_prompt, char *out, int out_size,
              int max_tokens);
const char *api_last_error(const ApiClient *api);

/* 第一步：发送用户输入+变量名目录（仅有键名无值），AI返回相关变量名 */
bool api_select_vars(ApiClient *api, const char *user_input,
                     const char *var_catalog, VarSelectResult *result);

/* 第二步：发送用户输入+被选中变量的值，AI返回叙事和所有变更 */
bool api_generate(ApiClient *api, const char *user_input,
                  const char *selected_vars, FullResponse *result);

/* 旅行时间查询：发送距离+方式，AI返回所需时间（分钟数） */
bool api_query_travel(ApiClient *api, double distance_km,
                      const char *method, int *out_minutes);

/* 世界创建结果 */
typedef struct {
    char player_card[4096];   /* 玩家角色卡 key=value */
    char locations[1024];     /* 地点: name|x|y 每行 */
    char npc_cards[8192];     /* NPC卡（---分隔） */
    char start_location[256]; /* 起始地点 大/小/具体 */
    char start_time[128];     /* 初始时间 年|月|日|时|分|星期 */
    char era[256];            /* 时代 */
    char raw[MAX_RESPONSE_LEN];   /* 原始响应 */
} WorldCreateResult;

/* 世界创建：用户完整角色卡+故事 → AI返回完整世界（含性格） */
bool api_create_world(ApiClient *api,
    const char *name, const char *age, const char *gender,
    const char *clothing, const char *money,
    const char *appearance, const char *constitution,
    const char *intelligence, const char *skills, const char *items,
    const char *story, WorldCreateResult *result);

/* Query cumulative token usage.  Fills the three out parameters; returns
   the total number of API calls made. */
int api_get_token_usage(const ApiClient *api,
                        long long *out_prompt,
                        long long *out_completion,
                        long long *out_total);

#endif

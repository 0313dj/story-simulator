#include "rule.h"
#include "log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
   Rule Engine implementation
   ═══════════════════════════════════════════════════════════════ */

void re_init(RuleEngine *re)
{
    memset(re, 0, sizeof(*re));
    re->next_id = 1;
}

int re_register(RuleEngine *re, int priority, const char *condition,
                const RuleEffect *effects, int effect_count)
{
    if (re->count >= RE_MAX_RULES) return -1;
    if (effect_count > RE_MAX_EFFECTS) effect_count = RE_MAX_EFFECTS;

    Rule *r = &re->rules[re->count];
    memset(r, 0, sizeof(*r));
    r->id = re->next_id++;
    r->priority = priority;
    strncpy(r->condition, condition, RE_MAX_CONDITION_LEN - 1);
    memcpy(r->effects, effects, effect_count * sizeof(RuleEffect));
    r->effect_count = effect_count;
    r->enabled = true;

    /* Insert sorted by priority (descending) */
    int insert_pos = re->count;
    for (int i = 0; i < re->count; i++) {
        if (re->rules[i].priority < priority ||
            (re->rules[i].priority == priority && re->rules[i].id > r->id)) {
            insert_pos = i;
            break;
        }
    }
    if (insert_pos < re->count) {
        memmove(&re->rules[insert_pos + 1], &re->rules[insert_pos],
                (re->count - insert_pos) * sizeof(Rule));
        re->rules[insert_pos] = *r;
    }
    re->count++;
    return r->id;
}

/* ═══════════════════════════════════════════════════════════════
   Field value lookup on a CharacterCard
   ═══════════════════════════════════════════════════════════════ */

/* Get an integer value for a named field on an entity.
   Returns true if the field was found and value was written. */
static bool entity_get_int(const CharacterCard *entity, const char *field,
                           int *out_val)
{
    if (!entity || !field || !*field) return false;

    /* Top-level fields */
    if (strcmp(field, "money") == 0) {
        *out_val = entity->money; return true;
    }
    if (strcmp(field, "age") == 0) {
        *out_val = entity->age; return true;
    }
    if (strcmp(field, "player_affinity") == 0) {
        *out_val = entity->player_affinity; return true;
    }
    if (strcmp(field, "status") == 0) {
        *out_val = (int)entity->status; return true;
    }

    /* attrs */
    if (strcmp(field, "attr.appearance") == 0 ||
        strcmp(field, "appearance") == 0) {
        *out_val = entity->attr.appearance; return true;
    }
    if (strcmp(field, "attr.constitution") == 0 ||
        strcmp(field, "constitution") == 0) {
        *out_val = entity->attr.constitution; return true;
    }
    if (strcmp(field, "attr.intelligence") == 0 ||
        strcmp(field, "intelligence") == 0) {
        *out_val = entity->attr.intelligence; return true;
    }

    /* Skills: "skill.技能名" → level */
    if (strncmp(field, "skill.", 6) == 0) {
        const char *skill_name = field + 6;
        for (int i = 0; i < entity->skill_count; i++) {
            if (strcmp(entity->skills[i].name, skill_name) == 0) {
                *out_val = entity->skills[i].level;
                return true;
            }
        }
        return false;
    }

    /* Items: "item.物品名" → quantity */
    if (strncmp(field, "item.", 5) == 0) {
        const char *item_name = field + 5;
        for (int i = 0; i < entity->item_count; i++) {
            if (strcmp(entity->items[i].name, item_name) == 0) {
                *out_val = entity->items[i].quantity;
                return true;
            }
        }
        return false;
    }

    /* Relation affinity: "rel.NPC名.affinity" */
    if (strncmp(field, "rel.", 4) == 0) {
        const char *target = field + 4;
        const char *dot = strstr(target, ".affinity");
        if (dot) {
            char tname[64];
            int len = (int)(dot - target);
            if (len >= (int)sizeof(tname)) len = sizeof(tname) - 1;
            memcpy(tname, target, len);
            tname[len] = '\0';
            for (int i = 0; i < entity->relation_count; i++) {
                if (strcmp(entity->relations[i].target, tname) == 0) {
                    *out_val = entity->relations[i].affinity;
                    return true;
                }
            }
        }
        return false;
    }

    /* Direct field.affinity (legacy format) */
    {
        const char *dot = strstr(field, ".affinity");
        if (dot) {
            char tname[64];
            int len = (int)(dot - field);
            if (len >= (int)sizeof(tname)) len = sizeof(tname) - 1;
            memcpy(tname, field, len);
            tname[len] = '\0';
            for (int i = 0; i < entity->relation_count; i++) {
                if (strcmp(entity->relations[i].target, tname) == 0) {
                    *out_val = entity->relations[i].affinity;
                    return true;
                }
            }
            /* Not found — return 0 as default */
            *out_val = 0;
            return true;
        }
    }

    /* Not found */
    return false;
}

/* Get a string value for a named field on an entity */
static bool entity_get_str(const CharacterCard *entity, const char *field,
                           char *out_str, int out_size)
{
    if (!entity || !field || !*field) return false;

    if (strcmp(field, "name") == 0) {
        strncpy(out_str, entity->name, out_size - 1);
        return true;
    }
    if (strcmp(field, "clothing") == 0) {
        strncpy(out_str, entity->clothing, out_size - 1);
        return true;
    }
    if (strcmp(field, "personality") == 0) {
        strncpy(out_str, entity->personality, out_size - 1);
        return true;
    }
    return false;
}

/* Get a world variable value by name (prefix "world.") */
static bool world_get_int(const WorldState *ws, const char *name, int *out_val)
{
    if (!ws || !name) return false;
    const char *vname = name;
    if (strncmp(name, "world.", 6) == 0) vname = name + 6;

    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, vname) == 0) {
            switch (ws->variables[i].type) {
            case VAR_INT:  *out_val = ws->variables[i].int_val; return true;
            case VAR_FLOAT:*out_val = (int)ws->variables[i].float_val; return true;
            case VAR_BOOL: *out_val = ws->variables[i].bool_val ? 1 : 0; return true;
            default: return false;
            }
        }
    }
    return false;
}

/* Get a world variable string value by name */
static bool world_get_str(const WorldState *ws, const char *name,
                          char *out_str, int out_size)
{
    if (!ws || !name || !out_str || out_size <= 0) return false;
    const char *vname = name;
    if (strncmp(name, "world.", 6) == 0) vname = name + 6;

    for (int i = 0; i < ws->variable_count; i++) {
        if (strcmp(ws->variables[i].name, vname) == 0) {
            if (ws->variables[i].type == VAR_STRING ||
                ws->variables[i].type == VAR_ENUM) {
                strncpy(out_str, ws->variables[i].str_val, out_size - 1);
                out_str[out_size - 1] = '\0';
                return true;
            }
            /* Also convert int/float/bool to string for comparison */
            switch (ws->variables[i].type) {
            case VAR_INT:
                snprintf(out_str, out_size, "%d", ws->variables[i].int_val);
                return true;
            case VAR_BOOL:
                snprintf(out_str, out_size, "%s",
                         ws->variables[i].bool_val ? "true" : "false");
                return true;
            default: return false;
            }
        }
    }
    return false;
}

/* ═══════════════════════════════════════════════════════════════
   Condition DSL Parser

   Grammar:
     condition  := expr
     expr       := or_expr
     or_expr    := and_expr ("OR" and_expr)*
     and_expr   := not_expr ("AND" not_expr)*
     not_expr   := "NOT" atom | atom
     atom       := comparison | "(" expr ")"
     comparison := operand OP operand
     operand    := FIELD_REF | NUMBER | STRING_LITERAL
     OP         := "==" | "!=" | ">=" | "<=" | ">" | "<"
   ═══════════════════════════════════════════════════════════════ */

/* ── Token types for the simple lexer ── */
typedef enum {
    TK_EOF,
    TK_IDENT,      /* field reference or keyword */
    TK_NUMBER,     /* integer literal */
    TK_STRING,     /* quoted string literal */
    TK_EQ,         /* == */
    TK_NEQ,        /* != */
    TK_GTE,        /* >= */
    TK_LTE,        /* <= */
    TK_GT,         /* > */
    TK_LT,         /* < */
    TK_LPAREN,     /* ( */
    TK_RPAREN,     /* ) */
    TK_AND,        /* AND */
    TK_OR,         /* OR */
    TK_NOT,        /* NOT */
    TK_UNKNOWN,
} TokenType;

typedef struct {
    TokenType type;
    char      text[128];
    int       int_val;
} Token;

typedef struct {
    const char *p;
    Token       current;
} Lexer;

/* ── Lexer ── */

static void lex_skip_ws(Lexer *l)
{
    while (*l->p && isspace((unsigned char)*l->p)) l->p++;
}

static bool lex_isdigit(char c) { return c >= '0' && c <= '9'; }
static bool lex_isalpha_(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void lex_next(Lexer *l)
{
    memset(&l->current, 0, sizeof(Token));
    lex_skip_ws(l);

    if (*l->p == '\0') {
        l->current.type = TK_EOF;
        return;
    }

    /* Two-char operators */
    if (l->p[0] == '=' && l->p[1] == '=') {
        l->current.type = TK_EQ;
        strcpy(l->current.text, "==");
        l->p += 2; return;
    }
    if (l->p[0] == '!' && l->p[1] == '=') {
        l->current.type = TK_NEQ;
        strcpy(l->current.text, "!=");
        l->p += 2; return;
    }
    if (l->p[0] == '>' && l->p[1] == '=') {
        l->current.type = TK_GTE;
        strcpy(l->current.text, ">=");
        l->p += 2; return;
    }
    if (l->p[0] == '<' && l->p[1] == '=') {
        l->current.type = TK_LTE;
        strcpy(l->current.text, "<=");
        l->p += 2; return;
    }

    /* Single-char tokens */
    if (*l->p == '>') { l->current.type = TK_GT; l->p++; return; }
    if (*l->p == '<') { l->current.type = TK_LT; l->p++; return; }
    if (*l->p == '(') { l->current.type = TK_LPAREN; l->p++; return; }
    if (*l->p == ')') { l->current.type = TK_RPAREN; l->p++; return; }

    /* String literal */
    if (*l->p == '"' || *l->p == '\'') {
        char quote = *l->p;
        l->p++;
        int i = 0;
        while (*l->p && *l->p != quote && i < 127) {
            l->current.text[i++] = *l->p++;
        }
        if (*l->p == quote) l->p++;
        l->current.text[i] = '\0';
        l->current.type = TK_STRING;
        return;
    }

    /* Number */
    if (lex_isdigit(*l->p) || (*l->p == '-' && lex_isdigit(l->p[1]))) {
        int sign = 1;
        if (*l->p == '-') { sign = -1; l->p++; }
        int val = 0;
        while (lex_isdigit(*l->p)) {
            val = val * 10 + (*l->p - '0');
            l->p++;
        }
        l->current.int_val = sign * val;
        snprintf(l->current.text, sizeof(l->current.text), "%d", l->current.int_val);
        l->current.type = TK_NUMBER;
        return;
    }

    /* Identifier or keyword */
    if (lex_isalpha_(*l->p)) {
        int i = 0;
        while ((lex_isalpha_(*l->p) || lex_isdigit(*l->p) || *l->p == '.')
               && i < 127) {
            l->current.text[i++] = *l->p++;
        }
        l->current.text[i] = '\0';

        /* Check keywords */
        if (strcmp(l->current.text, "AND") == 0) {
            l->current.type = TK_AND;
        } else if (strcmp(l->current.text, "OR") == 0) {
            l->current.type = TK_OR;
        } else if (strcmp(l->current.text, "NOT") == 0) {
            l->current.type = TK_NOT;
        } else {
            l->current.type = TK_IDENT;
        }
        return;
    }

    /* Unknown — skip */
    l->current.type = TK_UNKNOWN;
    l->p++;
}

/* ── Forward declarations for recursive descent ── */
static bool parse_expr(Lexer *l, const CharacterCard *entity, const WorldState *ws);

/* ── atom: comparison | "(" expr ")" | NOT atom ── */
static bool parse_atom(Lexer *l, const CharacterCard *entity, const WorldState *ws)
{
    /* NOT atom */
    if (l->current.type == TK_NOT) {
        lex_next(l);
        return !parse_atom(l, entity, ws);
    }

    /* "(" expr ")" */
    if (l->current.type == TK_LPAREN) {
        lex_next(l);
        bool result = parse_expr(l, entity, ws);
        /* expect RPAREN */
        if (l->current.type == TK_RPAREN) lex_next(l);
        return result;
    }

    /* comparison: operand OP operand */
    /* Left operand must be a field reference (IDENT) */
    if (l->current.type != TK_IDENT) {
        /* Skip unknown tokens */
        lex_next(l);
        return false;
    }

    char left_field[128];
    strncpy(left_field, l->current.text, sizeof(left_field) - 1);
    lex_next(l);

    /* Operator */
    TokenType op = l->current.type;
    if (op != TK_EQ && op != TK_NEQ && op != TK_GT && op != TK_LT &&
        op != TK_GTE && op != TK_LTE) {
        return false;
    }
    lex_next(l);

    /* Right operand: NUMBER or STRING or IDENT */
    int right_int = 0;
    char right_str[128] = {0};
    bool right_is_str = false;

    if (l->current.type == TK_NUMBER) {
        right_int = l->current.int_val;
    } else if (l->current.type == TK_STRING) {
        strncpy(right_str, l->current.text, sizeof(right_str) - 1);
        right_is_str = true;
    } else if (l->current.type == TK_IDENT) {
        /* Could be an enum value like BANKRUPT, DEAD, HUNGRY, TIRED, NORMAL, etc.
           Or a field reference — for now treat as string for enum comparison. */
        /* Also check if it's a world variable reference */
        if (strncmp(l->current.text, "world.", 6) == 0) {
            if (world_get_int(ws, l->current.text, &right_int)) {
                /* OK, use int comparison */
            } else {
                /* Treat as string / enum value */
                strncpy(right_str, l->current.text, sizeof(right_str) - 1);
                right_is_str = true;
            }
        } else {
            /* Try to parse as integer first */
            char *endp = NULL;
            long v = strtol(l->current.text, &endp, 10);
            if (endp && *endp == '\0') {
                right_int = (int)v;
            } else {
                strncpy(right_str, l->current.text, sizeof(right_str) - 1);
                right_is_str = true;
            }
        }
    } else {
        return false;
    }
    lex_next(l);

    /* Evaluate comparison */
    /* If right operand is a string, treat left as string field or enum field */
    if (right_is_str) {
        /* For status enum comparison */
        if (strcmp(left_field, "status") == 0) {
            int left_status = (int)entity->status;
            /* Map enum string to int */
            int right_status = -1;
            if (strcmp(right_str, "NORMAL") == 0) right_status = STATUS_NORMAL;
            else if (strcmp(right_str, "HUNGRY") == 0) right_status = STATUS_HUNGRY;
            else if (strcmp(right_str, "TIRED") == 0) right_status = STATUS_TIRED;
            else if (strcmp(right_str, "SICK") == 0) right_status = STATUS_SICK;
            else if (strcmp(right_str, "INJURED") == 0) right_status = STATUS_INJURED;
            else if (strcmp(right_str, "EXCITED") == 0) right_status = STATUS_EXCITED;
            else if (strcmp(right_str, "ANGRY") == 0) right_status = STATUS_ANGRY;
            else if (strcmp(right_str, "SAD") == 0) right_status = STATUS_SAD;
            else if (strcmp(right_str, "HAPPY") == 0) right_status = STATUS_HAPPY;
            else if (strcmp(right_str, "BANKRUPT") == 0) right_status = STATUS_SAD; /* proxy */
            else if (strcmp(right_str, "DEAD") == 0) right_status = STATUS_INJURED; /* proxy */

            switch (op) {
            case TK_EQ:  return left_status == right_status;
            case TK_NEQ: return left_status != right_status;
            default: return false;
            }
        }

        /* For string field comparison (name, clothing, etc.) */
        char left_str[128] = {0};
        if (entity_get_str(entity, left_field, left_str, sizeof(left_str))) {
            switch (op) {
            case TK_EQ:  return strcmp(left_str, right_str) == 0;
            case TK_NEQ: return strcmp(left_str, right_str) != 0;
            default: return false;
            }
        }

        /* For world variable string comparison (world.xxx == "string") */
        if (strncmp(left_field, "world.", 6) == 0) {
            char wstr[128] = {0};
            if (world_get_str(ws, left_field, wstr, sizeof(wstr))) {
                switch (op) {
                case TK_EQ:  return strcmp(wstr, right_str) == 0;
                case TK_NEQ: return strcmp(wstr, right_str) != 0;
                default: return false;
                }
            }
        }

        return false;
    }

    /* Integer comparison */
    int left_int = 0;

    /* Check if left is a world variable */
    if (strncmp(left_field, "world.", 6) == 0) {
        if (!world_get_int(ws, left_field, &left_int))
            return false;
    } else if (!entity_get_int(entity, left_field, &left_int)) {
        /* Bug #21 fix: field not found means the condition cannot be
           evaluated — return false instead of silently treating as 0.
           This prevents rules from accidentally matching on typos or
           non-existent fields. */
        return false;
    }

    switch (op) {
    case TK_EQ:  return left_int == right_int;
    case TK_NEQ: return left_int != right_int;
    case TK_GT:  return left_int >  right_int;
    case TK_LT:  return left_int <  right_int;
    case TK_GTE: return left_int >= right_int;
    case TK_LTE: return left_int <= right_int;
    default: return false;
    }
}

/* ── and_expr := atom ("AND" atom)* ── */
static bool parse_and_expr(Lexer *l, const CharacterCard *entity, const WorldState *ws)
{
    bool result = parse_atom(l, entity, ws);
    while (l->current.type == TK_AND) {
        lex_next(l);
        bool rhs = parse_atom(l, entity, ws);
        result = result && rhs;
    }
    return result;
}

/* ── or_expr := and_expr ("OR" and_expr)* ── */
static bool parse_or_expr(Lexer *l, const CharacterCard *entity, const WorldState *ws)
{
    bool result = parse_and_expr(l, entity, ws);
    while (l->current.type == TK_OR) {
        lex_next(l);
        bool rhs = parse_and_expr(l, entity, ws);
        result = result || rhs;
    }
    return result;
}

/* ── expr := or_expr ── */
static bool parse_expr(Lexer *l, const CharacterCard *entity, const WorldState *ws)
{
    return parse_or_expr(l, entity, ws);
}

/* ═══════════════════════════════════════════════════════════════
   Public API: Condition evaluation
   ═══════════════════════════════════════════════════════════════ */

bool re_evaluate_condition(const Rule *rule, const CharacterCard *entity,
                           const WorldState *ws)
{
    if (!rule || !rule->condition[0] || !entity) return false;

    Lexer l;
    memset(&l, 0, sizeof(l));
    l.p = rule->condition;
    lex_next(&l);

    bool result = parse_expr(&l, entity, ws);
    return result;
}

int re_evaluate_all(const RuleEngine *re, const CharacterCard *entity,
                    const WorldState *ws, int *hit_ids, int max_hits)
{
    if (!re || !entity || !hit_ids || max_hits <= 0) return 0;

    int count = 0;
    for (int i = 0; i < re->count && count < max_hits; i++) {
        if (!re->rules[i].enabled) continue;
        if (re_evaluate_condition(&re->rules[i], entity, ws)) {
            hit_ids[count++] = re->rules[i].id;
        }
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════
   Effect application
   ═══════════════════════════════════════════════════════════════ */

void re_apply_effects(const Rule *rule, CharacterCard *entity,
                      EventLog *events, long long tick)
{
    if (!rule || !entity) return;

    for (int i = 0; i < rule->effect_count; i++) {
        const RuleEffect *e = &rule->effects[i];

        switch (e->type) {
        case EFFECT_SET_INT:
            if (strcmp(e->target, "money") == 0) {
                entity->money = e->int_value;
                if (entity->money < 0) entity->money = 0;
            } else if (strcmp(e->target, "player_affinity") == 0) {
                cc_set_player_affinity(entity, e->int_value);
            } else if (strcmp(e->target, "attr.appearance") == 0 ||
                       strcmp(e->target, "appearance") == 0) {
                cc_set_attributes(entity, e->int_value,
                    entity->attr.constitution, entity->attr.intelligence);
            } else if (strcmp(e->target, "attr.constitution") == 0 ||
                       strcmp(e->target, "constitution") == 0) {
                cc_set_attributes(entity, entity->attr.appearance,
                    e->int_value, entity->attr.intelligence);
            } else if (strcmp(e->target, "attr.intelligence") == 0 ||
                       strcmp(e->target, "intelligence") == 0) {
                cc_set_attributes(entity, entity->attr.appearance,
                    entity->attr.constitution, e->int_value);
            } else if (strcmp(e->target, "age") == 0) {
                entity->age = e->int_value;
                if (entity->age < 0) entity->age = 0;
            }
            break;

        case EFFECT_ADD_INT:
            if (strcmp(e->target, "money") == 0) {
                cc_change_money(entity, e->int_value);
            } else if (strcmp(e->target, "player_affinity") == 0) {
                cc_set_player_affinity(entity, entity->player_affinity + e->int_value);
            } else if (strcmp(e->target, "attr.appearance") == 0 ||
                       strcmp(e->target, "appearance") == 0) {
                cc_set_attributes(entity,
                    entity->attr.appearance + e->int_value,
                    entity->attr.constitution, entity->attr.intelligence);
            } else if (strcmp(e->target, "attr.constitution") == 0 ||
                       strcmp(e->target, "constitution") == 0) {
                cc_set_attributes(entity, entity->attr.appearance,
                    entity->attr.constitution + e->int_value,
                    entity->attr.intelligence);
            } else if (strcmp(e->target, "attr.intelligence") == 0 ||
                       strcmp(e->target, "intelligence") == 0) {
                cc_set_attributes(entity, entity->attr.appearance,
                    entity->attr.constitution,
                    entity->attr.intelligence + e->int_value);
            }
            break;

        case EFFECT_SET_STATUS:
            if (e->int_value >= 0 && e->int_value <= STATUS_HAPPY) {
                entity->status = (StatusType)e->int_value;
            }
            break;

        case EFFECT_SET_STRING:
            if (e->str_value[0]) {
                if (strcmp(e->target, "name") == 0) {
                    strncpy(entity->name, e->str_value, MAX_NAME_LEN - 1);
                } else if (strcmp(e->target, "clothing") == 0) {
                    strncpy(entity->clothing, e->str_value, MAX_CLOTHING_LEN - 1);
                } else if (strcmp(e->target, "personality") == 0) {
                    strncpy(entity->personality, e->str_value, MAX_PERSONALITY_LEN - 1);
                }
            }
            break;

        case EFFECT_CLAMP:
            /* Clamp target field to [int_value, int_value2] range */
            {
                int cur = 0;
                if (entity_get_int(entity, e->target, &cur)) {
                    if (cur < e->int_value) {
                        /* Set to min */
                        RuleEffect set_eff = {EFFECT_SET_INT, "", e->int_value, 0, ""};
                        strncpy(set_eff.target, e->target, RE_MAX_TARGET_LEN - 1);
                        re_apply_effects(&(Rule){.effects = {set_eff}, .effect_count = 1},
                                        entity, events, tick);
                    } else if (cur > e->int_value2) {
                        RuleEffect set_eff = {EFFECT_SET_INT, "", e->int_value2, 0, ""};
                        strncpy(set_eff.target, e->target, RE_MAX_TARGET_LEN - 1);
                        re_apply_effects(&(Rule){.effects = {set_eff}, .effect_count = 1},
                                        entity, events, tick);
                    }
                }
            }
            break;
        }

        if (events) {
            event_push(events, tick, -1, -1, EVENT_RULE_HIT,
                "{\"rule_id\":%d,\"target\":\"%s\",\"effect\":\"%s\",\"value\":%d}",
                rule->id, entity->name, e->target, e->int_value);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   Built-in rules
   ═══════════════════════════════════════════════════════════════ */

void re_register_builtins(RuleEngine *re)
{
    RuleEffect effects[RE_MAX_EFFECTS];

    /* Rule 1: IF money < 0 THEN status = SAD (bankruptcy indicator) */
    {
        effects[0].type = EFFECT_SET_STATUS;
        effects[0].int_value = STATUS_SAD;
        strncpy(effects[0].target, "status", RE_MAX_TARGET_LEN - 1);
        re_register(re, 100, "money < 0", effects, 1);
    }

    /* Rule 2: IF attr.constitution <= 0 THEN status = INJURED (death/incapacitation proxy) */
    {
        effects[0].type = EFFECT_SET_STATUS;
        effects[0].int_value = STATUS_INJURED;
        strncpy(effects[0].target, "status", RE_MAX_TARGET_LEN - 1);
        re_register(re, 99, "attr.constitution <= 0", effects, 1);
    }

    /* Rule 3: IF money > 1000 AND status == SAD THEN status = NORMAL
              (recovery from bankruptcy) */
    {
        effects[0].type = EFFECT_SET_STATUS;
        effects[0].int_value = STATUS_NORMAL;
        strncpy(effects[0].target, "status", RE_MAX_TARGET_LEN - 1);
        re_register(re, 50, "money > 1000 AND status == SAD", effects, 1);
    }

    /* Rule 4: IF attr.constitution > 0 AND status == INJURED THEN status = NORMAL
              (recovery from incapacitation) */
    {
        effects[0].type = EFFECT_SET_STATUS;
        effects[0].int_value = STATUS_NORMAL;
        strncpy(effects[0].target, "status", RE_MAX_TARGET_LEN - 1);
        re_register(re, 50, "attr.constitution > 0 AND status == INJURED", effects, 1);
    }

    /* Rule 5: IF money >= 0 AND money <= 10 THEN status = HUNGRY
              (poverty indicator — hunger threshold) */
    {
        effects[0].type = EFFECT_SET_STATUS;
        effects[0].int_value = STATUS_HUNGRY;
        strncpy(effects[0].target, "status", RE_MAX_TARGET_LEN - 1);
        re_register(re, 30, "money >= 0 AND money <= 10 AND status == NORMAL", effects, 1);
    }

    /* Rule 6: IF money > 10 AND status == HUNGRY THEN status = NORMAL
              (escape poverty/hunger) */
    {
        effects[0].type = EFFECT_SET_STATUS;
        effects[0].int_value = STATUS_NORMAL;
        strncpy(effects[0].target, "status", RE_MAX_TARGET_LEN - 1);
        re_register(re, 30, "money > 10 AND status == HUNGRY", effects, 1);
    }
}

/* ═══════════════════════════════════════════════════════════════
   ActionProposal
   ═══════════════════════════════════════════════════════════════ */

void ap_init(ActionProposal *ap)
{
    memset(ap, 0, sizeof(*ap));
}

bool ap_add_int_change(ActionProposal *ap, const char *entity,
                       const char *field, int delta)
{
    if (ap->count >= AP_MAX_ACTIONS) return false;
    ActionProposalEntry *e = &ap->actions[ap->count];
    memset(e, 0, sizeof(*e));
    e->type = AP_CHANGE_INT;
    strncpy(e->entity, entity, sizeof(e->entity) - 1);
    strncpy(e->field, field, sizeof(e->field) - 1);
    e->delta = delta;
    ap->count++;
    return true;
}

bool ap_add_int_set(ActionProposal *ap, const char *entity,
                    const char *field, int value)
{
    if (ap->count >= AP_MAX_ACTIONS) return false;
    ActionProposalEntry *e = &ap->actions[ap->count];
    memset(e, 0, sizeof(*e));
    e->type = AP_SET_INT;
    strncpy(e->entity, entity, sizeof(e->entity) - 1);
    strncpy(e->field, field, sizeof(e->field) - 1);
    e->value = value;
    ap->count++;
    return true;
}

bool ap_add_str_set(ActionProposal *ap, const char *entity,
                    const char *field, const char *value)
{
    if (ap->count >= AP_MAX_ACTIONS) return false;
    ActionProposalEntry *e = &ap->actions[ap->count];
    memset(e, 0, sizeof(*e));
    e->type = AP_SET_STR;
    strncpy(e->entity, entity, sizeof(e->entity) - 1);
    strncpy(e->field, field, sizeof(e->field) - 1);
    strncpy(e->str_value, value, sizeof(e->str_value) - 1);
    ap->count++;
    return true;
}

bool ap_add_status_set(ActionProposal *ap, const char *entity, int status_code)
{
    if (ap->count >= AP_MAX_ACTIONS) return false;
    ActionProposalEntry *e = &ap->actions[ap->count];
    memset(e, 0, sizeof(*e));
    e->type = AP_SET_STATUS;
    strncpy(e->entity, entity, sizeof(e->entity) - 1);
    e->value = status_code;
    ap->count++;
    return true;
}

/* ── Parse legacy CHANGES: text into ActionProposal ── */

int ap_parse_changes_text(const char *changes_text, ActionProposal *ap)
{
    if (!changes_text || !*changes_text || !ap) return 0;

    log_info("ap_parse_changes_text: input length=%d, text=%.200s", (int)strlen(changes_text), changes_text);

    char buf[2048];
    strncpy(buf, changes_text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int parsed = 0;
    char *line = buf;
    while (line && *line) {
        /* Find end of line */
        char *nl = strchr(line, '\n');
        char *saveptr = NULL;
        if (nl) { *nl = '\0'; saveptr = nl + 1; }
        else saveptr = NULL;

        /* Skip leading whitespace */
        while (*line == ' ' || *line == '\r') line++;
        if (*line == '\0') { line = saveptr; continue; }

        /* Skip narrative/dialogue lines (contain Chinese punctuation or quotes) */
        if (strstr(line, "\xEF\xBC\x9A") ||  /* ： */
            strstr(line, "\xE3\x80\x82") ||  /* 。 */
            strstr(line, "\xEF\xBC\x8C") ||  /* ， */
            strstr(line, "\xEF\xBC\x81") ||  /* ！ */
            strstr(line, "\xEF\xBC\x9F") ||  /* ？ */
            strstr(line, "\xE3\x80\x8C") ||  /* 「 */
            strchr(line, '"') ||
            strchr(line, '"')) {
            line = saveptr;
            continue;
        }

        /* Parse: target.var+delta, target.var=value, var+delta, var=value */
        char *eq = strchr(line, '=');
        char *plus = strchr(line, '+');
        char *minus = strchr(line, '-');

        if (eq) {
            *eq = '\0';
            char *varname = line;
            char *value = eq + 1;

            while (*varname == ' ') varname++;
            char *vend = varname + strlen(varname) - 1;
            while (vend > varname && *vend == ' ') *vend-- = '\0';
            while (*value == ' ') value++;

            if (!*varname) { line = saveptr; continue; }

            /* Check for dotted field: "entity.field=value" */
            char *dot = strchr(varname, '.');
            if (dot) {
                *dot = '\0';
                char *entity_name = varname;
                char *field_name = dot + 1;

                if (strcmp(field_name, "affinity") == 0 || strcmp(field_name, "player_affinity") == 0) {
                    int val = atoi(value);
                    ap_add_int_set(ap, entity_name, "player_affinity", val);
                    parsed++;
                } else if (strcmp(field_name, "status") == 0) {
                    /* Map string status to int */
                    int sc = STATUS_NORMAL;
                    if (strcmp(value, "NORMAL") == 0) sc = STATUS_NORMAL;
                    else if (strcmp(value, "HUNGRY") == 0) sc = STATUS_HUNGRY;
                    else if (strcmp(value, "TIRED") == 0) sc = STATUS_TIRED;
                    else if (strcmp(value, "SICK") == 0) sc = STATUS_SICK;
                    else if (strcmp(value, "INJURED") == 0) sc = STATUS_INJURED;
                    else if (strcmp(value, "EXCITED") == 0) sc = STATUS_EXCITED;
                    else if (strcmp(value, "ANGRY") == 0) sc = STATUS_ANGRY;
                    else if (strcmp(value, "SAD") == 0) sc = STATUS_SAD;
                    else if (strcmp(value, "HAPPY") == 0) sc = STATUS_HAPPY;
                    else { ap_add_int_set(ap, entity_name, field_name, atoi(value)); parsed++; goto next_line; }
                    ap_add_status_set(ap, entity_name, sc);
                    parsed++;
                } else {
                    ap_add_int_set(ap, entity_name, field_name, atoi(value));
                    parsed++;
                }
            } else {
                /* Undotted: player field or skill */
                if (strcmp(varname, "name") == 0) {
                    ap_add_str_set(ap, "player", varname, value);
                    parsed++;
                } else if (strcmp(varname, "clothing") == 0) {
                    ap_add_str_set(ap, "player", varname, value);
                    parsed++;
                } else if (strcmp(varname, "personality") == 0) {
                    ap_add_str_set(ap, "player", varname, value);
                    parsed++;
                } else if (strcmp(varname, "status") == 0) {
                    int sc = STATUS_NORMAL;
                    if (strcmp(value, "NORMAL") == 0) sc = STATUS_NORMAL;
                    else if (strcmp(value, "HUNGRY") == 0) sc = STATUS_HUNGRY;
                    else if (strcmp(value, "TIRED") == 0) sc = STATUS_TIRED;
                    else if (strcmp(value, "SICK") == 0) sc = STATUS_SICK;
                    else if (strcmp(value, "INJURED") == 0) sc = STATUS_INJURED;
                    else if (strcmp(value, "EXCITED") == 0) sc = STATUS_EXCITED;
                    else if (strcmp(value, "ANGRY") == 0) sc = STATUS_ANGRY;
                    else if (strcmp(value, "SAD") == 0) sc = STATUS_SAD;
                    else if (strcmp(value, "HAPPY") == 0) sc = STATUS_HAPPY;
                    else sc = atoi(value);
                    ap_add_status_set(ap, "player", sc);
                    parsed++;
                } else {
                    ap_add_int_set(ap, "player", varname, atoi(value));
                    parsed++;
                }
            }
        } else if (plus || minus) {
            char *op;
            int sign;
            if (plus) { op = plus; sign = 1; }
            else      { op = minus; sign = -1; }

            *op = '\0';
            char *varname = line;
            while (*varname == ' ') varname++;
            char *vend = varname + strlen(varname) - 1;
            while (vend > varname && *vend == ' ') *vend-- = '\0';
            int delta = sign * atoi(op + 1);

            if (!*varname) { line = saveptr; continue; }

            char *dot = strchr(varname, '.');
            if (dot) {
                *dot = '\0';
                char *entity_name = varname;
                char *field_name = dot + 1;
                if (strcmp(field_name, "affinity") == 0 || strcmp(field_name, "player_affinity") == 0) {
                    ap_add_int_change(ap, entity_name, "player_affinity", delta);
                    parsed++;
                } else {
                    ap_add_int_change(ap, entity_name, field_name, delta);
                    parsed++;
                }
            } else {
                ap_add_int_change(ap, "player", varname, delta);
                parsed++;
            }
        }
        next_line:
        line = saveptr;
    }
    return parsed;
}

/* ── Convert a RuleEffect into a ChangeSetFull entry ── */
static void rule_effect_to_cs(const RuleEffect *e, const char *entity_name,
                              ChangeSetFull *out_cs)
{
    switch (e->type) {
    case EFFECT_SET_INT:
        cs_full_add_set_int(out_cs, entity_name, e->target, e->int_value);
        break;
    case EFFECT_ADD_INT:
        cs_full_add_delta(out_cs, entity_name, e->target, e->int_value);
        break;
    case EFFECT_SET_STATUS:
        cs_full_add_status(out_cs, entity_name, e->int_value);
        break;
    case EFFECT_SET_STRING:
        cs_full_add_set_str(out_cs, entity_name, e->target, e->str_value);
        break;
    case EFFECT_CLAMP:
        cs_full_add_clamp(out_cs, entity_name, e->target,
                          e->int_value, e->int_value2);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════
   Main pipeline: Proposal → Rule Engine → ChangeSet → Apply

   TWO-PHASE design (fixes Bug #2: rule effects could be overwritten
   by original changes because re_apply_effects modified entities
   directly before cs_apply re-applied the original changes):

   Phase A — Collect: convert all ActionProposal entries into ChangeSetFull.
   Phase B — Validate: evaluate rules against affected entities in their
              CURRENT state, and append rule-triggered effects to the SAME
              ChangeSetFull (later entries take precedence over earlier ones).

   The caller then calls cs_apply() ONCE, applying all changes atomically.
   A post-apply rule pass (in apply_response) handles cascading effects.
   ═══════════════════════════════════════════════════════════════ */

int re_process_proposal(const RuleEngine *re, const ActionProposal *ap,
                        CharacterCard *player, CharacterCard *npcs,
                        int npc_count, const WorldState *ws,
                        ChangeSetFull *out_cs, EventLog *events, long long tick)
{
    cs_full_init(out_cs);
    int rules_fired = 0;

    if (!ap || ap->count == 0) {
        log_info("RuleEngine: no actions to process");
        return 0;
    }
    log_info("RuleEngine: processing %d actions...", ap->count);

    /* ── Phase A: Collect all proposal entries into ChangeSetFull ── */
    for (int i = 0; i < ap->count; i++) {
        const ActionProposalEntry *e = &ap->actions[i];
        CharacterCard *target = cs_find_entity(e->entity, player, npcs, npc_count);
        if (!target) continue;

        switch (e->type) {
        case AP_CHANGE_INT:
            cs_full_add_delta(out_cs, e->entity, e->field, e->delta);
            break;
        case AP_SET_INT:
            cs_full_add_set_int(out_cs, e->entity, e->field, e->value);
            break;
        case AP_SET_STR:
            cs_full_add_set_str(out_cs, e->entity, e->field, e->str_value);
            break;
        case AP_SET_STATUS:
            cs_full_add_status(out_cs, e->entity, e->value);
            break;
        }
    }

    /* ── Phase B: Evaluate rules against affected entities (in current state)
                  and append rule-triggered effects to the ChangeSetFull.
                  Later entries take precedence over earlier ones in cs_apply,
                  so rule effects override the original proposal. ── */
    for (int i = 0; i < ap->count; i++) {
        const ActionProposalEntry *e = &ap->actions[i];
        CharacterCard *target = cs_find_entity(e->entity, player, npcs, npc_count);
        if (!target) continue;

        /* Evaluate rules against this entity in its CURRENT state */
        int hit_ids[RE_MAX_RULES];
        int n_hits = re_evaluate_all(re, target, ws, hit_ids, RE_MAX_RULES);

        for (int j = 0; j < re->count; j++) {
            if (!re->rules[j].enabled) continue;
            bool hit = false;
            for (int k = 0; k < n_hits; k++) {
                if (hit_ids[k] == re->rules[j].id) { hit = true; break; }
            }
            if (hit) {
                /* Append rule effects to ChangeSetFull (later entries win) */
                for (int ef = 0; ef < re->rules[j].effect_count; ef++) {
                    rule_effect_to_cs(&re->rules[j].effects[ef],
                                      e->entity, out_cs);
                }
                rules_fired++;
            }
            if (events) {
                event_push(events, tick, -1, -1,
                    hit ? EVENT_RULE_HIT : EVENT_SYSTEM,
                    "{\"rule_id\":%d,\"condition\":\"%.128s\",\"hit\":%s,\"entity\":\"%s\"}",
                    re->rules[j].id, re->rules[j].condition,
                    hit ? "true" : "false", target->name);
            }
        }
    }

    log_info("RuleEngine: done — %d actions, %d rules fired, %d changes in ChangeSet",
             ap->count, rules_fired, out_cs->count);
    return rules_fired;
}

/* ── Convenience: apply legacy CHANGES through Rule Engine ── */

int rule_engine_apply_changes(const RuleEngine *re, const char *changes_text,
                              CharacterCard *player, CharacterCard *npcs,
                              int npc_count, const WorldState *ws,
                              EventLog *events, long long tick)
{
    /* Parse legacy CHANGES text into an ActionProposal */
    ActionProposal ap;
    ap_init(&ap);
    int parsed = ap_parse_changes_text(changes_text, &ap);

    if (parsed == 0) return 0;

    /* Run through the Rule Engine pipeline */
    ChangeSetFull cs;
    int rules_fired = re_process_proposal(re, &ap, player, npcs, npc_count,
                                          ws, &cs, events, tick);

    /* Apply the validated ChangeSet */
    cs_apply(&cs, player, npcs, npc_count, events, tick);

    return rules_fired;
}

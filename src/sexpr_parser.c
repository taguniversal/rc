#include "sexpr_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "eval.h"
#include <dirent.h>
#include "log.h"

static const char *skip_whitespace(const char *s)
{
    while (isspace(*s))
        s++;
    return s;
}

char *load_file(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
        return NULL;

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    rewind(file);

    char *buffer = malloc(len + 1);
    if (!buffer)
    {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, len, file);
    buffer[len] = '\0';
    fclose(file);
    return buffer;
}

static char *parse_atom(const char **input)
{
    const char *start = *input;
    while (**input && !isspace(**input) && **input != '(' && **input != ')')
    {
        (*input)++;
    }
    size_t len = *input - start;
    char *atom = malloc(len + 1);
    strncpy(atom, start, len);
    atom[len] = '\0';
    return atom;
}

static SExpr *get_child_by_tag(SExpr *parent, const char *tag)
{
    for (size_t i = 1; i < parent->count; ++i)
    {
        SExpr *child = parent->list[i];
        if (child->type == S_EXPR_LIST && child->count > 0)
        {
            SExpr *head = child->list[0];
            if (head->type == S_EXPR_ATOM && strcmp(head->atom, tag) == 0)
            {
                return child;
            }
        }
    }
    return NULL;
}

static const char *get_atom_value(SExpr *list, size_t index)
{
    if (!list || index >= list->count)
        return NULL;
    SExpr *item = list->list[index];
    return (item->type == S_EXPR_ATOM) ? item->atom : NULL;
}

ConditionalInvocation *parse_conditional_invocation(SExpr *ci_expr, const char *filename)
{
    ConditionalInvocation *ci = calloc(1, sizeof(ConditionalInvocation));

    // 1. Get InvocationName
    SExpr *name_expr = get_child_by_tag(ci_expr, "InvocationName");
    const char *template = get_atom_value(name_expr, 1);
    if (!template)
    {
        LOG_ERROR("❌ [ConditionalInvocation:%s] Missing InvocationName", filename);
        free(ci);
        return NULL;
    }
    ci->invocation_template = strdup(template);

    // 2. Parse Case forms
    for (size_t i = 1; i < ci_expr->count; ++i)
    {
        SExpr *item = ci_expr->list[i];
        if (item->type != S_EXPR_LIST || item->count < 3)
            continue;

        SExpr *tag = item->list[0];
        if (tag->type != S_EXPR_ATOM || strcmp(tag->atom, "Case") != 0)
            continue;

        SExpr *val_attr = item->list[1];
        SExpr *result_atom = item->list[2];
        if (val_attr->type != S_EXPR_ATOM || result_atom->type != S_EXPR_ATOM)
            continue;

        ConditionalInvocationCase *cc = calloc(1, sizeof(ConditionalInvocationCase));

        cc->pattern = strdup(val_attr->atom);   // pattern (e.g., 01)
        cc->result = strdup(result_atom->atom); // result (e.g., 1)

        cc->next = ci->cases;
        ci->cases = cc;
    }

    return ci;
}

Definition *parse_definition(SExpr *expr, const char *filename)
{
    Definition *def = calloc(1, sizeof(Definition));

    // 1. Get Name
    SExpr *name_expr = get_child_by_tag(expr, "Name");
    const char *name = get_atom_value(name_expr, 1);
    if (!name)
    {
        LOG_ERROR("❌ [Definition:%s] Missing or invalid Name", filename);
        free(def);
        return NULL;
    }

    def->name = strdup(name);

    // 2. Get SourceList
    SExpr *src_list = get_child_by_tag(expr, "SourceList");
    if (src_list)
    {
        for (size_t i = 1; i < src_list->count; ++i)
        {
            SExpr *place = src_list->list[i];
            const char *pname = get_atom_value(place, 1);
            if (!pname)
                continue;

            SourcePlace *sp = calloc(1, sizeof(SourcePlace));
            sp->name = strdup(pname);
            sp->signal = &NULL_SIGNAL;

            sp->next = def->sources;
            def->sources = sp;
        }
    }

    // 3. Get DestinationList
    SExpr *dst_list = get_child_by_tag(expr, "DestinationList");
    if (dst_list)
    {
        for (size_t i = 1; i < dst_list->count; ++i)
        {
            SExpr *place = dst_list->list[i];
            const char *pname = get_atom_value(place, 1);
            if (!pname)
                continue;

            DestinationPlace *dp = calloc(1, sizeof(DestinationPlace));
            dp->name = strdup(pname);
            dp->signal = &NULL_SIGNAL;

            dp->next = def->destinations;
            def->destinations = dp;
        }
    }

    SExpr *ci_expr = get_child_by_tag(expr, "ConditionalInvocation");
    if (ci_expr)
    {
        def->conditional_invocation = parse_conditional_invocation(ci_expr, filename);
        if (def->conditional_invocation)
        {
            LOG_INFO("🧩 ConditionalInvocation parsed for %s", def->name);
        }
    }

    return def;
}

Invocation *parse_invocation(SExpr *expr, const char *filename)
{
    Invocation *inv = calloc(1, sizeof(Invocation));

    // 1. Get Name
    SExpr *name_expr = get_child_by_tag(expr, "Name");
    const char *name = get_atom_value(name_expr, 1);
    if (!name)
    {
        LOG_ERROR("❌ [Invocation:%s] Missing or invalid Name", filename);
        free(inv);
        return NULL;
    }

    inv->name = strdup(name);

    // 2. Get SourceList
    SExpr *src_list = get_child_by_tag(expr, "SourceList");
    if (src_list)
    {
        for (size_t i = 1; i < src_list->count; ++i)
        {
            SExpr *place = src_list->list[i];
            if (place->type != S_EXPR_LIST || place->count < 2)
                continue;

            SExpr *tag = place->list[0];
            SExpr *value = place->list[1];
            if (tag->type != S_EXPR_ATOM || value->type != S_EXPR_ATOM)
                continue;

            SourcePlace *sp = calloc(1, sizeof(SourcePlace));
            sp->signal = &NULL_SIGNAL;

            // Detect whether it's a literal or a reference
            if (isdigit(value->atom[0]) || value->atom[0] == '"')
            {
                sp->value = strdup(value->atom);
            }
            else
            {
                sp->name = strdup(value->atom);
            }

            sp->next = inv->sources;
            inv->sources = sp;
        }
    }

    // 3. Get DestinationList
    SExpr *dst_list = get_child_by_tag(expr, "DestinationList");
    if (dst_list)
    {
        for (size_t i = 1; i < dst_list->count; ++i)
        {
            SExpr *place = dst_list->list[i];
            const char *pname = get_atom_value(place, 1);
            if (!pname)
                continue;

            DestinationPlace *dp = calloc(1, sizeof(DestinationPlace));
            dp->name = strdup(pname);
            dp->signal = &NULL_SIGNAL;

            dp->next = inv->destinations;
            inv->destinations = dp;
        }
    }

    return inv;
}

int parse_block_from_sexpr(Block *blk, const char *inv_dir)
{
    DIR *dir = opendir(inv_dir);
    if (!dir)
    {
        LOG_ERROR("❌ Unable to open invocation directory: %s", inv_dir);
        exit(1);
    }

    LOG_INFO("🔍 Parsing block contents from directory: %s", inv_dir);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (!strstr(entry->d_name, ".sexpr"))
            continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", inv_dir, entry->d_name);

        LOG_INFO("📄 Loading S-expression: %s", path);
        char *contents = load_file(path);
        if (!contents)
        {
            LOG_ERROR("❌ Failed to read file: %s", path);
            exit(1);
        }

        SExpr *expr = parse_sexpr(contents);
        free(contents);

        if (!expr || expr->type != S_EXPR_LIST || expr->count == 0)
        {
            LOG_ERROR("❌ Invalid or empty S-expression in file: %s", path);
            if (expr)
                free_sexpr(expr);
            exit(1);
        }

        SExpr *head = expr->list[0];
        if (head->type != S_EXPR_ATOM)
        {
            LOG_ERROR("❌ Malformed S-expression (non-atom head) in file: %s", path);
            free_sexpr(expr);
            exit(1);
        }

        const char *top_tag = head->atom;
        const char *file_basename = entry->d_name;

        if (strcmp(top_tag, "Definition") == 0)
        {
            Definition *def = parse_definition(expr, file_basename);
            if (!def)
            {
                LOG_ERROR("❌ Failed to parse Definition from: %s", path);
                free_sexpr(expr);
                exit(1);
            }
            def->next = blk->definitions;
            blk->definitions = def;
            LOG_INFO("📦 Added Definition: %s", def->name);
        }
        else if (strcmp(top_tag, "Invocation") == 0)
        {
            Invocation *inv = parse_invocation(expr, file_basename);
            if (!inv)
            {
                LOG_ERROR("❌ Failed to parse Invocation from: %s", path);
                free_sexpr(expr);
                exit(1);
            }
            inv->next = blk->invocations;
            blk->invocations = inv;
            LOG_INFO("📦 Added Invocation: %s", inv->name);
        }
        else
        {
            LOG_ERROR("❌ Unknown top-level tag: (%s) in file %s", top_tag, file_basename);
            free_sexpr(expr);
            exit(1);
        }

        free_sexpr(expr);
    }

    closedir(dir);

    link_invocations(blk);
    LOG_INFO("✅ Block population from S-expression completed.");

    return 0;
}

static struct SExpr *parse_expr(const char **input);

static struct SExpr *parse_list(const char **input)
{
    struct SExpr *expr = malloc(sizeof(struct SExpr));
    expr->type = S_EXPR_LIST;
    expr->list = NULL;
    expr->count = 0;

    *input = skip_whitespace(*input);
    while (**input && **input != ')')
    {
        struct SExpr *child = parse_expr(input);
        expr->list = realloc(expr->list, sizeof(struct SExpr *) * (expr->count + 1));
        expr->list[expr->count++] = child;
        *input = skip_whitespace(*input);
    }

    if (**input == ')')
        (*input)++;
    return expr;
}

static struct SExpr *parse_expr(const char **input)
{
    *input = skip_whitespace(*input);
    if (**input == '(')
    {
        (*input)++;
        return parse_list(input);
    }
    else
    {
        struct SExpr *atom = malloc(sizeof(struct SExpr));
        atom->type = S_EXPR_ATOM;
        atom->atom = parse_atom(input);
        atom->list = NULL;
        atom->count = 0;
        return atom;
    }
}

SExpr *parse_sexpr(const char *input)
{
    return parse_expr(&input);
}

void print_sexpr(const SExpr *expr, int indent)
{
    if (expr->type == S_EXPR_ATOM)
    {
        printf("%*s%s\\n", indent, "", expr->atom);
    }
    else
    {
        printf("%*s(\\n", indent, "");
        for (size_t i = 0; i < expr->count; ++i)
        {
            print_sexpr(expr->list[i], indent + 2);
        }
        printf("%*s)\\n", indent, "");
    }
}

void free_sexpr(SExpr *expr)
{
    if (expr->type == S_EXPR_LIST)
    {
        for (size_t i = 0; i < expr->count; ++i)
        {
            free_sexpr(expr->list[i]);
        }
        free(expr->list);
    }
    else if (expr->type == S_EXPR_ATOM)
    {
        free(expr->atom);
    }
    free(expr);
}

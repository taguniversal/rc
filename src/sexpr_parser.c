#include "sexpr_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "eval.h"
#include <dirent.h>
#include "log.h"
#include <sys/stat.h>
#include <sys/types.h>

static struct SExpr *parse_expr(const char **input);

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

SExpr *get_child_by_tag(SExpr *parent, const char *tag)
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

const char *get_atom_value(SExpr *list, size_t index)
{
    if (!list || index >= list->count)
        return NULL;
    SExpr *item = list->list[index];
    return (item->type == S_EXPR_ATOM) ? item->atom : NULL;
}

ConditionalInvocation *parse_conditional_invocation(SExpr *ci_expr, const char *filename)
{
    ConditionalInvocation *ci = calloc(1, sizeof(ConditionalInvocation));

    // 1. Get Template
    SExpr *template_expr = get_child_by_tag(ci_expr, "Template");
    if (!template_expr || template_expr->count < 2)
    {
        LOG_ERROR("❌ [ConditionalInvocation:%s] Missing Template", filename);
        free(ci);
        return NULL;
    }

    size_t total_len = 0;
    for (size_t i = 1; i < template_expr->count; ++i)
    {
        if (template_expr->list[i]->type != S_EXPR_ATOM)
            continue;
        total_len += strlen(template_expr->list[i]->atom);
    }

    char *joined = calloc(total_len + 1, 1);
    for (size_t i = 1; i < template_expr->count; ++i)
    {
        if (template_expr->list[i]->type == S_EXPR_ATOM)
            strcat(joined, template_expr->list[i]->atom);
    }

    ci->invocation_template = joined;

    // Now populate template_args[]
    ci->arg_count = strlen(joined);
    ci->template_args = calloc(ci->arg_count, sizeof(char *));
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        ci->template_args[i] = malloc(2); // 1 char + null
        ci->template_args[i][0] = joined[i];
        ci->template_args[i][1] = '\0';
    }

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
        {
            LOG_ERROR("❌ [ConditionalInvocation:%s] Malformed Case — expected atom pattern and result", filename);
            exit(1);
        }

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
    else
    {
        LOG_ERROR("❌ [Definition:%s] Missing SourceList", def->name);
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
    else
    {
        LOG_ERROR("❌ [Definition:%s] Missing DestinationList", def->name);
    }

    SExpr *ci_expr = get_child_by_tag(expr, "ConditionalInvocation");
    if (ci_expr)
    {
        def->conditional_invocation = parse_conditional_invocation(ci_expr, filename);
        if (def->conditional_invocation)
        {
            LOG_INFO("🧩 ConditionalInvocation parsed for %s", def->name);
            for (ConditionalInvocationCase *c = def->conditional_invocation->cases; c; c = c->next)
            {
                LOG_INFO("    Case %s → %s", c->pattern, c->result);
            }
        }
    }

    // 5. Parse inline Invocation blocks (e.g., inside NOR)
    for (size_t i = 1; i < expr->count; ++i)
    {
        SExpr *item = expr->list[i];
        if (item->type != S_EXPR_LIST || item->count < 1)
            continue;

        SExpr *head = item->list[0];
        if (head->type != S_EXPR_ATOM || strcmp(head->atom, "Invocation") != 0)
            continue;

        // Parse it as a regular invocation
        Invocation *inv = parse_invocation_from_expr(item);
        if (!inv)
        {
            LOG_ERROR("❌ Failed to parse inline Invocation inside %s", def->name);
            continue;
        }

        inv->next = def->invocations; // NOTE: You'll need to add this field!
        def->invocations = inv;

        LOG_INFO("🔹 Parsed inline Invocation: %s in %s", inv->name, def->name);
    }

    return def;
}
Invocation *parse_invocation_from_expr(SExpr *expr)
{
    Invocation *inv = calloc(1, sizeof(Invocation));

    // Name
    SExpr *name_expr = get_child_by_tag(expr, "Name");
    const char *name = get_atom_value(name_expr, 1);
    if (!name)
    {
        LOG_ERROR("❌ [Invocation] Missing or invalid Name");
        free(inv);
        return NULL;
    }
    inv->name = strdup(name);

    // SourceList
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
    else
    {
        LOG_ERROR("❌ [Invocation:%s] Missing SourceList", inv->name);
    }

    // DestinationList
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
    else
    {
        LOG_ERROR("❌ [Invocation:%s] Missing DestinationList", inv->name);
    }

    return inv;
}

Invocation *parse_invocation(SExpr *expr, const char *filename)
{
    Invocation *inv = parse_invocation_from_expr(expr);
    if (!inv)
    {
        LOG_ERROR("❌ Failed to parse Invocation from file: %s", filename);
        return NULL;
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

static void emit_indent(FILE *out, int indent)
{
    for (int i = 0; i < indent; ++i)
        fputc(' ', out);
}

static void emit_atom(FILE *out, const char *atom)
{
    // Quote if needed (e.g., contains non-alphanum)
    if (strpbrk(atom, " ()\"\t\n"))
        fprintf(out, "\"%s\"", atom);
    else
        fputs(atom, out);
}

static void emit_source_list(FILE *out, SourcePlace *src, int indent)
{
    emit_indent(out, indent);
    fputs("(SourceList\n", out);
    for (; src; src = src->next)
    {
        emit_indent(out, indent + 2);
        fprintf(out, "(SourcePlace ");
        emit_atom(out, src->name ? src->name : src->value);
        fputs(")\n", out);
    }
    emit_indent(out, indent);
    fputs(")\n", out);
}

static void emit_destination_list(FILE *out, DestinationPlace *dst, int indent)
{
    emit_indent(out, indent);
    fputs("(DestinationList\n", out);
    for (; dst; dst = dst->next)
    {
        emit_indent(out, indent + 2);
        fprintf(out, "(DestinationPlace ");
        emit_atom(out, dst->name);
        fputs(")\n", out);
    }
    emit_indent(out, indent);
    fputs(")\n", out);
}

static void emit_conditional(FILE *out, ConditionalInvocation *ci, int indent)
{
    emit_indent(out, indent);
    fputs("(ConditionalInvocation\n", out);

    emit_indent(out, indent + 2);
    fputs("(Template", out);
    const char *s = ci->invocation_template;
    for (; *s; ++s)
    {
        fprintf(out, " %c", *s);
    }
    fputs(")\n", out);

    for (ConditionalInvocationCase *c = ci->cases; c; c = c->next)
    {
        emit_indent(out, indent + 2);
        fprintf(out, "(Case %s %s)\n", c->pattern, c->result);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}
static void emit_invocation(FILE *out, Invocation *inv, int indent)
{
    emit_indent(out, indent);
    fputs("(Invocation\n", out);

    emit_indent(out, indent + 2);
    fputs("(Name ", out);
    emit_atom(out, inv->name);
    fputs(")\n", out);

    emit_source_list(out, inv->sources, indent + 2);
    emit_destination_list(out, inv->destinations, indent + 2);

    emit_indent(out, indent);
    fputs(")\n", out);
}

void write_definition_to_file(Definition *def, FILE *out, int indent)
{
    emit_indent(out, indent);
    fputs("(Definition\n", out);

    emit_indent(out, indent + 2);
    fputs("(Name ", out);
    emit_atom(out, def->name);
    fputs(")\n", out);

    emit_source_list(out, def->sources, indent + 2);
    emit_destination_list(out, def->destinations, indent + 2);

    if (def->conditional_invocation)
    {
        emit_conditional(out, def->conditional_invocation, indent + 2);
    }

    for (Invocation *inv = def->invocations; inv; inv = inv->next)
    {
        emit_invocation(out, inv, indent + 2);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void write_definition_to_path(Definition *def, const char *filepath)
{
    FILE *out = fopen(filepath, "w");
    if (!out)
    {
        fprintf(stderr, "❌ Failed to open file for writing: %s\n", filepath);
        return;
    }

    write_definition_to_file(def, out, 0);
    fclose(out);
    LOG_INFO("✅ Wrote definition '%s' to %s", def->name, filepath);
}

void emit_all_definitions(Block *blk, const char *dirpath)
{
    // Create the output directory if it doesn't exist
    mkdir(dirpath, 0755);

    for (Definition *def = blk->definitions; def; def = def->next)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.sexpr", dirpath, def->name);
        write_definition_to_path(def, path);
    }
}

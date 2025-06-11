#include "emit_util.h"
#include "emit_sexpr.h"
#include "emit_util.h"
#include "instance.h"
#include "eval.h"
#include "log.h"
#include <stdio.h>
#include <errno.h>

void emit_instance(Instance *instance, const char *out_dir)
{
    if (!instance || !instance->name || !instance->invocation || !instance->definition)
    {
        LOG_WARN("⚠️  Skipping invalid instance during emission.");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.sexp", out_dir, instance->name);
    FILE *f = fopen(path, "w");
    if (!f)
    {
        LOG_ERROR("❌ Failed to open file for writing: %s", path);
        return;
    }

    fprintf(f, "(Instance\n");
    fprintf(f, "  (Name %s)\n", instance->name);

    // 🔁 Use the shared helper — preserves parsed-from and roles
    emit_invocation(f, instance->invocation, 2);

    // Emit Definition
    emit_definition(f, instance->definition, 2);

    fprintf(f, ")\n");
    fclose(f);

    LOG_INFO("📤 Emitted instance to %s", path);
}

void emit_all_instances(Block *blk, const char *dir)
{
    LOG_INFO("🌀 Emitting all Instances to s-expr: %s", dir);

    for (InstanceList *cur = blk->instances; cur != NULL; cur = cur->next)
    {
        Instance *instance = cur->instance;
        if (!instance || !instance->name)
        {
            LOG_WARN("⚠️ Skipping null instance or instance with no name");
            continue;
        }

        emit_instance(instance, dir);
    }

    LOG_INFO("✅ Done emitting all Instances.");
}

void emit_input_signals(FILE *out, StringList *inputs, int indent, const char *role_hint)
{
    if (!inputs || string_list_count(inputs) == 0)
        return;

    emit_indent(out, indent);
    fputs("(Inputs\n", out);

    for (size_t i = 0; i < string_list_count(inputs); ++i)
    {
        const char *signal = string_list_get_by_index(inputs, i);
        if (!signal)
            continue;

        emit_indent(out, indent + 2);
        emit_atom(out, signal);
        fputs("\n", out);

        if (role_hint)
        {
            emit_indent(out, indent + 2);
            fprintf(out, ";; role: %s\n", role_hint);
        }
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_output_signals(FILE *out, StringList *outputs, int indent, const char *role_hint)
{
    if (!outputs || string_list_count(outputs) == 0)
        return;

    emit_indent(out, indent);
    fputs("(Outputs\n", out);

    for (size_t i = 0; i < string_list_count(outputs); ++i)
    {
        const char *signal = string_list_get_by_index(outputs, i);
        if (!signal)
            continue;

        emit_indent(out, indent + 2);
        emit_atom(out, signal);
        fputs("\n", out);

        if (role_hint)
        {
            emit_indent(out, indent + 2);
            fprintf(out, ";; role: %s\n", role_hint);
        }
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_place_of_resolution(FILE *out, Definition *def, int indent)
{
    if (!def || !def->body)
        return;

    emit_indent(out, indent);
    fputs("(PlaceOfResolution\n", out);

    for (BodyItem *item = def->body; item != NULL; item = item->next)
    {
        switch (item->type)
        {
            case BODY_INVOCATION:
                if (item->data.invocation)
                    emit_invocation(out, item->data.invocation, indent + 2);
                break;

            case BODY_SIGNAL_OUTPUT:
                emit_indent(out, indent + 2);
                fputs("(SourcePlace\n", out);

                emit_indent(out, indent + 4);
                fprintf(out, "(Name %s)\n", item->data.signal_name);

                emit_indent(out, indent + 4);
                fputs("(ContentFrom ConditionalInvocationResult)\n", out);  // May customize later

                emit_indent(out, indent + 2);
                fputs(")\n", out);
                break;

            default:
                // Ignore BODY_SIGNAL_INPUT for POR
                break;
        }
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}



void emit_invocation(FILE *out, Invocation *inv, int indent)
{
    if (!inv)
        return;

    fprintf(out, ";; Invocation parsed from %s\n", inv->origin_sexpr_path ? inv->origin_sexpr_path : "(unknown)");
    emit_indent(out, indent);
    fputs("(Invocation\n", out);

    // ─── Target ─────────────────────────────────────────────────────
    emit_indent(out, indent + 2);
    fputs("(Target ", out);
    emit_atom(out, inv->target_name);
    fputs(")\n", out);

    // ─── Literal Bindings ───────────────────────────────────────────
    if (inv->literal_bindings)
    {
        for (size_t i = 0; i < inv->literal_bindings->count; ++i)
        {
            LiteralBinding *binding = &inv->literal_bindings->items[i];
            if (!binding->name || !binding->value)
                continue;

            emit_indent(out, indent + 2);
            fputc('(', out);
            emit_atom(out, binding->name);
            fputc(' ', out);
            emit_atom(out, binding->value);
            fputs(")\n", out);
        }
    }

    // ─── Inputs ─────────────────────────────────────────────────────
    emit_indent(out, indent + 2);
    fputs(";; Inputs (flow into definition)\n", out);
    emit_indent(out, indent + 2);
    fputs("(Inputs", out);
    for (size_t i = 0; i < string_list_count(inv->input_signals); ++i)
    {
        const char *sig = string_list_get_by_index(inv->input_signals, i);
        fputc(' ', out);
        emit_atom(out, sig);
    }
    fputs(")\n", out);

    // ─── Outputs ────────────────────────────────────────────────────
    emit_indent(out, indent + 2);
    fputs(";; Outputs (flow out of definition)\n", out);
    emit_indent(out, indent + 2);
    fputs("(Outputs", out);
    for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
    {
        const char *sig = string_list_get_by_index(inv->output_signals, i);
        fputc(' ', out);
        emit_atom(out, sig);
    }
    fputs(")\n", out);

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_conditional(FILE *out, ConditionalInvocation *ci, int indent)
{
    if (!ci)
        return;

    emit_indent(out, indent);
    fputs("(ConditionalInvocation\n", out);

    // Emit Output
    if (ci->output)
    {
        emit_indent(out, indent + 2);
        fprintf(out, "(Output %s)\n", ci->output);
    }

    // Emit Template
    emit_indent(out, indent + 2);
    fputs("(Template", out);
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *arg = ci->pattern_args[i];
        if (arg)
        {
            fprintf(out, " %s", arg);
        }
        else
        {
            fprintf(out, " ???"); // fallback for debugging
        }
    }
    fputs(")\n", out);

    // Emit Cases
    for (size_t i = 0; i < ci->case_count; ++i)
    {
        ConditionalCase *c = &ci->cases[i];
        if (!c->pattern || !c->result)
            continue;

        emit_indent(out, indent + 2);
        fprintf(out, "(Case %s %s)\n", c->pattern, c->result);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_definition(FILE *out, Definition *def, int indent)
{
    if (!def || !def->name)
        return;

    LOG_INFO("🧪 Emitting definition: %s", def->name);
    fprintf(out, ";; Definition parsed from %s\n", def->origin_sexpr_path ? def->origin_sexpr_path : "(unknown)");

    emit_indent(out, indent);
    fputs("(Definition\n", out);

    // Emit definition name
    emit_indent(out, indent + 2);
    fputs("(Name ", out);
    emit_atom(out, def->name);
    fputs(")\n", out);

    // Inputs
    emit_indent(out, indent + 2);
    fputs(";; Inputs (from invocation → used inside definition)\n", out);
    emit_input_signals(out, def->input_signals, indent + 2, "input");

    // Outputs
    emit_indent(out, indent + 2);
    fputs(";; Outputs (from definition → back to invocation)\n", out);
    emit_output_signals(out, def->output_signals, indent + 2, "output");

    // Body
    emit_indent(out, indent + 2);
    fputs("(Body\n", out);

    for (BodyItem *item = def->body; item; item = item->next)
    {
        switch (item->type)
        {
        case BODY_INVOCATION:
            emit_invocation(out, item->data.invocation, indent + 4);
            break;

        case BODY_SIGNAL_INPUT:
            emit_indent(out, indent + 4);
            fprintf(out, "(InputSignal %s)\n", item->data.signal_name);
            break;

        case BODY_SIGNAL_OUTPUT:
            emit_indent(out, indent + 4);
            fprintf(out, "(OutputSignal %s)\n", item->data.signal_name);
            break;

        default:
            emit_indent(out, indent + 4);
            fprintf(out, ";; ⚠️ Unknown BodyItemType: %d\n", item->type);
            break;
        }
    }

    emit_indent(out, indent + 2);
    fputs(")\n", out); // End Body

    emit_indent(out, indent);
    fputs(")\n", out); // End Definition
}

void emit_all_definitions(Block *blk, const char *dirpath)
{
    // Create the output directory if it doesn't exist
    if (mkdir_p(dirpath) != 0 && errno != EEXIST)
    {
        LOG_ERROR("❌ Failed to create output directory: %s", dirpath);
        return;
    }

    for (Definition *def = blk->definitions; def; def = def->next)
    {
        if (!def->name) {
            LOG_WARN("⚠️ Skipping unnamed definition");
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s.def.sexpr", dirpath, def->name);

        FILE *out = fopen(path, "w");
        if (!out)
        {
            LOG_ERROR("❌ Failed to open file for writing: %s", path);
            continue;
        }

        LOG_INFO("📦 Emitting definition: %s", def->name);
        emit_definition(out, def, 0);
        fclose(out);

        LOG_INFO("✅ Wrote definition '%s' to %s", def->name, path);
    }
}



void emit_all_invocations(Block *blk, const char *dirpath)
{
    // Create the output directory if it doesn't exist
    if (mkdir_p(dirpath) != 0 && errno != EEXIST)
    {
        LOG_ERROR("❌ Failed to create output directory: %s", dirpath);
        return;
    }

    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.inv.sexpr", dirpath, inv->target_name);
        FILE *out = fopen(path, "w");
        if (!out)
        {
            LOG_ERROR("❌ Failed to open file for writing: %s", path);
            continue;
        }

        // Emit the invocation
        emit_invocation(out, inv, 0);
        fclose(out);
        LOG_INFO("✅ Wrote invocation '%s' to %s", inv->target_name, path);
    }
}

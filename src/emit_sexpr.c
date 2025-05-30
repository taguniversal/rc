#include "emit_util.h"
#include "emit_sexpr.h"
#include "emit_util.h"
#include "eval.h"
#include "log.h"
#include <stdio.h>
#include <errno.h>

void emit_unit(Unit *unit, const char *out_dir)
{
    if (!unit || !unit->name || !unit->invocation || !unit->definition)
    {
        LOG_WARN("⚠️  Skipping invalid unit during emission.");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.sexp", out_dir, unit->name);
    FILE *f = fopen(path, "w");
    if (!f)
    {
        LOG_ERROR("❌ Failed to open file for writing: %s", path);
        return;
    }

    fprintf(f, "(Unit\n");
    fprintf(f, "  (Name %s)\n", unit->name);

    // 🔁 Use the shared helper — preserves parsed-from and roles
    emit_invocation(f, unit->invocation, 2);

    // Emit Definition
    emit_definition(f, unit->definition, 2);

    fprintf(f, ")\n");
    fclose(f);

    LOG_INFO("📤 Emitted unit to %s", path);
}


void emit_all_units(Block *blk, const char *dir)
{
    LOG_INFO("🌀 Emitting all Units to s-expr: %s", dir);
    for (UnitList *cur = blk->units; cur != NULL; cur = cur->next)
    {
        Unit *unit = cur->unit;
        if (!unit || !unit->name)
        {
            LOG_WARN("⚠️ Skipping null unit or unit with no name");
            continue;
        }

        emit_unit(unit, dir);
    }

    LOG_INFO("✅ Done emitting all Units.");
}

void emit_source_list(FILE *out, SourcePlaceList list, int indent, const char *role_hint)
{
    emit_indent(out, indent);
    fputs("(SourceList\n", out);

    for (size_t i = 0; i < list.count; ++i)
    {
        SourcePlace *src = list.items[i];
        emit_indent(out, indent + 2);
        fputs("(SourcePlace\n", out);

        const char *signame = src->resolved_name ? src->resolved_name : src->name;
        if (signame)
        {
            emit_indent(out, indent + 4);
            fputs("(Name ", out);
            emit_atom(out, signame);
            fputs(")\n", out);

            emit_indent(out, indent + 4);
            fprintf(out, ";; role: %s\n", role_hint ? role_hint : "unknown");
        }

        if (src->content)
        {
            LOG_INFO(
                "✏️ Emitting SourcePlace: name=%s signal=%s",
                signame,
                src->content);

            emit_indent(out, indent + 4);
            fputs("(Signal\n", out);
            emit_indent(out, indent + 6);
            fputs("(Content ", out);
            emit_atom(out, src->content);
            fputs(")\n", out);
            emit_indent(out, indent + 4);
            fputs(")\n", out);
        }
        else
        {
            emit_indent(out, indent + 4);
            fputs(";; ⚠️ Unwired SourcePlace\n", out);
        }

        emit_indent(out, indent + 2);
        fputs(")\n", out);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_destination_list(FILE *out, DestinationPlaceList list,
                           int indent, const char *role_hint)
{
    emit_indent(out, indent);
    fputs("(DestinationList\n", out);

    for (size_t i = 0; i < list.count; ++i)
    {
        DestinationPlace *dst = list.items[i];

        emit_indent(out, indent + 2);
        fputs("(DestinationPlace\n", out);

        const char *signame = dst->resolved_name ? dst->resolved_name : dst->name;
        if (signame)
        {
            emit_indent(out, indent + 4);
            fputs("(Name ", out);
            emit_atom(out, signame);
            fputs(")\n", out);

            // 🔍 Add per-place role annotation
            emit_indent(out, indent + 4);
            fprintf(out, ";; role: %s\n", role_hint ? role_hint : "unknown");
        }

        if (dst->content)
        {
            emit_indent(out, indent + 4);
            fputs("(Signal\n", out);
            emit_indent(out, indent + 6);
            fputs("(Content ", out);
            emit_atom(out, dst->content);
            fputs(")\n", out);
            emit_indent(out, indent + 4);
            fputs(")\n", out);
        }

        emit_indent(out, indent + 2);
        fputs(")\n", out);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_place_of_resolution(FILE *out, Definition *def, int indent)
{
    if (!def || (!def->place_of_resolution_invocations && def->place_of_resolution_sources.count == 0))
        return;

    emit_indent(out, indent);
    fputs("(PlaceOfResolution\n", out);

    // Emit internal invocations
    for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
    {
        emit_invocation(out, inv, indent + 2);
    }

    // Emit resolved output sources from array
    for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
    {
        SourcePlace *sp = def->place_of_resolution_sources.items[i];
        if (!sp)
            continue;

        emit_indent(out, indent + 2);
        fputs("(SourcePlace\n", out);

        const char *signame = sp->resolved_name ? sp->resolved_name : sp->name;
        if (signame)
        {
            emit_indent(out, indent + 4);
            fprintf(out, "(Name %s)\n", signame);
        }

        emit_indent(out, indent + 4);
        fputs("(ContentFrom ConditionalInvocationResult)\n", out); // placeholder

        emit_indent(out, indent + 2);
        fputs(")\n", out);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_invocation(FILE *out, Invocation *inv, int indent)
{
    fprintf(out, ";; Invocation parsed from from %s\n", inv->origin_sexpr_path);
    emit_indent(out, indent);
    fputs("(Invocation\n", out);

    emit_indent(out, indent + 2);
    fputs("(Name ", out);
    emit_atom(out, inv->name);
    fputs(")\n", out);

    // 🚧 Role Hint: Inputs for invocation
    emit_indent(out, indent + 2);
    fputs(";; Inputs (flow into definition)\n", out);
    emit_destination_list(out, inv->boundary_destinations, indent + 2, "input");

    // 🚧 Role Hint: Outputs from invocation
    emit_indent(out, indent + 2);
    fputs(";; Outputs (flow out of definition)\n", out);
    emit_source_list(out, inv->boundary_sources, indent + 2, "output");

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_conditional(FILE *out, ConditionalInvocation *ci, int indent)
{
    emit_indent(out, indent);
    fputs("(ConditionalInvocation\n", out);

    // Emit Resolved Template  emit_indent(out, indent + 2);
    fputs("(Template", out);
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *arg = ci->resolved_template_args[i];
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
    for (ConditionalInvocationCase *c = ci->cases; c; c = c->next)
    {
        emit_indent(out, indent + 2);
        fprintf(out, "(Case %s %s)\n", c->pattern, c->result);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_definition(FILE *out, Definition *def, int indent)
{
    LOG_INFO("🧪 Emitting definition: %s", def->name);
    fprintf(out, ";; Definition parsed from %s\n", def->origin_sexpr_path);

    emit_indent(out, indent);
    fputs("(Definition\n", out);

    // Emit definition name
    emit_indent(out, indent + 2);
    fputs("(Name ", out);
    emit_atom(out, def->name);
    fputs(")\n", out);

    // 🚧 Role Hint: Inputs to the definition
    emit_indent(out, indent + 2);
    fputs(";; Inputs (from invocation → used inside definition)\n", out);
    emit_source_list(out, def->boundary_sources, indent + 2, "input");

    // 🚧 Role Hint: Outputs from the definition
    emit_indent(out, indent + 2);
    fputs(";; Outputs (from definition → back to invocation))\n", out);
    emit_destination_list(out, def->boundary_destinations, indent + 2, "output");

    // Emit PlaceOfResolution if present
    LOG_INFO("🧪 Emitting POR for def: %s", def->name);
    if (!def->place_of_resolution_invocations && def->place_of_resolution_sources.count == 0)
    {
        LOG_WARN("⚠️ No POR data found in def: %s", def->name);
    }
    else
    {
        LOG_INFO("✅ POR present for def: %s", def->name);
    }

    emit_place_of_resolution(out, def, indent + 2);

    // Emit ConditionalInvocation if present
    if (def->conditional_invocation)
    {
        emit_conditional(out, def->conditional_invocation, indent + 2);
    }

    emit_indent(out, indent);
    fputs(")\n", out);
}

void emit_flattened_definition(FILE *out, Definition *def, int indent)
{
    emit_indent(out, indent);
    fputs("(Definition\n", out);

    emit_indent(out, indent + 2);
    fputs("(Name ", out);
    emit_atom(out, def->name);
    fputs(")\n", out);

    emit_source_list(out, def->boundary_sources, indent + 2, "input");
    emit_destination_list(out, def->boundary_destinations, indent + 2, "output");

    if (def->conditional_invocation)
        emit_conditional(out, def->conditional_invocation, indent + 2);

    emit_indent(out, indent);
    fputs(")\n", out);
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
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.def.sexpr", dirpath, def->name);
        FILE *out = fopen(path, "w");
        if (!out)
        {
            LOG_ERROR("❌ Failed to open file for writing: %s", path);
            continue;
        }
        // TODO DEBUG
        for (Definition *def = blk->definitions; def; def = def->next)
        {
            LOG_INFO("👀 Definition in blk: %s", def->name);
            if (def->place_of_resolution_invocations)
            {
                LOG_INFO("🔍 Has POR invocations");
            }
        }
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
        snprintf(path, sizeof(path), "%s/%s.inv.sexpr", dirpath, inv->name);
        FILE *out = fopen(path, "w");
        if (!out)
        {
            LOG_ERROR("❌ Failed to open file for writing: %s", path);
            continue;
        }

        // Emit the invocation
        emit_invocation(out, inv, 0);
        fclose(out);
        LOG_INFO("✅ Wrote invocation '%s' to %s", inv->name, path);
    }
}

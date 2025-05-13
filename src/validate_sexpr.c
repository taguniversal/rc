#include "sexpr_parser.h"
#include "log.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "sexpr_parser.h"
#include "eval.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <sexpr-directory>\n", argv[0]);
        return 1;
    }

    const char *dirpath = argv[1];
    DIR *dir = opendir(dirpath);
    if (!dir) {
        perror("opendir");
        return 1;
    }

    LOG_INFO("🔎 Validating .sexpr files in: %s", dirpath);
    struct dirent *entry;
    int errors = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".sexpr"))
            continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);
        char *contents = load_file(path);
        if (!contents) {
            LOG_ERROR("❌ Could not read %s", path);
            errors++;
            continue;
        }

        SExpr *expr = parse_sexpr(contents);
        free(contents);
        if (!expr || expr->type != S_EXPR_LIST || expr->count == 0) {
            LOG_ERROR("❌ Invalid S-expression in %s", path);
            errors++;
            continue;
        }

        const char *top_tag = expr->list[0]->atom;
        if (strcmp(top_tag, "Definition") != 0) {
            LOG_INFO("ℹ️ Skipping non-Definition: %s", entry->d_name);
            free_sexpr(expr);
            continue;
        }

        SExpr *ci_expr = get_child_by_tag(expr, "ConditionalInvocation");
        if (!ci_expr) {
            LOG_INFO("ℹ️ No ConditionalInvocation in %s", entry->d_name);
            free_sexpr(expr);
            continue;
        }

        ConditionalInvocation *ci = parse_conditional_invocation(ci_expr, entry->d_name);
        if (!ci) {
            LOG_ERROR("❌ Invalid ConditionalInvocation in %s", entry->d_name);
            errors++;
        } else {
            LOG_INFO("✅ Valid ConditionalInvocation in %s", entry->d_name);
        }

        free_sexpr(expr);
    }

    closedir(dir);

    if (errors) {
        LOG_ERROR("❌ Validation failed with %d error(s)", errors);
        return 1;
    } else {
        LOG_INFO("✅ All .sexpr files passed validation.");
        return 0;
    }
}

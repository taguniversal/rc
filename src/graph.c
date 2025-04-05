
#include "sqlite3.h"
#include <stdlib.h>
#include<stdio.h>
#include <unistd.h>
#include <string.h>

void export_dot(sqlite3 *db, const char *filename) {
    FILE *dot = fopen(filename, "w");
    if (!dot) {
        perror("fopen for dot");
        return;
    }

    fprintf(dot, "digraph G {\n");
    fprintf(dot, "  rankdir=LR;\n");
    fprintf(dot, "  node [shape=ellipse, fontname=\"Helvetica\"];\n");

    sqlite3_stmt *stmt;
    const char *query = "SELECT subject, predicate, object FROM triples WHERE predicate = 'rdf:type';";

    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *subj = (const char *)sqlite3_column_text(stmt, 0);
            const char *type = (const char *)sqlite3_column_text(stmt, 2);

            const char *color = strstr(type, "SourcePlace") ? "green" :
                                strstr(type, "DestinationPlace") ? "blue" : "gray";

            fprintf(dot, "  \"%s\" [style=filled, fillcolor=%s];\n", subj, color);
        }
        sqlite3_finalize(stmt);
    }

    // Draw matching edges by name (basic string equality match)
    const char *link_query = "SELECT DISTINCT s1.subject, s2.subject "
                             "FROM triples s1, triples s2 "
                             "WHERE s1.predicate = 'rdf:type' AND s1.object = 'inv:SourcePlace' "
                             "AND s2.predicate = 'rdf:type' AND s2.object = 'inv:DestinationPlace' "
                             "AND s1.subject = s2.subject;";

    if (sqlite3_prepare_v2(db, link_query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *from = (const char *)sqlite3_column_text(stmt, 0);
            const char *to = (const char *)sqlite3_column_text(stmt, 1);
            fprintf(dot, "  \"%s\" -> \"%s\";\n", from, to);
        }
        sqlite3_finalize(stmt);
    }

    fprintf(dot, "}\n");
    fclose(dot);

    printf("✅ DOT file written to %s\n", filename);
}
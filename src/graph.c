
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

   

    fprintf(dot, "}\n");
    fclose(dot);

    printf("✅ DOT file written to %s\n", filename);
}
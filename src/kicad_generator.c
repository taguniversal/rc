#include "s_expression.h"
#include "kicad_generator.h"
#include <stdio.h>

void generate_kicad_output(const char *filename) {
    printf("🔹 Generating KiCad output: %s\n", filename);
    // TODO: Implement the KiCad S-expression generation logic
    FILE *fp = fopen("generated.kicad_pcb", "w");

    SExprNode *pcb = s_expr_new("kicad_pcb");
    s_expr_add_child(pcb, s_expr_new("(version 20211014)"));

    SExprNode *layers = s_expr_new("layers");
    s_expr_add_child(layers, s_expr_new("(0 \"F.Cu\" signal)"));
    s_expr_add_child(layers, s_expr_new("(31 \"B.Cu\" signal)"));
    s_expr_add_child(pcb, layers);

    SExprNode *line = s_expr_new("gr_line");
    s_expr_add_child(line, s_expr_new("(start 0 0)"));
    s_expr_add_child(line, s_expr_new("(end 100 0)"));
    s_expr_add_child(line, s_expr_new("(layer \"Edge.Cuts\")"));
    s_expr_add_child(line, s_expr_new("(width 0.15)"));
    s_expr_add_child(pcb, line);

    s_expr_print(fp, pcb, 0);
    s_expr_free(pcb);

    fclose(fp);
 
}


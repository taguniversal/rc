#include "usd_generator.h"
#include "scene.h"
#include <stdio.h>
#include <stdlib.h>

void generate_usd(SceneGraph *scene, const char *output_path) {
    FILE *file = fopen(output_path, "w");
    if (!file) {
        fprintf(stderr, "[ERROR] Cannot write USD file: %s\n", output_path);
        return;
    }

    fprintf(file, "#usda 1.0\n");
    fprintf(file, "def Xform \"%s\" {\n", scene->root_name);

    for (int i = 0; i < scene->object_count; i++) {
        SceneObject obj = scene->objects[i];
        fprintf(file, "    def Xform \"%s\" {\n", obj.name);
        fprintf(file, "        float3 xformOp:translate = (%f, %f, %f)\n", obj.position.x, obj.position.y, obj.position.z);
        fprintf(file, "        def Mesh {\n");
        fprintf(file, "            float3[] points = %s\n", obj.geometry);
        fprintf(file, "            rel material:binding = </Materials/%s>\n", obj.material);
        fprintf(file, "        }\n");
        fprintf(file, "    }\n");
    }

    fprintf(file, "}\n");
    fclose(file);

    // Run USD CLI tool to validate
    char command[256];
    snprintf(command, sizeof(command), "usdcat %s", output_path);
    system(command);
}

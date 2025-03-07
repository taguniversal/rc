#include "scene.h"
#include <stdio.h>
#include <string.h>

void init_scene(SceneGraph *scene, const char *name) {
    strncpy(scene->root_name, name, sizeof(scene->root_name) - 1);
    scene->root_name[sizeof(scene->root_name) - 1] = '\0';
    scene->object_count = 0;
}

void add_object(SceneGraph *scene, const char *name, const char *geometry, const char *material, Vec3 position) {
    if (scene->object_count >= 128) {
        fprintf(stderr, "[ERROR] Scene is full, cannot add more objects.\n");
        return;
    }

    SceneObject *obj = &scene->objects[scene->object_count++];
    strncpy(obj->name, name, sizeof(obj->name) - 1);
    obj->name[sizeof(obj->name) - 1] = '\0';

    strncpy(obj->geometry, geometry, sizeof(obj->geometry) - 1);
    obj->geometry[sizeof(obj->geometry) - 1] = '\0';

    strncpy(obj->material, material, sizeof(obj->material) - 1);
    obj->material[sizeof(obj->material) - 1] = '\0';

    obj->position = position;
}

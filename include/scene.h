#ifndef SCENE_H
#define SCENE_H

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    char name[64];
    char geometry[256];
    char material[64];
    Vec3 position;
} SceneObject;

typedef struct {
    char root_name[64];
    SceneObject objects[128];
    int object_count;
} SceneGraph;

void init_scene(SceneGraph *scene, const char *name);
void add_object(SceneGraph *scene, const char *name, const char *geometry, const char *material, Vec3 position);

#endif // SCENE_H

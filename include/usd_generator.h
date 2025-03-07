#ifndef USD_GENERATOR_H
#define USD_GENERATOR_H

#include "scene.h"  // ✅ Ensure scene.h is included

void generate_usd(SceneGraph *scene, const char *output_path);

#endif // USD_GENERATOR_H

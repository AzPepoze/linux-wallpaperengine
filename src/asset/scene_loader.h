#ifndef SCENE_LOADER_H
#define SCENE_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

void scene_loader_init(void);
void scene_loader_load(const char* path);
void scene_loader_update(float dt);
void scene_loader_update_viewport(void);
void scene_loader_draw(void);
void scene_loader_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif  // SCENE_LOADER_H

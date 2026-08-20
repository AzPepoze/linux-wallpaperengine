#ifndef WALLPAPER_API_H
#define WALLPAPER_API_H

#include <stdbool.h>

#include "../../libs/linmath.h"

// Basic Vector Types (Mapped to discovered JSON strings)
typedef vec2 vec2_t;
typedef vec3 vec3_t;

typedef enum {
    OBJ_TYPE_IMAGE,
    OBJ_TYPE_PARTICLE,
    OBJ_TYPE_VIDEO,
    OBJ_TYPE_MODEL,
    OBJ_TYPE_TEXT,
    OBJ_TYPE_UNKNOWN
} object_type_t;

typedef enum { ASSET_SOURCE_LOCAL, ASSET_SOURCE_ENGINE, ASSET_SOURCE_NONE } asset_source_t;

// Material definition (.json / .mat)
typedef struct {
    char shader_path[256];
    // Dynamic uniforms and texture bindings
    struct {
        char name[64];
        char texture_path[256];
    } textures[8];
    int num_textures;
} we_material_t;

// Discoverable attributes for each scene object
typedef struct {
    char name[128];
    object_type_t type;
    vec3_t origin;
    vec2_t size;
    vec3_t scale;
    vec3_t angles;
    vec3_t color;
    float alpha;
    bool visible;
    char image_path[256];
    we_material_t material;  // Material linked to this object
    int id;
    vec2_t parallax_amount;
} we_object_t;

// Discoverable effect attributes
typedef struct {
    char file[256];
    char name[128];
    bool visible;
    // Dynamic options will be handled by a hash-map or linked list later
} we_effect_t;

// Global Scene Settings
typedef struct {
    struct {
        vec3_t center;
        vec3_t eye;
        vec3_t up;
    } camera;

    struct {
        vec3_t ambient_color;
        vec3_t clear_color;
        bool bloom;
        bool camera_parallax;
        float camera_parallax_amount;
        bool camera_shake;
        float fov;
    } general;

    we_object_t* objects;
    int num_objects;
} we_scene_t;

#endif  // WALLPAPER_API_H

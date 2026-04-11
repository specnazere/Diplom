//
// Created by A on 05.03.2026.
//

#ifndef GRAPHICS_PIPELINER_H
#define GRAPHICS_PIPELINER_H
#include <functional>
#include <set>


class graphics_pipeliner {
public:
    void static first_time_init();

    void static reload_all_textures();
    void static reload_texture_files();
    void static reload_all_chars();
    void static reload_localization();
    void static reload_fonts();
    void static reload_atlas(const std::set<unsigned int> &textures);

    void reload_atlas();

    void static reload_shaders();

    void static reload_frame();

    void static reload_all();
};



#endif //GRAPHICS_PIPELINER_H

//
// Created by A on 05.03.2026.
//

#include "graphics_pipeliner.h"

#include <set>

#include "renderer.h"
#include "texture_manager.h"

void graphics_pipeliner::first_time_init() {
}

void graphics_pipeliner::reload_all() {
}

void graphics_pipeliner::reload_all_textures() {
}

void graphics_pipeliner::reload_texture_files() {
    // texture_manager::reload_textures();
}

void graphics_pipeliner::reload_all_chars() {
    texture_manager::reload_charset();
}

void graphics_pipeliner::reload_localization() {
}

void graphics_pipeliner::reload_fonts() {
    texture_manager::reload_fonts();
}

void graphics_pipeliner::reload_atlas(const std::set<unsigned int>& texture_handlers) {
    texture_manager::create_atlas(texture_handlers);
}

void graphics_pipeliner::reload_shaders() {
    // renderer::load_shaders();
}

void graphics_pipeliner::reload_frame() {
    // renderer::on_frame_resize();
}

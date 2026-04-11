//
// Created by A on 28.01.2026.
//

#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H



#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

#include "font.h"
#include "texture.h"
#include "texture_atlas.h"

class texture_manager {
public:
    enum preload_textures {
        ohno = 0U
    };

    static unsigned int get_handler(const std::string& relative_path);
    static const std::unique_ptr<texture>& get_texture(const unsigned int &handler);
    static void bind(unsigned int handle);
    static void clear();

    static font *load_font(const std::string &path, const std::string &name, float size);

    // Собирает атлас из набора текстур, возвращает указатель на атлас (владелец — вызывающий код)
    static void create_atlas(std::set<unsigned int> texture_handles);

    static void init();

    static void reload_fonts();

    static void reload_charset(const std::set<char32_t>& charset);
    static void reload_charset();

    static inline texture_atlas* atlas;

private:
    texture_manager() = delete;

    static unsigned int load_texture(const std::string& path);
    static void preload_textures();
    static int get_max_texture_size();

    static void create_atlas();

    static inline std::unordered_map<std::string, unsigned int> path_to_texture_;
    static inline std::unordered_map<unsigned int, std::unique_ptr<texture>> textures_;
    static inline std::set<unsigned int> texture_handlers;
    static inline unsigned int next_texture_handle_ = 0;

    static inline std::unordered_map<std::string, unsigned int> font_name_to_desc_;
    static inline std::unordered_map<unsigned int, std::shared_ptr<font>> fonts_;
    static inline unsigned int next_font_handle_ = 0;

    static inline std::set<char32_t> charset_;
    static std::vector<unsigned int> universal_textures_;
};



#endif //TEXTUREMANAGER_H

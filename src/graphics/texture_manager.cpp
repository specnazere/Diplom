//
// Created by A on 28.01.2026.
//

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION



#include <glad/glad.h>
#include <stb/stb_image.h>
#include <stb/stb_rect_pack.h>
#include <vector>
#include <iostream>

#include "texture_manager.h"

#include <filesystem>
#include <ranges>

#include "font.h"
#include "misc/localisation_manager.h"
#include "stb/stb_truetype.h"
#include "tinyxml2/tinyxml2.h"

std::vector<unsigned int> texture_manager::universal_textures_;

unsigned int texture_manager::get_handler(const std::string& relative_path) {
    auto it = path_to_texture_.find(relative_path);
    if (it != path_to_texture_.end()) return it->second;

    unsigned int handle = load_texture(relative_path);
    if (handle) path_to_texture_[relative_path] = handle;
    return handle;
}

const std::unique_ptr<texture>& texture_manager::get_texture(const unsigned int &handler) {
    return textures_[handler];
}

void texture_manager::clear() {
    path_to_texture_.clear();
    textures_.clear();
    next_texture_handle_ = 0;
}

unsigned int texture_manager::load_texture(const std::string& path) {
    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "Texture load failed: " << path << std::endl;
        return 0;
    }

    unsigned int handle = next_texture_handle_++;
    textures_.emplace(handle, std::make_unique<texture>(width, height, path, data));
    return handle;
}

void texture_manager::preload_textures() {
    universal_textures_.push_back(load_texture("res/textures/placeholders/ohno.png"));
}

int texture_manager::get_max_texture_size() {
    int max_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    return max_size;
}


void texture_manager::create_atlas() {
    create_atlas(texture_handlers);
}

void texture_manager::create_atlas(std::set<unsigned int> texture_handles) {
    texture_handles.insert(universal_textures_.begin(), universal_textures_.end());

    if (texture_handles.empty()) {
        std::cerr << "Texture atlas empty" << std::endl;
        return;
    }

    // 1. Загрузить все текстуры и подготовить данные для упаковки
    std::vector<std::pair<unsigned int, texture*>> textures;
    textures.reserve(texture_handles.size());

    int total_area = 0;
    for (unsigned int handle : texture_handles) {
        auto it = textures_.find(handle);
        if (it == textures_.end() || !it->second) continue;

        texture* texture_ptr = it->second.get();

        textures.push_back(std::pair(it->first, texture_ptr));
        total_area += texture_ptr->width * texture_ptr->height;
    }
    if (textures.empty()) return;

    // 2. Определить размер атласа (степень двойки, не больше GL_MAX_TEXTURE_SIZE)
    int max_size = get_max_texture_size();
    int atlas_size = 1;
    while (atlas_size * atlas_size < total_area * 1.5f && atlas_size < max_size) atlas_size *= 2;
    if (atlas_size > max_size) throw std::runtime_error("Too many textures for atlas");

    // 3. Упаковка через stb_rect_pack
    std::vector<stbrp_node> nodes(1024);
    stbrp_context context;
    stbrp_init_target(&context, atlas_size, atlas_size, nodes.data(), (int)nodes.size());

    std::vector<stbrp_rect> rects(textures.size());
    for (size_t i = 0; i < textures.size(); ++i) {
        rects[i].id = (int)i;
        rects[i].w = (stbrp_coord)textures[i].second->width + 1;
        rects[i].h = (stbrp_coord)textures[i].second->height + 1;
    }

    if (!stbrp_pack_rects(&context, rects.data(), (int)rects.size())) {
        // Не поместилось — попробовать увеличить размер
        while (atlas_size < max_size) {
            atlas_size *= 2;
            stbrp_init_target(&context, atlas_size, atlas_size, nodes.data(), (int)nodes.size());
            if (stbrp_pack_rects(&context, rects.data(), (int)rects.size())) break;
        }
        if (atlas_size > max_size) {
            for (auto& t : textures) stbi_image_free(t.second->data);
            throw std::runtime_error("Too many textures for atlas");
        }
    }

    // 4. Создать буфер атласа и скопировать текстуры
    std::vector<unsigned char> atlas_pixels(atlas_size * atlas_size * 4, 0);
    for (size_t i = 0; i < textures.size(); ++i) {
        if (!rects[i].was_packed) continue;

        int x = rects[i].x;
        int y = rects[i].y;  // stb_rect_pack использует координаты от верхнего левого угла
        int w = textures[i].second->width;
        int h = textures[i].second->height;
        unsigned char* src = textures[i].second->data;

        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                int dst_idx = ((y + row) * atlas_size + (x + col)) * 4;
                int src_idx = (row * w + col) * 4;
                atlas_pixels[dst_idx + 0] = src[src_idx + 0];  // R
                atlas_pixels[dst_idx + 1] = src[src_idx + 1];  // G
                atlas_pixels[dst_idx + 2] = src[src_idx + 2];  // B
                atlas_pixels[dst_idx + 3] = src[src_idx + 3];  // A
            }
        }
    }

    // 5. Загрузить атлас в GPU
    unsigned int atlas_id;
    glGenTextures(1, &atlas_id);
    glBindTexture(GL_TEXTURE_2D, atlas_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_size, atlas_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas_pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 6. Создать объект атласа и заполнить UV-маппинг
    auto* atlas = new texture_atlas(atlas_id, atlas_size, atlas_size);
    for (size_t i = 0; i < textures.size(); ++i) {
        if (!rects[i].was_packed) continue;

        float u0 = (rects[i].x + textures[i].second->width) / (float)atlas_size;
        float v0 = rects[i].y / (float)atlas_size;
        float u1 = rects[i].x / (float)atlas_size;
        float v1 = (rects[i].y + textures[i].second->height) / (float)atlas_size;

        atlas->add_mapping(textures[i].first, u0, v0, u1, v1);
    }

    texture_manager::atlas = atlas;
}

void texture_manager::init() {
    preload_textures();
}

void texture_manager::reload_fonts() {
    std::filesystem::path root_path = ".";
    if (!std::filesystem::exists(root_path)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".xml") {
            const auto& file_path = entry.path();
            tinyxml2::XMLDocument doc;
            if (doc.LoadFile(file_path.string().c_str()) != tinyxml2::XML_SUCCESS) {
                std::cerr << "Failed to load: " << file_path << "\n";
                return;
            }

            auto root = doc.FirstChildElement();
            if (!root) return;

            for (auto* font = root->FirstChildElement("font"); font; font = font->NextSiblingElement("font")) {
                const char* name = font->Attribute("name");
                const char* file = font->Attribute("file");
                const char* size_str = font->Attribute("size");

                if (name && file && size_str) {
                    // Получаем директорию XML файла для относительных путей к шрифтам
                    std::string font_path = (file_path.parent_path() / file).string();
                    load_font(font_path, name, std::stof(size_str));
                }
            }
        }
    }
}

void texture_manager::reload_charset(const std::set<char32_t> &charset) {
    charset_ = charset;

    reload_charset();
}

void texture_manager::reload_charset() {
    for (auto font: fonts_ | std::views::values) {
        for (auto texture_handler : font->get_handlers()) {
            textures_.erase(texture_handler);
        }
    }
    fonts_.clear();
    font_name_to_desc_.clear();

    reload_fonts();
}

font *texture_manager::load_font(const std::string &path, const std::string& name, float size) {

    // 1. Загрузить TTF в память
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        std::cerr << "Failed to open font file: " << path << std::endl;
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    unsigned long long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* ttf_buffer = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * len));
    fread(ttf_buffer, len, 1, f);
    fclose(f);

    // 2. Инициализировать шрифт
    stbtt_fontinfo font_info;
    if (!stbtt_InitFont(&font_info, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0))) {
        delete[] ttf_buffer;
        std::cerr << "Failed to read font by path: " << path << std::endl;
        return nullptr;
    }

    // 3. Масштаб под заданный размер
    float scale = stbtt_ScaleForPixelHeight(&font_info, size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);

    // 4. Сгенерировать глифы для ASCII

    fonts_.emplace(next_font_handle_, std::make_unique<font>(path, size));
    auto& font_obj = fonts_[next_font_handle_];

    for (const char32_t& c : charset_) {

        font::glyph_info glyph;

        unsigned char* bitmap = stbtt_GetCodepointBitmap(
            &font_info, 0, scale, static_cast<int>(c), &glyph.width, &glyph.height, &glyph.offset_x, &glyph.offset_y
        );

        stbtt_GetCodepointHMetrics(&font_info, static_cast<int>(c), &glyph.advance, nullptr);
        glyph.advance *= scale;

        if (!bitmap || glyph.width == 0 || glyph.height == 0) {
            if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
            std::cerr << "Failed to generate glyph for char with code " << static_cast<int>(c) << std::endl;
            continue;
        }

        for (const char32_t& c2 : charset_) {
            glyph.kern[c2] = stbtt_GetCodepointKernAdvance(&font_info, static_cast<int>(c2), static_cast<int>(c)) * scale;
        }

        // Создать текстуру из битмапа (альфа-канал в RGBA)
        unsigned char* rgba = new unsigned char[glyph.width * glyph.height * 4];
        for (int i = 0; i < glyph.width * glyph.height; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = bitmap[i];
        }

        unsigned int curr_texture_handle = next_texture_handle_++;
        textures_.emplace(curr_texture_handle, std::make_unique<texture>(glyph.width, glyph.height, std::string(path + "_") + std::to_string(static_cast<int>(c)), rgba));

        stbtt_FreeBitmap(bitmap, nullptr);

        font_obj->add_glyph(c, glyph, curr_texture_handle);
    }

    free(ttf_buffer);
    font_name_to_desc_[name] = next_font_handle_++;

    return font_obj.get();
}

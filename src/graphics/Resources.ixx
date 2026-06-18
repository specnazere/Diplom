//
// Created by A on 14.04.2026.
//

module;

#include "glad/glad.h"
#include "glm/glm.hpp"

#include <codecvt>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <stb/stb_image.h>
#include <stb/stb_truetype.h>
#include <stb/stb_rect_pack.h>

#include "uv_rect.h"

export module resources;

export import multitype_index_repository;

export import texture;
export import locale;
export import font;

import atlas;

namespace
{
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv_utf8_utf32;

    std::u32string utf8_to_codepoints(const std::string& utf8) {

        return conv_utf8_utf32.from_bytes(utf8);
    }
}

export
class Resources
{
private:
public:
    repository<texture> texturesRepo;
    repository<Locale>  localesRepo;
    repository<Font>    fontsRepo;

    size_t load_texture_file(const std::string& sDesc, const std::string& colorPath, const std::string& normalePath = "none")
    {
        int width, height, channels;
        unsigned char* colorData = stbi_load(colorPath.c_str(), &width, &height, &channels, 4);
        unsigned char* normaleData = nullptr;
        if (!colorData) {
            throw std::runtime_error("Texture load failed: " + colorPath);
        }

        if (normalePath != "none") {
            normaleData = stbi_load(normalePath.c_str(), &width, &height, &channels, 4);
            if (!normaleData) {
                throw std::runtime_error("Texture load failed: " + normalePath);
            }
        }
        return texturesRepo.add(sDesc, texture(width, height, colorData, normaleData));
    }

    void load_locale_file(const std::string& path)
    {
        std::fstream file(path, std::ios::in | std::ios::binary);

        if (!file.is_open()) {
            std::cerr << "Locale load failed: " << path << std::endl;
            return;
        }

        std::string line;

        std::getline(file, line);
        if (line[line.size() - 1] == '\r') line.pop_back();
        if (line[0] != '|') {
            std::cerr << "First line in localisation .csv files must be languages: " << path << std::endl;
            std::cerr << "Style: \"|<language1>[|<language2>]...\"" << path << std::endl;
            return;
        }
        std::vector<int> separators;
        auto load_separators = [&](const std::string& separated_line){
            separators.clear();
            for (int pos = 0; pos < separated_line.size(); pos++) {
                if (separated_line[pos] == '|') {
                    separators.push_back(pos);
                }
            }
            separators.push_back(line.size());
        };
        load_separators(line);

        std::vector<std::string> locales;
        locales.reserve(separators.size());
        for (int i = 0; i < separators.size() - 1; i++) {
            locales.emplace_back(line.substr(separators[i] + 1, separators[i + 1] - 1));
        }

        for (std::string& localeDesc : locales) {
            if (!localesRepo.contains(localeDesc)) {
                localesRepo.add(localeDesc, Locale());
            }
        }

        while (std::getline(file, line)) {
            load_separators(line);
            std::string descriptor = line.substr(0, separators[0]);

            for (int i = 0; i < separators.size() - 1; i++) {
                localesRepo[locales[i]].object.add(descriptor, utf8_to_codepoints(line.substr(separators[i] + 1, separators[i + 1] - separators[i] - 1)));
            }
        }
    }

    void load_font_file(const std::string &path, const std::string &sDesc) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            std::cerr << "Failed to open font file: " << path << std::endl;
            return;
        }

        fseek(f, 0, SEEK_END);
        unsigned long long len = static_cast<unsigned long long>(ftell(f));
        fseek(f, 0, SEEK_SET);

        unsigned char* ttf_buffer = static_cast<unsigned char *>(malloc(len));
        if (!ttf_buffer) {
            fclose(f);
            return;
        }

        fread(ttf_buffer, 1, len, f);
        fclose(f);

        stbtt_fontinfo font_info;
        if (!stbtt_InitFont(&font_info, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0))) {
            free(ttf_buffer);
            std::cerr << "Failed to init font: " << path << std::endl;
            return;
        }

        fontsRepo.add(sDesc, Font(path));
        auto& font_obj = fontsRepo[sDesc].object;

        font_obj.ttf_data = ttf_buffer;
        font_obj.ttf_size = len;
        font_obj.font_info = font_info;
    }

    void init_font_with_charset(const std::set<char32_t>& charset, const int& size, const std::string& sDesc)
    {
        init_font_by_ref(charset, size, fontsRepo[sDesc].object);
    }
    void init_font_with_charset(const std::set<char32_t>& charset, const int& size, const unsigned int& iDesc)
    {
        init_font_by_ref(charset, size, fontsRepo[iDesc].object);
    }

private:
    void init_font_by_ref(const std::set<char32_t>& charset, const int& size, Font& fontRef) {
        fontRef.bake_glyphs(charset, texturesRepo, size);
    }

public:
    Atlas* create_atlas()
    {
        std::set<unsigned int> keysSet;
        keysSet.insert_range(texturesRepo.iDescs());
        return create_atlas(keysSet);
    }

    Atlas* create_atlas(std::set<unsigned int> textureDescs)
    {
        if (textureDescs.empty()) {
            std::cerr << "Texture atlas empty" << std::endl;
            return nullptr;
        }

        std::vector<std::pair<unsigned int, texture*>> textures;
        textures.reserve(textureDescs.size());

        int total_area = 0;
        for (unsigned int desc : textureDescs) {
            if (!texturesRepo.contains(desc)) {
                std::cerr << "No texture " << desc << " in repo" << std::endl;
                continue;
            }

            texture* texture_ptr = &texturesRepo[desc].object;

            textures.push_back(std::pair(desc, texture_ptr));
            total_area += texture_ptr->width * texture_ptr->height;
        }
        if (textures.empty()) {
            std::cerr << "Atlas empty" << std::endl;
            return nullptr;
        }

        int max_size = GL_MAX_TEXTURE_SIZE;
        int atlas_size = 1;
        while (atlas_size * atlas_size < total_area * 1.5f && atlas_size < max_size) atlas_size *= 2;
        if (atlas_size > max_size)
            throw std::runtime_error("Too many textures for atlas");

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
            while (atlas_size < max_size) {
                atlas_size *= 1.5;
                stbrp_init_target(&context, atlas_size, atlas_size, nodes.data(), (int)nodes.size());
                if (stbrp_pack_rects(&context, rects.data(), (int)rects.size())) break;
            }
            if (atlas_size > max_size) {
                for (auto& t : textures) stbi_image_free(t.second->colorData);
                throw std::runtime_error("Too many textures for atlas");
            }
        }

        std::vector<unsigned char> atlas_pixels(atlas_size * atlas_size * 4, 0);
        std::vector<unsigned char> atlas_normale_pixels(atlas_size * atlas_size * 4, 0);
        for (size_t i = 0; i < textures.size(); ++i) {
            if (!rects[i].was_packed) continue;

            int x = rects[i].x;
            int y = rects[i].y;  // stb_rect_pack использует координаты от верхнего левого угла
            int w = textures[i].second->width;
            int h = textures[i].second->height;
            unsigned char* color_src = textures[i].second->colorData;
            unsigned char* normale_src = textures[i].second->normaleData;

            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w; ++col) {
                    int src_row = h - 1 - row;
                    int dst_idx = ((y + row) * atlas_size + (x + col)) * 4;
                    int src_idx = (src_row * w + (w - 1 - col)) * 4;

                    atlas_pixels[dst_idx + 0] = color_src[src_idx + 0]; // R
                    atlas_pixels[dst_idx + 1] = color_src[src_idx + 1]; // G
                    atlas_pixels[dst_idx + 2] = color_src[src_idx + 2]; // B
                    atlas_pixels[dst_idx + 3] = color_src[src_idx + 3]; // A
                }
            }

            if (!normale_src) continue;

            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w; ++col) {
                    int src_row = h - 1 - row;
                    int dst_idx = ((y + row) * atlas_size + (x + col)) * 4;
                    int src_idx = (src_row * w + (w - 1 - col)) * 4;

                    atlas_normale_pixels[dst_idx + 0] = normale_src[src_idx + 0]; // R
                    atlas_normale_pixels[dst_idx + 1] = normale_src[src_idx + 1]; // G
                    atlas_normale_pixels[dst_idx + 2] = normale_src[src_idx + 2]; // B
                    atlas_normale_pixels[dst_idx + 3] = normale_src[src_idx + 3]; // A
                }
            }
        }

        unsigned int atlas_id;
        glGenTextures(1, &atlas_id);
        glBindTexture(GL_TEXTURE_2D, atlas_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_size, atlas_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas_pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        unsigned int normale_atlas_id;
        glGenTextures(1, &normale_atlas_id);
        glBindTexture(GL_TEXTURE_2D, normale_atlas_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_size, atlas_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas_normale_pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        auto atlas = new Atlas(atlas_id, normale_atlas_id, atlas_size, atlas_size);
        for (size_t i = 0; i < textures.size(); ++i) {
            if (!rects[i].was_packed) continue;

            float u0 = (rects[i].x + textures[i].second->width) / (float)atlas_size;
            float v0 = (rects[i].y + textures[i].second->height) / (float)atlas_size;
            float u1 = rects[i].x / (float)atlas_size;
            float v1 = rects[i].y / (float)atlas_size;

            atlas->add_mapping(textures[i].first, u0, v0, u1, v1);
        }
        return atlas;
    }

    size_t add_solid_color_texture(const std::string& sDesc, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
    {
        unsigned char* data = new unsigned char[4];
        data[0] = r;
        data[1] = g;
        data[2] = b;
        data[3] = a;

        return texturesRepo.add(sDesc, texture(1, 1, data, nullptr));
    }
};
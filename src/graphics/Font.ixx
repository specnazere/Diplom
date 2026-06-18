//
// Created by A on 05.02.2026.
//

module;

#include "uv_rect.h"

#include <stb/stb_truetype.h>

#include <iostream>
#include <unordered_map>
#include <cstdlib> // for free
#include <set>
#include <string>
#include <vector>
#include <iomanip>
#include <ostream>

export module font;

export import atlas;
export import texture;
export import multitype_index_repository;

export
class Font {
public:
    struct glyph_info {
        int width = 0;
        int height = 0;
        int offset_x = 0;
        int offset_y = 0;
        int advance = 0;
        char32_t ch = 0;

        std::unordered_map<char32_t, int> kern;
        size_t textureIDesc;
    };

private:
    int max_glyph_height = 0;
    std::unordered_map<char32_t, glyph_info> glyphs_;

public:
    const std::string name;
    int size{};

    unsigned char* ttf_data = nullptr;
    unsigned long long ttf_size{};
    stbtt_fontinfo font_info{};

    Font(const std::string& name) :
        name(name)
    {
        glyphs_.reserve(64);
    }

    ~Font() {
        if (ttf_data) {
            free(ttf_data);
            ttf_data = nullptr;
        }
    }

    bool has_glyph(char32_t ch) const {
        return glyphs_.find(ch) != glyphs_.end();
    }

    [[nodiscard]] const glyph_info& get_glyph(char32_t ch) const {
        auto it = glyphs_.find(ch);
        if (it != glyphs_.end()) {
            return it->second;
        }

        static glyph_info empty_glyph;
        return empty_glyph;
    }

    const int& get_size() const {
        return max_glyph_height;
    }

    static std::string get_codepoint_str(char32_t cp) {
        std::ostringstream oss;
        oss << "U+" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << static_cast<uint32_t>(cp);
        return oss.str();
    }

    void bake_glyphs(const std::set<char32_t>& charset, repository<texture>& texturesRepo, int bake_size)
    {
        size = bake_size;

        if (!ttf_data) {
            std::cerr << "Invalid font object or data not loaded." << std::endl;
            return;
        }

        float scale = stbtt_ScaleForPixelHeight(&font_info, bake_size);
        stbtt_fontinfo* info = &font_info;

        for (const char32_t& c : charset) {
            if (has_glyph(c)) continue;

            int width = 0, height = 0, xoff = 0, yoff = 0;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(
                info, 0, scale, static_cast<int>(c),
                &width, &height, &xoff, &yoff
            );

            int adv = 0;
            stbtt_GetCodepointHMetrics(info, static_cast<int>(c), &adv, nullptr);
            int advance = static_cast<int>(adv * scale);

            if (!bitmap || width == 0 || height == 0) {
                if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);

                glyph_info final_glyph;
                final_glyph.width   = 0;
                final_glyph.height  = 0;
                final_glyph.offset_x = xoff;
                final_glyph.offset_y = yoff;
                final_glyph.advance = advance;
                final_glyph.ch      = c;
                final_glyph.textureIDesc = 0;

                for (const char32_t& c2 : charset)
                    final_glyph.kern[c2] = stbtt_GetCodepointKernAdvance(info, static_cast<int>(c2), static_cast<int>(c)) * scale;

                glyphs_[c] = final_glyph;
                continue;
            }

            unsigned char* tex_buffer = new unsigned char[width * height * 4]();

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int src_idx = y * width + x;
                    int dst_idx = src_idx * 4;
                    unsigned char alpha = bitmap[src_idx];

                    tex_buffer[dst_idx + 0] = 255;   // R
                    tex_buffer[dst_idx + 1] = 255;   // G
                    tex_buffer[dst_idx + 2] = 255;   // B
                    tex_buffer[dst_idx + 3] = alpha; // A
                }
            }

            stbtt_FreeBitmap(bitmap, nullptr);

            texture* glyph_texture = new texture(width, height, tex_buffer, nullptr);
            std::string tex_name   = name + "_atlas_" + get_codepoint_str(c);

            max_glyph_height = max_glyph_height > height ? max_glyph_height : height;

            glyph_info final_glyph;

            final_glyph.width    = width;
            final_glyph.height   = height;
            final_glyph.offset_x = xoff;
            final_glyph.offset_y = yoff;
            final_glyph.advance  = advance;
            final_glyph.ch       = c;

            for (const char32_t& c2 : charset)
                final_glyph.kern[c2] = stbtt_GetCodepointKernAdvance(info, static_cast<int>(c2), static_cast<int>(c)) * scale;

            final_glyph.textureIDesc = texturesRepo.add(tex_name, *glyph_texture);

            glyphs_[c] = final_glyph;
        }
        return;
    }
};

//
// Created by A on 28.01.2026.
//

module;

export module texture;

export
struct texture {
    int width;
    int height;
    unsigned char *colorData;;
    unsigned char *normaleData;

    texture(int width, int height, unsigned char *colorData, unsigned char *normaleData)
    : width(width), height(height), colorData(colorData), normaleData(normaleData) {}
    ~texture() {
        //if (id) glDeleteTextures(1, &id);
    }
};
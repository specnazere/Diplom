//
// Created by A on 28.01.2026.
//

module;

export module texture;

export
struct texture {
    int width;
    int height;
    unsigned char *data;

    texture(int width, int height, unsigned char *data)
    : width(width), height(height), data(data) {}
    ~texture() {
        //if (id) glDeleteTextures(1, &id);
    }
};
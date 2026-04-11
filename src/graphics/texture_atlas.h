//
// Created by A on 29.01.2026.
//

#ifndef ATLAS_H
#define ATLAS_H



#include <unordered_map>
#include <glm/fwd.hpp>

class texture_atlas {
public:
    struct uv_rect {
        glm::float32 u0, v0;  // top-left
        glm::float32 u1, v1;  // bottom-right
    };

    texture_atlas(const unsigned int &atlas_id, const int &width, const int &height);
    ~texture_atlas();

    unsigned int id() const { return atlas_id_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Получить UV для исходной текстуры по её хендлу
    const uv_rect& get_uv(const unsigned int &texture_handle) const;

    // Добавить маппинг хендл → UV (вызывается только из TextureManager)
    void add_mapping(unsigned int &texture_handle, const float &u0, const float &v0, const float &u1, const float &v1);

    void bind() const;

private:
    unsigned int atlas_id_;
    int width_;
    int height_;
    std::unordered_map<unsigned int, uv_rect> uv_map_;
};



#endif //ATLAS_H

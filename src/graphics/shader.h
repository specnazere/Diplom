//
// Created by A on 01.02.2026.
//

#ifndef SHADER_H
#define SHADER_H



#include <string>
#include <glm/fwd.hpp>

class shader {
public:
    explicit shader(const std::string& path);
    ~shader();

    void use() const;

    [[nodiscard]] int get_uniform_location(const std::string& name) const;
    [[nodiscard]] unsigned int id() const { return program_id_; }

    void set_uniform(const std::string& name, int value) const;
    void set_uniform(const std::string& name, float value) const;
    void set_uniform(const std::string& name, bool value) const;
    void set_uniform(const std::string& name, const glm::vec2& value) const;
    void set_uniform(const std::string& name, const glm::vec3& value) const;
    void set_uniform(const std::string& name, const glm::vec4& value) const;
    void set_uniform(const std::string& name, const glm::mat4& value) const;

private:
    mutable int cached_locations_[64] = {};  // кэш локаций для ускорения
    mutable std::string cached_names_[64];
    mutable size_t cache_size_ = 0;

    int get_cached_location(const std::string& name) const;

    unsigned int program_id_;
    bool compile_shader(unsigned int type, const std::string& source) const;
    static std::string read_file(const std::string& path);
};



#endif //SHADER_H

//
// Created by A on 01.02.2026.
//

module;

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

export module shader;

export
class Shader {
public:
    explicit Shader(const std::string& path) {
        std::string vert_source = read_file(path + ".vert");
        std::string frag_source = read_file(path + ".frag");

        program_id_ = glCreateProgram();

        if (!compile_shader(GL_VERTEX_SHADER, vert_source)) {
            throw std::runtime_error("Vertex shader compilation failed: " + path);
        }
        if (!compile_shader(GL_FRAGMENT_SHADER, frag_source)) {
            throw std::runtime_error("Fragment shader compilation failed: " + path);
        }

        glLinkProgram(program_id_);
        GLint linked;
        glGetProgramiv(program_id_, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLchar log[1024];
            glGetProgramInfoLog(program_id_, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("Shader linking failed: ") + log);
        }
    }
    ~Shader() {
        if (program_id_) glDeleteProgram(program_id_);
    }

    void use() const {
        glUseProgram(program_id_);
    }


    [[nodiscard]] int get_uniform_location(const std::string& name) const {
        return glGetUniformLocation(program_id_, name.c_str());
    }
    [[nodiscard]] unsigned int id() const { return program_id_; }

    void set_uniform(const std::string& name, int value) const {
        int loc = get_cached_location(name);
        if (loc >= 0) glUniform1i(loc, value);
    }
    void set_uniform(const std::string& name, float value) const {
        int loc = get_cached_location(name);
        if (loc >= 0) glUniform1f(loc, value);
    }
    void set_uniform(const std::string& name, bool value) const {
        set_uniform(name, static_cast<int>(value));
    }
    void set_uniform(const std::string& name, const glm::vec2& value) const {
        int loc = get_cached_location(name);
        if (loc >= 0) glUniform2fv(loc, 1, glm::value_ptr(value));
    }
    void set_uniform(const std::string& name, const glm::vec3& value) const {
        int loc = get_cached_location(name);
        if (loc >= 0) glUniform3fv(loc, 1, glm::value_ptr(value));
    }
    void set_uniform(const std::string& name, const glm::vec4& value) const {
        int loc = get_cached_location(name);
        if (loc >= 0) glUniform4fv(loc, 1, glm::value_ptr(value));
    }
    void set_uniform(const std::string& name, const glm::mat4& value) const {
        int loc = get_cached_location(name);
        if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
    }

private:
    mutable int cached_locations_[64] = {};  // кэш локаций для ускорения
    mutable std::string cached_names_[64];
    mutable size_t cache_size_ = 0;

    int get_cached_location(const std::string& name) const {
        // Линейный поиск в кэше (достаточно для <64 юниформов)
        for (size_t i = 0; i < cache_size_; ++i) {
            if (cached_names_[i] == name) {
                return cached_locations_[i];
            }
        }

        int loc = glGetUniformLocation(program_id_, name.c_str());
        if (loc >= 0 && cache_size_ < 64) {
            cached_names_[cache_size_] = name;
            cached_locations_[cache_size_] = loc;
            ++cache_size_;
        }
        return loc;
    }

    unsigned int program_id_;
    bool compile_shader(unsigned int type, const std::string& source) const {
        const unsigned int shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint compiled;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLchar log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "Shader compilation error:\n" << log << std::endl;
            glDeleteShader(shader);
            return false;
        }

        glAttachShader(program_id_, shader);
        glDeleteShader(shader);
        return true;
    }
    static std::string read_file(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + path);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};
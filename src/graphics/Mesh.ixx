//
// Created by A on 21.04.2026.
//

module;

#include "glad/glad.h"
#include "glm/glm.hpp"

#include <vector>
#include <cassert>
#include <filesystem>

export module mesh;

// 2D-вершина меша: позиция на плоскости + UV-координаты
export
struct Vertex2D
{
    glm::vec2 position{};
    glm::vec2 uv{};
};

export
class Mesh
{
public:
    // CPU-копия геометрии — доступна для прямого редактирования извне.
    // После изменений вызови upload() чтобы отправить данные на GPU.
    std::vector<Vertex2D>  vertices;
    std::vector<glm::uint> indices;

    // Глубина, применяется ко всему мешу как Z в шейдере.
    float depth = 0.0f;

    // Создаёт меш с начальной геометрией (опционально).
    explicit Mesh(std::vector<Vertex2D>  verts  = {},
                  std::vector<glm::uint> inds   = {},
                  float                  depth_ = 0.0f)
        : vertices(std::move(verts))
        , indices(std::move(inds))
        , depth(depth_)
    {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        // Настраиваем атрибуты один раз.
        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        // location 0: position (vec2)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
            reinterpret_cast<void*>(offsetof(Vertex2D, position)));
        glEnableVertexAttribArray(0);

        // location 1: uv (vec2)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
            reinterpret_cast<void*>(offsetof(Vertex2D, uv)));
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);

        glBindVertexArray(0);

        // Если начальные данные переданы — сразу грузим на GPU.
        if (!vertices.empty()) upload();
    }

    ~Mesh()
    {
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (ebo_) glDeleteBuffers(1, &ebo_);
    }

    Mesh(const Mesh& mesh)
    {
        *this = Mesh(mesh.vertices, mesh.indices, mesh.depth);
    }

    Mesh(Mesh&& o) noexcept
        : vertices(std::move(o.vertices))
        , indices(std::move(o.indices))
        , depth(o.depth)
        , vao_(o.vao_), vbo_(o.vbo_), ebo_(o.ebo_)
        , index_count_(o.index_count_)
        , dirty_(o.dirty_)
    {
        o.vao_ = o.vbo_ = o.ebo_ = 0;
    }

    Mesh& operator=(Mesh&& o) noexcept
    {
        if (this == &o) return *this;
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (ebo_) glDeleteBuffers(1, &ebo_);

        vertices      = std::move(o.vertices);
        indices       = std::move(o.indices);
        depth         = o.depth;
        vao_          = o.vao_;
        vbo_          = o.vbo_;
        ebo_          = o.ebo_;
        index_count_  = o.index_count_;
        dirty_        = o.dirty_;

        o.vao_ = o.vbo_ = o.ebo_ = 0;
        return *this;
    }

    // Полная замена геометрии + немедленная загрузка на GPU.
    void set(std::vector<Vertex2D> verts, std::vector<glm::uint> inds)
    {
        vertices = std::move(verts);
        indices  = std::move(inds);
        upload();
    }

    // Помечает данные как изменённые. Renderer вызовет upload() перед рендером.
    void mark_dirty() { dirty_ = true; }

    bool is_dirty() const { return dirty_; }

    // Отправляет CPU-данные на GPU (GL_DYNAMIC_DRAW — данные меняются часто).
    // Вызывается вручную или автоматически через Renderer::renderMesh().
    void upload()
    {
        assert(vao_ && "Mesh not initialized");

        index_count_ = static_cast<GLsizei>(indices.size());

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex2D)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(glm::uint)),
                     indices.data(),
                     GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        dirty_ = false;
    }

    // Обновляет диапазон вершин без полной перезаливки буфера.
    // offset и count — в единицах Vertex2D, не байтах.
    void upload_partial(GLintptr offset_verts, GLsizeiptr count_verts)
    {
        assert(offset_verts + count_verts <= static_cast<GLsizeiptr>(vertices.size()));
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER,
                        offset_verts * sizeof(Vertex2D),
                        count_verts  * sizeof(Vertex2D),
                        vertices.data() + offset_verts);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    GLuint  vao()         const { return vao_; }
    GLsizei index_count() const { return index_count_; }

private:
    GLuint  vao_         = 0;
    GLuint  vbo_         = 0;
    GLuint  ebo_         = 0;
    GLsizei index_count_ = 0;
    bool    dirty_       = false;
};

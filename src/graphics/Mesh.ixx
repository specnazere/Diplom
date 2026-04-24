//
// Created by A on 21.04.2026.
//

module;

#include "glad/glad.h"
#include "glm/glm.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include <vector>
#include <cassert>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

export module mesh;

#define TAU 6.2831853

export
namespace MeshUtils
{
    // Проверка: находится ли точка p внутри треугольника v0, v1, v2
    bool point_in_triangle(const glm::vec2& p, const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2)
    {
        // Используем барицентрические координаты или знаковые площади
        auto sign = [](const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3) -> float {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        };

        float d1 = sign(p, v0, v1);
        float d2 = sign(p, v1, v2);
        float d3 = sign(p, v2, v0);

        bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

        // Если знаки разные, точка снаружи. Если все >=0 или все <=0 - внутри.
        // Для строгости можно добавить epsilon, но для сетки обычно хватает strict check
        return !(has_neg && has_pos);
    }

    // Расстояние от точки до отрезка (для более точного определения пересечения с радиусом)
    float dist_point_to_segment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
    {
        glm::vec2 pa = p - a;
        glm::vec2 ba = b - a;
        float h = std::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
        return glm::length(pa - ba * h);
    }

    float perpendicular(const glm::vec2& A, const glm::vec2& B, const glm::vec2& P)
    {
        // Вектор отрезка
        glm::vec2 AB = B - A;
        // Вектор от начала отрезка до точки
        glm::vec2 AP = P - A;

        // Длина отрезка
        float lenAB = glm::length(AB);

        // Защита от вырожденного отрезка (точки A и B совпадают)
        if (lenAB < 1e-6f) {
            return glm::length(AP);
        }

        // 2D "векторное произведение" (косое произведение): x1*y2 - x2*y1
        // Это площадь параллелограмма, построенного на векторах AB и AP
        float cross = AB.x * AP.y - AB.y * AP.x;

        // Расстояние = Площадь / Основание
        return std::abs(cross) / lenAB;
    }

    float dist_point_to_clamped_segment(const glm::vec2& A, const glm::vec2& B, const glm::vec2& P)
    {
        glm::vec2 AB = B - A;
        glm::vec2 AP = P - A;

        float len_sq_AB = glm::dot(AB, AB);

        // Защита от вырожденного отрезка (точки совпадают)
        if (len_sq_AB < 1e-6f) {
            return glm::length(AP);
        }

        // Параметр проекции t показывает, где находится проекция P на прямую AB.
        // t = dot(AP, AB) / |AB|^2
        // 0 <= t <= 1 означает, что проекция внутри отрезка.
        float t = glm::dot(AP, AB) / len_sq_AB;

        // Clamp t to [0, 1]
        if (t < 0.0f) {
            // Ближайшая точка - A
            return glm::length(AP);
        }
        else if (t > 1.0f) {
            // Ближайшая точка - B
            return glm::length(P - B);
        }
        else {
            // Проекция внутри отрезка.
            // Используем формулу расстояния до прямой через векторное произведение,
            // так как это численно стабильнее и быстрее, чем вычисление точки проекции и длины вектора разности.
            float cross = AB.x * AP.y - AB.y * AP.x;
            return std::abs(cross) / std::sqrt(len_sq_AB);
        }
    }

    glm::vec2 project_point_on_segment(const glm::vec2& A, const glm::vec2& B, const glm::vec2& P) {
        glm::vec2 AB = B - A;
        glm::vec2 AP = P - A;

        float len_sq = glm::dot(AB, AB);

        // Если A и B совпадают, возвращаем A (или B, они равны)
        if (len_sq < 1e-6f) {
            return A;
        }

        // t = dot(AP, AB) / |AB|^2
        float t = glm::dot(AP, AB) / len_sq;

        // Clamp t to [0, 1] to stay on the segment
        t = std::clamp(t, 0.0f, 1.0f);

        return A + t * AB;
    }
}

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

    std::vector<Vertex2D> get_boundary() const
    {
        std::map<std::pair<glm::uint, glm::uint>, int> edges;
        for (glm::uint i = 0; i < indices.size(); i += 3) {
            std::pair<glm::uint, glm::uint> edge;

            edge = std::make_pair(indices[i], indices[i + 1]);
            if (edge.first > edge.second) std::swap(edge.first, edge.second);
            if (edges.contains(edge)) edges.erase(edge);
            else edges.insert({edge, 1});

            edge = std::make_pair(indices[i], indices[i + 2]);
            if (edge.first > edge.second) std::swap(edge.first, edge.second);
            if (edges.contains(edge)) edges.erase(edge);
            else edges.insert({edge, 1});

            edge = std::make_pair(indices[i + 1], indices[i + 2]);
            if (edge.first > edge.second) std::swap(edge.first, edge.second);
            if (edges.contains(edge)) edges.erase(edge);
            else edges.insert({edge, 1});
        }

        std::vector<glm::uint> boundary_inds;
        boundary_inds.emplace_back(edges.begin()->first.first);
        boundary_inds.emplace_back(edges.begin()->first.second);
        edges.erase(edges.begin());

        while (edges.size() > 0) {
            for (auto it = edges.begin(); it != edges.end(); ++it) {
                if (it->first.first == boundary_inds.back()) {
                    boundary_inds.emplace_back(it->first.second);
                    edges.erase(it);
                    break;
                }
                if (it->first.second == boundary_inds.back()) {
                    boundary_inds.emplace_back(it->first.first);
                    edges.erase(it);
                    break;
                }
            }
        }

        std::vector<Vertex2D> boundary;
        for (const glm::uint& i : boundary_inds) {
            boundary.emplace_back(vertices[i]);
        }

        return boundary;
    }

    glm::vec2 get_size() const
    {
        if (vertices.empty()) return glm::vec2(0.0f);

        float min_x = vertices[0].position.x;
        float max_x = vertices[0].position.x;
        float min_y = vertices[0].position.y;
        float max_y = vertices[0].position.y;

        for (const auto& v : vertices)
        {
            min_x = std::min(min_x, v.position.x);
            max_x = std::max(max_x, v.position.x);
            min_y = std::min(min_y, v.position.y);
            max_y = std::max(max_y, v.position.y);
        }

        return glm::vec2(max_x - min_x, max_y - min_y);
    }

    bool contains(const glm::vec2& P) const
    {
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const auto& v0 = vertices[indices[i]].position;
            const auto& v1 = vertices[indices[i + 1]].position;
            const auto& v2 = vertices[indices[i + 2]].position;

            if (MeshUtils::point_in_triangle(P, v0, v1, v2))
            {
                return true;
            }
        }
        return false;
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
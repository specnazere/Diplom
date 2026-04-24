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

    GLuint  vao()         const { return vao_; }
    GLsizei index_count() const { return index_count_; }

    void subdivide_in_radius(const glm::vec2& center, float radius)
    {
        constexpr float radius_multipler_for_clamp = 1;
        // ===== Helpers =====
        auto point_in_circle = [&](const glm::vec2& p) -> bool {
            return glm::distance2(p, center) <= radius * radius * radius_multipler_for_clamp;
        };

        auto point_in_triangle = [&](const glm::vec2& point, const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2) -> bool
        {
            auto sign = [](const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3) -> float {
                return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
            };

            float d1 = sign(point, v0, v1);
            float d2 = sign(point, v1, v2);
            float d3 = sign(point, v2, v0);

            bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

            // Если знаки смешанные, точка снаружи. Если все одного знака (или ноль) — внутри.
            return !(hasNeg && hasPos);
        };

        auto segment_intersects_circle = [&](const glm::vec2& a, const glm::vec2& b) -> bool {
            glm::vec2 ab = b - a;
            float ab2 = glm::dot(ab, ab);
            if (ab2 < 1e-8f) return point_in_circle(a);
            glm::vec2 ac = center - a;
            float t = glm::clamp(glm::dot(ac, ab) / ab2, 0.0f, 1.0f);
            glm::vec2 closest = a + ab * t;
            return glm::distance2(closest, center) <= radius * radius * radius_multipler_for_clamp;
        };

        auto perpendicular = [&](const glm::vec2& A, const glm::vec2& B, const glm::vec2& P) -> float
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
        };

        auto getAngle = [&](const glm::vec2& A, const glm::vec2& B, const glm::vec2& C) {
            // Вектора из вершины B к A и C
            glm::vec2 BA = A - B;
            glm::vec2 BC = C - B;

            // Скалярное произведение: |BA| * |BC| * cos(theta)
            float dotProduct = glm::dot(BA, BC);

            // Длины векторов
            float lenBA = glm::length(BA);
            float lenBC = glm::length(BC);

            // Защита от деления на ноль (если точки совпадают)
            if (lenBA < 1e-6f || lenBC < 1e-6f) {
                return 0.0f;
            }

            // cos(theta) = dot / (|BA| * |BC|)
            float cosTheta = dotProduct / (lenBA * lenBC);

            // Ограничиваем диапазон [-1, 1] из-за погрешностей float
            cosTheta = glm::clamp(cosTheta, -1.0f, 1.0f);

            return glm::acos(cosTheta); // Результат в радианах
        };

        // Интерполяция UV через барицентрические координаты
        auto interpolate_uv_barycentric = [](const glm::vec2& p,
                                              const glm::vec2& a, const glm::vec2& b, const glm::vec2& c,
                                              const glm::vec2& uv_a, const glm::vec2& uv_b, const glm::vec2& uv_c) -> glm::vec2 {
            // Вычисляем барицентрические координаты точки p в треугольнике abc
            glm::vec2 v0 = c - a;
            glm::vec2 v1 = b - a;
            glm::vec2 v2 = p - a;

            float dot00 = glm::dot(v0, v0);
            float dot01 = glm::dot(v0, v1);
            float dot02 = glm::dot(v0, v2);
            float dot11 = glm::dot(v1, v1);
            float dot12 = glm::dot(v1, v2);

            float inv_denom = 1.0f / (dot00 * dot11 - dot01 * dot01);
            float u = (dot11 * dot02 - dot01 * dot12) * inv_denom;
            float v = (dot00 * dot12 - dot01 * dot02) * inv_denom;
            float w = 1.0f - u - v;

            // Интерполируем UV
            return w * uv_a + u * uv_c + v * uv_b;
        };

        struct Edge {
            glm::uint a, b;
            Edge(glm::uint x, glm::uint y) : a(glm::min(x,y)), b(glm::max(x,y)) {}
            bool operator==(const Edge& o) const { return a == o.a && b == o.b; }
        };
        struct EdgeHash {
            size_t operator()(const Edge& e) const {
                return std::hash<glm::uint>()(e.a) ^ (std::hash<glm::uint>()(e.b) << 1);
            }
        };

        // ===== Step 1: Mark triangles intersecting the circle =====
        glm::uint tri_count = glm::uint(indices.size() / 3);
        std::vector<bool> remove_tri(tri_count, false);
        std::set<unsigned int> outer_verts_old;
        glm::vec2 center_uv;

        // Сохраняем информацию о треугольниках, которые пересекают круг
        std::vector<std::tuple<glm::uint, glm::uint, glm::uint>> boundary_triangles;

        for (glm::uint t = 0; t < tri_count; ++t) {
            glm::uint i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
            const auto& p0 = vertices[i0].position;
            const auto& p1 = vertices[i1].position;
            const auto& p2 = vertices[i2].position;

            if (point_in_circle(p0) || point_in_circle(p1) || point_in_circle(p2) ||
                segment_intersects_circle(p0, p1) || segment_intersects_circle(p1, p2) ||
                segment_intersects_circle(p2, p0))
            {
                remove_tri[t] = true;
                outer_verts_old.insert(i0);
                outer_verts_old.insert(i1);
                outer_verts_old.insert(i2);
                boundary_triangles.emplace_back(i0, i1, i2);
            }

            if (point_in_triangle(center, p0, p1, p2)) {
                remove_tri[t] = true;
                outer_verts_old.insert(i0);
                outer_verts_old.insert(i1);
                outer_verts_old.insert(i2);
                boundary_triangles.emplace_back(i0, i1, i2);

                center_uv = interpolate_uv_barycentric(center,
                    p0, p1, p2,
                    vertices[i0].uv, vertices[i1].uv, vertices[i2].uv);
            }
        }

        // ===== Step 2: Build edge adjacency from REMAINING triangles =====
        std::unordered_map<Edge, glm::uint, EdgeHash> edge_count;
        for (glm::uint t = 0; t < tri_count; ++t) {
            if (remove_tri[t]) continue;
            glm::uint i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
            edge_count[Edge(i0, i1)]++;
            edge_count[Edge(i1, i2)]++;
            edge_count[Edge(i2, i0)]++;
        }

        // ===== Step 3: Identify boundary vertices =====
        std::vector<bool> is_boundary(vertices.size(), false);
        for (const auto& [edge, cnt] : edge_count) {
            if (cnt == 1) {
                is_boundary[edge.a] = true;
                is_boundary[edge.b] = true;
            }
        }

        // ===== Step 4: Mark vertices for removal =====
        std::vector<bool> remove_vert(vertices.size(), false);
        for (glm::uint i = 0; i < vertices.size(); ++i) {
            if (point_in_circle(vertices[i].position) && !is_boundary[i]) {
                remove_vert[i] = true;
            }
        }

        // ===== Step 5: Compact vertices =====
        std::vector<glm::uint> old_to_new(vertices.size(), glm::uint(-1));
        std::vector<Vertex2D> new_verts;
        new_verts.reserve(vertices.size() + 16);

        for (glm::uint i = 0; i < vertices.size(); ++i) {
            if (!remove_vert[i]) {
                old_to_new[i] = glm::uint(new_verts.size());
                new_verts.push_back(vertices[i]);
            }
        }

        // ===== Step 6: Rebuild indices =====
        std::vector<glm::uint> new_inds;
        new_inds.reserve(indices.size() + 32);

        for (glm::uint t = 0; t < tri_count; ++t) {
            if (remove_tri[t]) continue;
            glm::uint i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
            if (remove_vert[i0] || remove_vert[i1] || remove_vert[i2]) continue;
            new_inds.push_back(old_to_new[i0]);
            new_inds.push_back(old_to_new[i1]);
            new_inds.push_back(old_to_new[i2]);
        }

        // ===== Step 7: Collect boundary vertices =====
        std::vector<std::pair<float, glm::uint>> angled_verts;

        for (unsigned int old_idx : outer_verts_old) {
            glm::uint new_idx = old_to_new[old_idx];
            if (new_idx == glm::uint(-1)) continue;

            const auto& pos = new_verts[new_idx].position;
            float angle = atan2f(pos.y - center.y, pos.x - center.x);
            if (angle < 0) angle += 2.0f * glm::pi<float>();
            angled_verts.emplace_back(angle, new_idx);
        }

        std::sort(angled_verts.begin(), angled_verts.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // ===== Step 8: Create inner ring vertices (с правильной интерполяцией UV) =====
        const int ring_segments = 8;
        std::vector<glm::uint> ring_verts;
        ring_verts.reserve(ring_segments);

        float angle_random_shift = (float)(std::rand() % (int)(glm::tau<float>() * 1000)) / 1000.0f;

        for (int s = 0; s < ring_segments; ++s) {
            float angle = glm::tau<float>() * s / ring_segments + angle_random_shift;
            glm::vec2 pos = center + glm::vec2(cosf(angle), sinf(angle)) * radius * 0.95f;

            glm::vec2 uv = center;  // Fallback
            bool uv_found = false;

            // Пытаемся найти треугольник, в котором лежит точка (или близко к нему)
            // Ищем среди треугольников на границе
            for (const auto& [old_i0, old_i1, old_i2] : boundary_triangles) {
                const auto& p0 = vertices[old_i0].position;
                const auto& p1 = vertices[old_i1].position;
                const auto& p2 = vertices[old_i2].position;
                const auto& uv0 = vertices[old_i0].uv;
                const auto& uv1 = vertices[old_i1].uv;
                const auto& uv2 = vertices[old_i2].uv;

                // Вычисляем расстояние от точки до треугольника
                // Используем барицентрические координаты для проверки
                glm::vec2 v0 = p2 - p0;
                glm::vec2 v1 = p1 - p0;
                glm::vec2 v2 = pos - p0;

                float dot00 = glm::dot(v0, v0);
                float dot01 = glm::dot(v0, v1);
                float dot02 = glm::dot(v0, v2);
                float dot11 = glm::dot(v1, v1);
                float dot12 = glm::dot(v1, v2);

                float denom = (dot00 * dot11 - dot01 * dot01);
                if (fabs(denom) < 1e-8f) continue;

                float inv_denom = 1.0f / denom;
                float u = (dot11 * dot02 - dot01 * dot12) * inv_denom;
                float v = (dot00 * dot12 - dot01 * dot02) * inv_denom;
                float w = 1.0f - u - v;

                // Если точка находится в треугольнике (или близко - с небольшой толерантностью)
                float tolerance = 0.001f;
                if (u >= -tolerance && v >= -tolerance && w >= -tolerance) {
                    // Нормализуем барицентрические координаты
                    u = glm::clamp(u, 0.0f, 1.0f);
                    v = glm::clamp(v, 0.0f, 1.0f);
                    w = glm::clamp(w, 0.0f, 1.0f);
                    float sum = u + v + w;
                    if (sum > 0) {
                        u /= sum;
                        v /= sum;
                        w /= sum;
                    }

                    // Интерполируем UV
                    uv = w * uv0 + u * uv2 + v * uv1;
                    uv_found = true;
                    break;
                }
            }

            // Fallback: если треугольник не найден, интерполируем от ближайших граничных вершин
            if (!uv_found && !angled_verts.empty()) {
                float min_dist = std::numeric_limits<float>::max();
                int nearest_idx = 0;

                for (size_t i = 0; i < angled_verts.size(); ++i) {
                    float dist = glm::distance(pos, new_verts[angled_verts[i].second].position);
                    if (dist < min_dist) {
                        min_dist = dist;
                        nearest_idx = i;
                    }
                }

                // Интерполируем от двух ближайших вершин
                size_t next_idx = (nearest_idx + 1) % angled_verts.size();
                glm::uint idx0 = angled_verts[nearest_idx].second;
                glm::uint idx1 = angled_verts[next_idx].second;

                float d0 = glm::distance(pos, new_verts[idx0].position);
                float d1 = glm::distance(pos, new_verts[idx1].position);
                float total = d0 + d1;

                if (total > 0) {
                    float t = d0 / total;
                    uv = glm::mix(new_verts[idx0].uv, new_verts[idx1].uv, t);
                } else {
                    uv = new_verts[idx0].uv;
                }
            }

            Vertex2D v;
            v.position = pos;
            v.uv = uv;
            new_verts.push_back(v);
            ring_verts.push_back(glm::uint(new_verts.size()) - 1);
        }

        // ===== Step 9: Connect ring to boundary vertices =====
        auto get_angle = [&](glm::uint idx) -> float {
            glm::vec2 pos = new_verts[idx].position;
            float dx = pos.x - center.x;
            float dy = pos.y - center.y;
            float angle = atan2f(dy, dx);  // ИСПРАВЛЕНО: правильный порядок!
            if (angle < 0) angle += 2.0f * glm::pi<float>();
            return angle;
        };

        // Проверка winding order
        auto is_ccw = [&](glm::uint a, glm::uint b, glm::uint c) -> bool {
            glm::vec2 v1 = new_verts[b].position - new_verts[a].position;
            glm::vec2 v2 = new_verts[c].position - new_verts[a].position;
            return (v1.x * v2.y - v1.y * v2.x) > 0;
        };

        std::vector<glm::uint> sorted_outer(outer_verts_old.size());
        {
            size_t idx = 0;
            for (auto old_idx : outer_verts_old) {
                glm::uint new_idx = old_to_new[old_idx];
                if (new_idx != glm::uint(-1)) {
                    sorted_outer[idx++] = new_idx;
                }
            }
            sorted_outer.resize(idx);
        }

        std::vector<glm::uint> sorted_inner = ring_verts;

        std::sort(sorted_outer.begin(), sorted_outer.end(), [&](glm::uint a, glm::uint b) {
            return get_angle(a) < get_angle(b);
        });
        std::sort(sorted_inner.begin(), sorted_inner.end(), [&](glm::uint a, glm::uint b) {
            return get_angle(a) < get_angle(b);
        });

        size_t n_out = sorted_outer.size();
        size_t n_in = sorted_inner.size();

        if (n_out < 3 || n_in < 3)
            return;

        new_inds.reserve(new_inds.size() + (n_out + n_in) * 2);

        std::vector<std::pair<int, int>> step_idxs;
        std::vector<int> dangerous_triangles = std::vector(n_out, -1);

        for (int i = 0; i < n_out; ++i) {
            const glm::vec2& first  = new_verts[sorted_outer[i]].position;
            const glm::vec2& second = new_verts[sorted_outer[(i + 1) % n_out]].position;
            int closest = -1;
            float closest_dist = -1;


            for (int j = 0; j < n_in; ++j) {
                const glm::vec2& point = new_verts[sorted_inner[j]].position;
                float dist = perpendicular(first, second, point);
                if (closest_dist == -1 || dist < closest_dist) {
                    closest_dist = dist;
                    closest = j;
                }
            }
            glm::vec3 angles;
            angles[0] = getAngle(
                first,
                new_verts[sorted_inner[closest == 0 ? n_in - 1 : closest - 1]].position,
                second);
            angles[1] = getAngle(
                first,
                new_verts[sorted_inner[closest]].position,
                second);
            angles[2] = getAngle(
                first,
                new_verts[sorted_inner[closest == n_in - 1 ? 0 : closest + 1]].position,
                second);

            if (angles[0] > angles[1] && angles[0] > angles[2]) {
                closest = closest == 0 ? n_in - 1 : closest - 1;
            }
            else if (angles[2] > angles[0] && angles[2] > angles[1]) {
                closest = closest == n_in - 1 ? 0 : closest + 1;
            }

            step_idxs.push_back({closest, (i + 1) % n_out});
            new_inds.push_back(sorted_inner[closest]);
            new_inds.push_back(sorted_outer[i]);
            new_inds.push_back(sorted_outer[(i + 1) % n_out]);
        }
        int skipped_triangles = 0;
        int first_i = (step_idxs[0].first + n_in - 1) % n_in;
        // while (first_i == (step_idxs[skipped_triangles % step_idxs.size()].first + n_in - 1) % n_in)
        //     skipped_triangles++;
        for (int i = first_i, connected = 0; connected < n_in; ++connected) {
            int next_i = (i + 1) % n_in;
            new_inds.push_back(sorted_inner[i]);
            new_inds.push_back(sorted_inner[next_i]);
            new_inds.push_back(sorted_outer[step_idxs[(step_idxs.size() - 1 + skipped_triangles) % step_idxs.size()].second]);
            while (i == (step_idxs[skipped_triangles % step_idxs.size()].first + n_in - 1) % n_in)
                skipped_triangles++;

            i = next_i;
        }

        // ===== Step 10: Create center vertex =====
        Vertex2D center_v;
        center_v.position = center;
        center_v.uv = center_uv;
        glm::uint center_idx = glm::uint(new_verts.size());
        new_verts.push_back(center_v);

        // ===== Step 11: Fan triangulation from center =====
        for (int i = 0; i < ring_segments; ++i) {
            int next = (i + 1) % ring_segments;
            std::array<glm::uint, 3> tri = { center_idx, ring_verts[i], ring_verts[next] };
            if (!is_ccw(tri[0], tri[1], tri[2])) {
                std::swap(tri[1], tri[2]);
            }
            new_inds.push_back(tri[0]);
            new_inds.push_back(tri[1]);
            new_inds.push_back(tri[2]);
        }

        // ===== Commit =====
        vertices = std::move(new_verts);
        indices = std::move(new_inds);
    }

    void displace_vertices(const glm::vec2& center, float power, float radius)
    {
        if (power == 0.0f || radius <= 0.0f) return;

        float radius_sq = radius * radius;

        for (auto& vert : vertices)
        {
            glm::vec2 dir = vert.position - center;
            float dist_sq = glm::dot(dir, dir); // Квадрат расстояния (быстрее чем length)

            if (dist_sq < radius_sq && dist_sq > 0.0001f) // Избегаем деления на 0
            {
                float dist = std::sqrt(dist_sq);

                // Нормализованный вектор направления от центра
                glm::vec2 normal = dir / dist;

                // Коэффициент затухания: 1.0 в центре, 0.0 на краю радиуса
                // Можно использовать linear falloff: (1.0f - dist / radius)
                // Или smootherstep для мягкости:
                float t = dist / radius;
                float falloff = 1.0f - t; // Линейное затухание

                // Применяем смещение
                vert.position += normal * power * falloff;

                // UV координаты обычно не меняются при физическом разрушении,
                // если текстура "приклеена" к материалу.
                // Если нужна деформация текстуры, UV тоже нужно сдвигать.
            }
        }

        // Геометрия изменилась, нужно обновить VBO
        mark_dirty();
    }

private:
    GLuint  vao_         = 0;
    GLuint  vbo_         = 0;
    GLuint  ebo_         = 0;
    GLsizei index_count_ = 0;
    bool    dirty_       = false;
};
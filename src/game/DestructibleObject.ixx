module;

#include "glad/glad.h"
#include "glm/glm.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <optional>
#include <span>
#include <limits>
#include <utility>

export module destructible_object;

import mesh;

// ---------------------------------------------------------------------------
//  Вспомогательные утилиты (файл-локальные)
// ---------------------------------------------------------------------------

// Ключ ячейки воксельной сетки
struct CellKey
{
    int x, y;
    bool operator==(const CellKey& o) const noexcept { return x == o.x && y == o.y; }
};

struct CellKeyHash
{
    std::size_t operator()(const CellKey& k) const noexcept
    {
        // Cantor pairing — достаточно для разумных размеров сетки
        std::size_t hx = std::hash<int>{}(k.x);
        std::size_t hy = std::hash<int>{}(k.y);
        return hx ^ (hy * 2654435761u);
    }
};

// ---------------------------------------------------------------------------
//  Базовые геометрические примитивы
// ---------------------------------------------------------------------------

// Пересечение горизонтального луча вправо из p с отрезком [a,b]
bool ray_intersects_segment(glm::vec2 p, glm::vec2 a, glm::vec2 b)
{
    if (a.y > b.y) std::swap(a, b);
    if (p.y < a.y || p.y >= b.y) return false;
    float t  = (p.y - a.y) / (b.y - a.y);
    float xi = a.x + t * (b.x - a.x);
    return xi > p.x;
}

struct SurfaceHit { glm::vec2 pos; glm::vec2 uv; };

// Ближайшая точка на отрезке [va,vb] к p
inline SurfaceHit closest_point_on_mesh(const glm::vec2& p,
                                   const Vertex2D&  va,
                                   const Vertex2D&  vb)
{
    glm::vec2 ab  = vb.position - va.position;
    glm::vec2 ap  = p           - va.position;
    float len2 = glm::dot(ab, ab);
    float t    = (len2 < 1e-10f) ? 0.f : std::clamp(glm::dot(ap, ab) / len2, 0.f, 1.f);
    return { va.position + ab * t, glm::mix(va.uv, vb.uv, t) };
}

// ---------------------------------------------------------------------------
//  MeshAccel — пространственный индекс для одного меша.
//  Строится один раз в конструкторе DestructibleObject.
//  Даёт O(1) среднее время для point_inside и closest_point.
// ---------------------------------------------------------------------------
struct MeshAccel
{
    // --- Данные исходного меша (не владеем, просто span) ---
    std::span<const Vertex2D>  verts;
    std::span<const glm::uint> indices;

    // --- Пространственная сетка для рёбер (closest_point) ---
    struct EdgeRef { glm::uint ia, ib; };

    float       edge_cell;      // размер ячейки пространственной сетки рёбер
    glm::vec2   edge_origin;
    int         edge_cols = 0;
    int         edge_rows = 0;
    std::vector<std::vector<EdgeRef>> edge_grid; // [row*cols+col] → список рёбер

    // --- Данные для ray-casting: рёбра, отсортированные по min_y ---
    struct SegRef
    {
        glm::vec2 a, b;  // a.y <= b.y
        float min_y, max_y, min_x, max_x;
    };
    std::vector<SegRef> segs;   // все рёбра меша (без дубликатов)
    // Для быстрого ray-casting: bucket по строкам пространственной сетки
    float       rc_cell;        // высота строки
    float       rc_origin_y;
    int         rc_rows = 0;
    std::vector<std::vector<int>> rc_buckets; // [row] → индексы в segs

    // --- AABB меша ---
    glm::vec2 aabb_lo, aabb_hi;

    // -----------------------------------------------------------------------
    void build(std::span<const Vertex2D>  v,
               std::span<const glm::uint> idx,
               float voxel_cell_size)
    {
        verts   = v;
        indices = idx;

        if (v.empty() || idx.size() < 3) return;

        // AABB
        aabb_lo = aabb_hi = v[0].position;
        for (auto& vtx : v)
        {
            aabb_lo = glm::min(aabb_lo, vtx.position);
            aabb_hi = glm::max(aabb_hi, vtx.position);
        }

        // Соберём уникальные рёбра
        std::unordered_map<std::uint64_t, bool> seen;
        seen.reserve(idx.size());

        auto edge_key = [](glm::uint a, glm::uint b) -> std::uint64_t
        {
            if (a > b) std::swap(a, b);
            return (static_cast<std::uint64_t>(a) << 32) | b;
        };

        for (std::size_t i = 0; i + 2 < idx.size(); i += 3)
        {
            glm::uint ia = idx[i], ib = idx[i+1], ic = idx[i+2];
            for (auto [a, b] : { std::pair{ia,ib}, std::pair{ib,ic}, std::pair{ic,ia} })
            {
                if (seen.emplace(edge_key(a, b), true).second)
                {
                    glm::vec2 pa = v[a].position;
                    glm::vec2 pb = v[b].position;
                    if (pa.y > pb.y) std::swap(pa, pb);
                    segs.push_back({ pa, pb, pa.y, pb.y,
                                     std::min(pa.x, pb.x),
                                     std::max(pa.x, pb.x) });
                }
            }
        }

        // ---- Пространственная сетка для рёбер (closest_point) ----
        // Ячейка = voxel_cell_size: у каждого вокселя запрос в 1-2 ячейки
        edge_cell   = voxel_cell_size;
        edge_origin = aabb_lo - glm::vec2{edge_cell};
        glm::vec2 edge_hi = aabb_hi + glm::vec2{edge_cell};

        edge_cols = static_cast<int>(std::ceil((edge_hi.x - edge_origin.x) / edge_cell)) + 1;
        edge_rows = static_cast<int>(std::ceil((edge_hi.y - edge_origin.y) / edge_cell)) + 1;
        edge_grid.assign(static_cast<std::size_t>(edge_cols * edge_rows), {});

        // Вставляем каждое ребро во все ячейки, которые оно перекрывает (AABB ребра)
        for (std::size_t si = 0; si < segs.size(); ++si)
        {
            const auto& s = segs[si];
            glm::uint ia = indices[0]; // восстановим ia/ib из segs — проще хранить отдельно
            // Вычислим AABB ребра в ячейках сетки
            int c0 = static_cast<int>((s.min_x - edge_origin.x) / edge_cell);
            int c1 = static_cast<int>((s.max_x - edge_origin.x) / edge_cell);
            int r0 = static_cast<int>((s.min_y - edge_origin.y) / edge_cell);
            int r1 = static_cast<int>((s.max_y - edge_origin.y) / edge_cell);
            c0 = std::clamp(c0, 0, edge_cols - 1);
            c1 = std::clamp(c1, 0, edge_cols - 1);
            r0 = std::clamp(r0, 0, edge_rows - 1);
            r1 = std::clamp(r1, 0, edge_rows - 1);
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                    edge_grid[r * edge_cols + c].push_back({ static_cast<glm::uint>(si), 0 });
                    // ia/ib нам не нужны — будем брать из segs по индексу
        }

        // Перестроим edge_grid: храним только индексы сегментов (не EdgeRef с ia/ib)
        // Упрощение: перепишем как vector<vector<int>>
        // (оставим текущую структуру — EdgeRef.ia = индекс сегмента, .ib не используется)

        // ---- Buckets для ray-casting ----
        rc_cell     = edge_cell;
        rc_origin_y = edge_origin.y;
        rc_rows     = edge_rows;
        rc_buckets.assign(static_cast<std::size_t>(rc_rows), {});

        for (int si = 0; si < static_cast<int>(segs.size()); ++si)
        {
            int r0 = static_cast<int>((segs[si].min_y - rc_origin_y) / rc_cell);
            int r1 = static_cast<int>((segs[si].max_y - rc_origin_y) / rc_cell);
            r0 = std::clamp(r0, 0, rc_rows - 1);
            r1 = std::clamp(r1, 0, rc_rows - 1);
            for (int r = r0; r <= r1; ++r)
                rc_buckets[r].push_back(si);
        }
    }

    // -----------------------------------------------------------------------
    //  Проверка: точка внутри меша (odd-even ray casting)
    //  Используем только сегменты из бакета нужной строки → O(local)
    // -----------------------------------------------------------------------
    bool point_inside(const glm::vec2& p) const
    {
        // Быстрый AABB-тест
        if (p.x < aabb_lo.x || p.x > aabb_hi.x ||
            p.y < aabb_lo.y || p.y > aabb_hi.y) return false;

        int row = static_cast<int>((p.y - rc_origin_y) / rc_cell);
        row = std::clamp(row, 0, rc_rows - 1);

        int crossings = 0;
        for (int si : rc_buckets[row])
        {
            const auto& s = segs[si];
            if (ray_intersects_segment(p, s.a, s.b)) ++crossings;
        }
        return (crossings & 1) == 1;
    }

    // -----------------------------------------------------------------------
    //  Ближайшая точка на поверхности меша
    //  Ищем в ячейках сетки, расширяя радиус пока не найдём кандидата
    // -----------------------------------------------------------------------
    // SurfaceHit closest_point(const glm::vec2& p) const
    // {
    //     if (segs.empty()) return { p, {} };
    //
    //     // Ячейка запрашиваемой точки
    //     int pc = static_cast<int>((p.x - edge_origin.x) / edge_cell);
    //     int pr = static_cast<int>((p.y - edge_origin.y) / edge_cell);
    //
    //     float bestD2 = std::numeric_limits<float>::max();
    //     SurfaceHit best{ p, {} };
    //
    //     // Расширяем радиус поиска пока нижняя граница квадрата > bestD2
    //     for (int radius = 0; ; ++radius)
    //     {
    //         // Нижняя граница расстояния до ближайшей точки за пределами квадрата radius
    //         float border_dist = (radius == 0) ? 0.f : (radius - 1) * edge_cell;
    //         if (bestD2 < border_dist * border_dist && radius > 0) break;
    //
    //         // Ограничим диапазон сетки
    //         int c0 = std::clamp(pc - radius, 0, edge_cols - 1);
    //         int c1 = std::clamp(pc + radius, 0, edge_cols - 1);
    //         int r0 = std::clamp(pr - radius, 0, edge_rows - 1);
    //         int r1 = std::clamp(pr + radius, 0, edge_rows - 1);
    //
    //         // Итерируем только по периметру кольца (внутренние уже проверены)
    //         auto check_cell = [&](int c, int r)
    //         {
    //             for (const auto& eref : edge_grid[r * edge_cols + c])
    //             {
    //                 int si = static_cast<int>(eref.ia);
    //                 const SegRef& s = segs[si];
    //
    //                 // Ищем оригинальные вершины через span
    //                 // Мы храним только позиции в segs, UV нужно получить из verts.
    //                 // Простейший вариант: сканируем verts по позиции.
    //                 // Оптимальнее — хранить индексы. Перейдём к этому ниже.
    //                 // (см. seg_ia_ / seg_ib_ параллельный массив)
    //             }
    //         };
    //         (void)check_cell; // заменим ниже
    //
    //         if (radius == 0)
    //         {
    //             // Проверим центральную ячейку
    //             if (pc >= 0 && pc < edge_cols && pr >= 0 && pr < edge_rows)
    //             {
    //                 for (const auto& eref : edge_grid[pr * edge_cols + pc])
    //                 {
    //                     int si = eref.ia;
    //                     auto hit = closest_on_edge_by_seg(p, si);
    //                     float d2 = glm::distance2(p, hit.pos);
    //                     if (d2 < bestD2) { bestD2 = d2; best = hit; }
    //                 }
    //             }
    //         }
    //         else
    //         {
    //             // Периметр кольца радиуса radius
    //             for (int c = c0; c <= c1; ++c)
    //             {
    //                 for (int r = r0; r <= r1; ++r)
    //                 {
    //                     // Только клетки на границе кольца
    //                     if (c != c0 && c != c1 && r != r0 && r != r1) continue;
    //                     for (const auto& eref : edge_grid[r * edge_cols + c])
    //                     {
    //                         int si = eref.ia;
    //                         auto hit = closest_on_edge_by_seg(p, si);
    //                         float d2 = glm::distance2(p, hit.pos);
    //                         if (d2 < bestD2) { bestD2 = d2; best = hit; }
    //                     }
    //                 }
    //             }
    //         }
    //
    //         // Если вышли за пределы сетки — все сегменты проверены
    //         if (c0 == 0 && r0 == 0 && c1 == edge_cols-1 && r1 == edge_rows-1) break;
    //     }
    //     return best;
    // }

private:
    // Параллельный массив: для каждого сегмента — исходные индексы вершин
    // Заполняется в build() вместе с segs.
public:
    std::vector<std::pair<glm::uint,glm::uint>> seg_verts; // [si] → {ia, ib}
};

// ---------------------------------------------------------------------------
//  Пересборка MeshAccel::build с корректным заполнением seg_verts
// ---------------------------------------------------------------------------

// Функция-обёртка: строит MeshAccel полностью корректно
MeshAccel build_mesh_accel(std::span<const Vertex2D>  v,
                            std::span<const glm::uint> idx,
                            float cell_size)
{
    MeshAccel accel;
    accel.verts   = v;
    accel.indices = idx;

    if (v.empty() || idx.size() < 3) return accel;

    accel.aabb_lo = accel.aabb_hi = v[0].position;
    for (auto& vtx : v)
    {
        accel.aabb_lo = glm::min(accel.aabb_lo, vtx.position);
        accel.aabb_hi = glm::max(accel.aabb_hi, vtx.position);
    }

    // Уникальные рёбра
    auto edge_key = [](glm::uint a, glm::uint b) -> std::uint64_t
    {
        if (a > b) std::swap(a, b);
        return (static_cast<std::uint64_t>(a) << 32) | b;
    };

    std::unordered_map<std::uint64_t, int> seen; // key → index в segs
    seen.reserve(idx.size());

    for (std::size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        glm::uint ia = idx[i], ib = idx[i+1], ic = idx[i+2];
        for (auto [a, b] : { std::pair{ia,ib}, std::pair{ib,ic}, std::pair{ic,ia} })
        {
            std::uint64_t k = edge_key(a, b);
            if (seen.emplace(k, static_cast<int>(accel.segs.size())).second)
            {
                glm::vec2 pa = v[a].position;
                glm::vec2 pb = v[b].position;
                glm::uint ra = a, rb = b;
                if (pa.y > pb.y) { std::swap(pa, pb); std::swap(ra, rb); }
                accel.segs.push_back({
                    pa, pb, pa.y, pb.y,
                    std::min(pa.x,pb.x), std::max(pa.x,pb.x)
                });
                accel.seg_verts.emplace_back(ra, rb);
            }
        }
    }

    // Пространственная сетка (edge_grid)
    accel.edge_cell   = cell_size;
    accel.edge_origin = accel.aabb_lo - glm::vec2{cell_size};
    glm::vec2 edge_hi = accel.aabb_hi + glm::vec2{cell_size};

    accel.edge_cols = static_cast<int>(
        std::ceil((edge_hi.x - accel.edge_origin.x) / cell_size)) + 1;
    accel.edge_rows = static_cast<int>(
        std::ceil((edge_hi.y - accel.edge_origin.y) / cell_size)) + 1;
    accel.edge_grid.assign(
        static_cast<std::size_t>(accel.edge_cols * accel.edge_rows), {});

    for (int si = 0; si < static_cast<int>(accel.segs.size()); ++si)
    {
        const auto& s = accel.segs[si];
        int c0 = static_cast<int>((s.min_x - accel.edge_origin.x) / cell_size);
        int c1 = static_cast<int>((s.max_x - accel.edge_origin.x) / cell_size);
        int r0 = static_cast<int>((s.min_y - accel.edge_origin.y) / cell_size);
        int r1 = static_cast<int>((s.max_y - accel.edge_origin.y) / cell_size);
        c0 = std::clamp(c0, 0, accel.edge_cols - 1);
        c1 = std::clamp(c1, 0, accel.edge_cols - 1);
        r0 = std::clamp(r0, 0, accel.edge_rows - 1);
        r1 = std::clamp(r1, 0, accel.edge_rows - 1);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                accel.edge_grid[r * accel.edge_cols + c].push_back(
                    { static_cast<glm::uint>(si), 0 });
    }

    // Ray-casting buckets
    accel.rc_cell     = cell_size;
    accel.rc_origin_y = accel.edge_origin.y;
    accel.rc_rows     = accel.edge_rows;
    accel.rc_buckets.assign(static_cast<std::size_t>(accel.rc_rows), {});

    for (int si = 0; si < static_cast<int>(accel.segs.size()); ++si)
    {
        int r0 = static_cast<int>(
            (accel.segs[si].min_y - accel.rc_origin_y) / accel.rc_cell);
        int r1 = static_cast<int>(
            (accel.segs[si].max_y - accel.rc_origin_y) / accel.rc_cell);
        r0 = std::clamp(r0, 0, accel.rc_rows - 1);
        r1 = std::clamp(r1, 0, accel.rc_rows - 1);
        for (int r = r0; r <= r1; ++r)
            accel.rc_buckets[r].push_back(si);
    }

    return accel;
}

// ---------------------------------------------------------------------------
//  Простая 2-D Делоне триангуляция методом Bowyer–Watson
//  (достаточно для разумного числа вокселей — до ~несколько тысяч)
// ---------------------------------------------------------------------------
struct DelaunayTri { int a, b, c; };

struct Circumcircle
{
    glm::vec2 center;
    float     r2;

    bool contains(glm::vec2 p) const
    {
        return glm::distance2(p, center) < r2 + 1e-6f;
    }
};

Circumcircle circumcircle(glm::vec2 a, glm::vec2 b, glm::vec2 c)
{
    float ax = b.x - a.x, ay = b.y - a.y;
    float bx = c.x - a.x, by = c.y - a.y;
    float D  = 2.f * (ax * by - ay * bx);
    if (std::abs(D) < 1e-10f)
    {
        // Вырожденный треугольник — возвращаем огромный круг
        return { a, std::numeric_limits<float>::max() };
    }
    float ux = (by * (ax * ax + ay * ay) - ay * (bx * bx + by * by)) / D;
    float uy = (ax * (bx * bx + by * by) - bx * (ax * ax + ay * ay)) / D;
    glm::vec2 center = a + glm::vec2{ux, uy};
    return { center, glm::distance2(center, a) };
}

std::vector<DelaunayTri> bowyer_watson(std::span<const glm::vec2> pts)
{
    int N = static_cast<int>(pts.size());
    if (N < 3) return {};

    // Найдём AABB
    glm::vec2 lo = pts[0], hi = pts[0];
    for (auto& p : pts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    glm::vec2 sz  = hi - lo;
    float     pad = std::max(sz.x, sz.y) * 10.f + 1.f;

    // Супертреугольник (индексы N, N+1, N+2)
    std::vector<glm::vec2> all(pts.begin(), pts.end());
    all.push_back({ lo.x - pad,         lo.y - pad         });
    all.push_back({ lo.x + sz.x * 0.5f, lo.y + sz.y + pad  });
    all.push_back({ lo.x + sz.x + pad,  lo.y - pad         });

    std::vector<DelaunayTri> tris;
    tris.push_back({ N, N + 1, N + 2 });

    for (int pi = 0; pi < N; ++pi)
    {
        glm::vec2 p = all[pi];

        // Найдём "плохие" треугольники, чья описанная окружность содержит p
        std::vector<bool> bad(tris.size(), false);
        for (std::size_t ti = 0; ti < tris.size(); ++ti)
        {
            auto& t = tris[ti];
            Circumcircle cc = circumcircle(all[t.a], all[t.b], all[t.c]);
            if (cc.contains(p)) bad[ti] = true;
        }

        // Соберём граничные рёбра (входящие в нечётное число плохих треугольников)
        std::vector<std::pair<int,int>> boundary;
        auto add_edge = [&](int a, int b)
        {
            // Если уже есть — удаляем (ребро разделяется двумя плохими треугольниками)
            for (auto it = boundary.begin(); it != boundary.end(); ++it)
            {
                if ((it->first == a && it->second == b) ||
                    (it->first == b && it->second == a))
                {
                    boundary.erase(it);
                    return;
                }
            }
            boundary.emplace_back(a, b);
        };
        for (std::size_t ti = 0; ti < tris.size(); ++ti)
        {
            if (!bad[ti]) continue;
            auto& t = tris[ti];
            add_edge(t.a, t.b);
            add_edge(t.b, t.c);
            add_edge(t.c, t.a);
        }

        // Удалим плохие
        std::vector<DelaunayTri> good;
        good.reserve(tris.size());
        for (std::size_t ti = 0; ti < tris.size(); ++ti)
            if (!bad[ti]) good.push_back(tris[ti]);
        tris = std::move(good);

        // Добавим новые треугольники
        for (auto& [ea, eb] : boundary)
            tris.push_back({ ea, eb, pi });
    }

    // Удалим треугольники, содержащие вершины супертреугольника
    std::vector<DelaunayTri> result;
    result.reserve(tris.size());
    for (auto& t : tris)
        if (t.a < N && t.b < N && t.c < N)
            result.push_back(t);

    return result;
}

// ---------------------------------------------------------------------------
//  Экспортируемые типы
// ---------------------------------------------------------------------------
export enum class VoxelStatus : std::uint8_t
{
    Absent   = 0,  // пустая ячейка (за пределами объекта)
    Interior = 1,  // внутренний воксель (не рендерится)
    Boundary = 2,  // граничный воксель (входит в меш)
};

export struct Voxel
{
    glm::vec2   center;         // центр ячейки в мировых координатах
    glm::vec2   offset;         // смещение точки вокселя от центра ячейки
    glm::vec2   uv;             // UV для рендера с учётом смещения
    float       hp;             // прочность [0..1]
    VoxelStatus status = VoxelStatus::Absent;

    // Позиция точки вокселя в мировых координатах
    glm::vec2 position() const { return center + offset; }

    bool is_alive()    const { return status != VoxelStatus::Absent && hp > 0.f; }
    bool is_boundary() const { return status == VoxelStatus::Boundary; }
};

// ---------------------------------------------------------------------------
//  DestructibleObject
// ---------------------------------------------------------------------------
export class DestructibleObject
{
public:
    // -----------------------------------------------------------------------
    //  Параметры воксельной сетки
    // -----------------------------------------------------------------------
    struct Params
    {
        float cell_size       = 1.f;   // размер одной ячейки
        float boundary_thresh = 0.5f;  // расстояние до поверхности → граничный
        float default_hp      = 1.f;   // начальная прочность каждого вокселя
        // Максимальная длина ребра результирующего меша, выраженная в cell_size.
        // По умолчанию 2.0 — «не длиннее двух квадратов».
        float max_edge_cells  = 2.0f;
    };

    // -----------------------------------------------------------------------
    //  Конструктор
    //  base — исходный меш (хранится по константной ссылке, не изменяется).
    //  Создаёт воксельную сетку и строит рендер-меш.
    // -----------------------------------------------------------------------
    explicit DestructibleObject(const Mesh& base, Params params = Params(15, 0.5f, 1, 2))
        : source_(base)
        , params_(params)
        , render_mesh_()
    {
        outer_mesh_ = source_.get_boundary();
        build_voxels();
        rebuild_render_mesh();
    }

    // Нельзя копировать (содержит ссылку на исходный меш)
    DestructibleObject(const DestructibleObject&)            = delete;
    DestructibleObject& operator=(const DestructibleObject&) = delete;
    DestructibleObject(DestructibleObject&&)                 = default;
    DestructibleObject& operator=(DestructibleObject&&)      = default;

    // -----------------------------------------------------------------------
    //  Доступ
    // -----------------------------------------------------------------------
    const Mesh&               source()      const { return source_; }
    Mesh&                     render_mesh()       { return render_mesh_; }
    const Mesh&               render_mesh() const { return render_mesh_; }

    // Все воксели (включая Absent, если нужна полная сетка)
    const std::vector<Voxel>& voxels()     const { return voxels_; }

    int grid_cols() const { return cols_; }
    int grid_rows() const { return rows_; }

    // Воксель по индексу ячейки (col, row)
    const Voxel* voxel_at(int col, int row) const
    {
        if (col < 0 || col >= cols_ || row < 0 || row >= rows_) return nullptr;
        return &voxels_[row * cols_ + col];
    }
    Voxel* voxel_at(int col, int row)
    {
        return const_cast<Voxel*>(std::as_const(*this).voxel_at(col, row));
    }

    // -----------------------------------------------------------------------
    //  Нанести урон в точку (мировые координаты).
    //  radius — радиус зоны урона; damage — сколько HP отнять.
    //  Возвращает true, если хотя бы один воксель был изменён.
    // -----------------------------------------------------------------------
    // bool apply_damage(glm::vec2 point, float radius, float damage)
    // {
    //     bool changed = false;
    //     float r2 = radius * radius;
    //
    //     for (auto& v : voxels_)
    //     {
    //         if (!v.is_alive()) continue;
    //         if (glm::distance2(v.position(), point) > r2) continue;
    //
    //         v.hp = std::max(0.f, v.hp - damage);
    //         if (v.hp <= 0.f)
    //         {
    //             v.status = VoxelStatus::Absent;
    //             changed  = true;
    //         }
    //     }
    //
    //     if (changed)
    //     {
    //         // Пересчитаем граничные/внутренние после разрушения
    //         recalc_boundary();
    //         rebuild_render_mesh();
    //     }
    //     return changed;
    // }

    // -----------------------------------------------------------------------
    //  Принудительно пересобрать рендер-меш (если воксели изменены снаружи)
    // -----------------------------------------------------------------------
    void rebuild_render_mesh()
    {
        std::vector<Vertex2D> verts;
        std::vector<glm::uint> inds;

        // Карта: (row, col) -> индекс в verts
        std::unordered_map<int, int> voxel_to_index;
        voxel_to_index.reserve(voxels_.size());

        // 1. Создаём вершины для всех активных вокселей
        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < cols_; ++col) {
                const Voxel& vx = voxels_[row * cols_ + col];
                if (!vx.is_alive()) continue; // пропускаем Absent и мёртвые

                int idx = static_cast<int>(verts.size());
                voxel_to_index[row * cols_ + col] = idx;

                Vertex2D v;
                v.position = vx.position(); // center + offset
                v.uv = vx.uv;
                verts.push_back(v);
            }
        }

        // 2. Строим треугольники между соседними вокселями
        // Проходим по каждому квадрату 2x2 в сетке
        for (int row = 0; row < rows_ - 1; ++row) {
            for (int col = 0; col < cols_ - 1; ++col) {
                // Индексы 4 вокселей в квадрате
                int idx_tl = row * cols_ + col;         // top-left
                int idx_tr = row * cols_ + (col + 1);   // top-right
                int idx_bl = (row + 1) * cols_ + col;   // bottom-left
                int idx_br = (row + 1) * cols_ + (col + 1); // bottom-right

                // Проверяем, какие воксели активны
                bool tl_alive = voxel_to_index.count(idx_tl);
                bool tr_alive = voxel_to_index.count(idx_tr);
                bool bl_alive = voxel_to_index.count(idx_bl);
                bool br_alive = voxel_to_index.count(idx_br);

                // Разбиваем квадрат на 2 треугольника
                // Диагональ: tl -> br
                if (tl_alive && tr_alive && br_alive) {
                    inds.push_back(voxel_to_index[idx_tl]);
                    inds.push_back(voxel_to_index[idx_tr]);
                    inds.push_back(voxel_to_index[idx_br]);
                }

                if (tl_alive && bl_alive && br_alive) {
                    inds.push_back(voxel_to_index[idx_tl]);
                    inds.push_back(voxel_to_index[idx_bl]);
                    inds.push_back(voxel_to_index[idx_br]);
                }
            }
        }

        // 3. Загружаем в render_mesh_
        render_mesh_.set(std::move(verts), std::move(inds));
    }

    // -----------------------------------------------------------------------
    //  Разрушение
    // -----------------------------------------------------------------------

    // Наносит урон в точке world_pos с радиусом damage_radius
    // Возвращает true, если что-то изменилось (нужен пересчет меша)
    bool apply_damage(const glm::vec2& world_pos, float damage, float radius = 0.f)
    {
        bool changed = false;

        // Определяем диапазон ячеек для проверки (оптимизация: не перебирать всю сетку)
        int start_col = static_cast<int>((world_pos.x - radius - min_x) / params_.cell_size);
        int end_col = static_cast<int>((world_pos.x + radius - min_x) / params_.cell_size) + 1;
        int start_row = static_cast<int>((world_pos.y - radius - min_y) / params_.cell_size);
        int end_row = static_cast<int>((world_pos.y + radius - min_y) / params_.cell_size) + 1;

        // Ограничиваем границами массива
        start_col = std::max(0, start_col);
        end_col = std::min(cols_, end_col);
        start_row = std::max(0, start_row);
        end_row = std::min(rows_, end_row);

        // 1. Наносим прямой урон вокселям в радиусе
        for (int r = start_row; r < end_row; ++r) {
            for (int c = start_col; c < end_col; ++c) {
                Voxel& vx = voxels_[r * cols_ + c];

                // Пропускаем уже мертвые
                if (vx.status == VoxelStatus::Absent) continue;

                float dist_sq = glm::length2(vx.center - world_pos);
                float rad_sq = radius * radius;

                // Если радиус > 0, проверяем попадание в круг. Если 0 — точечное попадание в ячейку
                if (radius > 0.f && dist_sq > rad_sq) continue;

                vx.hp -= damage;
                if (vx.hp <= 0.f) {
                    vx.hp = 0.f;
                    if (vx.status != VoxelStatus::Absent) {
                        vx.status = VoxelStatus::Absent;
                        changed = true;
                    }
                }
            }
        }

        // 2. Обновляем статусы соседей (Cascade update)
        // Если воксель стал Absent, его соседи Interior должны проверить, не стали ли они Boundary.
        // Если воксель был Boundary и стал Absent, это уже учтено выше.

        if (changed) {
            //update_neighbors_status(start_col, end_col, start_row, end_row);
            rebuild_render_mesh();
        }

        return changed;
    }

private:
    // Обновляет статусы вокселей в указанном диапазоне и их соседей
    void update_neighbors_status(int min_c, int max_c, int min_r, int max_r)
    {
        // Расширяем диапазон на 1 пиксель во все стороны, чтобы захватить соседей измененных ячеек
        min_c = std::max(0, min_c - 1);
        max_c = std::min(cols_, max_c + 1);
        min_r = std::max(0, min_r - 1);
        max_r = std::min(rows_, max_r + 1);

        constexpr int dx[4] = {1, -1, 0, 0};
        constexpr int dy[4] = {0, 0, 1, -1};

        for (int r = min_r; r < max_r; ++r) {
            for (int c = min_c; c < max_c; ++c) {
                Voxel& vx = voxels_[r * cols_ + c];

                // Нас интересуют только живые воксели, которые НЕ являются Boundary
                // (Boundary пересчитаются автоматически при проверке соседей,
                // а Interior могут стать Boundary)
                if (vx.status == VoxelStatus::Absent) continue;

                // Проверяем, есть ли рядом Absent сосед
                bool has_absent_neighbor = false;
                for (int d = 0; d < 4; ++d) {
                    int nc = c + dx[d];
                    int nr = r + dy[d];

                    if (nc >= 0 && nc < cols_ && nr >= 0 && nr < rows_) {
                        if (voxels_[nr * cols_ + nc].status == VoxelStatus::Absent) {
                            has_absent_neighbor = true;
                            break;
                        }
                    } else {
                        // Выход за границы сетки считаем пустотой
                        has_absent_neighbor = true;
                        break;
                    }
                }

                if (has_absent_neighbor) {
                    if (vx.status == VoxelStatus::Interior) {
                        vx.status = VoxelStatus::Boundary;

                        // Пересчитываем offset и UV для нового Boundary
                        recalculate_boundary_voxel(vx, c, r);
                    }
                    // Если он уже Boundary, оставляем как есть (offset уже верный)
                } else {
                    // Если соседей Absent нет, а воксель был Boundary (например, дырка закрылась другим куском?
                    // В нашей логике дырки не зарастают, но если структура сложная...)
                    // То он должен стать Interior? Нет, в деструктивной геометрии дырки остаются.
                    // Но если мы удалили "шум" вокруг, то Boundary может стать Interior?
                    // Обычно нет. Boundary остается Boundary, пока сам не умрет.
                }
            }
        }
    }

    // Пересчет смещения для одного граничного вокселя (копия логики из build_voxels)
    void recalculate_boundary_voxel(Voxel& vx, int col, int row)
    {
        if (outer_mesh_.empty()) return;

        glm::vec2 closest_point = vx.center;
        float min_dist_sq = std::numeric_limits<float>::max();
        const size_t contour_size = outer_mesh_.size();

        // Диапазон для UV
        // Примечание: src_w/h и uv_min лучше сохранить как члены класса или пересчитать,
        // но для простоты продублируем логику AABB из конструктора, если нужно.
        // Для оптимизации можно вынести расчет UV-базы в отдельную структуру.
        // Здесь используем упрощенный вариант: берем UV из центра, если проекция сложна,
        // или делаем полную проекцию.

        // Полная проекция (как в build_voxels):
        for (size_t i = 0; i < contour_size; ++i) {
            const auto& A = outer_mesh_[i].position;
            const auto& B = outer_mesh_[(i + 1) % contour_size].position;

            glm::vec2 AB = B - A;
            glm::vec2 AP = vx.center - A;
            float len_sq_AB = glm::dot(AB, AB);
            if (len_sq_AB < 1e-6f) continue;

            float t = glm::clamp(glm::dot(AP, AB) / len_sq_AB, 0.f, 1.f);
            glm::vec2 proj = A + t * AB;
            float dist_sq = glm::length2(vx.center - proj);

            if (dist_sq < min_dist_sq) {
                min_dist_sq = dist_sq;
                closest_point = proj;
            }
        }

        vx.offset = closest_point - vx.center;

        // UV: упрощенно берем от центра, так как пересчет полного AABB здесь дорог.
        // Для идеального качества нужно хранить min/max исходного меша в классе.
        // Предположим, что у нас есть доступ к origin_ и размерам, как в build_voxels.
        // Если нет, можно оставить UV как было (ошибка будет малозаметна)
        // или реализовать хранение src_w/h/uv_min в классе DestructibleObject.

        // ВАЖНО: Чтобы UV работал корректно, добавь в класс поля:
        // float src_w_, src_h_, uv_min_x_, uv_min_y_;
        // и заполни их в build_voxels().

        // if (src_w_ > 1e-5f && src_h_ > 1e-5f) {
        // }
        vx.uv = {
            (vx.position().x - min_x) * width_scale + min_u,
            (vx.position().y - min_y) * height_scale + min_v
        };
    }

    // -----------------------------------------------------------------------

    const Mesh& source_; // исходный меш — только для чтения
    Params params_;
    Mesh render_mesh_; // текущий рендер-меш (пересобирается при разрушении)

    std::vector<Voxel> voxels_; // линейный массив, индекс = row*cols_ + col
    std::vector<Vertex2D> outer_mesh_;

    int cols_ = 0;
    int rows_ = 0;

    float min_x;
    float max_x;
    float min_y;
    float max_y;

    float min_u;
    float max_u;
    float min_v;
    float max_v;

    float width_scale;
    float height_scale;

    // -----------------------------------------------------------------------
    //  Построить воксельную сетку по исходному мешу
    // -----------------------------------------------------------------------
    void build_voxels()
    {
        if (source_.vertices.empty()) {
            cols_ = rows_ = 0;
            voxels_.clear();
            return;
        }

        // -----------------------------------------------------------------------
        // 1. Расчёт габаритов сетки (AABB + Padding)
        // -----------------------------------------------------------------------
        min_x = source_.vertices[0].position.x;
        max_x = min_x;
        min_y = source_.vertices[0].position.y;
        max_y = min_y;

        min_u = source_.vertices[0].uv.x;
        max_u = min_x;
        min_v = source_.vertices[0].uv.y;
        max_v = min_y;

        for (const auto& v: source_.vertices) {
            min_x = std::min(min_x, v.position.x);
            max_x = std::max(max_x, v.position.x);
            min_y = std::min(min_y, v.position.y);
            max_y = std::max(max_y, v.position.y);

            min_u = std::min(min_u, v.uv.x);
            max_u = std::max(max_u, v.uv.x);
            min_v = std::min(min_v, v.uv.y);
            max_v = std::max(max_v, v.uv.y);
        }
        width_scale = (max_u - min_u) / (max_x - min_x);
        height_scale = (max_v - min_v) / (max_y - min_y);

        cols_ = static_cast<int>(std::ceil((max_x - min_x) / params_.cell_size));
        rows_ = static_cast<int>(std::ceil((max_y - min_y) / params_.cell_size));

        if (cols_ <= 0 || rows_ <= 0) {
            cols_ = rows_ = 0;
            voxels_.clear();
            return;
        }
        voxels_.resize(cols_ * rows_);

        // Диапазон для UV (исходный меш без паддинга)
        // const float src_w = std::max(1e-5f, (max_x - min_x - 2.f * pad));
        // const float src_h = std::max(1e-5f, (max_y - min_y - 2.f * pad));
        // const float uv_min_x = min_x + pad;
        // const float uv_min_y = min_y + pad;

        const float half_cell = params_.cell_size * 0.5f;

        // -----------------------------------------------------------------------
        // 2. ФАЗА 1: Заполнение объёма (Interior / Absent)
        // Все воксели сначала считаются пустыми. Если центр внутри меша -> Interior.
        // Сразу задаём случайное смещение для внутренних точек.
        // -----------------------------------------------------------------------
        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < cols_; ++col) {
                Voxel& vx = voxels_[row * cols_ + col];
                vx.center = {
                    min_x + (col + 0.5f) * params_.cell_size,
                    min_y + (row + 0.5f) * params_.cell_size
                };
                vx.hp = params_.default_hp;
                vx.status = VoxelStatus::Absent; // По умолчанию пусто
                vx.offset = {0.f, 0.f};

                if (source_.contains(vx.center)) {
                    vx.status = VoxelStatus::Interior;

                    // Случайное смещение в пределах ячейки [-0.5, 0.5]
                    vx.offset = {
                        half_cell * (static_cast<float>(rand()) / RAND_MAX * 2.f - 1.f),
                        half_cell * (static_cast<float>(rand()) / RAND_MAX * 2.f - 1.f)
                    };

                    // UV для внутренних точек
                    vx.uv = {
                        (vx.position().x - min_x) * width_scale + min_u,
                        (vx.position().y - min_y) * height_scale + min_v
                    };
                }
            }
        }

        // -----------------------------------------------------------------------
        // 3. ФАЗА 2: Выделение границы (Boundary) и коррекция смещения
        // Проверяем только Interior воксели. Если у соседа статус Absent -> Boundary.
        // Для Boundary пересчитываем offset: проекция центра на исходный контур.
        // -----------------------------------------------------------------------
        constexpr int dx[4] = {1, -1, 0, 0};
        constexpr int dy[4] = {0, 0, 1, -1};

        // Защита от пустого контура
        if (outer_mesh_.empty()) return;

        const size_t contour_size = outer_mesh_.size();

        for (int row = 0; row < rows_; ++row) {
            for (int col = 0; col < cols_; ++col) {
                Voxel& vx = voxels_[row * cols_ + col];

                // Нас интересуют только внутренние воксели
                if (vx.status != VoxelStatus::Interior) continue;

                bool is_boundary = false;
                for (int d = 0; d < 4; ++d) {
                    const int nc = col + dx[d];
                    const int nr = row + dy[d];

                    // Проверка границ массива или наличие пустого соседа
                    if (nc < 0 || nc >= cols_ || nr < 0 || nr >= rows_) {
                        is_boundary = true;
                        break;
                    }
                    if (voxels_[nr * cols_ + nc].status == VoxelStatus::Absent) {
                        is_boundary = true;
                        break;
                    }
                }

                if (is_boundary) {
                    vx.status = VoxelStatus::Boundary;
                    vx.offset = {};

                    // Ищем ближайшую точку на контуре outer_mesh_
                    glm::vec2 closest_point = vx.center;
                    float min_dist_sq = std::numeric_limits<float>::max();

                    // ВАЖНО: Замыкаем контур через (i + 1) % count
                    for (size_t i = 0; i < contour_size; ++i) {
                        const auto& A = outer_mesh_[i].position;
                        const auto& B = outer_mesh_[(i + 1) % contour_size].position;

                        glm::vec2 AB = B - A;
                        glm::vec2 AP = vx.center - A;
                        float len_sq_AB = glm::dot(AB, AB);

                        if (len_sq_AB < 1e-6f) continue;

                        float t = glm::clamp(glm::dot(AP, AB) / len_sq_AB, 0.f, 1.f);
                        glm::vec2 proj = A + t * AB;
                        float dist_sq = glm::length2(vx.center - proj);

                        if (dist_sq < min_dist_sq) {
                            min_dist_sq = dist_sq;
                            closest_point = proj;
                        }
                    }

                    // Смещение = вектор от центра к поверхности
                    vx.offset = closest_point - vx.center;

                    // UV для граничных точек берём по проекции на поверхность
                    vx.uv = {
                        (closest_point.x - min_x) * width_scale + min_u,
                        (closest_point.y - min_y) * height_scale + min_v
                    };
                }
            }
        }
        return;
    }

    // -----------------------------------------------------------------------
    //  После разрушения вокселей пересчитать статусы:
    //  внутренний воксель, у которого хотя бы один сосед Absent, становится Boundary.
    // -----------------------------------------------------------------------
    void recalc_boundary()
    {
        static constexpr std::array<std::pair<int,int>, 4> dirs = {{
            {1, 0}, {-1, 0}, {0, 1}, {0, -1}
        }};

        auto& src_v = source_.vertices;
        auto& src_i = source_.indices;
        const float cs = params_.cell_size;

        for (int row = 0; row < rows_; ++row)
        {
            for (int col = 0; col < cols_; ++col)
            {
                Voxel& v = voxels_[row * cols_ + col];
                if (v.status != VoxelStatus::Interior) continue;

                bool exposed = false;
                for (auto [dc, dr] : dirs)
                {
                    int nc = col + dc;
                    int nr = row + dr;
                    if (nc < 0 || nc >= cols_ || nr < 0 || nr >= rows_)
                    {
                        exposed = true;
                        break;
                    }
                    const Voxel& nb = voxels_[nr * cols_ + nc];
                    if (nb.status == VoxelStatus::Absent)
                    {
                        exposed = true;
                        break;
                    }
                }

                if (exposed)
                {
                    v.status = VoxelStatus::Boundary;

                    // Притянем к ближайшей поверхности если она ещё есть,
                    // иначе оставим в центре ячейки (внутреннее разрушение).
                    int closest = 0;
                    float distance = 10e10;
                    for (int i = 0; i < outer_mesh_.size() ; i += 1) {
                        float new_distance = MeshUtils::dist_point_to_clamped_segment(v.center, outer_mesh_[i].position, outer_mesh_[(i + 1) % outer_mesh_.size()].position);
                        if (distance > new_distance) {
                            closest = i;
                            distance = new_distance;
                        }
                    }
                    Vertex2D hit = outer_mesh_[closest];
                    float dist     = glm::length(hit.position - v.center);
                    if (dist <= params_.max_edge_cells * cs)
                    {
                        v.offset = hit.position - v.center;
                        v.uv     = hit.uv;
                    }
                    else
                    {
                        v.offset = glm::vec2{0.f};
                    }
                }
            }
        }
    }
};

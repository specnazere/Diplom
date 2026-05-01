#version 430 core

struct Transform {
    vec2 pos;
    float angle;
    uint parent;
};

layout(location = 0) in vec2 aPos;          // Базовый квад [-0.5, 0.5]

// Инстанс-данные из SpriteData
layout(location = 2) in float iWidth;
layout(location = 3) in float iHeight;
layout(location = 4) in float iAngle;           // Локальный угол спрайта
layout(location = 5) in vec2  iPos;             // Локальная позиция спрайта
layout(location = 6) in float iParalax;
layout(location = 7) in vec4  iUVRect;
layout(location = 8) in uint  iTransformIdx;    // Индекс в иерархии
layout(location = 9) in float iDepth;    // Индекс в иерархии

// Глобальный массив трансформаций (SSBO)
// Формат узла: { x, y, angle, parentIndex_as_float }
layout(std430, binding = 0) buffer TransformBuffer {
    Transform nodes[];
};

uniform mat4 uView;
uniform vec2 uLightPos;
uniform int uMaxTransformDepth;

out vec2 vUV;
out vec3 vFragPos;
out float vSin;
out float vCos;

// Вычисление глобальной трансформации по цепочке родителей
vec3 computeGlobalTransform(uint startIdx) {
    const uint INVALID = 0xFFFFFFFFu;
    if (startIdx == INVALID) return vec3(0.0);

    // Сбор пути от листа к корню
    uint path[16];
    int depth = 0;
    uint cur = startIdx;

    for (int i = 0; i < uMaxTransformDepth && i < 16 && cur != INVALID; ++i) {
        path[depth++] = cur;
        uint parent = nodes[cur].parent;
        cur = (parent == INVALID) ? INVALID : parent;
    }

    // Применение от корня к листу
    float gx = 0.0, gy = 0.0, ga = 0.0;
    for (int i = depth - 1; i >= 0; --i) {
        Transform n = nodes[path[i]];
        // Поворот локального смещения узла на накопленный угол родителя
        float cosA = cos(ga), sinA = sin(ga);
        float rx = n.pos.x * cosA - n.pos.y * sinA;
        float ry = n.pos.x * sinA + n.pos.y * cosA;
        gx += rx; gy += ry; ga += n.angle;
    }
    return vec3(gx, gy, ga);
}

void main() {
    // 1. Глобальная трансформация из иерархии
    vec3 global = computeGlobalTransform(iTransformIdx);

    // 2. Локальная трансформация спрайта (применяется ПОСЛЕ глобальной)
    // Сначала поворачиваем локальное смещение на глобальный угол
    float cosG = cos(global.z), sinG = sin(global.z);
    float localRotX = iPos.x * cosG - iPos.y * sinG;
    float localRotY = iPos.x * sinG + iPos.y * cosG;

    vec2 worldPos = vec2(global.x + localRotX, global.y + localRotY);
    float totalAngle = global.z + iAngle;

    // 3. Масштаб и вращение квада
    vec2 scaled = aPos * vec2(iWidth, iHeight);
    float s = sin(-totalAngle), c = cos(-totalAngle);
    vec2 rotated = mat2(c, -s, s, c) * scaled;

    worldPos += rotated;

    // Вывод
    vFragPos = vec3(worldPos, 0.0);
    vSin = s; vCos = c;

    mat4 pv = uView;
    pv[3][3] *= iParalax;
    gl_Position = pv * vec4(worldPos, iDepth, 1.0);

    vUV = vec2(
        iUVRect.x + (iUVRect.z - iUVRect.x) * (aPos.x + 0.5),
        iUVRect.y + (iUVRect.w - iUVRect.y) * (aPos.y + 0.5)
    );
}
#version 330 core

// Базовый квад
layout(location = 0) in vec2 aPos;

// Инстанс-данные
layout(location = 2) in float iWidth;
layout(location = 3) in float iHeight;
layout(location = 4) in float iAngle;
layout(location = 5) in vec2  iPos;
layout(location = 6) in float iParalax;
layout(location = 7) in vec4  iUVRect;

// Юниформы
uniform mat4 uView;
uniform vec2 uLightPos; // Позиция света в 2D мире

// Выходные данные
out vec2 vUV;
out vec3 vFragPos;      // Позиция фрагмента в мировом пространстве (для расчета векторов)
out float vSin;         // Для поворота нормали
out float vCos;

void main() {
    // 1. Локальные координаты -> Мировые
    vec2 scaledPos = aPos * vec2(iWidth, iHeight);

    float s = sin(iAngle);
    float c = cos(iAngle);
    mat2 rotation = mat2(c, -s, s, c);
    vec2 rotatedPos = rotation * scaledPos;

    vec2 worldPos = rotatedPos + iPos;

    // Передаем позицию фрагмента дальше (Z = 0, так как 2D)
    vFragPos = vec3(worldPos, 0.0);

    // Передаем тригонометрию для поворота нормалей во фрагментном шейдере
    vSin = s;
    vCos = c;

    // 2. Проекция
    mat4 paralaxedView = uView;
    paralaxedView[3][3] *= iParalax;
    gl_Position = paralaxedView * vec4(worldPos, 0.0, 1.0);

    // 3. UV
    vUV = vec2(
        iUVRect.x + (iUVRect.z - iUVRect.x) * (aPos.x + 0.5),
        iUVRect.y + (iUVRect.w - iUVRect.y) * (aPos.y + 0.5)
    );
}
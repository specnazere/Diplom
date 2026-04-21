#version 330 core

// Базовый квад
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

// Инстанс-данные
uniform float iAngle;
uniform vec2  iPos;
uniform float iParalax;
uniform float iDepth;

// Юниформы
uniform mat4 uView;
uniform vec2 uLightPos; // Позиция света в 2D мире

// Выходные данные
out vec2 vUV;
out vec3 vFragPos;      // Позиция фрагмента в мировом пространстве (для расчета векторов)
out float vSin;         // Для поворота нормали
out float vCos;

void main() {
    float s = sin(iAngle);
    float c = cos(iAngle);
    mat2 rotation = mat2(c, -s, s, c);
    vec2 rotatedPos = rotation * aPos;

    vec2 worldPos = rotatedPos + iPos;

    // Передаем позицию фрагмента дальше (Z = 0, так как 2D)
    vFragPos = vec3(worldPos, iDepth);

    // Передаем тригонометрию для поворота нормалей во фрагментном шейдере
    vSin = s;
    vCos = c;

    // 2. Проекция
    mat4 paralaxedView = uView;
    paralaxedView[3][3] *= iParalax;
    gl_Position = paralaxedView * vec4(worldPos, 0.0, 1.0);

    // 3. UV
    vUV = aUV;
}
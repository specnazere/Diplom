#version 330 core

// Базовый квад
layout(location = 0) in vec2 aPos;

// Инстанс-данные
layout(location = 2) in float iWidth;
layout(location = 3) in float iHeight;
layout(location = 4) in float iAngle;
layout(location = 5) in vec2  iPos;
layout(location = 4) in float iParalax;
layout(location = 7) in vec4  iUVRect;

// Юниформы
uniform mat4 uView;

// Выходные данные
out vec2 vUV;

void main() {
    // Масштабирование квада до размера спрайта
    vec2 scaledPos = aPos * vec2(iWidth, iHeight);

    // Поворот (2D)
    float s = sin(iAngle);
    float c = cos(iAngle);
    mat2 rotation = mat2(c, -s, s, c);
    vec2 rotatedPos = rotation * scaledPos;

    // Позиционирование
    vec2 worldPos = rotatedPos + iPos;

    // Применение ортографической матрицы
    mat4 paralaxedView = uView;
    paralaxedView[3][3] *= iParalax;

    gl_Position = uView * vec4(worldPos, 0.0, 1.0);

    // UV координаты (обрезка под прямоугольник из атласа)
    vUV = vec2(
        iUVRect.x + (iUVRect.z - iUVRect.x) * (aPos.x + 0.5),
        iUVRect.y + (iUVRect.w - iUVRect.y) * (aPos.y + 0.5)
    );
}
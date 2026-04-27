#version 430 core

in vec2 vUV;
in vec3 vFragPos;
in float vSin;
in float vCos;

out vec4 FragColor;

uniform sampler2D textureSampler;
uniform sampler2D normalSampler;
uniform vec2 uLightPos;

// Настройки света (можно вынести в uniform, если нужно менять динамически)
const float LIGHT_HEIGHT = 200.0;      // Высота источника над экраном (влияет на угол падения)
const float LIGHT_INTENSITY = 2;
const float AMBIENT_STRENGTH = 1;
const float SPECULAR_STRENGTH = 0.2;
const float SHININESS = 10.0;

void main() {
    vec4 texColor = texture(textureSampler, vUV);
    if (texColor.a <= 0.0) discard;

    if (texture(normalSampler, vUV).a != 0)
    {
        // 1. Нормаль из текстуры
        vec3 normalMap = texture(normalSampler, vUV).rgb;
        vec3 normal = normalize(normalMap * 2.0 - 1.0);

        // 2. Поворот нормали (учет вращения спрайта)
        vec2 rotatedNormalXY;
        rotatedNormalXY.x = normal.x * vCos - normal.y * vSin;
        rotatedNormalXY.y = normal.x * vSin + normal.y * vCos;
        normal = vec3(rotatedNormalXY, normal.z);
        normal = normalize(normal);

        // 3. Векторы
        // Источник света теперь имеет высоту (Z), чтобы освещать "сверху-сбоку"
        vec3 lightPos3D = vec3(uLightPos, LIGHT_HEIGHT);
        vec3 lightDir = normalize(lightPos3D - vFragPos);

        // Вектор взгляда (камера тоже немного приподнята, смотрит под углом)
        // Если камера строго сверху (0,0,1), блики могут теряться при боковом свете.
        // Сделаем взгляд направленным чуть в сторону света или просто вверх.
        vec3 viewDir = normalize(vec3(0.0, 0.0, 1.5));

        // 4. Blinn-Phong Освещение
        // Диффузное
        float diff = max(dot(normal, lightDir), 0.0);

        // Спекулярное (Blinn-Phong)
        // Полу-вектор (середина между светом и взглядом)
        vec3 halfDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(normal, halfDir), 0.0);
        float specular = pow(specAngle, SHININESS);

        // Сборка
        vec3 ambient = AMBIENT_STRENGTH * vec3(1.0);
        vec3 diffuse = diff * vec3(1.0);

        // Итоговый цвет
        vec3 lighting = ambient + (diffuse * LIGHT_INTENSITY) + (specular * SPECULAR_STRENGTH);
        vec3 result = lighting * texColor.rgb;

        FragColor = vec4(result, texColor.a);
    }
    else
    {
        FragColor = texColor;
    }
}
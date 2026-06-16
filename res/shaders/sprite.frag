#version 430 core

in vec2 vUV;
in vec3 vFragPos;
in float vSin;
in float vCos;
in vec4 vColor;                                 // <-- Добавлено

out vec4 FragColor;

uniform sampler2D textureSampler;
uniform sampler2D normalSampler;
uniform vec2 uLightPos;

const float LIGHT_HEIGHT = 200.0;
const float LIGHT_INTENSITY = 2;
const float AMBIENT_STRENGTH = 0.7;
const float SPECULAR_STRENGTH = 0.2;
const float SHININESS = 10.0;

void main() {
    vec4 texColor = texture(textureSampler, vUV);

    // Применяем вершинный цвет к текстуре
    vec4 finalTex = texColor * vColor;

    if (finalTex.a <= 0.0) discard;

    if (texture(normalSampler, vUV).a != 0)
    {
        vec3 normalMap = texture(normalSampler, vUV).rgb;
        vec3 normal = normalize(normalMap * 2.0 - 1.0);

        vec2 rotatedNormalXY;
        rotatedNormalXY.x = normal.x *  vCos - normal.y * -vSin;
        rotatedNormalXY.y = normal.x * -vSin + normal.y *  vCos;
        normal = vec3(rotatedNormalXY, normal.z);
        normal = normalize(normal);

        vec3 lightPos3D = vec3(uLightPos, LIGHT_HEIGHT);
        vec3 lightDir = normalize(lightPos3D - vFragPos);
        vec3 viewDir = normalize(vec3(0.0, 0.0, 1.5));

        float diff = max(dot(normal, lightDir), 0.0);

        vec3 halfDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(normal, halfDir), 0.0);
        float specular = pow(specAngle, SHININESS);

        vec3 ambient = AMBIENT_STRENGTH * vec3(1.0);
        vec3 diffuse = diff * vec3(1.0);

        // Освещение применяется к уже затонированному цвету (finalTex.rgb)
        vec3 lighting = ambient + (diffuse * LIGHT_INTENSITY) + (specular * SPECULAR_STRENGTH);
        vec3 result = lighting * finalTex.rgb;

        FragColor = vec4(result, finalTex.a);
    }
    else
    {
        FragColor = finalTex;
    }
}
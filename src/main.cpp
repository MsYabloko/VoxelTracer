#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "utils/Texture.h"

const char* screenVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec3 pos;
out vec2 uv;
void main()
{
	gl_Position = vec4(pos.x, pos.y, pos.z, 1.0);
    uv = vec2(pos.x + 1, pos.y + 1) / 2;
})";
const char* screenFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
in vec2 uv;

#define CHUNK_AXIS 10

uniform vec2 uViewportSize;
uniform vec3 uUp;
uniform vec3 uDirection;
uniform vec3 uOrigin;
uniform float uFov;

uniform samplerCube skybox;
uniform sampler2D atlas;

struct Chunk
{
    ivec3 pos;
    uint[CHUNK_AXIS * CHUNK_AXIS * CHUNK_AXIS] blocks;
};

uniform Chunk uChunk;

int seed;

uint GetLocalBlock(Chunk ch, int x, int y, int z)
{
    if(x < 0 || y < 0 || z < 0) return uint(0);
    if(x >= CHUNK_AXIS || y >= CHUNK_AXIS || z >= CHUNK_AXIS) return uint(0);
    return ch.blocks[(z * CHUNK_AXIS * CHUNK_AXIS) + (y * CHUNK_AXIS) + x];
}
uint GetGlobalBlock(Chunk ch, ivec3 pos)
{
    ivec3 newPos = pos - ch.pos * CHUNK_AXIS;
    return GetLocalBlock(ch, pos.x, pos.y, pos.z);
}

#define PI 3.1415926535

vec3 GetTexture(vec2 vox_uv, vec2 pos)
{
    float step = 16.0 / 256.0;
    vec2 converted_uv;
    converted_uv.x = vox_uv.x / float(16);
    converted_uv.y = vox_uv.y / float(16);
    converted_uv.x += step * pos.x;
    converted_uv.y += step * 15;
    converted_uv.y -= step * pos.y;
    return texture(atlas, converted_uv).xyz;
}

float RandomNoise()
{
    vec2 co = uv;
    co *= seed;
    seed++;
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 RandomHemispherePoint(vec2 rand)
{
    float cosTheta = sqrt(1.0 - rand.x);
    float sinTheta = sqrt(rand.x);
    float phi = 2.0 * PI * rand.y;
    return vec3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
}

vec3 NormalOrientedHemispherePoint(vec2 rand, vec3 n)
{
    vec3 v = RandomHemispherePoint(rand);
    return dot(v, n) < 0.0 ? -v : v;
}

float Random1D()
{
    return RandomNoise();
}
vec2 Random2D()
{
    float x = RandomNoise();
    float y = RandomNoise();
    return vec2(x,y);
}
vec3 Random3D()
{
    float x = RandomNoise();
    float y = RandomNoise();
    float z = RandomNoise();
    return vec3(x,y,z);
}

vec3 GetInitDirection()
{
    vec2 texDiff = 0.5 * vec2(1.0 - 2.0 * uv.x, 2.0 * uv.y - 1.0);
    vec2 angleDiff = texDiff * vec2(uViewportSize.x / uViewportSize.y, 1.0) * tan(uFov * 0.5);

    vec3 rayDirection = normalize(vec3(angleDiff, 1.0f));

    vec3 right = normalize(cross(uUp, uDirection));
    mat3 viewToWorld = mat3(
        right,
        uUp,
        uDirection
    );

    return viewToWorld * rayDirection;
}

void getBounds(ivec3 chPos, out vec3 min, out vec3 max)
{
    min = vec3(-CHUNK_AXIS) / 2.0 + chPos * CHUNK_AXIS;
    max = vec3(CHUNK_AXIS) / 2.0 + chPos * CHUNK_AXIS;
}

bool RayChunkIntersection(ivec3 chPos, vec3 origin, vec3 direction, out float tMin, out float tMax)
{
    float tYMin, tYMax, tZMin, tZMax;

    vec3 minBound;
    vec3 maxBound;
    getBounds(chPos, minBound, maxBound);

    if(minBound.x < origin.x && minBound.y < origin.y && minBound.z < origin.z
        && maxBound.x > origin.x && maxBound.y > origin.y && maxBound.z > origin.z)
    {
        tMin = 0;
        return true;
    }

    vec3 minbmo = minBound - origin;
    vec3 maxbmo = maxBound - origin;

    vec3 inv_dir = vec3(1.0) / direction;
    if(inv_dir.x >= 0) {
        tMin = minbmo.x * inv_dir.x;
        tMax = maxbmo.x * inv_dir.x;
    } else {
        tMin = maxbmo.x * inv_dir.x;
        tMax = minbmo.x * inv_dir.x;
    }
    if(inv_dir.y >= 0) {
        tYMin = minbmo.y * inv_dir.y;
        tYMax = maxbmo.y * inv_dir.y;
    } else {
        tYMin = maxbmo.y * inv_dir.y;
        tYMax = minbmo.y * inv_dir.y;
    }
    if(tMin > tYMax || tYMin > tMax) return false;
    if(tYMin > tMin) tMin = tYMin;
    if(tYMax < tMax) tMax = tYMax;
    if(inv_dir.z >= 0) {
        tZMin = minbmo.z * inv_dir.z;
        tZMax = maxbmo.z * inv_dir.z;
    } else {
        tZMin = maxbmo.z * inv_dir.z;
        tZMax = minbmo.z * inv_dir.z;
    }
    if(tMin > tZMax || tZMin > tMax) return false;
    if(tZMin > tMin) tMin = tZMin;
    if(tZMax < tMax) tMax = tZMax;
    return (tMin > 0 && tMax > 0);
}

bool IntersectRayBox(vec3 origin, vec3 direction, vec3 boxPos, vec3 boxSize, out float fraction, out float farFrac, out vec3 normal, out vec2 uv)
{
    vec3 rd = direction;
    vec3 ro = (origin - boxPos);

    vec3 m = vec3(1.0) / rd;

    vec3 s = vec3((rd.x < 0.0) ? 1.0 : -1.0,
        (rd.y < 0.0) ? 1.0 : -1.0,
        (rd.z < 0.0) ? 1.0 : -1.0);
    vec3 t1 = m * (-ro + s * boxSize / 2);
    vec3 t2 = m * (-ro - s * boxSize / 2);

    float tN = max(max(t1.x, t1.y), t1.z);
    float tF = min(min(t2.x, t2.y), t2.z);

    if (tN > tF || tF < 0.0) return false;

    vec3 hitPoint = origin + direction * tN;
    hitPoint -= boxPos;
    if (t1.x > t1.y && t1.x > t1.z)
    {
        uv.x = (hitPoint.z / (boxSize.z / 2) + 1) / 2;
        uv.y = (hitPoint.y / (boxSize.y / 2) + 1) / 2;
        normal = vec3(s.x, 0, 0);
    }
    else if (t1.y > t1.z)
    {
        uv.x = (hitPoint.x / (boxSize.x / 2) + 1) / 2;
        uv.y = (hitPoint.z / (boxSize.z / 2) + 1) / 2;
        normal = vec3(0, s.y, 0);
    }
    else
    {
        uv.x = (hitPoint.x / (boxSize.x / 2) + 1) / 2;
        uv.y = (hitPoint.y / (boxSize.y / 2) + 1) / 2;
        normal = vec3(0, 0, s.z);
    }

    fraction = tN;
    farFrac = tF;

    return true;
}

bool TraceChunk(Chunk ch, vec3 origin, vec3 direction, out vec3 oNormal, out vec2 uv, out uint id, out float fraction, out float farFrac)
{
    float tMin, tMax;
    if(RayChunkIntersection(ch.pos, origin, direction, tMin, tMax))
    {
        tMin += 0.01;
        vec3 minbound, maxbound;
        getBounds(ch.pos, minbound, maxbound);
        vec3 start = origin + normalize(direction) * tMin;

        ivec3 startVoxel = ivec3(floor((start - minbound) / (maxbound-minbound) * CHUNK_AXIS));
        if(startVoxel.x == CHUNK_AXIS) startVoxel.x--;
        if(startVoxel.y == CHUNK_AXIS) startVoxel.y--;
        if(startVoxel.z == CHUNK_AXIS) startVoxel.z--;

        vec3 boxSize = maxbound - minbound;
        vec3 tVoxel;
        ivec3 step;
        if(direction.x >= 0)
        {
            tVoxel.x = float(startVoxel.x + 1) / float(CHUNK_AXIS);
            step.x = 1;
        }
        else
        {
            tVoxel.x = float(startVoxel.x) / float(CHUNK_AXIS);
            step.x = -1;
        }
        if(direction.y >= 0)
        {
            tVoxel.y = float(startVoxel.y + 1) / float(CHUNK_AXIS);
            step.y = 1;
        }
        else
        {
            tVoxel.y = float(startVoxel.y) / float(CHUNK_AXIS);
            step.y = -1;
        }
        if(direction.z >= 0)
        {
            tVoxel.z = float(startVoxel.z + 1) / float(CHUNK_AXIS);
            step.z = 1;
        }
        else
        {
            tVoxel.z = float(startVoxel.z) / float(CHUNK_AXIS);
            step.z = -1;
        }
        vec3 voxelMax = minbound + tVoxel * boxSize;
        vec3 tMax = (voxelMax - start) / direction;
        float voxelSize = 1;
        vec3 tDelta = voxelSize / abs(direction);
        int i = 0;
        while(startVoxel.x < CHUNK_AXIS && startVoxel.x >= 0 && startVoxel.y < CHUNK_AXIS && startVoxel.y >= 0
            && startVoxel.z < CHUNK_AXIS && startVoxel.z >= 0)
        {
            i++;
            if(i > 30) return false;
            uint get_id = GetGlobalBlock(ch, startVoxel);
            if(get_id != uint(0)
                && IntersectRayBox(origin, direction, vec3(startVoxel) - vec3(float(CHUNK_AXIS) / 2.0) + vec3(0.5), vec3(1), fraction, farFrac, oNormal, uv))
            {
                id = get_id;
                return true;
            }

            if(tMax.x <= tMax.y && tMax.x <= tMax.z)
            {
                tMax.x += tDelta.x;
                startVoxel.x += step.x;
            }
            else if(tMax.y <= tMax.z && tMax.y <= tMax.x)
            {
                tMax.y += tDelta.y;
                startVoxel.y += step.y;
            }
            else
            {
                tMax.z += tDelta.z;
                startVoxel.z += step.z;
            }
        }
    }
    return false;
}

vec3 GetBlockColor(vec3 normal, vec2 uv, uint id, out vec3 emmitance, out float roughness, out float opacity)
{
    emmitance = vec3(0);
    opacity = 1;
    if(id == uint(1))
    {
        roughness = 0;
        return GetTexture(uv, vec2(0, 1));
    }
    else if(id == uint(2))
    {
        roughness = 0.5;
        return GetTexture(uv, vec2(2, 8));
    }
    else if(id == uint(3))
    {
        roughness = 0;
        if(abs(normal.y) > 0)
        {
            return GetTexture(uv, vec2(9, 1));
        }
        else if(normal.x < 0)
        {
            vec3 tex = GetTexture(uv, vec2(11, 1));
            if(tex.r == tex.g)
            {
                roughness = 0.85;
            }
            return tex;
        }
        else
        {
            return GetTexture(uv, vec2(10, 1));
        }
    }
    else if(id == uint(4))
    {
        //emmitance = GetTexture(uv, vec2(9, 6)) * 5;
        roughness = 0;
        if(abs(normal.y) > 0)
        {
            return GetTexture(uv, vec2(14, 3));
        }
        else if(normal.z > 0)
        {
            vec3 tex = GetTexture(uv, vec2(13, 3));
            if(tex.r > tex.b)
            {
                emmitance = tex * 5;
            }
            return tex;
        }
        else
        {
            return GetTexture(uv, vec2(13, 2));
        }
    }
    else if(id == uint(5))
    {
        vec3 tex = GetTexture(uv, vec2(3, 4));
        if(tex.r == tex.b)
        {
            roughness = 0.75;
            opacity = 1;
            return tex;
        }
        roughness = 1;
        opacity = 0.3;
        return tex;
    }
    else if(id == uint(6))
    {
        roughness = 0;
        if(normal.y > 0)
        {
            vec3 tex = GetTexture(uv, vec2(10,6));
            if(tex.g > tex.r)
            {
                roughness = 1;
            }
            return tex;
        }
        else if(normal.y < 0)
        {
            return GetTexture(uv, vec2(13,6));
        }
        else
        {
            return GetTexture(uv, vec2(12,6));
        }
    }

    roughness = 0.45;
    opacity = 1;
    return vec3(1,0.25,0.6);
}

bool CastRay(vec3 origin, vec3 direction, out float oFraction, out float farFrac, out vec3 oNormal, out vec3 emmitance, out vec3 color, out float roughness, out float opacity)
{
    vec3 normal;
    vec2 uv;
    uint id;
    float fraction = 10000;

    if(TraceChunk(uChunk, origin, direction, normal, uv, id, fraction, farFrac))
    {
        color = GetBlockColor(normal, uv, id, emmitance, roughness, opacity);
        oNormal = normal;
        oFraction = fraction;
        return true;
    }
    return false;
}

float FresnelSchlick(vec3 direction, vec3 normal)
{
    float fresnel = pow(clamp(1. - dot(normal, -direction), 0., 1.), 5.);
    return clamp(fresnel, 0, 1);
}

vec3 IdealRefract(vec3 direction, vec3 normal, float nIn, float nOut)
{
    bool fromOutside = dot(normal, direction) < 0.0;
    float ratio = fromOutside ? nOut / nIn : nIn / nOut;

    vec3 refraction, reflection;
    refraction = fromOutside ? refract(direction, normal, ratio) : -refract(-direction, normal, ratio);
    reflection = reflect(direction, normal);

    return refraction == vec3(0.0) ? reflection : refraction;
}

vec3 RaycastRay(vec3 origin, vec3 direction)
{
    vec3 finalColor = vec3(1.0);
    vec3 finalLight = vec3(0);

    for(int i = 0; i < 4; i++)
    {
        float fraction;
        float farFrac;
        vec3 normal;
        vec3 emmitance;
        vec3 color;
        float roughness;
        float opacity;
        if(CastRay(origin, direction, fraction, farFrac, normal, emmitance, color, roughness, opacity))
        {
            vec3 hemisphereDistributedDirection = NormalOrientedHemispherePoint(Random2D(), normal);
            vec3 randomVec = normalize(2.0 * Random3D() - 1.0);

            //vec3 tangent = cross(randomVec, normal);
            //vec3 bitangent = cross(normal, tangent);
            //mat3 transform = mat3(tangent, bitangent, normal);

            vec3 newRayOrigin = origin + fraction * direction;
            vec3 newRayDirection = hemisphereDistributedDirection;

            vec3 idealReflection = reflect(direction, normal);
            newRayDirection = normalize(mix(newRayDirection, idealReflection, roughness  ));

            newRayOrigin += normal * 0.05;

            if(opacity < 1)
            {
                if(Random1D() < opacity)
                {
                    finalColor *= color;
                    origin = newRayOrigin;
                    direction = newRayDirection;
                    continue;
                }
                vec3 idealRefraction = IdealRefract(direction, normal, 0.9, 0.95);
                origin = origin + direction * farFrac + direction * 0.05;
                direction = idealRefraction;
                continue;
            }

            finalColor *= color;
            if(length(emmitance) > 0)
            {
                finalLight = emmitance;
                break;
            }
            origin = newRayOrigin;
            direction = newRayDirection;
        }
        else
        {
            vec3 skyColor = texture(skybox, direction).xyz;
            finalLight = skyColor;
            break;
        }
    }

    return finalColor * finalLight;
}

#define SAMPLES 32

void main()
{
    vec3 dir = GetInitDirection();
    vec3 color;
    vec3 result;
    seed = 0;
    for(int i = 0; i < SAMPLES; i++)
    {
        seed++;
        result += RaycastRay(uOrigin, dir);
    }
    result /= float(SAMPLES);
    result = pow(result, vec3(0.6));
	FragColor = vec4(result, 1);
})";

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

glm::vec3 camPos;
glm::vec3 camRot;

float lastFrame = 0;

void processInput(GLFWwindow* window)
{
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    float mult = 60;

    if(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        camRot.x += deltaTime * mult;
    if(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        camRot.x -= deltaTime * mult;
    if(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        camRot.y += deltaTime * mult;
    if(glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        camRot.y -= deltaTime * mult;

    glm::vec3 camDir;
    camDir.x = cos(glm::radians(camRot.y)) * cos(glm::radians(camRot.x));
    camDir.y = sin(glm::radians(camRot.x));
    camDir.z = sin(glm::radians(camRot.y)) * cos(glm::radians(camRot.x));

    glm::vec3 camMovDir = glm::vec3(0,0,0);
    glm::vec3 camRight = glm::normalize(glm::cross(glm::vec3(0, 1, 0), camDir));

    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camMovDir += camDir;
    if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camMovDir -= camDir;
    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camMovDir -= camRight;
    if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camMovDir += camRight;

    if(camMovDir.x != 0 || camMovDir.y != 0 || camMovDir.z != 0)
        camMovDir = glm::normalize(camMovDir);

    camPos += camMovDir * (deltaTime * 10);

    if(camRot.x >= 90) camRot.x = 89;
    if(camRot.x <= -90) camRot.x = -89;


}

void set_block(unsigned int shader, int x, int y, int z, unsigned int id)
{
    const int chunk_axis = 10;
    if(x < 0 || y < 0 || z < 0) return;
    if(x >= chunk_axis || y >= chunk_axis || z >= chunk_axis) return;
    int arr = (z * chunk_axis * chunk_axis) + (y * chunk_axis) + x;
    glUniform1ui(glGetUniformLocation(shader, (std::string("uChunk.blocks[") + std::to_string(arr) + "]").c_str() ), id);
}

int main()
{
#pragma region WindowInit
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Voxel Tracer", nullptr, nullptr);
    if(!window)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSwapInterval(true);

    if(!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress))
    {
        std::cout << "Failed to initialize OpenGL context" << std::endl;
        return -1;
    }

#pragma endregion

#pragma region ShaderLoad

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &screenVertexShaderSource, nullptr);
    glCompileShader(vertexShader);
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &screenFragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

#pragma endregion

#pragma region ScreenMeshLoad

    float vertices[] =
    {
            1,  1, 0.0f,
            1, -1, 0.0f,
            -1, -1, 0.0f,
            -1,  1, 0.0f
    };
    unsigned int indices[] =
    {
            0, 1, 3,
            1, 2, 3
    };
    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

#pragma endregion

    camRot.y = 0;
    camRot.x = 0;
    camRot.z = 0;

    camPos.x = 0;
    camPos.y = 0;
    camPos.z = 0;

    Texture skybox;
    Texture atlas;
    std::vector<std::string> faces
    {
        "data/textures/skybox/right.jpg",
        "data/textures/skybox/left.jpg",
        "data/textures/skybox/top.jpg",
        "data/textures/skybox/bottom.jpg",
        "data/textures/skybox/front.jpg",
        "data/textures/skybox/back.jpg",
    };

    static float lastFrame = 0;

    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwSetWindowTitle(window, ("Voxel Tracer " + std::to_string((int)(1 / deltaTime)) + " FPS").c_str());

        processInput(window);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        skybox.open_cubemap(faces);
        atlas.open("data/textures/atlas.png");
        glUniform1i(glGetUniformLocation(shaderProgram, "skybox"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "atlas"), 1);
        skybox.use(0);
        atlas.use(1);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glm::vec3 camDir;
        camDir.x = cos(glm::radians(camRot.y)) * cos(glm::radians(camRot.x));
        camDir.y = sin(glm::radians(camRot.x));
        camDir.z = sin(glm::radians(camRot.y)) * cos(glm::radians(camRot.x));
        glm::vec3 camRight = glm::normalize(glm::cross(glm::vec3(0, 1, 0), camDir));
        glm::vec3 camUp = glm::normalize(glm::cross(camDir, camRight));

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glUniform2f(glGetUniformLocation(shaderProgram, "uViewportSize"), width, height);
        glUniform3fv(glGetUniformLocation(shaderProgram, "uUp"), 1, glm::value_ptr(camUp));
        glUniform3fv(glGetUniformLocation(shaderProgram, "uDirection"), 1, glm::value_ptr(camDir));
        glUniform3fv(glGetUniformLocation(shaderProgram, "uOrigin"), 1, glm::value_ptr(camPos));
        glUniform1f(glGetUniformLocation(shaderProgram, "uFov"), 90);

        for(int x = 0; x < 10; x++)
        {
            for(int z = 0; z < 10; z++)
            {
                set_block(shaderProgram, x,0,z, 1);
            }
        }

        set_block(shaderProgram, 2,1,3, 2);
        set_block(shaderProgram, 7,1,5, 3);

        set_block(shaderProgram, 4,1,4, 4);

        set_block(shaderProgram, 6,1,4, 5);

        set_block(shaderProgram, 4,1,6, 6);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

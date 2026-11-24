#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "ObjLoader.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

// configuracion
static const char* MODEL_FILE = "Crash/crash.obj";
static const float MODEL_HEIGHT = 2.0f;
static const char* BACKGROUND_FILE = "Fondo/fondo.tga";
static const float DEFAULT_MOVE_SPEED = 3.4f;
static const float TURN_SPEED = 2.4f;
static const float JUMP_SPEED = 8.2f;
static const float GRAVITY = 14.0f;

// iluminacion
static const float LIGHT_POS[3]   = { 2.5f, 2.5f, 4.0f };
static const float LIGHT_COLOR[3] = { 1.0f, 1.0f, 1.0f };
static const float AMBIENT_STRENGTH  = 0.35f;
static const float DIFFUSE_STRENGTH  = 0.85f;
static const float SPECULAR_STRENGTH = 0.35f;
static const float SHININESS         = 32.0f;

// matematicas
static const float PI = 3.14159265f;

struct Vec3 { float x, y, z; };

static Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static Vec3 cross(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

static Vec3 normalize(Vec3 v) {
    float len = std::sqrt(dot(v, v));
    return { v.x / len, v.y / len, v.z / len };
}

struct Mat4 { float m[16]; };

static Mat4 identity() {
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static Mat4 translate(Vec3 p) {
    Mat4 r = identity();
    r.m[12] = p.x; r.m[13] = p.y; r.m[14] = p.z;
    return r;
}

static Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                r.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
    return r;
}

// bezier
static Vec3 bezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    float u = 1.0f - t;
    return p0 * (u*u*u) + p1 * (3*u*u*t) + p2 * (3*u*t*t) + p3 * (t*t*t);
}

static Mat4 rotateY(float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    Mat4 r = identity();
    r.m[0] = c;
    r.m[8] = s;
    r.m[2] = -s;
    r.m[10] = c;
    return r;
}

static Mat4 perspective(float fovy, float aspect, float zNear, float zFar) {
    float f = 1.0f / std::tan(fovy / 2.0f);
    Mat4 r = {};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = identity();
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8] = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

// shaders
static const char* vertexShaderSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    gl_Position = projection * view * worldPos;
    TexCoord = aTexCoord;
    Normal = mat3(model) * aNormal;
}
)";

static const char* fragmentShaderSrc = R"(
#version 330 core
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;
out vec4 FragColor;

uniform sampler2D tex;
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform float ambientStrength;
uniform float diffuseStrength;
uniform float specularStrength;
uniform float shininess;

void main()
{
    vec4 texel = texture(tex, TexCoord);
    if (texel.a < 0.5) discard;

    vec3 N = normalize(Normal);
    vec3 L = normalize(lightPos - FragPos);
    vec3 V = normalize(viewPos - FragPos);
    vec3 R = reflect(-L, N);

    float diff = max(dot(N, L), 0.0);
    float spec = (diff > 0.0) ? pow(max(dot(V, R), 0.0), shininess) : 0.0;

    vec3 ambient  = ambientStrength * lightColor;
    vec3 diffuse  = diffuseStrength * diff * lightColor;
    vec3 specular = specularStrength * spec * lightColor;

    vec3 result = (ambient + diffuse) * texel.rgb + specular;
    FragColor = vec4(min(result, vec3(1.0)), 1.0);
}
)";

// fondo
static const char* bgVertexSrc = R"(
#version 330 core
out vec2 uv;
uniform vec2 uvScale;
uniform float scrollX;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    uv = (p - 0.5) * uvScale + 0.5 + vec2(scrollX, 0.0);
}
)";

static const char* bgFragmentSrc = R"(
#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D bgTex;
uniform float brightness;
void main()
{
    FragColor = vec4(clamp(texture(bgTex, uv).rgb * brightness, 0.0, 1.0), 1.0);
}
)";

// trayectoria
static const char* lineVertexSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
void main() { gl_Position = projection * view * vec4(aPos, 1.0); }
)";
static const char* lineFragmentSrc = R"(
#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(0.20, 0.98, 0.94, 1.0); }
)";

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, log);
        std::cerr << "Error compilando shader:\n" << log << std::endl;
    }
    return shader;
}

static GLuint createProgram(const char* vsSrc = vertexShaderSrc, const char* fsSrc = fragmentShaderSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, 1024, nullptr, log);
        std::cerr << "Error enlazando programa:\n" << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// texturas
static GLuint uploadTexture(const unsigned char* bgra, int w, int h) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    glGenerateMipmap(GL_TEXTURE_2D);
    return texture;
}

static GLuint loadBackgroundTexture(const std::string& path, int& w, int& h) {
    ImageData img;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (loadTga(path, img)) {
        w = img.width;
        h = img.height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, img.bgra.data());
    } else {
        std::cerr << "Aviso: no se pudo cargar el fondo '" << path << "'" << std::endl;
        const unsigned char dark[4] = { 40, 30, 30, 255 };
        w = h = 1;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, dark);
    }
    return texture;
}

static GLuint loadTexture(const std::string& file) {
    ImageData img;
    if (!file.empty() && loadTga(file, img))
        return uploadTexture(img.bgra.data(), img.width, img.height);

    std::cerr << "Aviso: no se pudo cargar la textura '" << file << "' (se usa blanco)" << std::endl;
    const unsigned char white[4] = { 255, 255, 255, 255 };
    return uploadTexture(white, 1, 1);
}

// colisiones
struct Platform { float x0, x1, top; bool moving = false; };
struct Crate { float x0, x1, bottom, top; };

// geometria
static const char* solidVs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
void main() { gl_Position = projection * view * vec4(aPos,1.0); }
)";
static const char* solidFs = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 color;
void main() { FragColor = vec4(color,1.0); }
)";
static void appendBox(std::vector<float>& vertices, float x0,float y0,float z0,
                      float x1,float y1,float z1) {
    auto triangle = [&](Vec3 a, Vec3 b, Vec3 c) {
        vertices.insert(vertices.end(),{a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z});
    };
    Vec3 a{x0,y0,z1}, b{x1,y0,z1}, c{x1,y1,z1}, d{x0,y1,z1};
    Vec3 e{x0,y0,z0}, f{x1,y0,z0}, g{x1,y1,z0}, h{x0,y1,z0};
    triangle(a,b,c);triangle(a,c,d);
    triangle(e,g,f);triangle(e,h,g);
    triangle(e,a,d);triangle(e,d,h);
    triangle(b,f,g);triangle(b,g,c);
    triangle(d,c,g);triangle(d,g,h);
    triangle(e,f,b);triangle(e,b,a);
}

// objetos
static const char* objectVs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
out vec2 UV;
uniform mat4 view;
uniform mat4 projection;
void main() { UV=aUV; gl_Position=projection*view*vec4(aPos,1.0); }
)";
static const char* objectFs = R"(
#version 330 core
in vec2 UV;
out vec4 FragColor;
uniform sampler2D imageTex;
uniform float brightness;
void main() { FragColor=vec4(clamp(texture(imageTex,UV).rgb*brightness,0.0,1.0),1.0); }
)";
static void appendTexturedBox(std::vector<float>& v,float x0,float y0,float z0,float x1,float y1,float z1) {
    auto quad=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 d){
        auto pt=[&](Vec3 p,float u,float vv){ v.insert(v.end(),{p.x,p.y,p.z,u,vv}); };
        pt(a,0,0);pt(b,1,0);pt(c,1,1);pt(a,0,0);pt(c,1,1);pt(d,0,1);
    };
    quad({x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1});
    quad({x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0});
    quad({x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0});
    quad({x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1});
    quad({x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0});
    quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1});
}

// ventana
static int windowWidth = 1280;
static int windowHeight = 720;

static void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    windowWidth = width > 0 ? width : 1;
    windowHeight = height > 0 ? height : 1;
    glViewport(0, 0, width, height);
}

// busqueda
static bool findAndLoadModel(ObjModel& model) {
    const char* prefixes[] = { "", "../", "../../", "../../../" };
    for (const char* prefix : prefixes) {
        std::string path = std::string(prefix) + MODEL_FILE;
        if (loadObjModel(path, model, MODEL_HEIGHT)) {
            std::cout << "Modelo cargado: " << path << std::endl;
            return true;
        }
    }
    return false;
}

static std::string findFile(const char* relative) {
    const char* prefixes[] = { "", "../", "../../", "../../../" };
    for (const char* prefix : prefixes) {
        std::string path = std::string(prefix) + relative;
        std::ifstream f(path, std::ios::binary);
        if (f) return path;
    }
    return std::string(relative);
}

int main() {
    if (!glfwInit()) {
        std::cerr << "No se pudo inicializar GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight,
        "Crash Bandicoot | A/D mover | W saltar | Q/E girar | TAB panel", nullptr, nullptr);
    if (!window) {
        std::cerr << "No se pudo crear la ventana" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "No se pudo inicializar GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    // imgui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    bool panelVisible = true;
    bool previousTab = false;
    float movementSpeed = DEFAULT_MOVE_SPEED;
    float bezierSpeed = 0.25f;
    float brightness = 1.0f;
    float lightTint[3] = {1.0f,1.0f,1.0f};
    float ambient = AMBIENT_STRENGTH;
    float diffuse = DIFFUSE_STRENGTH;
    float specular = SPECULAR_STRENGTH;


    // modelo
    ObjModel model;
    if (!findAndLoadModel(model)) {
        std::cerr << "ERROR: no se encontro '" << MODEL_FILE << "'.\n"
                  << "Verifica que la carpeta Crash/ este junto a main.cpp." << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "Triangulos: " << model.vertices.size() / 24
              << " | Materiales: " << model.groups.size() << std::endl;

    // programa
    GLuint program = createProgram();

    // fondo
    GLuint bgProgram = createProgram(bgVertexSrc, bgFragmentSrc);
    GLuint bgVao;
    glGenVertexArrays(1, &bgVao);
    int bgW = 1, bgH = 1;
    GLuint bgTexture = loadBackgroundTexture(findFile(BACKGROUND_FILE), bgW, bgH);
    GLint bgScaleLoc = glGetUniformLocation(bgProgram, "uvScale");
    glUseProgram(bgProgram);
    glUniform1i(glGetUniformLocation(bgProgram, "bgTex"), 0);

    // buffers
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(float), model.vertices.data(), GL_STATIC_DRAW);

    const GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // texturas
    std::vector<GLuint> groupTextures;
    for (const MeshGroup& g : model.groups)
        groupTextures.push_back(loadTexture(g.textureFile));

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "tex"), 0);

    GLint modelLoc = glGetUniformLocation(program, "model");
    GLint viewLoc = glGetUniformLocation(program, "view");
    GLint projectionLoc = glGetUniformLocation(program, "projection");
    GLint viewPosLoc = glGetUniformLocation(program, "viewPos");

    glUniform3fv(glGetUniformLocation(program, "lightPos"), 1, LIGHT_POS);
    glUniform3fv(glGetUniformLocation(program, "lightColor"), 1, LIGHT_COLOR);
    glUniform1f(glGetUniformLocation(program, "ambientStrength"), AMBIENT_STRENGTH);
    glUniform1f(glGetUniformLocation(program, "diffuseStrength"), DIFFUSE_STRENGTH);
    glUniform1f(glGetUniformLocation(program, "specularStrength"), SPECULAR_STRENGTH);
    glUniform1f(glGetUniformLocation(program, "shininess"), SHININESS);

    // texturas
    GLuint staticTexture=loadTexture(findFile("Texturas/plataforma_estatica.tga"));
    GLuint movingTexture=loadTexture(findFile("Texturas/plataforma_movil.tga"));
    GLuint crateTexture=loadTexture(findFile("Texturas/caja_interrogacion.tga"));
    GLuint objectProgram=createProgram(objectVs,objectFs);
    GLuint objectVao,objectVbo;
    glGenVertexArrays(1,&objectVao);
    glGenBuffers(1,&objectVbo);
    glBindVertexArray(objectVao);
    glBindBuffer(GL_ARRAY_BUFFER,objectVbo);
    glBufferData(GL_ARRAY_BUFFER,8192*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glUseProgram(objectProgram);
    glUniform1i(glGetUniformLocation(objectProgram,"imageTex"),0);

    // plataformas
    std::vector<Platform> platforms = {
        {-6.65f,-4.90f,-1.14f,false},
        {-5.90f,-1.50f,-0.23f,false},
        {-2.55f, 1.45f, 1.82f,false},
        {-0.85f, 0.30f, 0.65f,false},
        { 5.10f, 6.65f, 0.25f,false},
        { 1.50f, 2.75f, 0.95f,true }
    };
    std::vector<Crate> crates = {
        {-3.65f,-2.95f,-0.23f,0.50f},
        {-0.38f, 0.25f, 1.82f,2.47f},
        { 5.85f, 6.42f, 0.25f,0.85f}
    };
    // bezier
    Vec3 control[4] = {
        {1.70f,0.95f,-0.15f},{2.45f,1.65f,-0.15f},
        {4.15f,0.65f,-0.15f},{4.70f,0.95f,-0.15f}
    };
    std::vector<float> curveVertices;
    for (int i=0;i<=120;i++) {
        Vec3 pt=bezier(control[0],control[1],control[2],control[3],i/120.f);
        curveVertices.insert(curveVertices.end(),{pt.x,pt.y+0.13f,pt.z});
    }
    GLuint lineProgram = createProgram(lineVertexSrc,lineFragmentSrc);
    GLuint lineVao,lineVbo;
    glGenVertexArrays(1,&lineVao);
    glGenBuffers(1,&lineVbo);
    glBindVertexArray(lineVao);
    glBindBuffer(GL_ARRAY_BUFFER,lineVbo);
    glBufferData(GL_ARRAY_BUFFER,curveVertices.size()*sizeof(float),curveVertices.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // geometria
    GLuint solidProgram=createProgram(solidVs,solidFs);
    GLuint solidVao,solidVbo;
    glGenVertexArrays(1,&solidVao);
    glGenBuffers(1,&solidVbo);
    glBindVertexArray(solidVao);
    glBindBuffer(GL_ARRAY_BUFFER,solidVbo);
    glBufferData(GL_ARRAY_BUFFER,4096*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    const float spawnX=-5.52f, spawnY=-1.14f;
    float playerX=spawnX, feetY=spawnY, vy=0.0f;
    float heading=0.0f, platformT=0.0f, oldMovingX=1.70f;
    int groundedIndex=0;
    bool showCurve=false, previousC=false, previousW=false, previousR=false;
    float respawnFlash=0.0f, acidBubbles=0.0f;
    const float halfWidth=0.22f, characterHeight=1.90f;
    const Vec3 cameraPosition={0.0f,1.0f,9.0f};
    float lastTime=(float)glfwGetTime();

    // bucle
    while (!glfwWindowShouldClose(window)) {
        float now = (float)glfwGetTime();
        float dt = std::min(now - lastTime, 0.05f);
        lastTime = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool tabDown=glfwGetKey(window,GLFW_KEY_TAB)==GLFW_PRESS;
        if(tabDown&&!previousTab) panelVisible=!panelVisible;
        previousTab=tabDown;
        if(panelVisible) {
            ImGui::SetNextWindowSize(ImVec2(350,0),ImGuiCond_FirstUseEver);
            ImGui::Begin("Control del nivel",&panelVisible);
            ImGui::Text("FPS: %.1f",ImGui::GetIO().Framerate);
            ImGui::Separator();
            ImGui::SliderFloat("Brillo del escenario",&brightness,0.25f,2.0f,"%.2f");
            ImGui::ColorEdit3("Color de la luz",lightTint);
            ImGui::SliderFloat("Luz ambiental",&ambient,0.0f,1.5f);
            ImGui::SliderFloat("Luz difusa",&diffuse,0.0f,2.0f);
            ImGui::SliderFloat("Luz especular",&specular,0.0f,1.5f);
            ImGui::Separator();
            ImGui::SliderFloat("Velocidad de Crash",&movementSpeed,1.0f,9.0f,"%.1f");
            ImGui::SliderFloat("Velocidad Bezier",&bezierSpeed,0.05f,1.0f,"%.2f");
            ImGui::Checkbox("Mostrar curva Bezier",&showCurve);
            ImGui::TextDisabled("A/D mover | W saltar | Q/E girar");
            ImGui::TextDisabled("R reiniciar | C curva | TAB panel");
            ImGui::End();
        }
        bool gameInput = !ImGui::GetIO().WantCaptureKeyboard;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // movimiento
        bool cDown=(gameInput && glfwGetKey(window,GLFW_KEY_C)==GLFW_PRESS);
        bool wDown=(gameInput && glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS);
        bool rDown=(gameInput && glfwGetKey(window,GLFW_KEY_R)==GLFW_PRESS);
        if(cDown&&!previousC)showCurve=!showCurve;
        previousC=cDown;
        auto respawn = [&]() {
            playerX=spawnX; feetY=spawnY; vy=0.0f; groundedIndex=0;
            respawnFlash=0.55f;
        };
        if(rDown&&!previousR) respawn();
        previousR=rDown;
        respawnFlash=std::max(0.0f,respawnFlash-dt);
        acidBubbles+=dt;

        // bezier
        platformT+=dt*bezierSpeed;
        float travel=(1.0f-std::cos(platformT*2.0f*PI))*0.5f;
        Vec3 moving=bezier(control[0],control[1],control[2],control[3],travel);
        float moveDelta=moving.x-oldMovingX;
        oldMovingX=moving.x;
        platforms.back()={moving.x-0.60f,moving.x+0.60f,moving.y,true};
        if(groundedIndex==(int)platforms.size()-1) {
            playerX+=moveDelta;
            feetY=moving.y;
        }

        float input=0.0f;
        if((gameInput && glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS))input-=1.0f;
        if((gameInput && glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS))input+=1.0f;
        float proposedX=std::max(-6.35f,std::min(6.35f,playerX+input*movementSpeed*dt));
        // colisiones
        for(const Crate& box:crates) {
            if(feetY < box.top-0.03f && feetY+characterHeight>box.bottom+0.05f) {
                if(input>0 && playerX+halfWidth<=box.x0+0.05f && proposedX+halfWidth>box.x0)
                    proposedX=box.x0-halfWidth;
                if(input<0 && playerX-halfWidth>=box.x1-0.05f && proposedX-halfWidth<box.x1)
                    proposedX=box.x1+halfWidth;
            }
        }
        playerX=proposedX;
        if((gameInput && glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS))heading+=TURN_SPEED*dt;
        if((gameInput && glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS))heading-=TURN_SPEED*dt;

        if(wDown&&!previousW && groundedIndex!=-1) {
            vy=JUMP_SPEED;
            groundedIndex=-1;
        }
        previousW=wDown;

        // gravedad
        float prevFeet=feetY;
        vy-=GRAVITY*dt;
        feetY+=vy*dt;
        int landed=-1;
        float landingTop=-999.0f;
        if(vy<=0.0f) {
            for(int i=0;i<(int)platforms.size();++i) {
                const Platform& p=platforms[i];
                if(playerX+halfWidth>p.x0 && playerX-halfWidth<p.x1 &&
                   prevFeet>=p.top-0.14f && feetY<=p.top && p.top>landingTop) {
                    landed=i;landingTop=p.top;
                }
            }
            for(const Crate& box:crates) {
                if(playerX+halfWidth>box.x0 && playerX-halfWidth<box.x1 &&
                   prevFeet>=box.top-0.12f && feetY<=box.top && box.top>landingTop) {
                    landed=-2;landingTop=box.top;
                }
            }
        }
        if(landed!=-1) {
            feetY=landingTop;vy=0.0f;groundedIndex=landed;
        } else {
            groundedIndex=-1;
        }
        // lava
        const bool touchingAcid=(playerX>-1.45f && playerX<6.55f && feetY<=-1.05f);
        if(touchingAcid || feetY<-3.6f) respawn();

        Vec3 crashPos={playerX,feetY+1.0f,0.03f};
        glClearColor(0.12f, 0.14f, 0.20f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // fondo
        {
            float winAspect = (float)windowWidth / (float)windowHeight;
            float imgAspect = (float)bgW / (float)bgH;
            float sx = 1.0f, sy = 1.0f;
            if (winAspect > imgAspect) sy = imgAspect / winAspect;
            else sx = winAspect / imgAspect;
            // fondo
            float scroll = -0.025f;
            glDisable(GL_DEPTH_TEST);
            glUseProgram(bgProgram);
            glUniform1f(glGetUniformLocation(bgProgram, "scrollX"), scroll);
            glUniform2f(bgScaleLoc, sx, sy);
            glUniform1f(glGetUniformLocation(bgProgram,"brightness"),brightness);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, bgTexture);
            glBindVertexArray(bgVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        }

        glUseProgram(program);
        glUniform3fv(glGetUniformLocation(program,"lightColor"),1,lightTint);
        glUniform1f(glGetUniformLocation(program,"ambientStrength"),ambient*brightness);
        glUniform1f(glGetUniformLocation(program,"diffuseStrength"),diffuse);
        glUniform1f(glGetUniformLocation(program,"specularStrength"),specular);

        // renderizado
        Mat4 view = lookAt(cameraPosition, {0.0f,1.0f,0.0f}, {0.0f,1.0f,0.0f});
        Mat4 projection = perspective(45.0f * PI / 180.0f, (float)windowWidth / (float)windowHeight, 0.1f, 100.0f);
        Mat4 modelMat = multiply(translate(crashPos), rotateY(heading));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, projection.m);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMat.m);
        glUniform3f(viewPosLoc, cameraPosition.x, cameraPosition.y, cameraPosition.z);

        // plataformas
        glUseProgram(objectProgram);
        glUniformMatrix4fv(glGetUniformLocation(objectProgram,"view"),1,GL_FALSE,view.m);
        glUniformMatrix4fv(glGetUniformLocation(objectProgram,"projection"),1,GL_FALSE,projection.m);
        glUniform1f(glGetUniformLocation(objectProgram,"brightness"),brightness);
        glBindVertexArray(objectVao);
        glActiveTexture(GL_TEXTURE0);
        auto texturedBox = [&](float x0,float y0,float x1,float y1,GLuint tex) {
            std::vector<float> geo;
            appendTexturedBox(geo,x0,y0,-0.35f,x1,y1,-0.18f);
            glBindBuffer(GL_ARRAY_BUFFER,objectVbo);
            glBufferSubData(GL_ARRAY_BUFFER,0,geo.size()*sizeof(float),geo.data());
            glBindTexture(GL_TEXTURE_2D,tex);
            glDrawArrays(GL_TRIANGLES,0,(GLsizei)(geo.size()/5));
        };
        for(const Platform& platform:platforms)
            texturedBox(platform.x0,platform.top-0.14f,platform.x1,platform.top+0.012f,
                        platform.moving?movingTexture:staticTexture);
        for(const Crate& crate:crates)
            texturedBox(crate.x0,crate.bottom,crate.x1,crate.top,crateTexture);
        glBindVertexArray(0);
        glUseProgram(program);

        glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE0);
        for (size_t i = 0; i < model.groups.size(); i++) {
            glBindTexture(GL_TEXTURE_2D, groupTextures[i]);
            glDrawArrays(GL_TRIANGLES, model.groups[i].firstVertex, model.groups[i].vertexCount);
        }
        glBindVertexArray(0);

        if(respawnFlash>0.0f) {
            // reinicio
            glUseProgram(solidProgram);
            glUniformMatrix4fv(glGetUniformLocation(solidProgram,"view"),1,GL_FALSE,view.m);
            glUniformMatrix4fv(glGetUniformLocation(solidProgram,"projection"),1,GL_FALSE,projection.m);
            glBindVertexArray(solidVao);
            std::vector<float> marker;
            appendBox(marker,spawnX-0.42f,spawnY-0.13f,-0.16f,spawnX+0.42f,spawnY-0.07f,-0.14f);
            glBindBuffer(GL_ARRAY_BUFFER,solidVbo);
            glBufferSubData(GL_ARRAY_BUFFER,0,marker.size()*sizeof(float),marker.data());
            glUniform3f(glGetUniformLocation(solidProgram,"color"),1.0f,0.87f,0.28f);
            glDrawArrays(GL_TRIANGLES,0,(GLsizei)(marker.size()/3));
            glBindVertexArray(0);
        }
        if (showCurve) {
            glUseProgram(lineProgram);
            glUniformMatrix4fv(glGetUniformLocation(lineProgram, "view"), 1, GL_FALSE, view.m);
            glUniformMatrix4fv(glGetUniformLocation(lineProgram, "projection"), 1, GL_FALSE, projection.m);
            glBindVertexArray(lineVao);
            glLineWidth(3.0f);
            glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(curveVertices.size()/3));
            glBindVertexArray(0);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // limpieza
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    GLuint sceneTextures[]={staticTexture,movingTexture,crateTexture};
    glDeleteTextures(3,sceneTextures);
    glDeleteBuffers(1,&objectVbo);
    glDeleteVertexArrays(1,&objectVao);
    glDeleteProgram(objectProgram);
    glDeleteBuffers(1, &solidVbo);
    glDeleteVertexArrays(1,&solidVao);
    glDeleteProgram(solidProgram);
    glDeleteBuffers(1, &lineVbo);
    glDeleteVertexArrays(1, &lineVao);
    glDeleteProgram(lineProgram);
    glDeleteTextures((GLsizei)groupTextures.size(), groupTextures.data());
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &bgTexture);
    glDeleteVertexArrays(1, &bgVao);
    glDeleteProgram(bgProgram);
    glDeleteProgram(program);

    glfwTerminate();
    return 0;
}

#pragma once
// ============================================================
//  ObjLoader.h
//  Cargador de modelos .obj (con .mtl y varios materiales) y de
//  texturas .tga. No usa librerias externas ni OpenGL.
// ============================================================
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Un "grupo" = todos los triangulos que usan el mismo material/textura
struct MeshGroup {
    std::string textureFile;   // ruta completa del .tga ("" = sin textura)
    int firstVertex = 0;
    int vertexCount = 0;
};

struct ObjModel {
    // 8 floats por vertice: x y z | u v | nx ny nz
    std::vector<float> vertices;
    std::vector<MeshGroup> groups;
};

struct ImageData {
    int width = 0, height = 0;
    std::vector<unsigned char> bgra;   // 4 bytes por pixel, fila 0 = abajo
};

// ---------------- utilidades de texto/rutas ----------------
inline std::string objDirectoryOf(const std::string& path)
{
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? std::string() : path.substr(0, p + 1);
}

inline std::string objTrim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------- MTL: material -> archivo de textura ----------------
inline std::map<std::string, std::string> loadMtl(const std::string& mtlPath, const std::string& dir)
{
    std::map<std::string, std::string> result;
    std::ifstream file(mtlPath);
    if (!file) {
        std::cerr << "Aviso: no se pudo abrir el .mtl: " << mtlPath << std::endl;
        return result;
    }
    std::string line, current;
    while (std::getline(file, line)) {
        line = objTrim(line);
        if (line.rfind("newmtl", 0) == 0) {
            current = objTrim(line.substr(6));
        }
        else if (line.rfind("map_Kd", 0) == 0 && !current.empty()) {
            std::string tex = objTrim(line.substr(6));
            result[current] = dir + tex;
        }
    }
    return result;
}

// ---------------- OBJ ----------------
struct ObjFaceIdx { int v, t, n; };

inline int objConvIndex(const std::string& s, int count)
{
    if (s.empty()) return -1;
    int i = std::atoi(s.c_str());
    if (i > 0) return i - 1;
    if (i < 0) return count + i;   // indices negativos
    return -1;
}

inline ObjFaceIdx objParseFaceToken(const std::string& tok, int pc, int tc, int nc)
{
    ObjFaceIdx r = { -1, -1, -1 };
    size_t p1 = tok.find('/');
    r.v = objConvIndex(tok.substr(0, p1), pc);
    if (p1 != std::string::npos) {
        size_t p2 = tok.find('/', p1 + 1);
        std::string b = tok.substr(p1 + 1, (p2 == std::string::npos) ? std::string::npos : p2 - p1 - 1);
        r.t = objConvIndex(b, tc);
        if (p2 != std::string::npos)
            r.n = objConvIndex(tok.substr(p2 + 1), nc);
    }
    return r;
}

// Carga el modelo, lo centra en el origen y lo escala para que mida
// 'targetHeight' unidades de alto.
inline bool loadObjModel(const std::string& objPath, ObjModel& model, float targetHeight)
{
    std::ifstream file(objPath);
    if (!file) return false;

    const std::string dir = objDirectoryOf(objPath);

    struct P3 { float x, y, z; };
    struct P2 { float x, y; };
    std::vector<P3> pos, nor;
    std::vector<P2> uv;

    std::map<std::string, std::string> materialTexture;     // nombre -> .tga
    std::vector<std::string> materialNames;                  // indice -> nombre ("" = sin material)
    std::vector<std::vector<float>> perMaterial;             // vertices por material
    std::map<std::string, int> materialIndex;

    auto getMaterial = [&](const std::string& name) -> int {
        auto it = materialIndex.find(name);
        if (it != materialIndex.end()) return it->second;
        int idx = (int)perMaterial.size();
        materialIndex[name] = idx;
        materialNames.push_back(name);
        perMaterial.emplace_back();
        return idx;
    };

    int current = getMaterial("");   // material por defecto (sin textura)

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") { P3 p; ss >> p.x >> p.y >> p.z; pos.push_back(p); }
        else if (tag == "vt") { P2 t; ss >> t.x >> t.y; uv.push_back(t); }
        else if (tag == "vn") { P3 n; ss >> n.x >> n.y >> n.z; nor.push_back(n); }
        else if (tag == "mtllib") {
            std::string name;
            std::getline(ss, name);
            name = objTrim(name);
            auto m = loadMtl(dir + name, dir);
            materialTexture.insert(m.begin(), m.end());
        }
        else if (tag == "usemtl") {
            std::string name;
            std::getline(ss, name);
            current = getMaterial(objTrim(name));
        }
        else if (tag == "f") {
            std::vector<ObjFaceIdx> face;
            std::string tok;
            while (ss >> tok)
                face.push_back(objParseFaceToken(tok, (int)pos.size(), (int)uv.size(), (int)nor.size()));

            // triangulacion en abanico (triangulos, cuadrilateros, poligonos)
            for (size_t i = 1; i + 1 < face.size(); i++) {
                ObjFaceIdx tri[3] = { face[0], face[i], face[i + 1] };
                bool ok = true;
                for (int k = 0; k < 3; k++)
                    if (tri[k].v < 0 || tri[k].v >= (int)pos.size()) ok = false;
                if (!ok) continue;

                // normal plana de respaldo (por si el OBJ no trae normales)
                P3 a = pos[tri[0].v], b = pos[tri[1].v], c = pos[tri[2].v];
                P3 e1 = { b.x - a.x, b.y - a.y, b.z - a.z };
                P3 e2 = { c.x - a.x, c.y - a.y, c.z - a.z };
                P3 fn = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
                float fl = std::sqrt(fn.x * fn.x + fn.y * fn.y + fn.z * fn.z);
                if (fl > 1e-12f) { fn.x /= fl; fn.y /= fl; fn.z /= fl; }

                for (int k = 0; k < 3; k++) {
                    P3 p = pos[tri[k].v];
                    P2 t = { 0.0f, 0.0f };
                    if (tri[k].t >= 0 && tri[k].t < (int)uv.size()) t = uv[tri[k].t];
                    P3 n = fn;
                    if (tri[k].n >= 0 && tri[k].n < (int)nor.size()) n = nor[tri[k].n];

                    std::vector<float>& out = perMaterial[current];
                    out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
                    out.push_back(t.x); out.push_back(t.y);
                    out.push_back(n.x); out.push_back(n.y); out.push_back(n.z);
                }
            }
        }
    }

    // juntar los vertices agrupados por material
    model.vertices.clear();
    model.groups.clear();
    for (size_t m = 0; m < perMaterial.size(); m++) {
        if (perMaterial[m].empty()) continue;
        MeshGroup g;
        g.firstVertex = (int)(model.vertices.size() / 8);
        g.vertexCount = (int)(perMaterial[m].size() / 8);
        auto it = materialTexture.find(materialNames[m]);
        if (it != materialTexture.end()) g.textureFile = it->second;
        model.vertices.insert(model.vertices.end(), perMaterial[m].begin(), perMaterial[m].end());
        model.groups.push_back(g);
    }

    if (model.vertices.empty()) return false;

    // centrar en el origen y escalar a targetHeight
    float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t i = 0; i < model.vertices.size(); i += 8)
        for (int k = 0; k < 3; k++) {
            mn[k] = std::min(mn[k], model.vertices[i + k]);
            mx[k] = std::max(mx[k], model.vertices[i + k]);
        }
    float height = mx[1] - mn[1];
    if (height < 1e-6f) height = std::max(mx[0] - mn[0], mx[2] - mn[2]);
    float s = targetHeight / height;
    float c[3] = { (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f };

    for (size_t i = 0; i < model.vertices.size(); i += 8)
        for (int k = 0; k < 3; k++)
            model.vertices[i + k] = (model.vertices[i + k] - c[k]) * s;

    return true;
}

// ---------------- TGA (sin comprimir, 24 o 32 bits) ----------------
inline bool loadTga(const std::string& path, ImageData& img)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    unsigned char h[18];
    f.read((char*)h, 18);
    if (!f) return false;

    int idLen = h[0];
    int type = h[2];
    int w = h[12] | (h[13] << 8);
    int hgt = h[14] | (h[15] << 8);
    int bpp = h[16];
    bool topLeft = (h[17] & 0x20) != 0;

    if (type != 2 || (bpp != 24 && bpp != 32) || w <= 0 || hgt <= 0) return false;

    f.seekg(idLen, std::ios::cur);
    int bytes = bpp / 8;
    std::vector<unsigned char> raw((size_t)w * hgt * bytes);
    f.read((char*)raw.data(), raw.size());
    if (!f) return false;

    img.width = w;
    img.height = hgt;
    img.bgra.assign((size_t)w * hgt * 4, 255);
    for (int y = 0; y < hgt; y++) {
        int dstY = topLeft ? (hgt - 1 - y) : y;   // dejamos siempre fila 0 = abajo
        for (int x = 0; x < w; x++) {
            const unsigned char* s = &raw[((size_t)y * w + x) * bytes];
            unsigned char* d = &img.bgra[((size_t)dstY * w + x) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
            d[3] = (bytes == 4) ? s[3] : 255;
        }
    }
    return true;
}

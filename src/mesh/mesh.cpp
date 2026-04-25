#include "mesh/mesh.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <map>
#include <set>
#include <algorithm>

namespace rf {

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------
void Mesh::rebuild_gpu()
{
    // Build triangle list from half-edge faces
    std::vector<float> buf;
    triCount = 0;

    // Compute smooth vertex normals if needed
    std::vector<glm::vec3> vertNormals;
    if (shadeSmooth) {
        vertNormals.resize(verts.size(), glm::vec3(0));
        for (auto& f : faces) {
            int start = f.edge;
            int cur = start;
            do {
                int vi = hedges[cur].vertex;
                vertNormals[vi] += f.normal;
                cur = hedges[cur].next;
            } while (cur != start);
        }
        for (auto& n : vertNormals) {
            float len = glm::length(n);
            if (len > 0.0001f) n /= len;
        }
    }

    for (int fi = 0; fi < (int)faces.size(); fi++) {
        auto& f = faces[fi];
        // Collect face verts by walking the half-edge loop
        std::vector<int> faceVerts;
        int start = f.edge;
        int cur = start;
        do {
            faceVerts.push_back(hedges[cur].vertex);
            cur = hedges[cur].next;
        } while (cur != start && (int)faceVerts.size() < 64);

        // Fan triangulate
        for (int i = 1; i + 1 < (int)faceVerts.size(); i++) {
            int vi[3] = { faceVerts[0], faceVerts[i], faceVerts[i+1] };
            for (int k = 0; k < 3; k++) {
                auto& v = verts[vi[k]];
                glm::vec3 n = shadeSmooth ? vertNormals[vi[k]] : f.normal;
                buf.push_back(v.pos.x);
                buf.push_back(v.pos.y);
                buf.push_back(v.pos.z);
                buf.push_back(n.x);
                buf.push_back(n.y);
                buf.push_back(n.z);
                buf.push_back(v.uv.x);
                buf.push_back(v.uv.y);
            }
            triCount++;
        }
    }

    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);

    // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // uv
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Build wireframe edge buffer (actual edges, not triangulated)
    std::vector<float> wireBuf;
    for (auto& e : edges) {
        int he = e.he;
        int va = hedges[hedges[he].prev].vertex;
        int vb = hedges[he].vertex;
        wireBuf.push_back(verts[va].pos.x);
        wireBuf.push_back(verts[va].pos.y);
        wireBuf.push_back(verts[va].pos.z);
        wireBuf.push_back(verts[vb].pos.x);
        wireBuf.push_back(verts[vb].pos.y);
        wireBuf.push_back(verts[vb].pos.z);
    }
    wireLineCount = (int)edges.size();

    if (!wireVao) {
        glGenVertexArrays(1, &wireVao);
        glGenBuffers(1, &wireVbo);
    }
    glBindVertexArray(wireVao);
    glBindBuffer(GL_ARRAY_BUFFER, wireVbo);
    glBufferData(GL_ARRAY_BUFFER, wireBuf.size() * sizeof(float), wireBuf.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Normals
// ---------------------------------------------------------------------------
void Mesh::recalc_normals()
{
    for (auto& f : faces) {
        std::vector<int> fv;
        int start = f.edge;
        int cur = start;
        do {
            fv.push_back(hedges[cur].vertex);
            cur = hedges[cur].next;
        } while (cur != start && (int)fv.size() < 64);

        if (fv.size() >= 3) {
            glm::vec3 a = verts[fv[1]].pos - verts[fv[0]].pos;
            glm::vec3 b = verts[fv[2]].pos - verts[fv[0]].pos;
            f.normal = glm::normalize(glm::cross(a, b));
        }
    }
}

// ---------------------------------------------------------------------------
// Helper: build a quad face and append half-edges
// ---------------------------------------------------------------------------
static void add_quad(Mesh& m, int v0, int v1, int v2, int v3)
{
    int fi = (int)m.faces.size();
    int base = (int)m.hedges.size();
    int vi[4] = {v0, v1, v2, v3};

    for (int i = 0; i < 4; i++) {
        HEdge he;
        he.vertex = vi[(i + 1) % 4]; // destination
        he.face = fi;
        he.next = base + (i + 1) % 4;
        he.prev = base + (i + 3) % 4;
        he.twin = -1;
        m.hedges.push_back(he);
        m.verts[vi[i]].edge = base + i;
    }

    Face face;
    face.edge = base;
    face.selected = false;
    face.normal = {0, 0, 0};
    m.faces.push_back(face);
}

static void link_twins(Mesh& m)
{
    // For each half-edge, source = vertex of the previous half-edge's destination
    // i.e. source(he) = hedges[he.prev].vertex, dest(he) = he.vertex
    // Twin: edge going in opposite direction (dest->source matches source->dest)
    for (int i = 0; i < (int)m.hedges.size(); i++) {
        if (m.hedges[i].twin >= 0) continue;
        int iTo = m.hedges[i].vertex;
        int iFrom = m.hedges[m.hedges[i].prev].vertex;
        for (int j = i + 1; j < (int)m.hedges.size(); j++) {
            if (m.hedges[j].twin >= 0) continue;
            int jTo = m.hedges[j].vertex;
            int jFrom = m.hedges[m.hedges[j].prev].vertex;
            if (iFrom == jTo && iTo == jFrom) {
                m.hedges[i].twin = j;
                m.hedges[j].twin = i;
                break;
            }
        }
    }

    // Build edge list
    std::vector<bool> visited(m.hedges.size(), false);
    for (int i = 0; i < (int)m.hedges.size(); i++) {
        if (visited[i]) continue;
        visited[i] = true;
        if (m.hedges[i].twin >= 0) visited[m.hedges[i].twin] = true;
        m.edges.push_back({i, false});
    }
}

// ---------------------------------------------------------------------------
// Cube primitive
// ---------------------------------------------------------------------------
Mesh Mesh::create_cube(float s)
{
    Mesh m;
    m.name = "Cube";

    float h = s * 0.5f;
    glm::vec3 p[8] = {
        {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h},
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},
    };
    for (int i = 0; i < 8; i++)
        m.verts.push_back({p[i], {0,0}, -1, false});

    // 6 quad faces (CCW winding when viewed from outside)
    add_quad(m, 0, 3, 2, 1); // front (-Z)
    add_quad(m, 4, 5, 6, 7); // back  (+Z)
    add_quad(m, 0, 1, 5, 4); // bottom
    add_quad(m, 2, 3, 7, 6); // top
    add_quad(m, 0, 4, 7, 3); // left
    add_quad(m, 1, 2, 6, 5); // right

    link_twins(m);
    m.recalc_normals();
    m.rebuild_gpu();
    return m;
}

// ---------------------------------------------------------------------------
// Plane primitive
// ---------------------------------------------------------------------------
Mesh Mesh::create_plane(float s)
{
    Mesh m;
    m.name = "Plane";

    float h = s * 0.5f;
    m.verts.push_back({{-h, 0, -h}, {0,0}, -1, false});
    m.verts.push_back({{ h, 0, -h}, {1,0}, -1, false});
    m.verts.push_back({{ h, 0,  h}, {1,1}, -1, false});
    m.verts.push_back({{-h, 0,  h}, {0,1}, -1, false});

    add_quad(m, 0, 1, 2, 3);
    link_twins(m);
    m.recalc_normals();
    m.rebuild_gpu();
    return m;
}

// ---------------------------------------------------------------------------
// OBJ export
// ---------------------------------------------------------------------------
bool Mesh::export_obj(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "# Reflow OBJ Export\n";
    f << "o " << name << "\n";

    // Vertices
    for (auto& v : verts)
        f << "v " << v.pos.x << " " << v.pos.y << " " << v.pos.z << "\n";

    // UVs
    for (auto& v : verts)
        f << "vt " << v.uv.x << " " << v.uv.y << "\n";

    // Normals
    for (auto& face : faces)
        f << "vn " << face.normal.x << " " << face.normal.y << " " << face.normal.z << "\n";

    // Faces
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        std::vector<int> fv;
        int start = faces[fi].edge;
        int cur = start;
        do {
            fv.push_back(hedges[cur].vertex);
            cur = hedges[cur].next;
        } while (cur != start && (int)fv.size() < 64);

        f << "f";
        for (int vi : fv)
            f << " " << (vi+1) << "/" << (vi+1) << "/" << (fi+1);
        f << "\n";
    }

    return true;
}

// ---------------------------------------------------------------------------
// Ray-triangle intersection (Moller-Trumbore)
// ---------------------------------------------------------------------------
static bool ray_tri(const glm::vec3& o, const glm::vec3& d,
                    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                    float& t)
{
    glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
    glm::vec3 h = glm::cross(d, e2);
    float a = glm::dot(e1, h);
    if (a > -1e-6f && a < 1e-6f) return false;
    float f = 1.0f / a;
    glm::vec3 s = o - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, e1);
    float v = f * glm::dot(d, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * glm::dot(e2, q);
    return t > 1e-6f;
}

// Distance from point to ray (closest approach)
static float point_ray_dist(const glm::vec3& p, const glm::vec3& rayO, const glm::vec3& rayD)
{
    glm::vec3 v = p - rayO;
    float t = glm::dot(v, rayD);
    if (t < 0) return glm::length(v);
    glm::vec3 closest = rayO + rayD * t;
    return glm::length(p - closest);
}

// Distance from line segment to ray
static float seg_ray_dist(const glm::vec3& a, const glm::vec3& b,
                           const glm::vec3& rayO, const glm::vec3& rayD)
{
    // Closest approach between two lines
    glm::vec3 u = b - a;
    glm::vec3 w = a - rayO;
    float uu = glm::dot(u, u);
    float ud = glm::dot(u, rayD);
    float dd = glm::dot(rayD, rayD);
    float uw = glm::dot(u, w);
    float dw = glm::dot(rayD, w);
    float denom = uu * dd - ud * ud;
    if (denom < 1e-8f) {
        // Parallel — distance from point a to ray
        return point_ray_dist(a, rayO, rayD);
    }
    float s = (ud * dw - dd * uw) / denom;
    float t = (uu * dw - ud * uw) / denom;
    s = glm::clamp(s, 0.0f, 1.0f);
    if (t < 0) t = 0;
    glm::vec3 pa = a + u * s;
    glm::vec3 pb = rayO + rayD * t;
    return glm::length(pa - pb);
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------
int Mesh::pick_vertex(const glm::vec3& rayO, const glm::vec3& rayD, float threshold) const
{
    int best = -1;
    float bestDist = threshold;
    for (int i = 0; i < (int)verts.size(); i++) {
        float d = point_ray_dist(verts[i].pos + position, rayO, rayD);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

int Mesh::pick_edge(const glm::vec3& rayO, const glm::vec3& rayD, float threshold) const
{
    int best = -1;
    float bestDist = threshold;
    for (int i = 0; i < (int)edges.size(); i++) {
        int he = edges[i].he;
        int va = hedges[hedges[he].prev].vertex;
        int vb = hedges[he].vertex;
        float d = seg_ray_dist(verts[va].pos + position, verts[vb].pos + position, rayO, rayD);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

int Mesh::pick_face(const glm::vec3& rayO, const glm::vec3& rayD) const
{
    int best = -1;
    float bestT = 1e30f;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        // Collect face verts
        std::vector<int> fv;
        int start = faces[fi].edge;
        int cur = start;
        do {
            fv.push_back(hedges[cur].vertex);
            cur = hedges[cur].next;
        } while (cur != start && (int)fv.size() < 64);

        // Fan-triangulate and test
        for (int i = 1; i + 1 < (int)fv.size(); i++) {
            float t;
            if (ray_tri(rayO, rayD,
                        verts[fv[0]].pos + position,
                        verts[fv[i]].pos + position,
                        verts[fv[i+1]].pos + position, t)) {
                if (t < bestT) {
                    bestT = t;
                    best = fi;
                }
            }
        }
    }
    return best;
}

void Mesh::deselect_all()
{
    for (auto& v : verts) v.selected = false;
    for (auto& e : edges) e.selected = false;
    for (auto& f : faces) f.selected = false;
}

// ---------------------------------------------------------------------------
// Selection queries
// ---------------------------------------------------------------------------
std::vector<int> Mesh::get_selected_vert_indices() const
{
    std::vector<int> r;
    for (int i = 0; i < (int)verts.size(); i++)
        if (verts[i].selected) r.push_back(i);
    return r;
}

glm::vec3 Mesh::get_selection_center() const
{
    glm::vec3 sum(0);
    int count = 0;
    for (auto& v : verts) {
        if (v.selected) { sum += v.pos; count++; }
    }
    return count > 0 ? (sum / (float)count + position) : position;
}

glm::vec3 Mesh::get_selected_face_normal() const
{
    glm::vec3 avg(0);
    int count = 0;
    for (auto& f : faces) {
        if (f.selected) { avg += f.normal; count++; }
    }
    return count > 0 ? glm::normalize(avg) : glm::vec3(0, 1, 0);
}

int Mesh::count_selected_faces() const
{
    int c = 0;
    for (auto& f : faces) if (f.selected) c++;
    return c;
}

int Mesh::count_selected_verts() const
{
    int c = 0;
    for (auto& v : verts) if (v.selected) c++;
    return c;
}

int Mesh::count_selected_edges() const
{
    int c = 0;
    for (auto& e : edges) if (e.selected) c++;
    return c;
}

// ---------------------------------------------------------------------------
// Translate selected vertices
// ---------------------------------------------------------------------------
void Mesh::translate_selected(const glm::vec3& delta)
{
    for (auto& v : verts)
        if (v.selected) v.pos += delta;
    recalc_normals();
    rebuild_gpu();
}

// ---------------------------------------------------------------------------
// Extrude selected faces
// ---------------------------------------------------------------------------
void Mesh::extrude_selected_faces()
{
    std::vector<int> selFaces;
    for (int i = 0; i < (int)faces.size(); i++)
        if (faces[i].selected) selFaces.push_back(i);
    if (selFaces.empty()) return;

    // Collect all face vertex loops
    struct FLoop { std::vector<int> v; bool sel; };
    std::vector<FLoop> allLoops;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        FLoop fl;
        fl.sel = faces[fi].selected;
        int start = faces[fi].edge;
        int cur = start;
        do {
            fl.v.push_back(hedges[cur].vertex);
            cur = hedges[cur].next;
        } while (cur != start && (int)fl.v.size() < 64);
        allLoops.push_back(fl);
    }

    // Duplicate verts used by selected faces
    std::map<int, int> dupMap;
    for (auto& fl : allLoops) {
        if (!fl.sel) continue;
        for (int vi : fl.v) {
            if (dupMap.find(vi) == dupMap.end()) {
                int newVi = (int)verts.size();
                MeshVertex nv = verts[vi];
                nv.selected = true;
                nv.edge = -1;
                verts.push_back(nv);
                dupMap[vi] = newVi;
            }
        }
    }

    // Deselect old verts
    for (auto& [old, nw] : dupMap)
        verts[old].selected = false;

    // Build set of selected face indices for edge-sharing check
    std::set<int> selSet(selFaces.begin(), selFaces.end());

    // Build new face loops
    std::vector<FLoop> newLoops;
    for (int fi = 0; fi < (int)allLoops.size(); fi++) {
        auto& fl = allLoops[fi];
        if (fl.sel) {
            // Top face with new verts
            FLoop top;
            top.sel = true;
            for (int vi : fl.v) top.v.push_back(dupMap[vi]);
            newLoops.push_back(top);

            // Side faces for boundary edges
            for (int i = 0; i < (int)fl.v.size(); i++) {
                int a = fl.v[i];
                int b = fl.v[(i + 1) % fl.v.size()];

                // Check if edge a→b is shared with another selected face
                bool shared = false;
                for (int fi2 : selFaces) {
                    if (fi2 == fi) continue;
                    auto& fl2 = allLoops[fi2];
                    for (int j = 0; j < (int)fl2.v.size(); j++) {
                        if (fl2.v[j] == b && fl2.v[(j + 1) % fl2.v.size()] == a) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared) break;
                }

                if (!shared) {
                    FLoop side;
                    side.sel = false;
                    side.v = {a, b, dupMap[b], dupMap[a]};
                    newLoops.push_back(side);
                }
            }
        } else {
            newLoops.push_back(fl);
        }
    }

    // Rebuild topology from face loops
    hedges.clear();
    faces.clear();
    edges.clear();
    for (auto& v : verts) v.edge = -1;

    for (auto& fl : newLoops) {
        int fi = (int)faces.size();
        int base = (int)hedges.size();
        int n = (int)fl.v.size();

        for (int i = 0; i < n; i++) {
            HEdge he;
            he.vertex = fl.v[(i + 1) % n];
            he.face = fi;
            he.next = base + (i + 1) % n;
            he.prev = base + (i + n - 1) % n;
            he.twin = -1;
            hedges.push_back(he);
            verts[fl.v[i]].edge = base + i;
        }

        Face face;
        face.edge = base;
        face.selected = fl.sel;
        face.normal = {0, 0, 0};
        faces.push_back(face);
    }

    // Link twins
    for (int i = 0; i < (int)hedges.size(); i++) {
        if (hedges[i].twin >= 0) continue;
        int iTo = hedges[i].vertex;
        int iFrom = hedges[hedges[i].prev].vertex;
        for (int j = i + 1; j < (int)hedges.size(); j++) {
            if (hedges[j].twin >= 0) continue;
            if (hedges[hedges[j].prev].vertex == iTo && hedges[j].vertex == iFrom) {
                hedges[i].twin = j;
                hedges[j].twin = i;
                break;
            }
        }
    }

    // Build edge list
    std::vector<bool> visited(hedges.size(), false);
    for (int i = 0; i < (int)hedges.size(); i++) {
        if (visited[i]) continue;
        visited[i] = true;
        if (hedges[i].twin >= 0) visited[hedges[i].twin] = true;
        edges.push_back({i, false});
    }

    recalc_normals();
    rebuild_gpu();
}

// ---------------------------------------------------------------------------
// Delete selected elements
// ---------------------------------------------------------------------------
void Mesh::delete_selected()
{
    // Collect face vertex loops for faces to keep
    struct FLoop { std::vector<int> v; };
    std::vector<FLoop> keepLoops;

    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (faces[fi].selected) continue;
        // Also skip faces that reference any selected vertex
        bool hasSelVert = false;
        int start = faces[fi].edge;
        int cur = start;
        std::vector<int> fv;
        do {
            int vi = hedges[cur].vertex;
            fv.push_back(vi);
            if (verts[vi].selected) hasSelVert = true;
            cur = hedges[cur].next;
        } while (cur != start && (int)fv.size() < 64);

        if (!hasSelVert) {
            keepLoops.push_back({fv});
        }
    }

    if ((int)keepLoops.size() == (int)faces.size()) return;

    // Find used verts and remap
    std::set<int> usedSet;
    for (auto& fl : keepLoops)
        for (int vi : fl.v) usedSet.insert(vi);

    std::map<int, int> remap;
    std::vector<MeshVertex> newVerts;
    for (int vi : usedSet) {
        remap[vi] = (int)newVerts.size();
        MeshVertex nv = verts[vi];
        nv.selected = false;
        nv.edge = -1;
        newVerts.push_back(nv);
    }

    verts = newVerts;
    hedges.clear();
    faces.clear();
    edges.clear();

    for (auto& fl : keepLoops) {
        int fi = (int)faces.size();
        int base = (int)hedges.size();
        int n = (int)fl.v.size();

        for (int i = 0; i < n; i++) {
            HEdge he;
            he.vertex = remap[fl.v[(i + 1) % n]];
            he.face = fi;
            he.next = base + (i + 1) % n;
            he.prev = base + (i + n - 1) % n;
            he.twin = -1;
            hedges.push_back(he);
            verts[remap[fl.v[i]]].edge = base + i;
        }

        Face face;
        face.edge = base;
        face.selected = false;
        face.normal = {0, 0, 0};
        faces.push_back(face);
    }

    // Link twins
    for (int i = 0; i < (int)hedges.size(); i++) {
        if (hedges[i].twin >= 0) continue;
        int iTo = hedges[i].vertex;
        int iFrom = hedges[hedges[i].prev].vertex;
        for (int j = i + 1; j < (int)hedges.size(); j++) {
            if (hedges[j].twin >= 0) continue;
            if (hedges[hedges[j].prev].vertex == iTo && hedges[j].vertex == iFrom) {
                hedges[i].twin = j;
                hedges[j].twin = i;
                break;
            }
        }
    }

    std::vector<bool> visited(hedges.size(), false);
    for (int i = 0; i < (int)hedges.size(); i++) {
        if (visited[i]) continue;
        visited[i] = true;
        if (hedges[i].twin >= 0) visited[hedges[i].twin] = true;
        edges.push_back({i, false});
    }

    recalc_normals();
    rebuild_gpu();
}

// ---------------------------------------------------------------------------
// Project save
// ---------------------------------------------------------------------------
bool save_project(const std::string& path, const std::vector<Mesh>& meshes)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "RFLW 1\n";
    f << "meshes " << meshes.size() << "\n";

    for (auto& m : meshes) {
        f << "mesh " << m.name << "\n";
        f << "position " << m.position.x << " " << m.position.y << " " << m.position.z << "\n";

        f << "vertices " << m.verts.size() << "\n";
        for (auto& v : m.verts)
            f << "v " << v.pos.x << " " << v.pos.y << " " << v.pos.z
              << " " << v.uv.x << " " << v.uv.y << "\n";

        // Collect face vertex loops
        f << "faces " << m.faces.size() << "\n";
        for (auto& face : m.faces) {
            std::vector<int> fv;
            int start = face.edge;
            int cur = start;
            do {
                fv.push_back(m.hedges[cur].vertex);
                cur = m.hedges[cur].next;
            } while (cur != start && (int)fv.size() < 64);

            f << "f " << fv.size();
            for (int vi : fv) f << " " << vi;
            f << "\n";
        }

        f << "endmesh\n";
    }

    return true;
}

// ---------------------------------------------------------------------------
// Project load
// ---------------------------------------------------------------------------
bool load_project(const std::string& path, std::vector<Mesh>& meshes)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    // Header
    if (!std::getline(f, line)) return false;
    if (line.substr(0, 4) != "RFLW") return false;

    int meshCount = 0;
    if (!std::getline(f, line)) return false;
    std::istringstream(line) >> line >> meshCount;

    meshes.clear();

    for (int mi = 0; mi < meshCount; mi++) {
        Mesh m;

        // "mesh Name"
        if (!std::getline(f, line)) return false;
        m.name = line.substr(5);

        // "position x y z"
        if (!std::getline(f, line)) return false;
        std::istringstream(line) >> line >> m.position.x >> m.position.y >> m.position.z;

        // "vertices N"
        int vertCount = 0;
        if (!std::getline(f, line)) return false;
        std::istringstream(line) >> line >> vertCount;

        for (int i = 0; i < vertCount; i++) {
            if (!std::getline(f, line)) return false;
            MeshVertex v;
            v.edge = -1;
            v.selected = false;
            std::string tag;
            std::istringstream(line) >> tag >> v.pos.x >> v.pos.y >> v.pos.z >> v.uv.x >> v.uv.y;
            m.verts.push_back(v);
        }

        // "faces N"
        int faceCount = 0;
        if (!std::getline(f, line)) return false;
        std::istringstream(line) >> line >> faceCount;

        // Read face vertex loops and build half-edge structure
        struct FLoop { std::vector<int> v; };
        std::vector<FLoop> faceLoops;

        for (int i = 0; i < faceCount; i++) {
            if (!std::getline(f, line)) return false;
            std::istringstream iss(line);
            std::string tag;
            int n;
            iss >> tag >> n;
            FLoop fl;
            for (int j = 0; j < n; j++) {
                int vi;
                iss >> vi;
                fl.v.push_back(vi);
            }
            faceLoops.push_back(fl);
        }

        // Build half-edge topology
        for (auto& fl : faceLoops) {
            int fi = (int)m.faces.size();
            int base = (int)m.hedges.size();
            int n = (int)fl.v.size();

            for (int i = 0; i < n; i++) {
                HEdge he;
                he.vertex = fl.v[(i + 1) % n];
                he.face = fi;
                he.next = base + (i + 1) % n;
                he.prev = base + (i + n - 1) % n;
                he.twin = -1;
                m.hedges.push_back(he);
                m.verts[fl.v[i]].edge = base + i;
            }

            Face face;
            face.edge = base;
            face.selected = false;
            face.normal = {0, 0, 0};
            m.faces.push_back(face);
        }

        // Link twins
        for (int i = 0; i < (int)m.hedges.size(); i++) {
            if (m.hedges[i].twin >= 0) continue;
            int iTo = m.hedges[i].vertex;
            int iFrom = m.hedges[m.hedges[i].prev].vertex;
            for (int j = i + 1; j < (int)m.hedges.size(); j++) {
                if (m.hedges[j].twin >= 0) continue;
                if (m.hedges[m.hedges[j].prev].vertex == iTo && m.hedges[j].vertex == iFrom) {
                    m.hedges[i].twin = j;
                    m.hedges[j].twin = i;
                    break;
                }
            }
        }

        // Build edge list
        std::vector<bool> visited(m.hedges.size(), false);
        for (int i = 0; i < (int)m.hedges.size(); i++) {
            if (visited[i]) continue;
            visited[i] = true;
            if (m.hedges[i].twin >= 0) visited[m.hedges[i].twin] = true;
            m.edges.push_back({i, false});
        }

        // "endmesh"
        if (!std::getline(f, line)) return false;

        m.recalc_normals();
        m.rebuild_gpu();
        meshes.push_back(std::move(m));
    }

    return true;
}

} // namespace rf

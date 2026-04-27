#include "mesh/mesh.h"
#include "ui/ui.h"
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

static void add_tri(Mesh& m, int v0, int v1, int v2)
{
    int fi = (int)m.faces.size();
    int base = (int)m.hedges.size();
    int vi[3] = {v0, v1, v2};

    for (int i = 0; i < 3; i++) {
        HEdge he;
        he.vertex = vi[(i + 1) % 3];
        he.face = fi;
        he.next = base + (i + 1) % 3;
        he.prev = base + (i + 2) % 3;
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
// Sphere primitive
// ---------------------------------------------------------------------------
Mesh Mesh::create_sphere(int rings, int segments, float radius)
{
    Mesh m;
    m.name = "Sphere";
    m.shadeSmooth = true;

    // Top pole
    m.verts.push_back({{0, radius, 0}, {0.5f, 1.0f}, -1, false});

    // Ring vertices
    for (int r = 1; r < rings; r++) {
        float phi = 3.14159265f * (float)r / (float)rings;
        float y = radius * cosf(phi);
        float ringR = radius * sinf(phi);
        for (int s = 0; s < segments; s++) {
            float theta = 2.0f * 3.14159265f * (float)s / (float)segments;
            float x = ringR * sinf(theta);
            float z = ringR * cosf(theta);
            float u = (float)s / (float)segments;
            float v = 1.0f - (float)r / (float)rings;
            m.verts.push_back({{x, y, z}, {u, v}, -1, false});
        }
    }

    // Bottom pole
    int bottomPole = (int)m.verts.size();
    m.verts.push_back({{0, -radius, 0}, {0.5f, 0.0f}, -1, false});

    // Top cap triangles (pole to first ring)
    for (int s = 0; s < segments; s++) {
        int next = (s + 1) % segments;
        add_tri(m, 0, 1 + s, 1 + next);
    }

    // Middle quads
    for (int r = 0; r < rings - 2; r++) {
        int ringStart = 1 + r * segments;
        int nextRingStart = 1 + (r + 1) * segments;
        for (int s = 0; s < segments; s++) {
            int next = (s + 1) % segments;
            add_quad(m, ringStart + s, nextRingStart + s, nextRingStart + next, ringStart + next);
        }
    }

    // Bottom cap triangles (last ring to pole)
    int lastRingStart = 1 + (rings - 2) * segments;
    for (int s = 0; s < segments; s++) {
        int next = (s + 1) % segments;
        add_tri(m, bottomPole, lastRingStart + next, lastRingStart + s);
    }

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
// Edge loop finding (for loop cut)
// ---------------------------------------------------------------------------
std::vector<int> Mesh::find_edge_loop(int edgeIdx) const
{
    // Returns list of edge indices that form a loop perpendicular to the given edge.
    // Walks across quad faces: from an edge, find the opposite edge in the face, cross twin, repeat.
    std::vector<int> loop;
    if (edgeIdx < 0 || edgeIdx >= (int)edges.size()) return loop;

    // Helper: given a half-edge entering a face, find the "opposite" half-edge
    // For quads (4 sides): skip 2 half-edges forward from the given he
    // For non-quads: stop (can't do clean loop cut)
    auto get_opposite_he = [&](int he) -> int {
        int faceIdx = hedges[he].face;
        if (faceIdx < 0) return -1;
        // Count face sides
        int count = 0;
        int cur = faces[faceIdx].edge;
        int start = cur;
        do { count++; cur = hedges[cur].next; } while (cur != start && count < 64);
        if (count != 4) return -1; // only works for quads
        // Opposite = 2 edges forward
        return hedges[hedges[he].next].next;
    };

    // Walk in one direction from the starting edge
    auto walk = [&](int startHe, std::vector<int>& result) {
        int he = startHe;
        std::set<int> visited;
        while (true) {
            int oppHe = get_opposite_he(he);
            if (oppHe < 0) break;
            // Find which edge this opposite half-edge belongs to
            int oppEdge = -1;
            for (int ei = 0; ei < (int)edges.size(); ei++) {
                int eHe = edges[ei].he;
                if (eHe == oppHe || (hedges[eHe].twin >= 0 && hedges[eHe].twin == oppHe)) {
                    oppEdge = ei;
                    break;
                }
                if (hedges[oppHe].twin >= 0 && hedges[oppHe].twin == eHe) {
                    oppEdge = ei;
                    break;
                }
            }
            if (oppEdge < 0) break;
            if (visited.count(oppEdge)) break; // completed loop
            if (oppEdge == edgeIdx) break; // back to start
            visited.insert(oppEdge);
            result.push_back(oppEdge);
            // Cross to twin and continue
            int twin = hedges[oppHe].twin;
            if (twin < 0) break;
            he = twin;
        }
    };

    loop.push_back(edgeIdx);

    // Walk from one half-edge direction
    int he0 = edges[edgeIdx].he;
    int twin0 = hedges[he0].twin;

    std::vector<int> fwd, bwd;
    walk(he0, fwd);
    if (twin0 >= 0)
        walk(twin0, bwd);

    // Combine: reverse bwd + start + fwd
    std::reverse(bwd.begin(), bwd.end());
    std::vector<int> combined;
    for (int e : bwd) combined.push_back(e);
    combined.push_back(edgeIdx);
    for (int e : fwd) combined.push_back(e);

    // Check if it actually loops (fwd reached back to start or bwd)
    // Remove duplicates
    std::set<int> seen;
    loop.clear();
    for (int e : combined) {
        if (seen.insert(e).second) loop.push_back(e);
    }

    return loop;
}

// ---------------------------------------------------------------------------
// Edge ring (loop containing the edge, for selection)
// ---------------------------------------------------------------------------
std::vector<int> Mesh::find_edge_ring(int edgeIdx) const
{
    // Walks edges end-to-end through quad faces.
    // From half-edge he (A→B), continuation at B = he.next.twin.next
    std::vector<int> ring;
    if (edgeIdx < 0 || edgeIdx >= (int)edges.size()) return ring;

    // Map half-edge to edge index
    std::map<int, int> heToEdge;
    for (int ei = 0; ei < (int)edges.size(); ei++) {
        heToEdge[edges[ei].he] = ei;
        int tw = hedges[edges[ei].he].twin;
        if (tw >= 0) heToEdge[tw] = ei;
    }

    // Walk in one direction from a half-edge
    auto walk = [&](int he, std::vector<int>& result) {
        std::set<int> visited;
        visited.insert(edgeIdx);
        while (true) {
            // Continuation: he.next.twin.next
            int next = hedges[he].next;
            // Check the face is a quad
            int f = hedges[he].face;
            if (f < 0) break;
            int count = 0;
            int cur = faces[f].edge;
            int s = cur;
            do { count++; cur = hedges[cur].next; } while (cur != s && count < 64);
            if (count != 4) break;

            int twin = hedges[next].twin;
            if (twin < 0) break;
            int cont = hedges[twin].next;

            auto it = heToEdge.find(cont);
            if (it == heToEdge.end()) break;
            int ei = it->second;
            if (visited.count(ei)) break;
            visited.insert(ei);
            result.push_back(ei);
            he = cont;
        }
    };

    ring.push_back(edgeIdx);

    int he0 = edges[edgeIdx].he;
    int twin0 = hedges[he0].twin;

    std::vector<int> fwd, bwd;
    walk(he0, fwd);
    if (twin0 >= 0)
        walk(twin0, bwd);

    std::reverse(bwd.begin(), bwd.end());
    std::vector<int> combined;
    for (int e : bwd) combined.push_back(e);
    combined.push_back(edgeIdx);
    for (int e : fwd) combined.push_back(e);

    std::set<int> seen;
    ring.clear();
    for (int e : combined) {
        if (seen.insert(e).second) ring.push_back(e);
    }

    return ring;
}

// ---------------------------------------------------------------------------
// Loop cut
// ---------------------------------------------------------------------------
std::vector<Mesh::SlideVert> Mesh::loop_cut(int edgeIdx, int numCuts)
{
    auto loopEdges = find_edge_loop(edgeIdx);
    std::vector<SlideVert> slideResult;
    if (loopEdges.empty() || numCuts < 1) return slideResult;

    // Map half-edge index -> edge index
    std::map<int, int> heToEdge;
    for (int ei = 0; ei < (int)edges.size(); ei++) {
        heToEdge[edges[ei].he] = ei;
        int tw = hedges[edges[ei].he].twin;
        if (tw >= 0) heToEdge[tw] = ei;
    }
    std::set<int> loopSet(loopEdges.begin(), loopEdges.end());

    // Orient all edges consistently (src = side A, dst = side B)
    struct EdgeDir { int src, dst; };
    std::map<int, EdgeDir> edgeDir;

    // First edge: pick canonical direction
    {
        int he = edges[loopEdges[0]].he;
        edgeDir[loopEdges[0]] = { hedges[hedges[he].prev].vertex, hedges[he].vertex };
    }

    // Walk the loop and orient each subsequent edge using shared face connectivity
    for (int li = 0; li < (int)loopEdges.size(); li++) {
        int eiCur = loopEdges[li];
        int eiNext = loopEdges[(li + 1) % loopEdges.size()];
        if (edgeDir.count(eiNext)) continue;

        int heCur = edges[eiCur].he;
        int heNext = edges[eiNext].he;
        int twinCur = hedges[heCur].twin;
        int twinNext = hedges[heNext].twin;

        bool found = false;
        for (int hc : {heCur, twinCur}) {
            if (hc < 0 || found) continue;
            for (int hn : {heNext, twinNext}) {
                if (hn < 0 || found) continue;
                if (hedges[hc].face >= 0 && hedges[hc].face == hedges[hn].face) {
                    int srcC = hedges[hedges[hc].prev].vertex;
                    int srcN = hedges[hedges[hn].prev].vertex;
                    int dstN = hedges[hn].vertex;
                    // In quad: srcC connects to dstN (same side), dstC connects to srcN (same side)
                    auto& dc = edgeDir[eiCur];
                    if (dc.src == srcC)
                        edgeDir[eiNext] = { dstN, srcN };
                    else
                        edgeDir[eiNext] = { srcN, dstN };
                    found = true;
                }
            }
        }
        if (!found) {
            int he = edges[eiNext].he;
            edgeDir[eiNext] = { hedges[hedges[he].prev].vertex, hedges[he].vertex };
        }
    }

    // Create new vertices on each loop edge using consistent direction
    std::map<int, std::vector<int>> edgeNewVerts;

    for (int ei : loopEdges) {
        auto& d = edgeDir[ei];
        std::vector<int> nv;
        for (int c = 1; c <= numCuts; c++) {
            float t = (float)c / (float)(numCuts + 1);
            MeshVertex mv;
            mv.pos = glm::mix(verts[d.src].pos, verts[d.dst].pos, t);
            mv.uv = glm::mix(verts[d.src].uv, verts[d.dst].uv, t);
            mv.edge = -1;
            mv.selected = false;
            int idx = (int)verts.size();
            verts.push_back(mv);
            nv.push_back(idx);
            slideResult.push_back({idx, verts[d.src].pos, verts[d.dst].pos, t});
        }
        edgeNewVerts[ei] = nv;
    }

    // Get new verts ordered from vertex 'from' toward vertex 'to'
    auto getOrdered = [&](int ei, int from, int to) -> std::vector<int> {
        auto& nv = edgeNewVerts[ei];
        auto& d = edgeDir[ei];
        if (d.src == from) return nv;
        return std::vector<int>(nv.rbegin(), nv.rend());
    };

    // Collect all face vertex loops (for non-cut faces to keep as-is)
    struct FLoop { std::vector<int> v; };
    std::vector<FLoop> newFaces;

    for (int fi = 0; fi < (int)faces.size(); fi++) {
        // Walk this face's half-edges
        std::vector<int> faceHEs;
        int start = faces[fi].edge;
        int cur = start;
        do { faceHEs.push_back(cur); cur = hedges[cur].next; } while (cur != start);

        if (faceHEs.size() != 4) {
            // Non-quad: keep as-is
            FLoop fl;
            for (int he : faceHEs) fl.v.push_back(hedges[he].vertex);
            newFaces.push_back(fl);
            continue;
        }

        // Find which half-edges are cut edges
        int cutHE0 = -1, cutHE1 = -1;
        for (int i = 0; i < 4; i++) {
            auto it = heToEdge.find(faceHEs[i]);
            if (it != heToEdge.end() && loopSet.count(it->second)) {
                if (cutHE0 < 0) cutHE0 = i;
                else cutHE1 = i;
            }
        }

        if (cutHE0 < 0 || cutHE1 < 0) {
            // Not a cut face: keep as-is
            FLoop fl;
            for (int he : faceHEs) fl.v.push_back(hedges[he].vertex);
            newFaces.push_back(fl);
            continue;
        }

        // We have a quad with 2 cut half-edges at positions cutHE0 and cutHE1
        // Each half-edge goes src→dst. Get actual vertex indices.
        int heA = faceHEs[cutHE0];
        int srcA = hedges[hedges[heA].prev].vertex;
        int dstA = hedges[heA].vertex;
        int eiA = heToEdge[heA];

        int heB = faceHEs[cutHE1];
        int srcB = hedges[hedges[heB].prev].vertex;
        int dstB = hedges[heB].vertex;
        int eiB = heToEdge[heB];

        // Rail A: srcA → new verts → dstA
        std::vector<int> railA;
        railA.push_back(srcA);
        for (int v : getOrdered(eiA, srcA, dstA)) railA.push_back(v);
        railA.push_back(dstA);

        // Rail B: srcB → new verts → dstB
        // But we need rail B to go PARALLEL to rail A.
        // In the face loop, the two cut edges go in opposite directions.
        // dstA connects to srcB (via non-cut edges), so rail B reversed is parallel to rail A.
        // Rail B reversed: dstB → reversed new verts → srcB
        std::vector<int> railB;
        railB.push_back(dstB);
        for (int v : getOrdered(eiB, dstB, srcB)) railB.push_back(v);
        railB.push_back(srcB);

        // Generate strip quads
        for (int i = 0; i <= numCuts; i++) {
            FLoop q;
            q.v = { railA[i], railA[i+1], railB[i+1], railB[i] };
            newFaces.push_back(q);
        }
    }

    // Rebuild topology from face loops
    hedges.clear();
    faces.clear();
    edges.clear();
    for (auto& v : verts) v.edge = -1;

    for (auto& fl : newFaces) {
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
        Face f;
        f.edge = base;
        f.selected = false;
        f.normal = {0, 0, 0};
        faces.push_back(f);
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
    return slideResult;
}

// ---------------------------------------------------------------------------
// Slide vertices along their edge rails
// ---------------------------------------------------------------------------
void Mesh::slide_verts(const std::vector<SlideVert>& slideData, float offset)
{
    for (auto& sv : slideData) {
        if (sv.vertIdx < 0 || sv.vertIdx >= (int)verts.size()) continue;
        float t = glm::clamp(sv.defaultT + offset, 0.001f, 0.999f);
        verts[sv.vertIdx].pos = glm::mix(sv.posA, sv.posB, t);
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
// Triangulate selected faces
// ---------------------------------------------------------------------------
void Mesh::triangulate_selected_faces(TriMode mode)
{
    struct FLoop { std::vector<int> v; bool sel; };
    std::vector<FLoop> newFaces;
    int quadIdx = 0;

    for (int fi = 0; fi < (int)faces.size(); fi++) {
        std::vector<int> fv;
        int start = faces[fi].edge;
        int cur = start;
        do { fv.push_back(hedges[cur].vertex); cur = hedges[cur].next; }
        while (cur != start && (int)fv.size() < 64);

        if (faces[fi].selected && (int)fv.size() > 3) {
            if ((int)fv.size() == 4) {
                bool useDiag02;
                if (mode == TriMode::Fixed) {
                    useDiag02 = true;
                } else {
                    // Alternate: flip diagonal every other quad
                    useDiag02 = (quadIdx % 2 == 0);
                }
                quadIdx++;

                if (useDiag02) {
                    newFaces.push_back({{fv[0], fv[1], fv[2]}, true});
                    newFaces.push_back({{fv[0], fv[2], fv[3]}, true});
                } else {
                    newFaces.push_back({{fv[1], fv[2], fv[3]}, true});
                    newFaces.push_back({{fv[1], fv[3], fv[0]}, true});
                }
            } else {
                // Ngon: fan triangulate from first vertex
                for (int i = 1; i + 1 < (int)fv.size(); i++)
                    newFaces.push_back({{fv[0], fv[i], fv[i+1]}, true});
            }
        } else {
            newFaces.push_back({fv, faces[fi].selected});
        }
    }

    // Rebuild topology
    hedges.clear();
    faces.clear();
    edges.clear();
    for (auto& v : verts) v.edge = -1;

    for (auto& fl : newFaces) {
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
        Face f;
        f.edge = base;
        f.selected = fl.sel;
        f.normal = {0, 0, 0};
        faces.push_back(f);
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
// Untriangulate (tris to quads) selected faces
// ---------------------------------------------------------------------------
void Mesh::untriangulate_selected_faces()
{
    // Find pairs of adjacent selected triangles that can merge into a quad
    std::vector<bool> merged(faces.size(), false);

    struct FLoop { std::vector<int> v; bool sel; };
    std::vector<FLoop> newFaces;

    // For each pair of selected triangles sharing an edge, try to merge
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (merged[fi]) continue;
        if (!faces[fi].selected) {
            // Keep non-selected faces as-is
            std::vector<int> fv;
            int start = faces[fi].edge;
            int cur = start;
            do { fv.push_back(hedges[cur].vertex); cur = hedges[cur].next; }
            while (cur != start && (int)fv.size() < 64);
            newFaces.push_back({fv, false});
            continue;
        }

        // Check if this is a triangle
        std::vector<int> fv;
        int start = faces[fi].edge;
        int cur = start;
        do { fv.push_back(hedges[cur].vertex); cur = hedges[cur].next; }
        while (cur != start && (int)fv.size() < 64);

        if ((int)fv.size() != 3) {
            newFaces.push_back({fv, true});
            continue;
        }

        // Find best adjacent selected triangle to merge with
        int bestFj = -1;
        int bestSharedHe = -1;
        float bestScore = -1e30f;

        cur = start;
        do {
            int tw = hedges[cur].twin;
            if (tw >= 0) {
                int fj = hedges[tw].face;
                if (fj >= 0 && fj != fi && !merged[fj] && faces[fj].selected) {
                    // Check fj is also a triangle
                    int cnt = 0;
                    int s2 = faces[fj].edge;
                    int c2 = s2;
                    do { cnt++; c2 = hedges[c2].next; } while (c2 != s2 && cnt < 64);
                    if (cnt == 3) {
                        // Score: prefer merging where the resulting quad is most planar/rectangular
                        // Use the dot product of the two triangle normals as score
                        float score = glm::dot(faces[fi].normal, faces[fj].normal);
                        if (score > bestScore) {
                            bestScore = score;
                            bestFj = fj;
                            bestSharedHe = cur;
                        }
                    }
                }
            }
            cur = hedges[cur].next;
        } while (cur != start);

        if (bestFj >= 0 && bestScore > 0.0f) {
            merged[fi] = true;
            merged[bestFj] = true;

            // Build quad: walk around both triangles skipping the shared edge
            // Shared edge: bestSharedHe (in fi) and its twin (in bestFj)
            int tw = hedges[bestSharedHe].twin;

            // From fi: the two vertices NOT on the shared edge's destination
            // Walk fi skipping bestSharedHe
            std::vector<int> quad;
            cur = hedges[bestSharedHe].next;
            while (cur != bestSharedHe) {
                quad.push_back(hedges[hedges[cur].prev].vertex);
                cur = hedges[cur].next;
            }
            // From fj: walk skipping tw
            cur = hedges[tw].next;
            while (cur != tw) {
                quad.push_back(hedges[hedges[cur].prev].vertex);
                cur = hedges[cur].next;
            }

            newFaces.push_back({quad, true});
        } else {
            newFaces.push_back({fv, true});
        }
    }

    // Rebuild topology
    hedges.clear();
    faces.clear();
    edges.clear();
    for (auto& v : verts) v.edge = -1;

    for (auto& fl : newFaces) {
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
        Face f;
        f.edge = base;
        f.selected = fl.sel;
        f.normal = {0, 0, 0};
        faces.push_back(f);
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
// Merge selected faces into one polygon
// ---------------------------------------------------------------------------
void Mesh::merge_selected_faces()
{
    // Find boundary half-edges of the selected face region:
    // A boundary half-edge is one whose twin's face is NOT selected (or has no twin).
    std::set<int> selFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++)
        if (faces[fi].selected) selFaces.insert(fi);
    if (selFaces.size() < 2) return;

    // Collect boundary half-edges (in selected faces, whose opposite is not selected)
    std::vector<int> boundaryHEs;
    for (int fi : selFaces) {
        int start = faces[fi].edge;
        int cur = start;
        do {
            bool isBoundary = true;
            int tw = hedges[cur].twin;
            if (tw >= 0 && selFaces.count(hedges[tw].face))
                isBoundary = false;
            if (isBoundary)
                boundaryHEs.push_back(cur);
            cur = hedges[cur].next;
        } while (cur != start);
    }

    if (boundaryHEs.empty()) return;

    // Build a map: for each boundary he, destination vertex -> he index
    // so we can chain them: the next boundary he starts where the previous one ends
    std::map<int, int> srcToHE; // source vertex -> boundary he starting there
    for (int he : boundaryHEs) {
        int src = hedges[hedges[he].prev].vertex;
        srcToHE[src] = he;
    }

    // Walk the boundary loop starting from the first boundary he
    std::vector<int> mergedLoop;
    int startHE = boundaryHEs[0];
    int curHE = startHE;
    std::set<int> visited;
    do {
        int dst = hedges[curHE].vertex;
        mergedLoop.push_back(dst);
        visited.insert(curHE);
        // Find next boundary he starting from dst
        auto it = srcToHE.find(dst);
        if (it == srcToHE.end()) break;
        curHE = it->second;
    } while (curHE != startHE && !visited.count(curHE) && (int)mergedLoop.size() < 1000);

    if ((int)mergedLoop.size() < 3) return;

    // Rebuild: keep unselected faces, replace selected with merged polygon
    struct FLoop { std::vector<int> v; bool sel; };
    std::vector<FLoop> newFaces;

    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (selFaces.count(fi)) continue;
        std::vector<int> fv;
        int start = faces[fi].edge;
        int cur = start;
        do { fv.push_back(hedges[cur].vertex); cur = hedges[cur].next; }
        while (cur != start && (int)fv.size() < 64);
        newFaces.push_back({fv, false});
    }
    newFaces.push_back({mergedLoop, true});

    // Rebuild topology
    hedges.clear();
    faces.clear();
    edges.clear();
    for (auto& v : verts) v.edge = -1;

    for (auto& fl : newFaces) {
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
        Face f;
        f.edge = base;
        f.selected = fl.sel;
        f.normal = {0, 0, 0};
        faces.push_back(f);
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

    std::vector<bool> visited2(hedges.size(), false);
    for (int i = 0; i < (int)hedges.size(); i++) {
        if (visited2[i]) continue;
        visited2[i] = true;
        if (hedges[i].twin >= 0) visited2[hedges[i].twin] = true;
        edges.push_back({i, false});
    }

    recalc_normals();
    rebuild_gpu();
}

// ---------------------------------------------------------------------------
// Project save
// ---------------------------------------------------------------------------
bool save_project(const std::string& path, const std::vector<Mesh>& meshes, const UIState* ui)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "RFLW 2\n";
    f << "meshes " << meshes.size() << "\n";

    for (auto& m : meshes) {
        f << "mesh " << m.name << "\n";
        f << "position " << m.position.x << " " << m.position.y << " " << m.position.z << "\n";
        f << "shadeSmooth " << (m.shadeSmooth ? 1 : 0) << "\n";

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

    // Material / viewport settings
    if (ui) {
        f << "material\n";
        f << "viewMode " << (int)ui->viewMode << "\n";
        f << "lightAngleX " << ui->lightAngleX << "\n";
        f << "lightAngleY " << ui->lightAngleY << "\n";
        f << "lightFollowCam " << (ui->lightFollowCam ? 1 : 0) << "\n";
        f << "unlit " << (ui->unlit ? 1 : 0) << "\n";
        f << "toon " << (ui->toon ? 1 : 0) << "\n";
        f << "fresnel " << (ui->fresnel ? 1 : 0) << "\n";
        f << "specular " << (ui->specular ? 1 : 0) << "\n";
        f << "specRoughness " << ui->specRoughness << "\n";
        f << "rampInterp " << (int)ui->rampInterp << "\n";
        f << "rampStops " << ui->rampStops.size() << "\n";
        for (auto& s : ui->rampStops)
            f << "rs " << s.first << " " << s.second << "\n";
        f << "specRampInterp " << (int)ui->specRampInterp << "\n";
        f << "specRampStops " << ui->specRampStops.size() << "\n";
        for (auto& s : ui->specRampStops)
            f << "srs " << s.first << " " << s.second << "\n";
        f << "endmaterial\n";
    }

    return true;
}

// ---------------------------------------------------------------------------
// Project load
// ---------------------------------------------------------------------------
bool load_project(const std::string& path, std::vector<Mesh>& meshes, UIState* ui)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    // Header
    if (!std::getline(f, line)) return false;
    if (line.substr(0, 4) != "RFLW") return false;
    int version = 1;
    { std::string tag; std::istringstream(line) >> tag >> version; }

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

        // v2: "shadeSmooth 0/1"
        if (version >= 2) {
            if (!std::getline(f, line)) return false;
            int ss = 0;
            std::istringstream(line) >> line >> ss;
            m.shadeSmooth = (ss != 0);
        }

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

    // Material / viewport settings (v2+)
    if (version >= 2 && ui && std::getline(f, line)) {
        if (line == "material") {
            while (std::getline(f, line)) {
                if (line == "endmaterial") break;
                std::istringstream iss(line);
                std::string key;
                iss >> key;
                if (key == "viewMode") { int v; iss >> v; ui->viewMode = (ViewMode)v; }
                else if (key == "lightAngleX") { iss >> ui->lightAngleX; }
                else if (key == "lightAngleY") { iss >> ui->lightAngleY; }
                else if (key == "lightFollowCam") { int v; iss >> v; ui->lightFollowCam = (v != 0); }
                else if (key == "unlit") { int v; iss >> v; ui->unlit = (v != 0); }
                else if (key == "toon") { int v; iss >> v; ui->toon = (v != 0); }
                else if (key == "fresnel") { int v; iss >> v; ui->fresnel = (v != 0); }
                else if (key == "specular") { int v; iss >> v; ui->specular = (v != 0); }
                else if (key == "specRoughness") { iss >> ui->specRoughness; }
                else if (key == "rampInterp") { int v; iss >> v; ui->rampInterp = (UIState::RampInterp)v; }
                else if (key == "rampStops") {
                    int count; iss >> count;
                    ui->rampStops.clear();
                    for (int i = 0; i < count; i++) {
                        if (!std::getline(f, line)) break;
                        std::istringstream rss(line);
                        std::string tag; float pos, val;
                        rss >> tag >> pos >> val;
                        ui->rampStops.push_back({pos, val});
                    }
                }
                else if (key == "specRampInterp") { int v; iss >> v; ui->specRampInterp = (UIState::RampInterp)v; }
                else if (key == "specRampStops") {
                    int count; iss >> count;
                    ui->specRampStops.clear();
                    for (int i = 0; i < count; i++) {
                        if (!std::getline(f, line)) break;
                        std::istringstream rss(line);
                        std::string tag; float pos, val;
                        rss >> tag >> pos >> val;
                        ui->specRampStops.push_back({pos, val});
                    }
                }
            }
        }
    }

    return true;
}

} // namespace rf

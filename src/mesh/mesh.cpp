#include "mesh/mesh.h"
#include <fstream>
#include <cmath>

namespace rf {

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------
void Mesh::rebuild_gpu()
{
    // Build triangle list from half-edge faces
    std::vector<float> buf;
    triCount = 0;

    for (auto& f : faces) {
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
                buf.push_back(v.pos.x);
                buf.push_back(v.pos.y);
                buf.push_back(v.pos.z);
                buf.push_back(f.normal.x);
                buf.push_back(f.normal.y);
                buf.push_back(f.normal.z);
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

} // namespace rf

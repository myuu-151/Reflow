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
// Cube primitive
// ---------------------------------------------------------------------------
Mesh Mesh::create_cube(float s)
{
    Mesh m;
    m.name = "Cube";

    float h = s * 0.5f;
    // 8 vertices of a cube
    glm::vec3 p[8] = {
        {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h},
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},
    };
    for (int i = 0; i < 8; i++) {
        m.verts.push_back({p[i], {0,0}, -1, false});
    }

    // 6 faces (quads), defined by vertex indices (CCW winding)
    int faceIdx[6][4] = {
        {0, 3, 2, 1}, // front (-Z)
        {4, 5, 6, 7}, // back  (+Z)
        {0, 1, 5, 4}, // bottom
        {2, 3, 7, 6}, // top
        {0, 4, 7, 3}, // left
        {1, 2, 6, 5}, // right
    };

    int heIdx = 0;
    for (int fi = 0; fi < 6; fi++) {
        Face face;
        face.edge = heIdx;
        face.selected = false;

        // Create 4 half-edges for this quad
        for (int ei = 0; ei < 4; ei++) {
            HEdge he;
            he.vertex = faceIdx[fi][(ei + 1) % 4];
            he.face = fi;
            he.next = heIdx + (ei + 1) % 4;
            he.prev = heIdx + (ei + 3) % 4;
            he.twin = -1;
            m.hedges.push_back(he);

            // Point vertex to an outgoing edge
            m.verts[faceIdx[fi][ei]].edge = heIdx + ei;
            heIdx++;
        }
        m.faces.push_back(face);
    }

    // Link twins: find matching half-edge pairs
    for (int i = 0; i < (int)m.hedges.size(); i++) {
        if (m.hedges[i].twin >= 0) continue;
        int srcV = m.hedges[m.hedges[i].prev].vertex; // source of edge i = dest of prev
        // Actually source = vertex that prev points to? No.
        // Half-edge i goes from verts[hedges[prev].vertex] to verts[hedges[i].vertex]
        int fromV = m.hedges[m.hedges[i].prev].vertex;
        // Wait, that's wrong for how we set it up. Let me reconsider.
        // hedges[i].vertex = destination. Source = the vertex at the start of this half-edge.
        // In our setup: face quad [v0,v1,v2,v3], half-edge 0: v0->v1, he.vertex=v1
        // So source of he[i] is faceIdx[fi][ei], dest is faceIdx[fi][(ei+1)%4]
        // With prev: hedges[prev].vertex = source of current half-edge? No.
        // hedges[prev] goes from some vertex to the source of hedges[i].
        // hedges[prev].vertex = destination of prev = source of current.
        // So: fromV = hedges[hedges[i].prev].vertex... wait that's circular.
        // Let me just use: source(i) = hedges[prev(i)].vertex ... no.
        // Actually for the cube: he[0] has vertex = faceIdx[0][1], he[0].prev = he[3]
        // he[3].vertex = faceIdx[0][0]. So hedges[prev].vertex IS the source. But that
        // means fromV above is hedges[hedges[i].prev].vertex which points to the source.
        // Hmm, no. he[3] goes from v3 to v0, so he[3].vertex = v0 = faceIdx[0][0].
        // And he[0] goes from v0 to v1. Source of he[0] = v0.
        // hedges[he[0].prev].vertex = he[3].vertex = v0. Yes! That's the source.

        int iFrom = m.hedges[m.hedges[i].prev].vertex;
        int iTo   = m.hedges[i].vertex;

        for (int j = i + 1; j < (int)m.hedges.size(); j++) {
            if (m.hedges[j].twin >= 0) continue;
            int jFrom = m.hedges[m.hedges[j].prev].vertex;
            int jTo   = m.hedges[j].vertex;
            if (iFrom == jTo && iTo == jFrom) {
                m.hedges[i].twin = j;
                m.hedges[j].twin = i;
                break;
            }
        }
    }

    // Build edge list from half-edge pairs
    std::vector<bool> visited(m.hedges.size(), false);
    for (int i = 0; i < (int)m.hedges.size(); i++) {
        if (visited[i]) continue;
        visited[i] = true;
        if (m.hedges[i].twin >= 0) visited[m.hedges[i].twin] = true;
        m.edges.push_back({i, false});
    }

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

    // One quad face
    for (int i = 0; i < 4; i++) {
        HEdge he;
        he.vertex = (i + 1) % 4;
        he.face = 0;
        he.next = (i + 1) % 4;
        he.prev = (i + 3) % 4;
        he.twin = -1;
        m.hedges.push_back(he);
        m.verts[i].edge = i;
    }
    m.faces.push_back({0, {0,1,0}, false});
    m.edges.push_back({0, false});
    m.edges.push_back({1, false});
    m.edges.push_back({2, false});
    m.edges.push_back({3, false});

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

#pragma once

#include "core/reflow.h"
#include <vector>
#include <string>

namespace rf {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Half-edge mesh for editing operations
struct HEdge {
    int vertex;     // destination vertex index
    int face;       // face this half-edge belongs to (-1 = boundary)
    int next;       // next half-edge in face loop
    int prev;       // previous half-edge in face loop
    int twin;       // opposite half-edge (-1 = boundary)
};

struct Face {
    int edge;       // one half-edge of this face
    glm::vec3 normal;
    bool selected;
};

struct MeshVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    int edge;       // one outgoing half-edge
    bool selected;
};

struct Edge {
    int he;         // one of the two half-edges
    bool selected;
};

struct Mesh {
    std::string name;
    std::vector<MeshVertex> verts;
    std::vector<HEdge>      hedges;
    std::vector<Face>        faces;
    std::vector<Edge>        edges;

    // Mirror mode
    bool mirrorX = false;

    // Transform
    glm::vec3 position = {0, 0, 0};
    glm::vec3 rotation = {0, 0, 0};
    glm::vec3 scale    = {1, 1, 1};

    // GPU buffers (rebuilt on edit)
    GLuint vao = 0, vbo = 0, ebo = 0;
    int triCount = 0;
    GLuint wireVao = 0, wireVbo = 0;
    int wireLineCount = 0;

    void rebuild_gpu();
    void recalc_normals();

    // Primitives
    static Mesh create_cube(float size = 1.0f);
    static Mesh create_plane(float size = 1.0f);
    static Mesh create_cylinder(int segments = 8, float radius = 0.5f, float height = 1.0f);
    static Mesh create_sphere(int rings = 4, int segments = 8, float radius = 0.5f);
    static Mesh create_cone(int segments = 8, float radius = 0.5f, float height = 1.0f);

    // Export
    bool export_obj(const std::string& path) const;

    // Picking (returns -1 if nothing hit)
    int pick_vertex(const glm::vec3& rayO, const glm::vec3& rayD, float threshold = 0.08f) const;
    int pick_edge(const glm::vec3& rayO, const glm::vec3& rayD, float threshold = 0.05f) const;
    int pick_face(const glm::vec3& rayO, const glm::vec3& rayD) const;
    void deselect_all();

    // Edit operations
    void extrude_selected_faces();
    void delete_selected();
    void translate_selected(const glm::vec3& delta);
    std::vector<int> get_selected_vert_indices() const;
    glm::vec3 get_selection_center() const;
    glm::vec3 get_selected_face_normal() const;
    int count_selected_faces() const;
    int count_selected_verts() const;
    int count_selected_edges() const;
};

} // namespace rf

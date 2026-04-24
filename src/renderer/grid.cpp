#include "renderer/grid.h"
#include <vector>

namespace rf {

void Grid::init(float size, float step)
{
    std::vector<float> verts;
    for (float x = -size; x <= size; x += step) {
        verts.insert(verts.end(), {x, 0, -size});
        verts.insert(verts.end(), {x, 0,  size});
    }
    for (float z = -size; z <= size; z += step) {
        verts.insert(verts.end(), {-size, 0, z});
        verts.insert(verts.end(), { size, 0, z});
    }
    vertCount = (int)verts.size() / 3;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Grid::draw() const
{
    glBindVertexArray(vao);
    glDrawArrays(GL_LINES, 0, vertCount);
    glBindVertexArray(0);
}

} // namespace rf

#pragma once

#include "core/reflow.h"

namespace rf {

struct Grid {
    GLuint vao = 0, vbo = 0;
    int vertCount = 0;

    void init(float size = 10.0f, float step = 1.0f);
    void draw() const;
};

} // namespace rf

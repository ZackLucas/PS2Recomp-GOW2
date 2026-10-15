// Uploads the generated GS addressing tables and binds them where gs_vram_tables.glsl expects them.
#pragma once
#include "gl_context.h"
#include "gs_vram_tables.h"

namespace gl
{
inline GLuint bindGsTables()
{
    GLuint buf = 0;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(kGsVramTables), kGsVramTables, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, buf);
    return buf;
}
} // namespace gl

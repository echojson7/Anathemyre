#include "gl.h"

#include <string.h>

#include "../core/log.h"
#include "../platform/platform.h"

#define GL_DEFINE(ret, name, args) PFN_gl##name gl##name;
GL_FUNCS(GL_DEFINE)
GL_FUNCS_OPT(GL_DEFINE)
#undef GL_DEFINE

bool gl_load(void)
{
    bool ok = true;
#define GL_LOAD(ret, name, args) \
    gl##name = (PFN_gl##name)platform_gl_proc("gl" #name); \
    if (!gl##name) { log_error("GL: нет функции gl" #name); ok = false; }
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
#define GL_LOAD_OPT(ret, name, args) gl##name = (PFN_gl##name)platform_gl_proc("gl" #name);
    GL_FUNCS_OPT(GL_LOAD_OPT)
#undef GL_LOAD_OPT
    return ok;
}

bool gl_has_extension(const char *name)
{
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && strcmp(e, name) == 0) return true;
    }
    return false;
}

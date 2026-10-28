#include "gl.h"

#include "../core/log.h"
#include "../platform/platform.h"

#define GL_DEFINE(ret, name, args) PFN_gl##name gl##name;
GL_FUNCS(GL_DEFINE)
#undef GL_DEFINE

bool gl_load(void)
{
    bool ok = true;
#define GL_LOAD(ret, name, args) \
    gl##name = (PFN_gl##name)platform_gl_proc("gl" #name); \
    if (!gl##name) { log_error("GL: нет функции gl" #name); ok = false; }
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
    return ok;
}

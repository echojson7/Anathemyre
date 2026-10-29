// Собственные объявления OpenGL 4.5 core: только то, что используем
#ifndef ANA_GL_H
#define ANA_GL_H

#include "../core/types.h"

#if defined(_WIN32)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int          GLint;
typedef int          GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef float        GLfloat;
typedef char         GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t    GLintptr;
typedef ptrdiff_t    GLsizeiptr;
typedef uint64_t     GLuint64;

typedef void (GLAPIENTRY *GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity,
                                       GLsizei length, const GLchar *message, const void *user);

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_RGBA8 0x8058
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_RGBA16F 0x881A
#define GL_R32F 0x822E
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER 0x8D40
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#define GL_DEBUG_OUTPUT 0x92E0
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define GL_TIMESTAMP 0x8E28

// X-макрос: тип возврата, имя, аргументы
#define GL_FUNCS(X) \
    X(void, Clear, (GLbitfield mask)) \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, Enable, (GLenum cap)) \
    X(void, Disable, (GLenum cap)) \
    X(void, CullFace, (GLenum mode)) \
    X(void, DepthFunc, (GLenum func)) \
    X(void, PolygonOffset, (GLfloat factor, GLfloat units)) \
    X(const GLubyte *, GetString, (GLenum name)) \
    X(GLenum, GetError, (void)) \
    X(void, PixelStorei, (GLenum pname, GLint param)) \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *data)) \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X(void, DrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices)) \
    X(void, CreateBuffers, (GLsizei n, GLuint *buffers)) \
    X(void, NamedBufferStorage, (GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags)) \
    X(void, DeleteBuffers, (GLsizei n, const GLuint *buffers)) \
    X(void, CreateVertexArrays, (GLsizei n, GLuint *arrays)) \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
    X(void, BindVertexArray, (GLuint array)) \
    X(void, VertexArrayVertexBuffer, (GLuint vao, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)) \
    X(void, VertexArrayElementBuffer, (GLuint vao, GLuint buffer)) \
    X(void, EnableVertexArrayAttrib, (GLuint vao, GLuint index)) \
    X(void, VertexArrayAttribFormat, (GLuint vao, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset)) \
    X(void, VertexArrayAttribIFormat, (GLuint vao, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset)) \
    X(void, VertexArrayAttribBinding, (GLuint vao, GLuint attribindex, GLuint bindingindex)) \
    X(void, CreateTextures, (GLenum target, GLsizei n, GLuint *textures)) \
    X(void, DeleteTextures, (GLsizei n, const GLuint *textures)) \
    X(void, TextureStorage2D, (GLuint texture, GLsizei levels, GLenum internalformat, GLsizei w, GLsizei h)) \
    X(void, TextureParameteri, (GLuint texture, GLenum pname, GLint param)) \
    X(void, TextureParameterfv, (GLuint texture, GLenum pname, const GLfloat *param)) \
    X(void, BindTextureUnit, (GLuint unit, GLuint texture)) \
    X(void, GenerateTextureMipmap, (GLuint texture)) \
    X(void, CreateFramebuffers, (GLsizei n, GLuint *framebuffers)) \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
    X(void, NamedFramebufferTexture, (GLuint fb, GLenum attachment, GLuint texture, GLint level)) \
    X(void, NamedFramebufferDrawBuffer, (GLuint fb, GLenum buf)) \
    X(GLenum, CheckNamedFramebufferStatus, (GLuint fb, GLenum target)) \
    X(void, BindFramebuffer, (GLenum target, GLuint fb)) \
    X(void, BlitNamedFramebuffer, (GLuint src, GLuint dst, GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)) \
    X(GLuint, CreateShader, (GLenum type)) \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths)) \
    X(void, CompileShader, (GLuint shader)) \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei max, GLsizei *len, GLchar *log)) \
    X(void, DeleteShader, (GLuint shader)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint program, GLuint shader)) \
    X(void, LinkProgram, (GLuint program)) \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei max, GLsizei *len, GLchar *log)) \
    X(void, DeleteProgram, (GLuint program)) \
    X(void, UseProgram, (GLuint program)) \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar *name)) \
    X(void, Uniform1i, (GLint loc, GLint v)) \
    X(void, Uniform1f, (GLint loc, GLfloat v)) \
    X(void, Uniform2f, (GLint loc, GLfloat a, GLfloat b)) \
    X(void, Uniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c)) \
    X(void, Uniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d)) \
    X(void, UniformMatrix4fv, (GLint loc, GLsizei count, GLboolean transpose, const GLfloat *v)) \
    X(void, DebugMessageCallback, (GLDEBUGPROC callback, const void *user)) \
    X(void, CreateQueries, (GLenum target, GLsizei n, GLuint *ids)) \
    X(void, DeleteQueries, (GLsizei n, const GLuint *ids)) \
    X(void, QueryCounter, (GLuint id, GLenum target)) \
    X(void, GetQueryObjectuiv, (GLuint id, GLenum pname, GLuint *params)) \
    X(void, GetQueryObjectui64v, (GLuint id, GLenum pname, GLuint64 *params))

#define GL_DECLARE(ret, name, args) typedef ret (GLAPIENTRY *PFN_gl##name) args; extern PFN_gl##name gl##name;
GL_FUNCS(GL_DECLARE)
#undef GL_DECLARE

// Загружает все функции; false, если чего-то нет
bool gl_load(void);

#endif

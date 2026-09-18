/* A loader for exactly the OpenGL 3.3 core entry points this program calls.
 *
 * The usual choice here is glad or gl3w, but both are code generators that
 * want Python at configure time, which is an odd thing for a C++ build to
 * require. The set of functions a renderer this size touches is small enough
 * to list by hand, so it is listed below and resolved through
 * glfwGetProcAddress at start-up.
 *
 * No system GL header is involved: types, enums and prototypes are all
 * declared here, which keeps the Windows build clear of <windows.h> and of
 * the 1.1-era declarations in <GL/gl.h> that would collide with these
 * pointers.
 */
#ifndef GL33_H
#define GL33_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Win32 GL is __stdcall, which matters for 32-bit builds. */
#if defined(_WIN32)
#define GL33_APIENTRY __stdcall
#else
#define GL33_APIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0

/* primitives */
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004

/* types */
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406

/* state */
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BACK 0x0405
#define GL_LEQUAL 0x0203
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100

/* queries */
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5

/* buffers */
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8

/* shaders */
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

/* textures */
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F

/* framebuffers */
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

typedef const GLubyte *(GL33_APIENTRY *PFN_glGetString)(GLenum);
typedef void(GL33_APIENTRY *PFN_glGetIntegerv)(GLenum, GLint *);
typedef GLenum(GL33_APIENTRY *PFN_glGetError)(void);
typedef void(GL33_APIENTRY *PFN_glEnable)(GLenum);
typedef void(GL33_APIENTRY *PFN_glDisable)(GLenum);
typedef void(GL33_APIENTRY *PFN_glClear)(GLbitfield);
typedef void(GL33_APIENTRY *PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(GL33_APIENTRY *PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(GL33_APIENTRY *PFN_glDepthFunc)(GLenum);
typedef void(GL33_APIENTRY *PFN_glCullFace)(GLenum);
typedef void(GL33_APIENTRY *PFN_glPolygonOffset)(GLfloat, GLfloat);
typedef void(GL33_APIENTRY *PFN_glLineWidth)(GLfloat);
typedef void(GL33_APIENTRY *PFN_glPixelStorei)(GLenum, GLint);
typedef void(GL33_APIENTRY *PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum,
                                              GLenum, void *);
typedef void(GL33_APIENTRY *PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void(GL33_APIENTRY *PFN_glDrawElements)(GLenum, GLsizei, GLenum, const void *);

typedef void(GL33_APIENTRY *PFN_glGenTextures)(GLsizei, GLuint *);
typedef void(GL33_APIENTRY *PFN_glDeleteTextures)(GLsizei, const GLuint *);
typedef void(GL33_APIENTRY *PFN_glBindTexture)(GLenum, GLuint);
typedef void(GL33_APIENTRY *PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                              GLint, GLenum, GLenum, const void *);
typedef void(GL33_APIENTRY *PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void(GL33_APIENTRY *PFN_glActiveTexture)(GLenum);
typedef void(GL33_APIENTRY *PFN_glGenerateMipmap)(GLenum);

typedef void(GL33_APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint *);
typedef void(GL33_APIENTRY *PFN_glDeleteBuffers)(GLsizei, const GLuint *);
typedef void(GL33_APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void(GL33_APIENTRY *PFN_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void(GL33_APIENTRY *PFN_glGenVertexArrays)(GLsizei, GLuint *);
typedef void(GL33_APIENTRY *PFN_glDeleteVertexArrays)(GLsizei, const GLuint *);
typedef void(GL33_APIENTRY *PFN_glBindVertexArray)(GLuint);
typedef void(GL33_APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                                       GLsizei, const void *);
typedef void(GL33_APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef void(GL33_APIENTRY *PFN_glDisableVertexAttribArray)(GLuint);
typedef void(GL33_APIENTRY *PFN_glVertexAttrib3f)(GLuint, GLfloat, GLfloat, GLfloat);

typedef GLuint(GL33_APIENTRY *PFN_glCreateShader)(GLenum);
typedef void(GL33_APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar *const *,
                                                const GLint *);
typedef void(GL33_APIENTRY *PFN_glCompileShader)(GLuint);
typedef void(GL33_APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint *);
typedef void(GL33_APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void(GL33_APIENTRY *PFN_glDeleteShader)(GLuint);
typedef GLuint(GL33_APIENTRY *PFN_glCreateProgram)(void);
typedef void(GL33_APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void(GL33_APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void(GL33_APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint *);
typedef void(GL33_APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *,
                                                     GLchar *);
typedef void(GL33_APIENTRY *PFN_glDeleteProgram)(GLuint);
typedef void(GL33_APIENTRY *PFN_glUseProgram)(GLuint);
typedef GLint(GL33_APIENTRY *PFN_glGetUniformLocation)(GLuint, const GLchar *);
typedef void(GL33_APIENTRY *PFN_glUniform1i)(GLint, GLint);
typedef void(GL33_APIENTRY *PFN_glUniform1f)(GLint, GLfloat);
typedef void(GL33_APIENTRY *PFN_glUniform2f)(GLint, GLfloat, GLfloat);
typedef void(GL33_APIENTRY *PFN_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void(GL33_APIENTRY *PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean,
                                                    const GLfloat *);

typedef void(GL33_APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void(GL33_APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint *);
typedef void(GL33_APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void(GL33_APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint,
                                                        GLint);
typedef void(GL33_APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum,
                                                           GLuint);
typedef GLenum(GL33_APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);
typedef void(GL33_APIENTRY *PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint,
                                                   GLint, GLint, GLint, GLbitfield,
                                                   GLenum);
typedef void(GL33_APIENTRY *PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void(GL33_APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);
typedef void(GL33_APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void(GL33_APIENTRY *PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);

/* Every entry point this program uses. The list drives both the pointer
 * declarations below and the loading loop in gl33.c, so a new call means a
 * typedef above and one line here. */
#define GL33_FUNCTIONS(X)                                                            \
  X(glGetString) X(glGetIntegerv) X(glGetError) X(glEnable) X(glDisable) X(glClear)  \
  X(glClearColor) X(glViewport) X(glDepthFunc) X(glCullFace) X(glPolygonOffset)      \
  X(glLineWidth) X(glPixelStorei) X(glReadPixels) X(glDrawArrays) X(glDrawElements)  \
  X(glGenTextures) X(glDeleteTextures) X(glBindTexture) X(glTexImage2D)              \
  X(glTexParameteri) X(glActiveTexture) X(glGenerateMipmap) X(glGenBuffers)          \
  X(glDeleteBuffers) X(glBindBuffer) X(glBufferData) X(glGenVertexArrays)            \
  X(glDeleteVertexArrays) X(glBindVertexArray) X(glVertexAttribPointer)              \
  X(glEnableVertexAttribArray) X(glDisableVertexAttribArray) X(glVertexAttrib3f)     \
  X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv)            \
  X(glGetShaderInfoLog) X(glDeleteShader) X(glCreateProgram) X(glAttachShader)       \
  X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glDeleteProgram)       \
  X(glUseProgram) X(glGetUniformLocation) X(glUniform1i) X(glUniform1f)              \
  X(glUniform2f) X(glUniform3f) X(glUniformMatrix4fv) X(glGenFramebuffers)           \
  X(glDeleteFramebuffers) X(glBindFramebuffer) X(glFramebufferTexture2D)             \
  X(glFramebufferRenderbuffer) X(glCheckFramebufferStatus) X(glBlitFramebuffer)      \
  X(glGenRenderbuffers) X(glDeleteRenderbuffers) X(glBindRenderbuffer)               \
  X(glRenderbufferStorage)

#define GL33_DECLARE(name) extern PFN_##name gl33_##name;
GL33_FUNCTIONS(GL33_DECLARE)
#undef GL33_DECLARE

/* Resolve every pointer above. getproc is glfwGetProcAddress. Returns the
 * name of the first function that came back null, or NULL on success. */
const char *gl33_load(void *(*getproc)(const char *));

#define glGetString gl33_glGetString
#define glGetIntegerv gl33_glGetIntegerv
#define glGetError gl33_glGetError
#define glEnable gl33_glEnable
#define glDisable gl33_glDisable
#define glClear gl33_glClear
#define glClearColor gl33_glClearColor
#define glViewport gl33_glViewport
#define glDepthFunc gl33_glDepthFunc
#define glCullFace gl33_glCullFace
#define glPolygonOffset gl33_glPolygonOffset
#define glLineWidth gl33_glLineWidth
#define glPixelStorei gl33_glPixelStorei
#define glReadPixels gl33_glReadPixels
#define glDrawArrays gl33_glDrawArrays
#define glDrawElements gl33_glDrawElements
#define glGenTextures gl33_glGenTextures
#define glDeleteTextures gl33_glDeleteTextures
#define glBindTexture gl33_glBindTexture
#define glTexImage2D gl33_glTexImage2D
#define glTexParameteri gl33_glTexParameteri
#define glActiveTexture gl33_glActiveTexture
#define glGenerateMipmap gl33_glGenerateMipmap
#define glGenBuffers gl33_glGenBuffers
#define glDeleteBuffers gl33_glDeleteBuffers
#define glBindBuffer gl33_glBindBuffer
#define glBufferData gl33_glBufferData
#define glGenVertexArrays gl33_glGenVertexArrays
#define glDeleteVertexArrays gl33_glDeleteVertexArrays
#define glBindVertexArray gl33_glBindVertexArray
#define glVertexAttribPointer gl33_glVertexAttribPointer
#define glEnableVertexAttribArray gl33_glEnableVertexAttribArray
#define glDisableVertexAttribArray gl33_glDisableVertexAttribArray
#define glVertexAttrib3f gl33_glVertexAttrib3f
#define glCreateShader gl33_glCreateShader
#define glShaderSource gl33_glShaderSource
#define glCompileShader gl33_glCompileShader
#define glGetShaderiv gl33_glGetShaderiv
#define glGetShaderInfoLog gl33_glGetShaderInfoLog
#define glDeleteShader gl33_glDeleteShader
#define glCreateProgram gl33_glCreateProgram
#define glAttachShader gl33_glAttachShader
#define glLinkProgram gl33_glLinkProgram
#define glGetProgramiv gl33_glGetProgramiv
#define glGetProgramInfoLog gl33_glGetProgramInfoLog
#define glDeleteProgram gl33_glDeleteProgram
#define glUseProgram gl33_glUseProgram
#define glGetUniformLocation gl33_glGetUniformLocation
#define glUniform1i gl33_glUniform1i
#define glUniform1f gl33_glUniform1f
#define glUniform2f gl33_glUniform2f
#define glUniform3f gl33_glUniform3f
#define glUniformMatrix4fv gl33_glUniformMatrix4fv
#define glGenFramebuffers gl33_glGenFramebuffers
#define glDeleteFramebuffers gl33_glDeleteFramebuffers
#define glBindFramebuffer gl33_glBindFramebuffer
#define glFramebufferTexture2D gl33_glFramebufferTexture2D
#define glFramebufferRenderbuffer gl33_glFramebufferRenderbuffer
#define glCheckFramebufferStatus gl33_glCheckFramebufferStatus
#define glBlitFramebuffer gl33_glBlitFramebuffer
#define glGenRenderbuffers gl33_glGenRenderbuffers
#define glDeleteRenderbuffers gl33_glDeleteRenderbuffers
#define glBindRenderbuffer gl33_glBindRenderbuffer
#define glRenderbufferStorage gl33_glRenderbufferStorage

#ifdef __cplusplus
}
#endif

#endif /* GL33_H */

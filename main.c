#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include "bcm_host.h"

#include "GLES2/gl2.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

typedef struct {
  uint32_t screen_width;
  uint32_t screen_height;

  // OpenGL|ES objects
  EGLDisplay display;
  EGLSurface surface;
  EGLContext context;

  GLuint vshader;
  GLuint fshader;
  GLuint program;
  GLuint tex;

  // Program attributes and uniforms.
  GLuint attr_vertex;
  GLuint unif_center;
  GLuint unif_t;
} gl_state;

static gl_state _state, *state = &_state;

static const GLfloat vertex_data[] = {
  -1.0, -1.0, 1.0, 1.0,
  1.0, -1.0, 1.0, 1.0,
  1.0, 1.0, 1.0, 1.0,
  -1.0, 1.0, 1.0, 1.0
};
static const GLushort tri_indices[] = {
  0, 1, 3, 2
};

static void showlog(GLint shader) {
  char log[1024];
  glGetShaderInfoLog(shader, sizeof log, NULL, log);
  printf("%d:shader:\n%s\n", shader, log);
}

static void showprogramlog(GLint shader) {
  char log[1024];
  glGetProgramInfoLog(shader, sizeof log, NULL, log);
  printf("%d:program:\n%s\n", shader, log);
}

static void init_ogl(gl_state *state) {
  int success = 0;
  EGLBoolean result;
  EGLint num_config;

  static EGL_DISPMANX_WINDOW_T nativewindow;

  DISPMANX_ELEMENT_HANDLE_T dispman_element;
  DISPMANX_DISPLAY_HANDLE_T dispman_display;
  DISPMANX_UPDATE_HANDLE_T dispman_update;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;

  static const EGLint attribute_list[] = {
     EGL_RED_SIZE, 8,
     EGL_GREEN_SIZE, 8,
     EGL_BLUE_SIZE, 8,
     EGL_ALPHA_SIZE, 8,
     EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
     EGL_NONE
  };

  static const EGLint context_attributes[] = {
     EGL_CONTEXT_CLIENT_VERSION, 2,
     EGL_NONE
  };
  EGLConfig config;

  // get an EGL display connection
  state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(state->display != EGL_NO_DISPLAY);

  // initialize the EGL display connection
  result = eglInitialize(state->display, NULL, NULL);
  assert(EGL_FALSE != result);

  // get an appropriate EGL frame buffer configuration
  result = eglChooseConfig(state->display, attribute_list, &config, 1, &num_config);
  assert(EGL_FALSE != result);

  // get an appropriate EGL frame buffer configuration
  result = eglBindAPI(EGL_OPENGL_ES_API);
  assert(EGL_FALSE != result);

  // create an EGL rendering context
  state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, context_attributes);
  assert(state->context != EGL_NO_CONTEXT);

  // create an EGL window surface
  success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
  assert(success >= 0);

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = state->screen_width;
  dst_rect.height = state->screen_height;

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = state->screen_width << 16;
  src_rect.height = state->screen_height << 16;

  dispman_display = vc_dispmanx_display_open(0 /* LCD */);
  dispman_update = vc_dispmanx_update_start(0);

  dispman_element = vc_dispmanx_element_add(dispman_update, dispman_display,
     0 /* layer */, &dst_rect, 0 /* src */,
     &src_rect, DISPMANX_PROTECTION_NONE, 0 /* alpha */, 0 /* clamp */, 0 /* transform */);

  nativewindow.element = dispman_element;
  nativewindow.width = state->screen_width;
  nativewindow.height = state->screen_height;
  vc_dispmanx_update_submit_sync(dispman_update);

  state->surface = eglCreateWindowSurface(state->display, config, &nativewindow, NULL);
  assert(state->surface != EGL_NO_SURFACE);

  // connect the context to the surface
  result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
  assert(EGL_FALSE != result);
}

static void init_shaders(gl_state *state)
{
  const GLchar *vshader_source =
"attribute vec4 vertex;"
"void main(void) {"
"  vec4 pos = vertex;"
"  gl_Position = pos;"
"}";

   const GLchar *fshader_source =
"uniform vec2 center;"
"uniform float t;"
"uniform sampler2D tex;"
"void main(void) {"
//"  vec2 to_center = vec2(gl_FragCoord.x - center.x, gl_FragCoord.y - center.y);"
//"  float d = 100.0 * t * inversesqrt(dot(to_center, to_center));"
//"  float theta = 100.0 * t * atan(to_center.y, to_center.x) / 3.14159;"
//"  gl_FragColor = texture2D(tex, vec2(d, theta));"
"  gl_FragColor = vec4(sin(t), cos(t), 0, 1);"
"}";

  state->vshader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(state->vshader, 1, &vshader_source, 0);
  glCompileShader(state->vshader);
  showlog(state->vshader);

  state->fshader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(state->fshader, 1, &fshader_source, 0);
  glCompileShader(state->fshader);
  showlog(state->fshader);

  state->program = glCreateProgram();
  glAttachShader(state->program, state->vshader);
  glAttachShader(state->program, state->fshader);
  glLinkProgram(state->program);
  showprogramlog(state->program);

  state->attr_vertex = glGetAttribLocation(state->program, "vertex");
  state->unif_center = glGetUniformLocation(state->program, "center");
  state->unif_t      = glGetUniformLocation(state->program, "t");

  {
    GLuint vertex_buf;
    glGenBuffers(1, &vertex_buf);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buf);
  }

  {
    GLuint index_buf;
    glGenBuffers(1, &index_buf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buf);
  }

  glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(tri_indices), tri_indices, GL_STATIC_DRAW);

  glVertexAttribPointer(state->attr_vertex, 4, GL_FLOAT, 0, 16, 0);
  glEnableVertexAttribArray(state->attr_vertex);

  glViewport(0, 0, state->screen_width, state->screen_height);
}

static void load_wormhole_texture(gl_state *state) {
  #define TEX_SIZE 256
  glGenTextures(1, &state->tex);
  glBindTexture(GL_TEXTURE_2D, state->tex);
  {
    // TODO something less cheesy
    int x, y;
    size_t length = 3 * TEX_SIZE * TEX_SIZE;
    char *buf = (char *)malloc(length);
    char *p = buf;
    memset(buf, 0, length);
    for (y = 0; y < TEX_SIZE; y++) {
      for (x = 0; x < TEX_SIZE; x++) {
        if (0 == rand() % 1000) {
          p[0] = p[1] = p[2] = 255;
        }
        p += 3;
      }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEX_SIZE, TEX_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, buf);
    free(buf);
  }
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

static void draw_triangles(gl_state *state, GLfloat cx, GLfloat cy, GLuint t) {
  glClear(GL_COLOR_BUFFER_BIT);

  glUniform2f(state->unif_center, cx, cy);
  glUniform1f(state->unif_t, t);

  glDrawElements(GL_TRIANGLE_STRIP, sizeof(tri_indices) / sizeof(tri_indices[0]), GL_UNSIGNED_SHORT, 0);
  eglSwapBuffers(state->display, state->surface);
}

int main() {
  GLfloat cx, cy, t;
  bcm_host_init();

  init_ogl(state);
  init_shaders(state);

  cx = state->screen_width * 0.5f;
  cy = state->screen_height * 0.5f;
  load_wormhole_texture(state);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glUseProgram(state->program);
  glBindTexture(GL_TEXTURE_2D, state->tex);

  while (1) {
    draw_triangles(state, cx, cy, t);
    t += 0.1f;
  }
  return 0;
}

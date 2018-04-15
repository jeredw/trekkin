#include <stdio.h>
#include <stdlib.h>
#include <cassert>
#include <sys/time.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>

namespace {

struct GraphicsState {
  uint32_t screen_width;
  uint32_t screen_height;

  // OpenGL|ES objects
  EGLDisplay display;
  EGLSurface surface;
  EGLContext context;

  GLuint program;
  GLuint vbo_quad;
  GLuint tex;
  GLint attr_vertex;
};

static struct timeval start;

static void PrintLog(GLuint object) {
  GLint log_length = 0;
  if (glIsShader(object))
    glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
  else if (glIsProgram(object))
    glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
  else {
    fprintf(stderr, "printlog: Not a shader or a program\n");
    return;
  }

  char *log = (char *)malloc(log_length);

  if (glIsShader(object))
    glGetShaderInfoLog(object, log_length, NULL, log);
  else if (glIsProgram(object))
    glGetProgramInfoLog(object, log_length, NULL, log);

  fprintf(stderr, "%s", log);
  free(log);
}

static GLuint CreateShader(const GLchar *source, GLenum type) {
  GLuint res = glCreateShader(type);
  const GLchar *sources[] = {
// Define GLSL version
#ifdef GL_ES_VERSION_2_0
      "#version 100\n"
#else
      "#version 120\n"
#endif
      ,
// GLES2 precision specifiers
#ifdef GL_ES_VERSION_2_0
      // Define default float precision for fragment shaders:
      (type == GL_FRAGMENT_SHADER) ? "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
                                     "precision highp float;           \n"
                                     "#else                            \n"
                                     "precision mediump float;         \n"
                                     "#endif                           \n"
                                   : ""
// Note: OpenGL ES automatically defines this:
// #define GL_ES
#else
      // Ignore GLES 2 precision specifiers:
      "#define lowp   \n"
      "#define mediump\n"
      "#define highp  \n"
#endif
      ,
      source};
  glShaderSource(res, 3, sources, NULL);

  glCompileShader(res);
  GLint compile_ok = GL_FALSE;
  glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
  if (compile_ok == GL_FALSE) {
    PrintLog(res);
    glDeleteShader(res);
    return 0;
  }

  return res;
}

static void InitOpenGL(GraphicsState *state) {
  EGLBoolean result;

  // create an EGL window surface
  int success = graphics_get_display_size(0 /* LCD */, &state->screen_width,
                                          &state->screen_height);
  assert(success >= 0);

  // get an EGL display connection
  state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(state->display != EGL_NO_DISPLAY);

  // initialize the EGL display connection
  int major, minor;
  result = eglInitialize(state->display, &major, &minor);
  assert(EGL_FALSE != result);

  // get an appropriate EGL frame buffer configuration
  static const EGLint attribute_list[] = {
      EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8,  EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE};
  EGLConfig config;
  EGLint num_config;
  result =
      eglChooseConfig(state->display, attribute_list, &config, 1, &num_config);
  assert(EGL_FALSE != result);

  // bind the OpenGL API to the EGL
  result = eglBindAPI(EGL_OPENGL_ES_API);
  assert(EGL_FALSE != result);

  // create an EGL rendering context
  static const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                              EGL_NONE};
  state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT,
                                    context_attributes);
  assert(state->context != EGL_NO_CONTEXT);

  VC_RECT_T dst_rect;
  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = state->screen_width;
  dst_rect.height = state->screen_height;

  VC_RECT_T src_rect;
  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = state->screen_width << 16;
  src_rect.height = state->screen_height << 16;

  DISPMANX_DISPLAY_HANDLE_T dispman_display =
      vc_dispmanx_display_open(0 /* LCD */);
  DISPMANX_UPDATE_HANDLE_T dispman_update = vc_dispmanx_update_start(0);
  DISPMANX_ELEMENT_HANDLE_T dispman_element = vc_dispmanx_element_add(
      dispman_update, dispman_display, 0 /* layer */, &dst_rect, 0 /* src */,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /* alpha */, 0 /* clamp */,
      DISPMANX_NO_ROTATE);

  static EGL_DISPMANX_WINDOW_T nativewindow;
  nativewindow.element = dispman_element;
  nativewindow.width = state->screen_width;
  nativewindow.height = state->screen_height;
  vc_dispmanx_update_submit_sync(dispman_update);

  state->surface =
      eglCreateWindowSurface(state->display, config, &nativewindow, NULL);
  assert(state->surface != EGL_NO_SURFACE);

  // connect the context to the surface
  result = eglMakeCurrent(state->display, state->surface, state->surface,
                          state->context);
  assert(EGL_FALSE != result);
}

static void InitGeometry(GraphicsState *state) {
  GLfloat triangle_vertices[] = {-1.0, -1.0, 1.0, -1.0, -1.0, 1.0,
                                 1.0,  -1.0, 1.0, 1.0,  -1.0, 1.0};

  glGenBuffers(1, &state->vbo_quad);
  glBindBuffer(GL_ARRAY_BUFFER, state->vbo_quad);
  glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_vertices), triangle_vertices,
               GL_STATIC_DRAW);
}

static void InitShaders(GraphicsState *state) {
  const GLchar *vshader_source =
      "attribute vec2 coord2d;"
      "void main(void) {"
      "  gl_Position = vec4(coord2d, 0.0, 1.0);"
      "}";

  const GLchar *fshader_source =
      "uniform vec2 center;"
      "uniform float time;"
      "uniform sampler2D tex;"
      "void main(void) {"
      "  vec2 to_center = vec2(gl_FragCoord.x - center.x, gl_FragCoord.y - "
      "center.y);"
      "  float d = .35 * time + 40.0 * inversesqrt(dot(to_center, to_center));"
      "  float theta = atan(to_center.y, to_center.x) / 3.14159;"
      "  vec4 color = texture2D(tex, vec2(d, theta));"
      "  color.r = color.r * d * .05;"
      "  gl_FragColor = color;"
      "}";

  GLuint vs = CreateShader(vshader_source, GL_VERTEX_SHADER);
  GLuint fs = CreateShader(fshader_source, GL_FRAGMENT_SHADER);

  state->program = glCreateProgram();
  glAttachShader(state->program, vs);
  glAttachShader(state->program, fs);
  glLinkProgram(state->program);
  GLint link_ok = GL_FALSE;
  glGetProgramiv(state->program, GL_LINK_STATUS, &link_ok);
  if (!link_ok) {
    fprintf(stderr, "glLinkProgram:");
    PrintLog(state->program);
  }

  state->attr_vertex = glGetAttribLocation(state->program, "coord2d");
}

static void LoadWormholeTexture(GraphicsState *state) {
  glGenTextures(1, &state->tex);
  glBindTexture(GL_TEXTURE_2D, state->tex);
  {
    // TODO something less cheesy
    size_t length = 3 * state->screen_height * state->screen_width;
    char *buf = (char *)malloc(length);
    char *p = buf;
    memset(buf, 0, length);
    for (uint32_t y = 0; y < state->screen_height; y++) {
      for (uint32_t x = 0; x < state->screen_width; x++) {
        if (0 == rand() % 1000) {
          p[0] = p[1] = p[2] = 255;
        }
        p += 3;
      }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, state->screen_width,
                 state->screen_height, 0, GL_RGB, GL_UNSIGNED_BYTE, buf);
    free(buf);
  }
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

static void Draw(GraphicsState *state, GLfloat cx, GLfloat cy, GLfloat time) {
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(state->program);

  GLuint unif_center = glGetUniformLocation(state->program, "center");
  glUniform2f(unif_center, cx, cy);
  GLuint unif_time = glGetUniformLocation(state->program, "time");
  glUniform1f(unif_time, time);
  GLuint unif_tex = glGetUniformLocation(state->program, "tex");
  glUniform1i(unif_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, state->tex);

  glBindBuffer(GL_ARRAY_BUFFER, state->vbo_quad);
  glVertexAttribPointer(state->attr_vertex, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(state->attr_vertex);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(state->attr_vertex);

  eglSwapBuffers(state->display, state->surface);
}

static float GetDeltaTime() {
  struct timeval now;
  gettimeofday(&now, NULL);

  float dt = (now.tv_sec - start.tv_sec);
  dt += (now.tv_usec - start.tv_usec) / 1000000.0;  // us to s
  return dt;
}
}

#if 0
int main() {
  atexit(bcm_host_deinit);
  bcm_host_init();

  GraphicsState state;
  InitOpenGL(&state);
  InitGeometry(&state);
  InitShaders(&state);
  LoadWormholeTexture(&state);

  gettimeofday(&start, NULL);
  GLfloat cx = 0.5f * state.screen_width;
  GLfloat cy = 0.5f * state.screen_height;
  while (1) {
    Draw(&state, cx, cy, GetDeltaTime());
    sleep(0.01);
  }
  return 0;
}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <cassert>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>

// because stb defines a bunch of crap
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#include "stb/stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb/stb_image.h"

typedef void *uv_handle_t;  // the display program doesn't use libuv
#include "game.h"
#include "scores.h"
#include "log.h"

#define LOG(...) log("D", __VA_ARGS__)

using namespace trek;

namespace {

#include "hud_objects.h"
static const char *const HUD_ATLAS_PATH = "art/hudatlas.png";

const int FONT_ATLAS_WIDTH = 512;
const int FONT_ATLAS_HEIGHT = 512;
static const char *const MAIN_FONT =
    "fonts/Bungee_Inline/BungeeInline-Regular.ttf";
// static const char* const MAIN_FONT = "fonts/Monoton/Monoton-Regular.ttf";

static const char *const STARSHIP_NAMES[] = {
    "Argonaut",  "Bellerophon", "Conquistador", "Daedalus", "Endurance",
    "Fomalhaut", "Hyperion",    "Icarus",       "Jupiter",  "Nemesis",
    "Orion",     "Prometheus",  "Serenity",     "Titan",    "USS Budget",
    "Valkyrie",  "Yamato"};

struct PushBuffer {
  PushBuffer() : pending_indices(0) {}
  std::vector<GLfloat> attribs;
  GLuint vbo;
  GLsizei num_indices;
  GLsizei pending_indices;
};

struct GraphicsState {
  uint32_t screen_width;
  uint32_t screen_height;
  GLfloat projection_matrix[16];

  EGLDisplay display;
  EGLSurface surface;
  EGLContext context;

  struct {
    GLuint program;
    GLuint vbo;
    GLuint tex;
    GLint attr_vertex;
  } bg;

  struct {
    GLuint program;
    PushBuffer push_buffer;
    GLuint tex;
    GLint attr_vertex;
    GLint attr_tex_coord;
  } hud;

  struct {
    GLuint program;
    PushBuffer push_buffer;
    GLuint tex;
    GLint attr_vertex;
    GLint attr_tex_coord;
    stbtt_bakedchar font_chars[96];  // ASCII 32..126 is 95 glyphs
  } text;
};

static DisplayUpdate D;
static GraphicsState G;
static float now;

static char *read_file_or_die(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    LOG("couldn't stat %s: %s", path, strerror(errno));
    exit(1);
    return nullptr;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == nullptr) {
    LOG("couldn't open %s: %s", path, strerror(errno));
    exit(1);
    return nullptr;
  }
  char *buf = (char *)malloc(st.st_size);
  assert(buf != nullptr);
  if (fread(buf, st.st_size, 1, fp) != 1) {
    free(buf);
    LOG("failed to read %s: %s", path, strerror(errno));
    exit(1);
    return nullptr;
  }
  fclose(fp);
  return buf;
}

static void advance_time() {
  static timeval start_time;
  if (start_time.tv_sec == 0) {
    gettimeofday(&start_time, NULL);
  }

  timeval current_time;
  gettimeofday(&current_time, NULL);
  now = current_time.tv_sec - start_time.tv_sec;
  now += (current_time.tv_usec - start_time.tv_usec) / 1000000.0;  // us to s
}

static void read_display_update() {
  static char buf[sizeof(DisplayUpdate)];
  static ssize_t len;

  ssize_t bytes_read = -1;
  int read_calls = 0;
  while ((bytes_read == -1 || bytes_read > 0) && read_calls < 10) {
    bytes_read = read(0, buf, sizeof(buf) - len);
    read_calls++;
    if (bytes_read > 0) {
      len += bytes_read;
      if (len >= (ssize_t)sizeof(DisplayUpdate)) {
        memcpy((void *)&D, buf, sizeof(DisplayUpdate));
        len = 0;
      }
    } else if (bytes_read == -1 && errno != EAGAIN) {
      break;
    }
  }
  if (errno != EAGAIN) {
    LOG("read failure %s", strerror(errno));
    exit(1);
  }
}

static void init_io() {
  int flags = fcntl(0, F_GETFL, 0);
  fcntl(0, F_SETFL, flags | O_NONBLOCK);
}

static void print_shader(GLuint object) {
  GLint log_length = 0;
  if (glIsShader(object)) {
    glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
  } else if (glIsProgram(object)) {
    glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
  } else {
    LOG("print_shader: Not a shader or a program");
    return;
  }

  char *log_buf = (char *)malloc(log_length);

  if (glIsShader(object)) {
    glGetShaderInfoLog(object, log_length, nullptr, log_buf);
  } else if (glIsProgram(object)) {
    glGetProgramInfoLog(object, log_length, nullptr, log_buf);
  }

  LOG("%s", log_buf);
  free(log_buf);
}

static GLuint create_shader(const GLchar *source, GLenum type) {
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
    print_shader(res);
    glDeleteShader(res);
    return 0;
  }

  return res;
}

static GLuint link_program(const GLchar* vshader_source, const GLchar* fshader_source) {
  GLuint vs = create_shader(vshader_source, GL_VERTEX_SHADER);
  GLuint fs = create_shader(fshader_source, GL_FRAGMENT_SHADER);

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint link_ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &link_ok);
  if (!link_ok) {
    LOG("failed to link shader");
    print_shader(program);
    exit(1);
  }
  return program;
}

static void build_projection_matrix(GLfloat left, GLfloat right, GLfloat bottom,
                                    GLfloat top, GLfloat near, GLfloat far) {
  GLfloat *m = G.projection_matrix;
  m[0] = 2.f / (right - left);
  m[1] = 0.f;
  m[2] = 0.f;
  m[3] = 0.f;

  m[4] = 0.f;
  m[5] = 2.f / (top - bottom);
  m[6] = 0.f;
  m[7] = 0.f;

  m[8] = 0.f;
  m[9] = 0.f;
  m[10] = 2.f / (far - near);
  m[11] = 0.f;

  m[12] = -(right + left) / (right - left);
  m[13] = -(top + bottom) / (top - bottom);
  m[14] = -(far + near) / (far - near);
  m[15] = 1.f;
}

static void init_open_gl() {
  EGLBoolean result;

  // create an EGL window surface
  int success =
      graphics_get_display_size(0 /* LCD */, &G.screen_width, &G.screen_height);
  assert(success >= 0);

  // get an EGL display connection
  G.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(G.display != EGL_NO_DISPLAY);

  // initialize the EGL display connection
  int major, minor;
  result = eglInitialize(G.display, &major, &minor);
  assert(EGL_FALSE != result);

  // get an appropriate EGL frame buffer configuration
  static const EGLint attribute_list[] = {
      EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8,  EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE};
  EGLConfig config;
  EGLint num_config;
  result = eglChooseConfig(G.display, attribute_list, &config, 1, &num_config);
  assert(EGL_FALSE != result);

  // bind the OpenGL API to the EGL
  result = eglBindAPI(EGL_OPENGL_ES_API);
  assert(EGL_FALSE != result);

  // create an EGL rendering context
  static const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                              EGL_NONE};
  G.context =
      eglCreateContext(G.display, config, EGL_NO_CONTEXT, context_attributes);
  assert(G.context != EGL_NO_CONTEXT);

  VC_RECT_T dst_rect;
  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = G.screen_width;
  dst_rect.height = G.screen_height;

  VC_RECT_T src_rect;
  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = G.screen_width << 16;
  src_rect.height = G.screen_height << 16;

  DISPMANX_DISPLAY_HANDLE_T dispman_display =
      vc_dispmanx_display_open(0 /* LCD */);
  DISPMANX_UPDATE_HANDLE_T dispman_update = vc_dispmanx_update_start(0);
  DISPMANX_ELEMENT_HANDLE_T dispman_element = vc_dispmanx_element_add(
      dispman_update, dispman_display, 0 /* layer */, &dst_rect, 0 /* src */,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /* alpha */, 0 /* clamp */,
      DISPMANX_NO_ROTATE);

  static EGL_DISPMANX_WINDOW_T nativewindow;
  nativewindow.element = dispman_element;
  nativewindow.width = G.screen_width;
  nativewindow.height = G.screen_height;
  vc_dispmanx_update_submit_sync(dispman_update);

  G.surface = eglCreateWindowSurface(G.display, config, &nativewindow, NULL);
  assert(G.surface != EGL_NO_SURFACE);

  // connect the context to the surface
  result = eglMakeCurrent(G.display, G.surface, G.surface, G.context);
  assert(EGL_FALSE != result);
  glViewport(0, 0, G.screen_width, G.screen_height);
  build_projection_matrix(0.f, G.screen_width, G.screen_height, 0.f, -1.f, 1.f);
}

static void init_bg_vbo() {
  GLfloat triangle_vertices[] = {-1.0, -1.0, 1.0, -1.0, -1.0, 1.0,
                                 1.0,  -1.0, 1.0, 1.0,  -1.0, 1.0};

  glGenBuffers(1, &G.bg.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, G.bg.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_vertices), triangle_vertices,
               GL_STATIC_DRAW);
}

static void init_bg_shaders() {
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

  G.bg.program = link_program(vshader_source, fshader_source);
  G.bg.attr_vertex = glGetAttribLocation(G.bg.program, "coord2d");
}

static void init_text_shaders() {
  const GLchar *vshader_source =
      "attribute vec2 coord2d;"
      "attribute vec2 in_texCoord;"
      "uniform mat4 projection;"
      "varying vec2 texCoord;"
      "void main(void) {"
      "  gl_Position = projection * vec4(coord2d, 0.0, 1.0);"
      "  texCoord = in_texCoord;"
      "}";

  const GLchar *fshader_source =
      "varying vec2 texCoord;"
      "uniform vec2 center;"
      "uniform float time;"
      "uniform sampler2D tex;"
      "void main(void) {"
      "  vec4 color = vec4(1.0, 0.0, 1.0, 1.0);"
      "  vec4 mask = texture2D(tex, texCoord);"
      "  gl_FragColor = vec4(color.rgb * mask.a, 1.0);"
      "}";

  G.text.program = link_program(vshader_source, fshader_source);
  G.text.attr_vertex = glGetAttribLocation(G.text.program, "coord2d");
  G.text.attr_tex_coord = glGetAttribLocation(G.text.program, "in_texCoord");
}

static void init_hud_shaders() {
  const GLchar *vshader_source =
      "attribute vec2 coord2d;"
      "attribute vec2 in_texCoord;"
      "uniform mat4 projection;"
      "varying vec2 texCoord;"
      "void main(void) {"
      "  gl_Position = projection * vec4(coord2d, 0.0, 1.0);"
      "  texCoord = in_texCoord;"
      "}";

  const GLchar *fshader_source =
      "varying vec2 texCoord;"
      "uniform vec2 center;"
      "uniform float time;"
      "uniform sampler2D tex;"
      "void main(void) {"
      "  vec4 color = vec4(1.0, 0.0, 1.0, 1.0);"
      "  gl_FragColor = texture2D(tex, texCoord);"
      "}";

  G.hud.program = link_program(vshader_source, fshader_source);
  G.hud.attr_vertex = glGetAttribLocation(G.hud.program, "coord2d");
  G.hud.attr_tex_coord = glGetAttribLocation(G.hud.program, "in_texCoord");
}

static void init_shaders() {
  init_bg_shaders();
  init_text_shaders();
  init_hud_shaders();
}

static void bake_fonts() {
  unsigned char *temp_bitmap =
      (unsigned char *)malloc(FONT_ATLAS_WIDTH * FONT_ATLAS_HEIGHT);
  unsigned char *ttf = (unsigned char *)read_file_or_die(MAIN_FONT);
  stbtt_BakeFontBitmap(ttf, 0, 64.0, temp_bitmap, FONT_ATLAS_WIDTH,
                       FONT_ATLAS_HEIGHT, 32, 96,
                       G.text.font_chars);  // no guarantee this fits!
  free(ttf);
  glGenTextures(1, &G.text.tex);
  glBindTexture(GL_TEXTURE_2D, G.text.tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, FONT_ATLAS_WIDTH, FONT_ATLAS_HEIGHT,
               0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap);
  free(temp_bitmap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

static void push_text(GLfloat x, GLfloat y, const char *text) {
  PushBuffer *b = &G.text.push_buffer;

#define PUSH_VERTEX(x, y, s, t) \
  b->attribs.push_back(x);      \
  b->attribs.push_back(y);      \
  b->attribs.push_back(s);      \
  b->attribs.push_back(t);      \
  b->pending_indices++;

  while (*text) {
    if (*text >= 32 && *text < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(G.text.font_chars, FONT_ATLAS_WIDTH, FONT_ATLAS_HEIGHT,
                         *text - 32, &x, &y, &q, 1 /* OpenGL */);
      // 00 10
      // 01 11
      PUSH_VERTEX(q.x0, q.y0, q.s0, q.t0);
      PUSH_VERTEX(q.x0, q.y1, q.s0, q.t1);
      PUSH_VERTEX(q.x1, q.y1, q.s1, q.t1);
      PUSH_VERTEX(q.x1, q.y1, q.s1, q.t1);
      PUSH_VERTEX(q.x1, q.y0, q.s1, q.t0);
      PUSH_VERTEX(q.x0, q.y0, q.s0, q.t0);
    }
    ++text;
  }
#undef PUSH_VERTEX
}

static void push_hud_object(float x, float y, const HudObject &obj) {
  PushBuffer *b = &G.hud.push_buffer;
  float x0 = x;
  float y0 = y;
  float x1 = x + obj.width;
  float y1 = y + obj.height;

#define PUSH_VERTEX(x, y, s, t) \
  b->attribs.push_back(x);      \
  b->attribs.push_back(y);      \
  b->attribs.push_back(s);      \
  b->attribs.push_back(t);      \
  b->pending_indices++;

  // 00 10
  // 01 11
  PUSH_VERTEX(x0, y0, obj.s0, obj.t0);
  PUSH_VERTEX(x0, y1, obj.s0, obj.t1);
  PUSH_VERTEX(x1, y1, obj.s1, obj.t1);
  PUSH_VERTEX(x1, y1, obj.s1, obj.t1);
  PUSH_VERTEX(x1, y0, obj.s1, obj.t0);
  PUSH_VERTEX(x0, y0, obj.s0, obj.t0);
#undef PUSH_VERTEX
}

static void clear_push_buffer(PushBuffer *b) {
  if (b->num_indices == 0) {
    return;
  }
  // num_indices > 0 implies this buffer was submitted before
  glDeleteBuffers(1, &b->vbo);
  b->num_indices = 0;
  b->pending_indices = 0;
  b->attribs.clear();
}

static void clear_hud_objects() {
  clear_push_buffer(&G.hud.push_buffer);
}

static void clear_text() {
  clear_push_buffer(&G.text.push_buffer);
}

static void clear() {
  clear_text();
  clear_hud_objects();
}

static void submit_one_push_buffer(PushBuffer *b) {
  if (b->pending_indices == 0) {
    return;
  }
  if (b->num_indices > 0) {
    // num_indices > 0 implies this buffer was submitted before
    glDeleteBuffers(1, &b->vbo);
  }
  glGenBuffers(1, &b->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * b->attribs.size(), b->attribs.data(), GL_STATIC_DRAW);
  b->num_indices = b->pending_indices;
  b->pending_indices = 0;
  b->attribs.clear();
}

static void submit_push_buffers() {
  submit_one_push_buffer(&G.hud.push_buffer);
  submit_one_push_buffer(&G.text.push_buffer);
}

static void load_wormhole_texture() {
  glGenTextures(1, &G.bg.tex);
  glBindTexture(GL_TEXTURE_2D, G.bg.tex);
  {
    // TODO something less cheesy
    size_t length = 3 * G.screen_height * G.screen_width;
    char *buf = (char *)malloc(length);
    char *p = buf;
    memset(buf, 0, length);
    for (uint32_t y = 0; y < G.screen_height; y++) {
      for (uint32_t x = 0; x < G.screen_width; x++) {
        if (0 == rand() % 1000) {
          p[0] = p[1] = p[2] = 255;
        }
        p += 3;
      }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, G.screen_width, G.screen_height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, buf);
    free(buf);
  }
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

static void load_hud_atlas() {
  glGenTextures(1, &G.hud.tex);
  glBindTexture(GL_TEXTURE_2D, G.hud.tex);
  int width, height, channels;
  unsigned char *data =
      stbi_load(HUD_ATLAS_PATH, &width, &height, &channels, 4);
  assert(width == (int)ATLAS_WIDTH);
  assert(height == (int)ATLAS_HEIGHT);
  assert(channels == 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  stbi_image_free(data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

static void draw_bg() {
  glUseProgram(G.bg.program);

  GLuint unif_center = glGetUniformLocation(G.bg.program, "center");
  glUniform2f(unif_center, 0.5f * G.screen_width, 0.5f * G.screen_height);
  GLuint unif_time = glGetUniformLocation(G.bg.program, "time");
  glUniform1f(unif_time, now);
  GLuint unif_tex = glGetUniformLocation(G.bg.program, "tex");
  glUniform1i(unif_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, G.bg.tex);

  glBindBuffer(GL_ARRAY_BUFFER, G.bg.vbo);
  glVertexAttribPointer(G.bg.attr_vertex, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(G.bg.attr_vertex);
  glDrawArrays(GL_TRIANGLES, 0, 6);  // 2 tris / one fullscreen quad
  glDisableVertexAttribArray(G.bg.attr_vertex);
}

static void draw_hud() {
  if (G.hud.push_buffer.num_indices == 0) {
    return;
  }
  glUseProgram(G.hud.program);

  GLuint unif_projection = glGetUniformLocation(G.hud.program, "projection");
  glUniformMatrix4fv(unif_projection, 1, GL_FALSE, G.projection_matrix);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, G.hud.tex);

  glBindBuffer(GL_ARRAY_BUFFER, G.hud.push_buffer.vbo);
  glVertexAttribPointer(G.hud.attr_vertex, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), 0);
  glVertexAttribPointer(G.hud.attr_tex_coord, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
  glEnableVertexAttribArray(G.hud.attr_vertex);
  glEnableVertexAttribArray(G.hud.attr_tex_coord);
  glDrawArrays(GL_TRIANGLES, 0, G.hud.push_buffer.num_indices);
  glDisableVertexAttribArray(G.hud.attr_vertex);
  glDisableVertexAttribArray(G.hud.attr_tex_coord);
}

static void draw_text() {
  if (G.text.push_buffer.num_indices == 0) {
    return;
  }
  glUseProgram(G.text.program);

  GLuint unif_projection = glGetUniformLocation(G.text.program, "projection");
  glUniformMatrix4fv(unif_projection, 1, GL_FALSE, G.projection_matrix);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, G.text.tex);

  glBindBuffer(GL_ARRAY_BUFFER, G.text.push_buffer.vbo);
  glVertexAttribPointer(G.text.attr_vertex, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), 0);
  glVertexAttribPointer(G.text.attr_tex_coord, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
  glEnableVertexAttribArray(G.text.attr_vertex);
  glEnableVertexAttribArray(G.text.attr_tex_coord);
  glDrawArrays(GL_TRIANGLES, 0, G.text.push_buffer.num_indices);
  glDisableVertexAttribArray(G.text.attr_vertex);
  glDisableVertexAttribArray(G.text.attr_tex_coord);
}

static void draw_frame() {
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  draw_bg();
  draw_hud();
  draw_text();

  eglSwapBuffers(G.display, G.surface);
}

static void layout_title_screen() {
  clear();
  float x = G.screen_width / 2 - TITLE.width / 2;
  float y = G.screen_height / 2 - TITLE.height / 2;
  push_hud_object(x, y, TITLE);
}

static void layout_high_scores() {
  clear();
}

static void layout() {
  switch (D.mode) {
    case ATTRACT: // fallthrough
    case RESET_GAME: {
      if (fmod(now, 10) < 5) {
        layout_title_screen();
      } else {
        layout_high_scores();
      }
      break;
    }
    case START_WAIT: {
      break;
    }
    case SETUP_NEW_MISSION: // fallthrough
    case NEW_MISSION: {
      break;
    }
    case PLAYING: {
      break;
    }
    case END_WAIT: {
      break;
    }
    case SETUP_GAME_OVER: // fallthrough
    case GAME_OVER: {
      break;
    }
  }
}
}

int display_main() {
  atexit(bcm_host_deinit);
  bcm_host_init();

  init_io();
  init_open_gl();
  init_shaders();
  bake_fonts();
  load_hud_atlas();
  load_wormhole_texture();
  init_bg_vbo();

  LOG("display started");

  while (1) {
    advance_time();
    read_display_update();
    layout();
    submit_push_buffers();
    draw_frame();
    sleep(0.01);
  }
  return 0;
}

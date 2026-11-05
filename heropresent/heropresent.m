/*
 * heropresent (Galaxy Note 8): the panel copy on the Mali instead of the CPU.
 *
 * iOS draws a 750x1542 frame; the kernel (iokit/Kernel/arm/SweetFramebuffer.cpp) used to scale every one of them onto the
 * 1440x2960 panel with the CPU, ~0.5 s a frame once CoreAnimation renders on the GPU and redraws the whole screen (boot-v93).
 * This process asks HeroGPU for each full-screen frame (FrameWait), samples that IOSurface in place as a texture (the EAGL
 * shim's BGL_WRAPSAMPLE path), draws it scaled onto the panel surface, a direct GPU render target, and says FrameDone. The
 * kernel copies with the CPU whenever this is not running or not in time.
 *
 * Started by the autorun from the drop zone (DROP_DIR heropresent); logs to the kernel log through HeroGPU's Log call.
 */
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceRef.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES2/glext.h>
#include <mach/mach.h>

/* IOKit.framework is in the SDK without its headers for iOS: what this uses of it. */
typedef mach_port_t io_object_t, io_service_t, io_connect_t;
extern const mach_port_t kIOMainPortDefault;
CFMutableDictionaryRef IOServiceMatching(const char *name);
io_service_t IOServiceGetMatchingService(mach_port_t mainPort, CFDictionaryRef matching);
kern_return_t IOServiceOpen(io_service_t service, task_port_t owningTask, uint32_t type, io_connect_t *connect);
kern_return_t IOConnectCallScalarMethod(mach_port_t connection, uint32_t selector, const uint64_t *input, uint32_t inputCnt,
    uint64_t *output, uint32_t *outputCnt);
kern_return_t IOConnectCallStructMethod(mach_port_t connection, uint32_t selector, const void *inputStruct, size_t inputStructCnt,
    void *outputStruct, size_t *outputStructCnt);
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

@interface EAGLContext (HeroPresentPrivate)
- (BOOL)attachImage:(NSUInteger)image toCoreSurface:(IOSurfaceRef)surface invertedRender:(BOOL)inverted;
- (BOOL)texImageIOSurface:(IOSurfaceRef)ioSurface target:(NSUInteger)target internalFormat:(NSUInteger)internalFormat
                    width:(uint32_t)width height:(uint32_t)height format:(NSUInteger)format type:(NSUInteger)type
                    plane:(uint32_t)plane invert:(BOOL)invert;
@end

static io_connect_t gpu;

static void
say(const char *fmt, ...)
{
   char line[300];
   va_list ap;
   va_start(ap, fmt);
   int n = vsnprintf(line, sizeof(line), fmt, ap);
   va_end(ap);
   if (gpu != MACH_PORT_NULL && n > 0)
      IOConnectCallStructMethod(gpu, 5, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1), NULL, NULL);
}

static GLuint
shader(GLenum kind, const char *text)
{
   GLuint s = glCreateShader(kind);
   glShaderSource(s, 1, &text, NULL);
   glCompileShader(s);
   GLint ok = 0;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[256] = { 0 };
      glGetShaderInfoLog(s, sizeof(log) - 1, NULL, log);
      say("heropresent: shader %#x: %s", kind, log);
   }
   return s;
}

int
main(void)
{
   @autoreleasepool {
      io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("HeroGPU"));
      if (svc == MACH_PORT_NULL || IOServiceOpen(svc, mach_task_self(), 0, &gpu) != KERN_SUCCESS)
         return 1;
      setenv("BGL_WRAPSAMPLE", "1", 1);            /* the frame is sampled where CoreAnimation drew it */
      if (!dlopen("/private/var/herolte/lib/libheroeagl.dylib", RTLD_NOW)) {
         say("heropresent: no EAGL shim: %s", dlerror());
         return 1;
      }
      uint64_t panel[4] = { 0 };
      uint32_t n = 4;
      IOConnectCallScalarMethod(gpu, 10, NULL, 0, panel, &n);
      IOSurfaceRef target = panel[0] ? IOSurfaceLookup((IOSurfaceID)panel[0]) : NULL;
      EAGLContext *ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
      if (target == NULL || ctx == nil || ![EAGLContext setCurrentContext:ctx]) {
         say("heropresent: panel surface %llu %s, context %p", panel[0], target ? "found" : "missing", ctx);
         return 1;
      }
      uint32_t pw = (uint32_t)panel[1], ph = (uint32_t)panel[2];

      /* The panel: a texture whose storage is the panel surface itself (direct GPU render target). */
      GLuint fbo = 0, dst = 0;
      glGenTextures(1, &dst);
      glBindTexture(GL_TEXTURE_2D, dst);
      BOOL attached = [ctx attachImage:GL_TEXTURE_2D toCoreSurface:target invertedRender:NO];
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
      GLenum complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);

      static const char *vs =
         "#version 300 es\n"
         "out vec2 uv;\n"
         "void main() {\n"
         "  vec2 p = vec2(float((gl_VertexID & 1) << 1), float(gl_VertexID & 2));\n"
         "  uv = vec2(p.x * 0.5, 1.0 - p.y * 0.5);\n"
         "  gl_Position = vec4(p - 1.0, 0.0, 1.0);\n"
         "}\n";
      static const char *fs =
         "#version 300 es\n"
         "precision mediump float;\n"
         "uniform sampler2D frame;\n"
         "uniform float flip;\n"
         "in vec2 uv;\n"
         "out vec4 colour;\n"
         "void main() { colour = texture(frame, vec2(uv.x, flip > 0.5 ? 1.0 - uv.y : uv.y)); }\n";
      GLuint prog = glCreateProgram();
      glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
      glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
      glLinkProgram(prog);
      glUseProgram(prog);
      glUniform1i(glGetUniformLocation(prog, "frame"), 0);
      /* DROP_DIR heropresent.flip turns the picture over, should it come out upside down. */
      glUniform1f(glGetUniformLocation(prog, "flip"), access("/private/var/herolte/heropresent.flip", F_OK) == 0 ? 1.0f : 0.0f);
      GLuint vao = 0;
      glGenVertexArrays(1, &vao);
      glBindVertexArray(vao);
      glViewport(0, 0, (GLsizei)pw, (GLsizei)ph);
      glDisable(GL_BLEND);
      glDisable(GL_DEPTH_TEST);
      say("heropresent: panel surface %llu %ux%u attached %d, framebuffer %#x, GL error %#x: waiting for frames",
          panel[0], pw, ph, attached, complete, glGetError());

      /* Source textures, one per surface of CoreAnimation's swap chain. */
      enum { SOURCES = 8 };
      uint32_t ids[SOURCES] = { 0 };
      GLuint tex[SOURCES] = { 0 };
      IOSurfaceRef surf[SOURCES] = { NULL };
      BOOL bound[SOURCES] = { NO };
      unsigned next = 0, drawn = 0, failed = 0;
      uint64_t t_total = 0;
      for (;;) {
         uint64_t in = 500, f[6] = { 0 };
         n = 6;
         if (IOConnectCallScalarMethod(gpu, 8, &in, 1, f, &n) != KERN_SUCCESS || f[0] == 0)
            continue;
         uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
         uint32_t id = (uint32_t)f[0];
         int k = -1;
         for (int i = 0; i < SOURCES; i++)
            if (ids[i] == id) k = i;
         if (k < 0) {
            k = (int)(next++ % SOURCES);
            if (surf[k]) CFRelease(surf[k]);
            if (tex[k]) glDeleteTextures(1, &tex[k]);
            surf[k] = IOSurfaceLookup(id);
            ids[k] = surf[k] ? id : 0;
            bound[k] = NO;
            glGenTextures(1, &tex[k]);
         }
         BOOL ok = surf[k] != NULL;
         if (ok) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex[k]);
            /* Bound again every frame: bound once, the picture stayed black (boot-v95). The binding's CPU cache walk is
             * skipped for this process by the kernel (HeroGPU SyncBO), which made each frame cost ~40 ms (boot-v94). */
            bound[k] = [ctx texImageIOSurface:surf[k] target:GL_TEXTURE_2D internalFormat:GL_RGBA
                                        width:(uint32_t)IOSurfaceGetWidth(surf[k]) height:(uint32_t)IOSurfaceGetHeight(surf[k])
                                       format:GL_BGRA_EXT type:GL_UNSIGNED_BYTE plane:0 invert:NO];
            ok = bound[k];
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glFinish();
            ok = ok && glGetError() == GL_NO_ERROR;
         }
         uint64_t done[2] = { f[5], ok ? 1u : 0u };
         IOConnectCallScalarMethod(gpu, 9, done, 2, NULL, NULL);
         t_total += clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
         ok ? drawn++ : failed++;
         if ((drawn + failed) % 120 == 1)
            say("heropresent: %u drawn, %u failed, %.1f ms a frame (surface %u)", drawn, failed,
                t_total / 1e6 / (drawn + failed), id);
      }
   }
}

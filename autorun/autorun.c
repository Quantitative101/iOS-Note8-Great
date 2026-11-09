/*
 * /private/var/herolte/autorun for the Galaxy Note 8: runs once per boot as root (launchd job com.herolte.autorun).
 * 1. Writes launchd's lists of disabled jobs so that the services that only burn CPU on this phone are not started from
 *    the NEXT boot on: bluetoothd (no Bluetooth driver) and fairplayd were relaunched every few seconds, searchd and the
 *    Spotlight index extensions crashed and started again, ReportCrash then spent a core on every crash. The first group
 *    is the S7 port's list (proven there with the same iOS 16 root); the rest comes from this phone's boot logs.
 *    DROP_DIR disabled.list, one job label a line, adds to them with no new build; "#" starts a comment. Too many at
 *    once can leave SpringBoard waiting for ever, so extra jobs are best added a few at a time.
 * 2. Reduce Transparency and Reduce Motion, through the preferences the Accessibility pane writes (that pane crashes
 *    here, and the software renderer draws the blur effects badly). DROP_DIR ax.off turns this off.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <objc/message.h>
#include <objc/runtime.h>
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *);
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DROP_DIR "/private/var/herolte/"
#define LAUNCHD_DIR "/private/var/db/com.apple.xpc.launchd"
/* One mark per canvas: a new canvas size clears the cache once more. */
#define MG_MARK DROP_DIR "mgcache.gone.750x1542"

static const char *const jobs[] = {
   /* the S7 port's list */
   "com.apple.bluetoothd", "com.apple.ReportCrash", "com.apple.analyticsd", "com.apple.aggregated",
   "com.apple.homed", "com.apple.cloudd", "com.apple.searchd", "com.apple.voicemail.vmd",
   /* from this phone's logs: relaunched over and over */
   "com.apple.fairplayd.H2", "com.apple.wifip2pd", "com.apple.siriinferenced",
};

static void
write_list(const char *path)
{
   FILE *f = fopen(path, "w");
   if (!f)
      return;
   fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n", f);
   for (size_t i = 0; i < sizeof(jobs) / sizeof(jobs[0]); i++)
      fprintf(f, "\t<key>%s</key>\n\t<true/>\n", jobs[i]);
   FILE *extra = fopen(DROP_DIR "disabled.list", "r");
   char job[256];
   while (extra && fgets(job, sizeof(job), extra)) {
      job[strcspn(job, " \t\r\n")] = 0;
      if (job[0] && job[0] != '#')
         fprintf(f, "\t<key>%s</key>\n\t<true/>\n", job);
   }
   if (extra)
      fclose(extra);
   fputs("</dict>\n</plist>\n", f);
   fclose(f);
}

/*
 * MobileGestalt keeps its answers in a cache that outlives a change of the device tree: with /product artwork-display-gamut
 * "sRGB" (note8/tools/mkadt_greatlte.py) WebCore still had the iPhone X's P3 answer and drew pages into 10-bit surfaces that
 * nothing shows (the S7 port found this, hero-run.c). Once (DROP_DIR mgcache.gone marks it) the cache goes, and
 * MobileGestalt answers anew from the NEXT boot on. Idea and path from the S7 port's hero-run.
 */
static void
remove_mg_cache_once(void)
{
   const char *groups = "/private/var/containers/Shared/SystemGroup";
   if (access(MG_MARK, F_OK) == 0)
      return;
   close(open(MG_MARK, O_WRONLY | O_CREAT, 0644));
   DIR *d = opendir(groups);
   struct dirent *e;
   while (d && (e = readdir(d))) {
      char path[1024];
      if (e->d_name[0] == '.')
         continue;
      snprintf(path, sizeof(path), "%s/%s/Library/Caches/com.apple.MobileGestalt.plist", groups, e->d_name);
      unlink(path);
   }
   if (d)
      closedir(d);
}

static void
set_accessibility(void)
{
   CFStringRef domain = CFSTR("com.apple.Accessibility");
   CFStringRef user = CFSTR("mobile");
   const CFStringRef keys[] = { CFSTR("EnhancedBackgroundContrastEnabled"), CFSTR("ReduceMotionEnabled") };
   for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
      CFPreferencesSetValue(keys[i], kCFBooleanTrue, domain, user, kCFPreferencesAnyHost);
   CFPreferencesSynchronize(domain, user, kCFPreferencesAnyHost);
}

/*
 * DROP_DIR osmesa-test (the S7 port's gpu/mesa test: Mesa's Panfrost draws a triangle off screen through HeroGPU and logs
 * to the kernel log) runs once per boot when it is in the drop zone, in the background (iOS has no /bin/sh to delay it).
 */
static void
run_gl_test(void)
{
   if (access(DROP_DIR "osmesa-test", X_OK) != 0)
      return;
   pid_t pid;
   char *argv[] = { DROP_DIR "osmesa-test", NULL };
   char *envp[] = { NULL };
   posix_spawn(&pid, argv[0], NULL, NULL, argv, envp);
}

/* DROP_DIR heropresent (note8/heropresent): the panel copy on the GPU; runs for good, in the background. */
static void
run_present(void)
{
   if (access(DROP_DIR "heropresent", X_OK) != 0)
      return;
   pid_t pid;
   char *argv[] = { DROP_DIR "heropresent", NULL };
   char *envp[] = { NULL };
   posix_spawn(&pid, argv[0], NULL, NULL, argv, envp);
}

/*
 * DROP_DIR eagl_ca_test (the S7 port's gpu/eagl CoreAnimation test, CARenderer through the EAGL shim): run with the shim, its
 * output (stdout and stderr) to a file, then every line of it to the kernel log through the libdrm stand-in's herodrm_log.
 */
void herodrm_log(const char *fmt, ...) __attribute__((weak_import));

static void
run_ca_test(void)
{
   if (access(DROP_DIR "eagl_ca_test", X_OK) != 0)
      return;
   const char *out = DROP_DIR "eagl_ca_test.out";
   posix_spawn_file_actions_t fa;
   posix_spawn_file_actions_init(&fa);
   posix_spawn_file_actions_addopen(&fa, 1, out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   posix_spawn_file_actions_adddup2(&fa, 1, 2);
   char *argv[] = { DROP_DIR "eagl_ca_test", NULL };
   char *envp[] = { "EAGL_INJECT_LIB=/private/var/herolte/lib/libheroeagl.dylib", "EAGL_SHIM_DEBUG=1", NULL };
   pid_t pid;
   int status = -1, rc = posix_spawn(&pid, argv[0], &fa, NULL, argv, envp);
   posix_spawn_file_actions_destroy(&fa);
   if (rc == 0) {
      for (int t = 0; t < 120 && waitpid(pid, &status, WNOHANG) == 0; t++)
         sleep(1);
   }
   herodrm_log("autorun: eagl_ca_test spawn %d, status %#x", rc, status);
   FILE *f = fopen(out, "r");
   char line[400];
   for (int n = 0; f && n < 200 && fgets(line, sizeof(line), f); n++) {
      line[strcspn(line, "\n")] = 0;
      herodrm_log("ca_test: %s", line);
   }
   if (f)
      fclose(f);
}

/* herogl=3: what dyld loaded into backboardd (DYLD_PRINT_LIBRARIES to bbd-dyld.txt): our libraries, warnings and errors. */
static void
log_backboardd_dyld(void)
{
   FILE *f = fopen(DROP_DIR "bbd-dyld.txt", "r");
   char line[400];
   int total = 0, shown = 0;
   while (f && fgets(line, sizeof(line), f)) {
      total++;
      line[strcspn(line, "\n")] = 0;
      if (shown < 60 && (strstr(line, "herolte") || strstr(line, "OpenGLES") || strstr(line, "QuartzCore") ||
          strstr(line, "rror") || strstr(line, "arning") || strstr(line, "fail") || strstr(line, "not ") || total <= 3)) {
         herodrm_log("bbd-dyld: %s", line);
         shown++;
      }
   }
   herodrm_log("bbd-dyld: %d lines (%s)", total, f ? "file read" : "no file");
   if (f)
      fclose(f);
}

/*
 * DROP_DIR herotest: dyld's own account of DYLD_INSERT_LIBRARIES in a root-filesystem (platform) program, as backboardd
 * would get it: libherotest (says when its constructor runs) and the EAGL shim inserted, dyld's prints on, all output
 * to the kernel log.
 */
static void
run_dyld_probe(void)
{
   if (access(DROP_DIR "herotest2", X_OK) != 0)
      return;
   const char *out = DROP_DIR "herotest2.out";
   posix_spawn_file_actions_t fa;
   posix_spawn_file_actions_init(&fa);
   posix_spawn_file_actions_addopen(&fa, 1, out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   posix_spawn_file_actions_adddup2(&fa, 1, 2);
   /* herotest2 (the same program): no variables from here, the kernel adds them as for backboardd */
   char *argv[] = { DROP_DIR "herotest2", NULL };
   char *envp[] = { "PATH=/usr/bin:/bin", NULL };
   pid_t pid;
   int status = -1, rc = posix_spawn(&pid, argv[0], &fa, NULL, argv, envp);
   posix_spawn_file_actions_destroy(&fa);
   if (rc == 0) {
      for (int t = 0; t < 30 && waitpid(pid, &status, WNOHANG) == 0; t++)
         sleep(1);
   }
   herodrm_log("autorun: herotest spawn %d, status %#x", rc, status);
   FILE *f = fopen(out, "r");
   char line[400];
   int n = 0, total = 0;
   while (f && fgets(line, sizeof(line), f)) {
      total++;
      line[strcspn(line, "\n")] = 0;
      if (n < 80 && (strstr(line, "herolte") || strstr(line, "herotest") || strstr(line, "rror") || strstr(line, "arning") ||
          strstr(line, "fail") || strstr(line, "not ") || strstr(line, "insert") || total <= 4)) {
         herodrm_log("dyldprobe: %s", line);
         n++;
      }
   }
   herodrm_log("dyldprobe: %d lines", total);
   if (f)
      fclose(f);
}

/*
 * mediaserverd's audio HAL loads the Bluetooth audio plug-in, which looks up its XPC helper
 * (com.apple.BTAudioHALPlugin.xpc) about 930 times a second and never gets it: mediaserverd and launchd then spend more
 * than a core between them (greatlte_sysprof, 2026-11-05). There is no Bluetooth here, so the plug-in is renamed out of
 * the HAL's way (*.off); takes effect from the next mediaserverd start. DROP_DIR btaudio.keep turns this off.
 */
static void
hide_bt_audio(void)
{
   static const char *const dirs[] = { "/System/Library/Audio/Plug-Ins/HAL", "/System/Library/Audio/Plug-Ins",
      "/Library/Audio/Plug-Ins/HAL" };
   if (access(DROP_DIR "btaudio.keep", F_OK) == 0)
      return;
   FILE *note = fopen(DROP_DIR "btaudio.txt", "w");
   for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
      DIR *d = opendir(dirs[i]);
      struct dirent *e;
      while (d && (e = readdir(d))) {
         size_t n = strlen(e->d_name);
         if (strncmp(e->d_name, "BTAudioHALPlugin", 16) != 0 || (n > 4 && strcmp(e->d_name + n - 4, ".off") == 0))
            continue;
         char from[1024], to[1100];
         snprintf(from, sizeof(from), "%s/%s", dirs[i], e->d_name);
         snprintf(to, sizeof(to), "%s.off", from);
         int r = rename(from, to);
         if (herodrm_log)
            herodrm_log("autorun: %s -> .off: %d", from, r);
         if (note)
            fprintf(note, "%s %d\n", from, r);
      }
      if (d)
         closedir(d);
   }
   if (note)
      fclose(note);
}

/*
 * backboardd spent a third of its drawing in CoreAnimation's software 4x4 color matrix (greatlte_pcprof, 2026-11-05),
 * with the phone in Dark Mode. DROP_DIR nodim: Dark Mode without the wallpaper dimming (SpringBoard's
 * SBEnableWallpaperDimming). DROP_DIR light: Light Mode (UIKit's UserInterfaceStyleMode 1). Both take effect for
 * SpringBoard's next start.
 */
static void
set_appearance(void)
{
   CFStringRef user = CFSTR("mobile");
   if (access(DROP_DIR "nodim", F_OK) == 0) {
      CFPreferencesSetValue(CFSTR("SBEnableWallpaperDimming"), kCFBooleanFalse, CFSTR("com.apple.springboard"), user,
          kCFPreferencesAnyHost);
      CFPreferencesSynchronize(CFSTR("com.apple.springboard"), user, kCFPreferencesAnyHost);
   }
   /* DROP_DIR ax.reset: Reduce Transparency and Reduce Motion back off (ax.off alone only stops setting them). */
   if (access(DROP_DIR "ax.reset", F_OK) == 0) {
      CFStringRef ax = CFSTR("com.apple.Accessibility");
      CFPreferencesSetValue(CFSTR("EnhancedBackgroundContrastEnabled"), kCFBooleanFalse, ax, user, kCFPreferencesAnyHost);
      CFPreferencesSetValue(CFSTR("ReduceMotionEnabled"), kCFBooleanFalse, ax, user, kCFPreferencesAnyHost);
      CFPreferencesSynchronize(ax, user, kCFPreferencesAnyHost);
   }
   if (access(DROP_DIR "light", F_OK) == 0 || access(DROP_DIR "dark", F_OK) == 0) {
      int one = access(DROP_DIR "light", F_OK) == 0 ? 1 : 2;
      CFNumberRef mode = CFNumberCreate(NULL, kCFNumberIntType, &one);
      CFStringRef domain = CFSTR("com.apple.uikitservices.userInterfaceStyleMode");
      CFPreferencesSetValue(CFSTR("UserInterfaceStyleMode"), mode, domain, user, kCFPreferencesAnyHost);
      CFPreferencesSynchronize(domain, user, kCFPreferencesAnyHost);
      CFRelease(mode);
   }
}

/* Log channel: "spawn" a path that does not exist; the kernel prints "SPAWN FAILED <proc>: <path> -> 2". */
extern char **environ;

static void
alog(const char *format, ...)
{
   char line[400], path[420];
   va_list ap;
   va_start(ap, format);
   vsnprintf(line, sizeof line, format, ap);
   va_end(ap);
   snprintf(path, sizeof path, "/autorun/%s", line);
   pid_t pid;
   char *argv[2] = { path, NULL };
   posix_spawn(&pid, path, NULL, NULL, argv, environ);
}

/*
 * Apps from the drop zone (tools/drop.py app=FILE.ipa or app=DIR.app), after the S7 port's hero-run: DROP_DIR apps/NAME.app and
 * then apps/.ready. Each moves to /Applications, where a bundle of that name we did not put there is left alone, and
 * LaunchServices is told by fresh processes of this program, each with an alarm: never a wait on lsd for as long as the
 * phone runs. Not before the LaunchServices migrator has run and the system has settled (on the S7, at 20 s LSRegisterURL
 * failed; the rebuild it then asked for never ended, nor did SpringBoard's wait). DROP_DIR apps.remove, one NAME.app a
 * line, takes one of ours out. The apps need the no-container and no-sandbox entitlements (note8/apps/entitlements.plist).
 */
#define APPS_DIR DROP_DIR "apps/"
#define APP_MARK ".herolte-app"
#define MAX_APPS 32
#define LS_MIGRATOR "/System/Library/DataClassMigrators/00LaunchServicesMigrator.migrator"
#define LS_MIGRATED DROP_DIR "lsmigrator.done"

static id
nsstring(const char *text)
{
   return ((id (*)(id, SEL, const char *))objc_msgSend)((id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), text);
}

static id
file_url(const char *path)
{
   return ((id (*)(id, SEL, id))objc_msgSend)((id)objc_getClass("NSURL"), sel_registerName("fileURLWithPath:"), nsstring(path));
}

static int
remove_tree(const char *path)
{
   struct stat st;
   if (lstat(path, &st) != 0)
      return errno == ENOENT ? 0 : -1;
   if (!S_ISDIR(st.st_mode))
      return unlink(path);
   DIR *d = opendir(path);
   struct dirent *e;
   while (d && (e = readdir(d))) {
      char child[1024];
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
         continue;
      snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
      remove_tree(child);
   }
   if (d)
      closedir(d);
   return rmdir(path);
}

static void
remove_listed_apps(void)
{
   FILE *f = fopen(DROP_DIR "apps.remove", "r");
   char name[256];
   while (f && fgets(name, sizeof(name), f)) {
      char path[320], mark[360];
      name[strcspn(name, "\r\n")] = 0;
      size_t len = strlen(name);
      if (len < 5 || strchr(name, '/') || strcmp(name + len - 4, ".app") != 0)
         continue;
      snprintf(path, sizeof(path), "/Applications/%s", name);
      snprintf(mark, sizeof(mark), "%s/" APP_MARK, path);
      if (access(mark, F_OK) != 0) {
         alog("app %s: not one of ours, left alone", name);
         continue;
      }
      alog("app %s: taken out of /Applications (%d)", name, remove_tree(path));
   }
   if (f) {
      fclose(f);
      unlink(DROP_DIR "apps.remove");
   }
}

/* A fresh process: objc in a forked child of a threaded one aborts. */
static int
ls_migrate(void)
{
   alarm(120);
   void *pool = objc_autoreleasePoolPush();
   dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_NOW);
   id bundle = ((id (*)(id, SEL, id))objc_msgSend)((id)objc_getClass("NSBundle"), sel_registerName("bundleWithPath:"), nsstring(LS_MIGRATOR));
   BOOL loaded = bundle ? ((BOOL (*)(id, SEL))objc_msgSend)(bundle, sel_registerName("load")) : NO;
   id cls = bundle ? ((id (*)(id, SEL))objc_msgSend)(bundle, sel_registerName("principalClass")) : nil;
   alog("LS migrator: bundle %d, loaded %d, class %d", bundle != nil, loaded, cls != nil);
   if (cls) {
      id migrator = ((id (*)(id, SEL))objc_msgSend)(((id (*)(id, SEL))objc_msgSend)(cls, sel_registerName("alloc")), sel_registerName("init"));
      time_t start = time(NULL);
      BOOL done = ((BOOL (*)(id, SEL))objc_msgSend)(migrator, sel_registerName("performMigration"));
      alog("LS migrator: performMigration %d, %ld s", done, (long)(time(NULL) - start));
   }
   objc_autoreleasePoolPop(pool);
   return 0;
}

static int
register_apps(int argc, char **argv)
{
   alarm(120);
   void *pool = objc_autoreleasePoolPush();
   int failed = 0, status[MAX_APPS];
   dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_NOW);
   dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_NOW);
   int (*lsregister)(id, unsigned char) = (int (*)(id, unsigned char))dlsym(RTLD_DEFAULT, "LSRegisterURL");
   for (int i = 2; i < argc && i - 2 < MAX_APPS; i++)
      status[i - 2] = lsregister ? lsregister(file_url(argv[i]), 1) : -1;       /* as root */
   if (setgid(501) != 0 || setuid(501) != 0)
      alog("apps: cannot become mobile");
   Class ws = objc_getClass("LSApplicationWorkspace");
   id workspace = ws ? ((id (*)(id, SEL))objc_msgSend)((id)ws, sel_registerName("defaultWorkspace")) : nil;
   SEL reg = sel_registerName("registerApplication:");
   for (int i = 2; i < argc && i - 2 < MAX_APPS; i++) {
      BOOL ok = NO;
      if (workspace && ((BOOL (*)(id, SEL, SEL))objc_msgSend)(workspace, sel_registerName("respondsToSelector:"), reg))
         ok = ((BOOL (*)(id, SEL, id))objc_msgSend)(workspace, reg, file_url(argv[i]));
      alog("app %s: LSRegisterURL %d, registerApplication %d", argv[i], status[i - 2], ok);
      failed += status[i - 2] != 0 && !ok;
   }
   objc_autoreleasePoolPop(pool);
   return failed ? 1 : 0;
}

static int
run_self(char **args)
{
   pid_t pid;
   int status = 0, err = posix_spawn(&pid, args[0], NULL, NULL, args, environ);
   if (err == 0)
      waitpid(pid, &status, 0);
   alog("%s: spawn %d, status %#x", args[1], err, status);
   return err || !WIFEXITED(status) ? -1 : WEXITSTATUS(status);
}

int memorystatus_control(uint32_t command, int32_t pid, uint32_t flags, void *buffer, size_t buffersize);

static void
install_apps(void)
{
   static char paths[MAX_APPS][320];
   char *args[MAX_APPS + 3] = { DROP_DIR "autorun", "--register-apps" };
   int n = 0;
   if (access(APPS_DIR ".ready", F_OK) != 0)
      return;
   DIR *d = opendir(APPS_DIR);
   struct dirent *e;
   while (d && (e = readdir(d)) && n < MAX_APPS) {
      size_t len = strlen(e->d_name);
      char from[320], mark[360];
      struct stat st;
      if (len < 5 || strcmp(e->d_name + len - 4, ".app") != 0)
         continue;
      snprintf(from, sizeof(from), APPS_DIR "%s", e->d_name);
      snprintf(paths[n], sizeof(paths[n]), "/Applications/%s", e->d_name);
      snprintf(mark, sizeof(mark), "%s/" APP_MARK, paths[n]);
      if (lstat(paths[n], &st) == 0) {
         if (access(mark, F_OK) != 0) {
            alog("app %s: /Applications has its own by that name; left alone", e->d_name);
            continue;
         }
         remove_tree(paths[n]);
      }
      if (rename(from, paths[n]) != 0) {
         alog("app %s: cannot move it: errno %d", e->d_name, errno);
         continue;
      }
      close(open(mark, O_WRONLY | O_CREAT, 0644));
      alog("app %s: put in /Applications", e->d_name);
      args[2 + n] = paths[n];
      n++;
   }
   if (d)
      closedir(d);
   unlink(APPS_DIR ".ready");
   args[2 + n] = NULL;
   if (n == 0)
      return;
   /* boot-app1: SIGKILLed while it slept, as an idle daemon; memorystatus band 10 (MEMORYSTATUS_CMD_SET_PRIORITY_PROPERTIES). */
   struct { int32_t priority; uint64_t user_data; } band = { 10, 0 };
   alog("apps: memorystatus band 10: %d", memorystatus_control(2, getpid(), 0, &band, sizeof(band)));
   sleep(45);
   if (access(LS_MIGRATED, F_OK) != 0) {
      char *migrate[] = { DROP_DIR "autorun", "--ls-migrate", NULL };
      close(open(LS_MIGRATED, O_WRONLY | O_CREAT, 0644));
      run_self(migrate);
   }
   alog("apps: %d given to LaunchServices: %d", n, run_self(args));
}

int
main(int argc, char **argv)
{
   if (argc > 1 && strcmp(argv[1], "--register-apps") == 0)
      return register_apps(argc, argv);
   if (argc > 1 && strcmp(argv[1], "--ls-migrate") == 0)
      return ls_migrate();
   remove_listed_apps();
   set_appearance();
   if (access(DROP_DIR "present.on", F_OK) == 0)
      run_present();
   hide_bt_audio();
   mkdir(LAUNCHD_DIR, 0755);
   write_list(LAUNCHD_DIR "/disabled.plist");           /* the system domain */
   write_list(LAUNCHD_DIR "/disabled.501.plist");       /* the mobile user's domain (searchd and friends) */
   if (access(DROP_DIR "ax.off", F_OK) != 0)
      set_accessibility();
   remove_mg_cache_once();
   install_apps();
   return 0;
}

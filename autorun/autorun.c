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
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
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

int
main(void)
{
   if (access(DROP_DIR "present.on", F_OK) == 0)
      run_present();
   hide_bt_audio();
   mkdir(LAUNCHD_DIR, 0755);
   write_list(LAUNCHD_DIR "/disabled.plist");           /* the system domain */
   write_list(LAUNCHD_DIR "/disabled.501.plist");       /* the mobile user's domain (searchd and friends) */
   if (access(DROP_DIR "ax.off", F_OK) != 0)
      set_accessibility();
   remove_mg_cache_once();
   return 0;
}

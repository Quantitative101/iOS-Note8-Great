/*
 * The dyld injection probe (Note 8): autorun starts this with DYLD_INSERT_LIBRARIES and dyld's print variables, its
 * output to a file. herotest prints its own environment's DYLD_* variables and whether the inserted library's
 * constructor ran; libherotest's constructor says so itself. The kernel gives processes named "herotest" the same
 * dyld policy as backboardd (bsd/dev/herolte_gl.c).
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef HEROTEST_LIB
int herotest_lib_loaded;

__attribute__((constructor)) static void
herotest_lib_init(void)
{
   herotest_lib_loaded = 1;
   printf("libherotest: constructor ran in pid %d\n", getpid());
   fflush(stdout);
}
#else
extern char **environ;

int
main(void)
{
   printf("herotest: main in pid %d\n", getpid());
   for (char **e = environ; *e; e++) {
      if (strncmp(*e, "DYLD_", 5) == 0 || strncmp(*e, "HEROTEST", 8) == 0)
         printf("herotest: env %s\n", *e);
   }
   fflush(stdout);
   return 0;
}
#endif

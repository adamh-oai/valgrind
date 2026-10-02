/* Check capture boundaries using watched data addresses, not architecture-
   dependent instruction counts or symbol addresses affected by ASLR. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "valgrind.h"
#include "callgrind/callgrind.h"
#include "runtime_inline.h"

static volatile unsigned watched[16];
static FILE *manifest;

void lackey_test_beta(volatile unsigned *destination);

__attribute__((noinline))
void lackey_test_alpha(volatile unsigned *destination)
{
   *destination = 23;
}

__attribute__((noinline))
void lackey_test_inline_caller(volatile unsigned *destination)
{
   lackey_inline_target(destination);
}

__attribute__((noinline))
void lackey_test_tail(volatile unsigned *included, volatile unsigned *excluded)
{
   *included = 31;
   /* At -O2 this is a tail jump on the primary platforms. The selected and
      excluded instructions may be chased into one translated superblock. */
   lackey_test_beta(excluded);
}

static void command(const char *text)
{
   assert(VALGRIND_MONITOR_COMMAND(text) == 0);
}

static void expect(const char *output, unsigned slot, unsigned stores)
{
   assert(fprintf(manifest, "%s\t%u\t%p\t%u\n", output, slot,
                  (const void *)&watched[slot], stores) > 0);
}

static void control(void)
{
   unsigned i;
   /* The function is already translated before the first enable request. */
   lackey_test_alpha(&watched[0]);
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[1]);
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[2]);
   CALLGRIND_STOP_INSTRUMENTATION;
   lackey_test_alpha(&watched[3]);
   CALLGRIND_START_INSTRUMENTATION;
   lackey_test_alpha(&watched[4]);
   command("lackey instrumentation off");
   lackey_test_alpha(&watched[5]);
   for (i = 0; i < 6; i++)
      expect("runtime-control.trace", i, i == 1 || i == 2 || i == 4);
}

static void function_filter(void)
{
   unsigned i;
   lackey_test_alpha(&watched[0]);
   lackey_test_beta(&watched[1]);
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[2]);
   lackey_test_beta(&watched[3]);
   command("lackey function clear");
   command("lackey function add lackey_test_?eta");
   lackey_test_alpha(&watched[4]);
   lackey_test_beta(&watched[5]);
   command("lackey function add lackey_test_a*");
   lackey_test_alpha(&watched[6]);
   lackey_test_beta(&watched[7]);
   command("lackey function clear");
   command("lackey function add does_not_match");
   lackey_test_alpha(&watched[8]);
   command("lackey instrumentation off");
   for (i = 0; i < 9; i++)
      expect("runtime-function.trace", i, i == 2 || i == 5 || i == 6 || i == 7);
}

static void file_filter(void)
{
   unsigned i;
   command("lackey file add runtime_control.c");
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[0]);
   lackey_test_beta(&watched[1]);
   command("lackey file clear");
   command("lackey file add *runtime_other.c");
   lackey_test_alpha(&watched[2]);
   lackey_test_beta(&watched[3]);
   command("lackey file add *runtime_control.c");
   lackey_test_alpha(&watched[4]);
   lackey_test_beta(&watched[5]);
   command("lackey function clear");
   command("lackey function add lackey_test_a*");
   lackey_test_alpha(&watched[6]);
   lackey_test_beta(&watched[7]);
   command("lackey file clear");
   command("lackey file add */runtime_control.c");
   lackey_test_alpha(&watched[8]);
   command("lackey instrumentation off");
   for (i = 0; i < 9; i++)
      expect("runtime-file.trace", i, i == 0 || i == 3 || i == 4 || i == 5 || i == 6 || i == 8);
}

static void missing_debug_info(void)
{
   command("lackey file add *");
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[0]);
   command("lackey file clear");
   /* -g0 removes source information, but the function symbol remains. */
   lackey_test_alpha(&watched[1]);
   command("lackey filters clear");
   lackey_test_alpha(&watched[2]);
   command("lackey instrumentation off");
   expect("runtime-nodebug.trace", 0, 0);
   expect("runtime-nodebug.trace", 1, 1);
   expect("runtime-nodebug.trace", 2, 1);
}

static void output_switch(void)
{
   unsigned i;
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[0]);
   command("lackey output runtime-output second.trace");
   lackey_test_alpha(&watched[1]);
   command("lackey output runtime-output.trace");
   lackey_test_alpha(&watched[2]);
   /* The existing trace is a regular file, so opening a child must fail. */
   assert(VALGRIND_MONITOR_COMMAND("lackey output runtime-output.trace/child") == 1);
   lackey_test_alpha(&watched[3]);
   command("lackey output log");
   lackey_test_alpha(&watched[4]);
   command("lackey instrumentation off");
   lackey_test_alpha(&watched[5]);
   for (i = 0; i < 6; i++) {
      expect("runtime-output.trace", i, i == 0 || i == 2 || i == 3);
      expect("runtime-output second.trace", i, i == 1);
      expect("runtime-output.diagnostics", i, i == 4);
   }
}

static void fork_control(void)
{
   unsigned i;
   int status;
   pid_t child;
   lackey_test_alpha(&watched[0]);
   child = fork();
   assert(child >= 0);
   if (child == 0) {
      command("lackey output runtime-fork-child.trace");
      command("lackey instrumentation on");
      lackey_test_alpha(&watched[1]);
      command("lackey instrumentation off");
      _exit(0);
   }
   assert(waitpid(child, &status, 0) == child);
   assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
   lackey_test_alpha(&watched[2]);
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[3]);
   command("lackey instrumentation off");
   for (i = 0; i < 4; i++) {
      expect("runtime-fork.trace", i, i == 3);
      expect("runtime-fork-child.trace", i, i == 1);
   }
}

static void no_capture(int enabled)
{
   if (enabled) command("lackey instrumentation on");
   lackey_test_alpha(&watched[0]);
   if (enabled) command("lackey instrumentation off");
   expect(enabled ? "runtime-empty.trace" : "runtime-never.trace", 0, 0);
}

static void inline_filter(void)
{
   command("lackey instrumentation on");
   lackey_test_inline_caller(&watched[0]);
   command("lackey instrumentation off");
   expect("runtime-inline.trace", 0, 1);
}

static void tail_filter(void)
{
   command("lackey instrumentation on");
   lackey_test_tail(&watched[0], &watched[1]);
   command("lackey instrumentation off");
   expect("runtime-tail.trace", 0, 1);
   expect("runtime-tail.trace", 1, 0);
}

static void exec_capture(void)
{
   expect("runtime-exec.trace", 0, 1);
   assert(fclose(manifest) == 0);
   puts("complete");
   assert(fflush(stdout) == 0);
   command("lackey instrumentation on");
   lackey_test_alpha(&watched[0]);
   /* A successful exec does not run the old tool's fini callback. A partial
      output buffer must be drained before replacing the client process. */
   execl("../../tests/true", "true", (char *)NULL);
   perror("exec ../../tests/true");
   assert(0);
}

int main(int argc, char **argv)
{
   char path[128];
   assert(argc == 2);
   assert(RUNNING_ON_VALGRIND != 0);
   assert(snprintf(path, sizeof(path), "runtime-%s.addresses", argv[1]) > 0);
   manifest = fopen(path, "w");
   assert(manifest != NULL);
   if (strcmp(argv[1], "control") == 0) control();
   else if (strcmp(argv[1], "function") == 0) function_filter();
   else if (strcmp(argv[1], "file") == 0) file_filter();
   else if (strcmp(argv[1], "nodebug") == 0) missing_debug_info();
   else if (strcmp(argv[1], "output") == 0) output_switch();
   else if (strcmp(argv[1], "fork") == 0) fork_control();
   else if (strcmp(argv[1], "never") == 0) no_capture(0);
   else if (strcmp(argv[1], "empty") == 0) no_capture(1);
   else if (strcmp(argv[1], "inline") == 0) inline_filter();
   else if (strcmp(argv[1], "tail") == 0) tail_filter();
   else if (strcmp(argv[1], "exec") == 0) exec_capture();
   else assert(0);
   assert(fclose(manifest) == 0);
   puts("complete");
   return 0;
}

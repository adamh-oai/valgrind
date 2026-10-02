include $(top_srcdir)/Makefile.tool-tests.am

dist_noinst_SCRIPTS = filter_stderr check_runtime_trace

check_PROGRAMS = runtime_control runtime_nodebug
AM_CFLAGS += $(AM_FLAG_M3264_PRI)
runtime_control_SOURCES = runtime_control.c runtime_other.c
runtime_control_CFLAGS = $(AM_CFLAGS) -O2
runtime_nodebug_SOURCES = runtime_control.c runtime_other.c
runtime_nodebug_CFLAGS = $(AM_CFLAGS) -O2 -g0
noinst_HEADERS = runtime_inline.h

EXTRA_DIST = true.stderr.exp true.vgtest \
	runtime-control.vgtest runtime-control.stdout.exp runtime-control.stderr.exp runtime-control.post.exp \
	runtime-function.vgtest runtime-function.stdout.exp runtime-function.stderr.exp runtime-function.post.exp \
	runtime-file.vgtest runtime-file.stdout.exp runtime-file.stderr.exp runtime-file.post.exp \
	runtime-nodebug.vgtest runtime-nodebug.stdout.exp runtime-nodebug.stderr.exp runtime-nodebug.post.exp \
	runtime-output.vgtest runtime-output.stdout.exp runtime-output.stderr.exp runtime-output.post.exp \
	runtime-fork.vgtest runtime-fork.stdout.exp runtime-fork.stderr.exp runtime-fork.post.exp \
	runtime-never.vgtest runtime-never.stdout.exp runtime-never.stderr.exp runtime-never.post.exp \
	runtime-empty.vgtest runtime-empty.stdout.exp runtime-empty.stderr.exp runtime-empty.post.exp \
	runtime-inline.vgtest runtime-inline.stdout.exp runtime-inline.stderr.exp runtime-inline.post.exp \
	runtime-tail.vgtest runtime-tail.stdout.exp runtime-tail.stderr.exp runtime-tail.post.exp \
	runtime-exec.vgtest runtime-exec.stdout.exp runtime-exec.stderr.exp runtime-exec.post.exp \
	rust-requests/Cargo.toml rust-requests/Cargo.lock rust-requests/src/main.rs rust-requests/README.md

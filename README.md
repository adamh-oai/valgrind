# Valgrind with runtime-controlled Lackey

This fork adds selective instrumentation and runtime control to Lackey. It is
based on the upstream **Valgrind 3.27.1** release. The original Valgrind
documentation is in [README](README) and [README_DEVELOPERS](README_DEVELOPERS).

The additions let a program warm up without Lackey instrumentation, select
native functions and source files using debug information, enable tracing, and
disable it again. Changing instrumentation or filters discards cached
translations so previously executed code follows the new settings.

Control works with the published **`valgrind-requests = "1.1.0"`** Rust crate.
No patched Rust crate is required. The same requests can be called from a PyO3
extension.

## Build

On Linux, with a C/C++ compiler, GNU Make, Autoconf, and Automake installed:

```sh
git clone https://github.com/adamh-oai/valgrind.git
cd valgrind
./autogen.sh
./configure --enable-only64bit
make -j8
```

Run `./vg-in-place` from the build tree, or use `make install` with the
appropriate `--prefix` supplied to `configure`. The `--enable-only64bit` flag
avoids needing a 32-bit compiler and libraries on an x86-64 host.

## Choose what to instrument

```sh
./vg-in-place --tool=lackey \
    --instr-atstart=no \
    --trace-mem=yes --basic-counts=no \
    '--trace-function=hot_loop*' \
    '--trace-file=*/kernels/*.c' \
    --trace-output=memory.trace \
    ./my-benchmark
```

Compile the benchmark with debug information (`-g` for C/C++, or Rust debug
information enabled in the selected Cargo profile). Quote patterns so the
shell does not expand them.

| Option | Meaning |
| --- | --- |
| `--instr-atstart=yes\|no` | Start with instrumentation enabled or disabled; default `yes`. |
| `--trace-function=<glob>` | Include native function names matching the pattern. Repeat to add alternatives. |
| `--trace-file=<glob>` | Include source filenames or debug paths matching the pattern. Repeat to add alternatives. |
| `--trace-output=<path>` | Append trace records to a separate file. By default, use Valgrind's log. |

Patterns use `*` and `?`. Alternatives within one category are ORed; when both
function and file filters are present, both categories must match. An empty
category imposes no restriction. Code without the information required by an
active filter does not match that filter.

Selection happens at individual guest instruction boundaries, including when
Valgrind combines instructions from multiple functions into one translated
block. This omits instrumentation for excluded instructions, rather than
generating a full trace and filtering its output later. Calls from a selected
function do not automatically select the called functions.

Use `--read-inline-info=yes` to include inline function information in matching.
File filters follow the instruction's source location; inlined code can point
to a header or library source file rather than the caller's file.
These are **native** debug-info names and files. Python bytecode function names
are not native symbols. JIT-generated code must have debug information that
Valgrind can resolve to match these filters; otherwise it can still be traced
with no filters.

## Control from Rust or PyO3

```toml
[dependencies]
valgrind-requests = "1.1.0"
```

The existing Callgrind start/stop requests are also understood by this Lackey
fork:

```rust
use valgrind_requests::{callgrind, valgrind};

// Launch with --tool=lackey --instr-atstart=no.
assert!(valgrind::running_on_valgrind() > 0);
warmup();
callgrind::start_instrumentation();
measured_work();
callgrind::stop_instrumentation();
```

Filters and the output destination can be changed through the crate's generic
monitor-command request:

```rust
use std::ffi::CString;
use valgrind_requests::valgrind;

fn lackey(command: &str) {
    let command = CString::new(command).expect("command contains a NUL byte");
    assert!(valgrind::monitor_command(command), "Lackey command failed");
}

// Configure while instrumentation is off, then enable it once.
lackey("lackey filters clear");
lackey("lackey function add hot_loop*");
lackey("lackey file add */kernels/*.c");
lackey("lackey output measurement.trace");
lackey("lackey instrumentation on");
measured_work();
lackey("lackey instrumentation off");
```

| Monitor command | Effect |
| --- | --- |
| `lackey instrumentation on` / `off` | Enable or disable instrumentation, discarding cached translations when the state changes. |
| `lackey function add <glob>` | Add a function pattern and invalidate cached translations. |
| `lackey function clear` | Remove function restrictions and invalidate cached translations. |
| `lackey file add <glob>` | Add a source-file pattern and invalidate cached translations. |
| `lackey file clear` | Remove source-file restrictions and invalidate cached translations. |
| `lackey filters clear` | Remove both categories of restrictions and invalidate cached translations. |
| `lackey output <path>` | Flush the current trace output and append subsequent records to the new file. |
| `lackey output log` | Flush the current trace file and resume using Valgrind's log. |
| `lackey status` | Print the current configuration. |

The same commands can be sent with `vgdb`, for example
`vgdb "lackey instrumentation on"`. In C/C++, use
`VALGRIND_MONITOR_COMMAND("lackey instrumentation on")` from `valgrind.h`, or
`CALLGRIND_START_INSTRUMENTATION` / `CALLGRIND_STOP_INSTRUMENTATION` from
`callgrind.h`.

Instrumentation state and filters apply to the whole process. Disabling
instrumentation keeps the program running under Valgrind; it removes Lackey's
measurement hooks from future execution. Counters accumulate across enabled
intervals. Configuration uses monitor requests because their tool callback can
safely discard translations; `VALGRIND_CLO_CHANGE` is not the runtime interface
for these settings.

## Trace output

Separate trace files are buffered to avoid a write syscall for each record.
Switching destinations, stopping instrumentation, and normal process exit
flush buffered output. Files are opened in append mode: switching A → B → A
preserves both intervals in A. Remove old trace files before a fresh run when
you want independent results.

A failed attempt to open a destination reports an error and leaves the existing
destination active. Trace output is separate from Valgrind diagnostics and
Lackey's summary, which continue to use the normal Valgrind log.

## Validate

After building:

```sh
make -C tests check
make -C lackey/tests check
tests/vg_regtest --no-bunsen lackey/tests
```

After editing `Makefile.am`, run `automake -a && ./config.status` before
rebuilding; the default build does not automatically regenerate these files.

The regressions check the actual stores made by selected functions, including
calls before and after translation-cache invalidation, function and file
patterns, missing source information, output switching and failure, process
forks, and runs with no selected instructions. They do not rely on fixed
addresses or architecture-specific total instruction counts.

An optional [Rust integration fixture](lackey/tests/rust-requests/README.md)
exercises the published `valgrind-requests` crate against the built fork.

## License and upstream

The fork retains Valgrind's [GPL license](COPYING) and upstream history.
Upstream development is at
[sourceware.org](https://sourceware.org/git/valgrind.git). The GitHub fork is
connected to the [Valgrind Git mirror](https://github.com/PragsPud/valgrind-git-mirror).

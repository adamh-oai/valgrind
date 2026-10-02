# Rust client-request smoke test

This optional fixture exercises the public `valgrind-requests` 1.1.0 API against
the Lackey fork. It uses monitor requests for filters and output, and both monitor
requests and Callgrind requests for instrumentation. No crate fork, C shim, or
custom assembly is needed. It is separate from the C regression suite so running
the upstream tests does not require Cargo or network access.

Build the fixture with the fork's installed headers (the include root must contain
`valgrind/valgrind.h` and the other tool headers):

```sh
export CARGO_TARGET_DIR="${TMPDIR:-/tmp}/lackey-requests-smoke-target"
VALGRIND_REQUESTS_VALGRIND_INCLUDE=/path/to/install/include cargo build --locked
```

Keep Cargo's generated tree outside `lackey/tests`: the upstream regression runner
recursively scans that directory for tests.

From this directory, remove only previous fixture outputs and run the built fork:

```sh
rm -f rust-requests.trace rust-requests-second.trace rust-requests.addresses
/path/to/install/bin/valgrind --tool=lackey --basic-counts=no \
  --trace-mem=yes --instr-atstart=no "$CARGO_TARGET_DIR/debug/lackey-requests-smoke"
perl ../check_runtime_trace rust-requests.addresses
```

The fixture must print `complete`, and the checker must print
`all watched stores matched`. The checker verifies that warmup, disabled calls,
and newly excluded calls are absent, while the two enabled calls appear in their
respective output files exactly once. Reusing an already translated target tests
translation invalidation. Native execution deliberately fails to prevent a false
pass from no-op client requests.

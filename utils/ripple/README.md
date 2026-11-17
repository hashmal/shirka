# ripple

A persistent Shirka REPL. The loop, command dispatch, help, and orchestration
live in `ripple.shk`; the C host supplies terminal input and safe access to a
separate user session. The original `examples/repl.shk` remains available.

## Build and launch

From the repository root:

```sh
make ripple
utils/ripple/ripple
```

Initial support is Unix-like systems; tested on macOS with GNU Readline 8.3.
GNU Readline supplies editing, history navigation, and bracketed paste. On macOS,
install it with `brew install readline`; on Debian/Ubuntu, install
`libreadline-dev`. The Makefile detects the usual Homebrew paths. For another
installation, override `RIPPLE_CPPFLAGS` and `RIPPLE_LIBS`, for example:

```sh
make ripple RIPPLE_CPPFLAGS=-I/opt/readline/include \
  RIPPLE_LIBS='-L/opt/readline/lib -lreadline -lm'
```

The normal `make` interpreter target does not depend on Readline. The host embeds
this checkout's resource path at build time so launching from another directory
works. Moving the checkout requires rebuilding ripple. This is not an installed
system-wide runtime yet.

Readline is GPL-licensed. Linking a distributed ripple executable has GPL
obligations; the Shirka source remains MIT-licensed. No third-party source is
vendored here. Readline's own license is authoritative for redistribution.

## Session

```text
> 2
2 > 3
2 3 > +
5 > 42 -> x
5 > x
5 42 >
```

Values and top-level reserves/operation definitions survive submissions. Input is
evaluated on the actual user stack, never on `try`'s empty local stack. Results
are not printed or consumed automatically. The prompt previews at most three
values, bottom-to-top, without cloning or executing them. `/stack` shows indexed
values with the top at index zero.

Examples:

```shirka
[1 +] => increment
increment
[1 TRUE +] try
```

The last example explicitly requests Shirka's normal isolated-stack `try`.
Repeated `=>` definitions in the same session still follow Shirka's existing
no-redefinition rule. Rebinding values with `->` retains older bindings as the
interpreter currently specifies; ripple does not change this language behavior.

## Commands

- `/help`: show commands.
- `/exit`: exit.
- `/stack`: inspect up to 256 stack values, each bounded to 511 bytes/depth six.
- `/clear`: destroy stack values, keeping definitions.
- `/load PATH`: execute a file in the session, preserving its top-level bindings.

Commands are exact, complete single-line submissions, starting at column zero.
`/load` takes the remainder of the line as a path, including spaces; it does not
perform shell expansion or remove shell quotes. Commands within multiline code,
strings, or comments are not dispatched. An unknown slash-prefixed name is
ordinary Shirka code. Use `/help` inside a larger program as an ordinary identifier;
a line consisting exactly of `/help` is reserved for the REPL command.

## Errors and ownership

Syntax errors make no evaluation changes. Runtime failures are **not
transactional**: completed stack operations and session definitions remain.
Pending instructions and scopes belonging to unfinished nested calls are freed.
Operand-validation failures generally leave their operands on the user stack.
The next prompt reflects the resulting stack; use `/clear` when desired.

One persistent session scope is kept above the prelude. Each submission executes
without adding another root scope. A private trailing literal, removed by the
host on successful return, makes returning to the host a real continuation. This
prevents the existing tail-call optimization from running a final callee's local
reserves in the persistent session scope. Tail calls *within* submitted programs
retain the existing interpreter behavior; ripple does not fix the language's
known tail-scope issues.

The controller uses its own stack/environment, inaccessible to submitted code.
On exit, session and controller stacks, scopes, pending ASTs, and input buffers
are freed. Intentional global symbol interning remains unchanged.

## Input and inspection

Readline supplies arrows, insertion, Backspace/Delete, Home/End, Ctrl-A/Ctrl-E,
Alt-B/Alt-F word movement, history arrows and reverse search, and bracketed paste.
Your Readline configuration may customize bindings. Multiline lists, prefix
expressions, and strings get `... ` continuation prompts. The **actual parser**
returns complete/error/incomplete status; there is no second nesting parser.
Incomplete EOF is reported, and pending input is discarded on exit.

Ctrl-C cancels the entire pending submission. During evaluation it requests
cooperative cancellation, checked between interpreter tokens, including nested
`try` evaluations. The signal handler only sets atomic flags. Blocking native I/O
or a long parser/native operation need not stop immediately; cancellation is not
a preemptive timeout. SIGTERM/SIGHUP request orderly shutdown by the same route.
Terminal settings are restored on normal exits and these supported signals;
SIGKILL and fatal process corruption cannot be recovered.

`skO_inspect` is a reusable bounded inspector; it leaves `print` unchanged. It
preserves brackets, distinguishes `Symbol(name)` from `:name`, and shows booleans and escaped
characters. Short complete character lists use quoted string notation; long or
mixed lists use bracket notation. Nonprintable bytes use visible `\xNN` notation
(not new parser syntax). Ellipses indicate depth or length truncation. Preview
values are limited to 127 bytes/depth three. It never scans a whole large stack
or deep-copies values. Inspection is diagnostic, not a serialization format.

Source submissions are limited to 1 MiB. Literal ASCII terminal-control bytes, including
ESC and NUL, are rejected before parsing; use Shirka escape syntax for character
data. Bracketed paste is inserted as text, not interpreted as editor commands.
Explicit Shirka output still behaves normally and may itself contain control
characters. Input highlighting is deferred: Readline has no simple token-color
callback, and a custom redisplay layer would complicate editing. No syntax colors
are emitted, including when `NO_COLOR` is set. Readline still uses terminal-control
sequences for interactive editing.

History uses `$XDG_STATE_HOME/ripple/history`, or
`$HOME/.local/state/ripple/history`. It is private (0600), capped at 500 entries,
and entries larger than 8192 bytes are not recorded. Unavailable storage is
nonfatal; avoid simultaneous sessions if history merge behavior matters (last
writer wins).

## Streams and tests

```sh
printf '2\n3\n+ puts\n' | utils/ripple/ripple
make test-ripple
make test
make test-reclamation
```

Pipes, redirected output, and `TERM=dumb` use plain stream input: no prompts, ANSI
editing, or history writes. Errors are reported on stderr, processing continues,
and the final stream exit status is nonzero if any submission failed.

`make test-ripple` uses C parser/inspection checks and Unix pseudo-terminals to
exercise real editing, paste, history persistence, Ctrl-C cancellation and
execution interruption, EOF, `/load`, and terminal restoration. The existing
allocation-counter test also exercises 100 successful and failing submissions
against one persistent environment. Native calls, parsing, and Readline internals
are not promised to have bounded execution latency.

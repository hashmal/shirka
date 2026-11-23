# ripple

A persistent Shirka REPL. The loop, multiline assembly, command recognition and
dispatch, history decisions, error policy, help, and stack presentation live in
`ripple.shk`. The C host supplies terminal input and safe access to a separate
user session. The original `examples/repl.shk` remains available.

## Implementation boundary

The controller carries pending source, accumulated success, and a loop condition
on its own stack. Native code has no hidden current submission or command name.
`ripple/command`, `ripple/submit`, `ripple/step`, and `ripple/stack` are ordinary
Shirka operations. `/exit` produces the false loop condition; it needs no native
stop operation. Adding a command requires editing only `ripple.shk`.

Eight private intrinsics are available to the controller:

| Intrinsic | Stack effect | Native responsibility |
| --- | --- | --- |
| `ripple/read-line` | `prompt -- text status` | One editor/stream read; status is `:line`, `:cancel`, `:eof`, `:invalid`, or `:error` (unrecoverable input failure). |
| `ripple/parse` | `text -- instructions status` | Actual parser; `:ready`, `:more`, or `:invalid`. Non-ready results carry an empty instruction list. |
| `ripple/execute` | `instructions -- success?` | Consume instructions and execute on the persistent user stack, catching errors and unwinding unfinished calls. |
| `ripple/parse-file` | `path -- instructions success?` | Read and parse a whole file; recover from file/parser errors without executing anything. Failure returns an empty instruction list. |
| `ripple/stack-view` | `count width depth -- descriptions omitted?` | Bounded character-list descriptions, top first; no session values are copied or consumed. Limits are 256 values, 511 bytes, depth six. |
| `ripple/bindings` | `path count width depth -- entries omitted? found?` | Bounded binding descriptions and hidden-name markers; lookup never executes a binding. Empty path selects the session, `.` selects its top stack record. |
| `ripple/clear-stack` | `--` | Destroy session stack values safely. |
| `ripple/history` | `text --` | Store a completed submission through Readline; no-op in stream mode. |

`ripple/color?` is an ordinary Boolean binding set once at startup. Inspection
returns data independently of presentation policy.

The controller chooses when to parse, continue reading, record history, evaluate,
print, or stop. `ripple/stack` numbers and prints the returned descriptions using
the self-hosted `each`. Native inspection and clearing remain necessary because
the controller must not take ownership of the session stack. The self-hosted
`ripple/load` composes file parsing with session execution; native file access
does not decide whether to execute its result. Prompt inspection stays
native so refreshing it never clones the user's values. Prompt assembly and
highlighting live in `highlight.shk`; the host explicitly loads `lib/ansi.shk`
into the controller. It is not implicitly loaded into the user session.

Text assembly uses ordinary Shirka lists and the existing `++` operation. This
preserves self-hosting at the cost of Shirka's existing copying and concatenation
costs; large multiline submissions can be expensive. This does not introduce a
second parser, changes to ownership semantics, or a new string representation.
Flat token/string construction and list cloning use tail pointers rather than
repeated end-of-list scans. A local C99 build measured parsing/cloning 32,000
numeric tokens at about 0.002/0.001 seconds, versus 0.58/0.60 seconds before the
change, timing `skO_read` and `skO_clone` separately with `clock()`. These timings
cover flat builders, not evaluation, captures, ANSI styling, or the whole REPL.

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
.. 2
2 .. 3
2 3 .. +
5 .. 42 -> x
5 .. x
5 42 ..
```

Values and top-level reserves/operation definitions survive submissions. Input is
evaluated on the actual user stack, never on `try`'s empty local stack. Results
are not printed or consumed automatically. The prompt previews at most three
values, bottom-to-top, without cloning or executing them. The primary marker is
`..`, colored gray (bright black) when color is enabled. `/stack` shows indexed
values with the top at index zero.

Examples:

```shirka
[1 +] => increment
increment
[1 TRUE +] try
```

The last example explicitly requests Shirka's normal isolated-stack `try`.
On this lexical-scoping branch, `=>` and `->` replace the current binding.
Existing operations and list values retain snapshot captures of prior bindings;
they do not pick up later replacements. See `doc/lexical-scoping.md`.

## Commands

- `/help`: show commands.
- `/exit`: exit.
- `/stack`: inspect up to 256 stack values, each bounded to 511 bytes/depth six.
- `/clear`: destroy stack values, keeping definitions.
- `/bindings`: inspect session bindings and hidden-name markers, including captured prelude words.
- `/fields [PATH]`: inspect a named list's bindings, or the top stack list if omitted.
- `/load PATH`: execute a file in the session, preserving its top-level bindings.

Commands are exact, complete single-line submissions, starting at column zero.
`/load` takes the remainder of the line as a path, including spaces; it does not
perform shell expansion or remove shell quotes. Commands within multiline code,
strings, or comments are not dispatched. An unknown slash-prefixed name is
ordinary Shirka code. Use `/help` inside a larger program as an ordinary identifier;
a line consisting exactly of `/help` is reserved for the REPL command.

## Inspecting scoped lists

```shirka
(! -> record) [42 -> x [abort] => danger []]
/fields record
/fields record.danger
record
/fields
```

These commands inspect bindings without invoking operations, executing list
bodies, consuming the receiver, or cloning user values. An unevaluated list stays
unevaluated. Dotted paths, including stack-relative `.child`, use normal lookup.
Missing paths and non-list receivers report an error and leave the session usable.

Each row shows the name, binding kind (`value`, `operation`, `native`, or `hidden`), origin
(`local` or `captured`), and bounded diagnostic value. All effective bindings are
included, including captured prelude/native words and hidden-name markers left
by restore (`<-`). Hidden entries display `<hidden>` and continue masking older
bindings; shadowed entries are omitted. Order is unspecified. Output stops at
128 entries, with values
bounded to 255 bytes and depth three. These are shallow namespace views: nested
list dictionaries are inspected separately with another `/fields` command.
Large views can be slow through the self-hosted formatter; output limits do not
guarantee bounded latency.

The native bridge returns ordinary records with `.name`, `.kind`, `.origin`, and
`.value` fields. Names and values are escaped diagnostic character lists, never
executable values. Shirka chooses limits, formats rows, applies ANSI styling, and
dispatches commands. Reusable C APIs `skH_each` and `skE_lookup` supply dictionary
iteration and borrowed lookup without evaluation.

## Errors and ownership

Syntax errors make no evaluation changes. Runtime failures are **not
transactional**: completed stack operations and session definitions remain.
Pending instructions and scopes belonging to unfinished nested calls are freed.
Operand-validation failures generally leave their operands on the user stack.
The next prompt reflects the resulting stack; use `/clear` when desired.
`/load` parses the entire file before executing it, so malformed files cannot
partially run. Embedded NUL bytes in source files are rejected rather than
silently truncating the source; escaped NUL characters in strings remain valid.

One persistent session scope is kept above the prelude. Each submission executes
without adding another root scope. Final callees use the evaluator's normal
invocation scopes; ripple no longer appends a private trailing literal to force
a continuation. This does not provide general proper tail calls: recursion
through self-hosted `if` can still exhaust the C stack.

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
characters.

Interactive stack previews and `/stack` are colored using the self-hosted
`ansi-style`: QuotedSymbols red, executable symbols in the default color,
strings and Characters green, numbers cyan, Booleans magenta, and truncation
markers dim. List brackets and
separators keep the default color. Values inside lists are highlighted too.
Executable symbols display as their names, without the diagnostic `Symbol(...)`
wrapper. A symbol named `TRUE` uses the default color; the actual Boolean `TRUE`
is magenta. This presentation also applies without color and in `/stack`.
The highlighter scans bounded diagnostic descriptions, including escaped quotes,
character literals containing brackets, and incomplete/truncated spans; it never
parses or executes user values. Colors use selective cleanup, without full resets.

Color is disabled if `NO_COLOR` is present (even empty), for `TERM=dumb`, or in
stream mode. There is no additional terminal color-capability negotiation.
Readline still uses terminal-control sequences for interactive editing.
The native adapter marks SGR sequences as nonprinting for Readline's cursor and
wrapping calculations. Colored output is never stored in history.

Recalled history and current input remain plain editable source. Both share
Readline's input buffer; coloring either requires a live redisplay layer, which
is still deferred. Adding ANSI bytes to history would corrupt source and cursor
positions, so history entries are kept unchanged.

History uses `$XDG_STATE_HOME/ripple/history`, or
`$HOME/.local/state/ripple/history`. It is private (0600), capped at 500 entries,
and entries larger than 8192 bytes are not recorded. Completed submissions with
syntax errors are remembered for correction; incomplete or cancelled input is
not. A completed multiline submission remains one entry after restart.

Persistence requires an owned private `ripple` directory (created as 0700), Unix descriptor
APIs, and `/dev/fd`. Existing files are opened without following symlinks and
checked through their descriptor. Unreadable, oversized (over 4 MiB), symlink,
or non-regular history files disable persistence without preventing editing.
In-memory history remains bounded even when storage is unavailable.

History uses Readline's timestamp-delimited format with record separator
(`0x1e`) as its metadata marker. That byte is forbidden in raw editor input,
so source lines such as `#123` inside multiline strings cannot split an entry.
Source text itself is stored verbatim. Existing `#`-timestamp history files are
imported and converted on the next successful save. New files require ripple's
configured marker rather than Readline's default `#` marker.

Saving writes a temporary file in that directory and atomically replaces
`history` only after a successful write/close. A link inserted at `history`
during the session is replaced, never followed. A failed save leaves the
previous file intact. This is atomic replacement, not a promise of durability
through power loss. Simultaneous sessions still use last-writer-wins behavior.

## Streams and tests

```sh
printf '2\n3\n+ puts\n' | utils/ripple/ripple
make test-ripple
make test-input
make test
make test-reclamation
```

Pipes, redirected output, and `TERM=dumb` use plain stream input: no prompts, ANSI
editing, or history writes. Parser, evaluator, and native I/O diagnostics use
stderr; controller messages (such as `/load` usage and incomplete EOF) use Shirka's
normal `puts` on stdout. Processing continues after recoverable errors, and the
final stream exit status is nonzero if any submission failed.

`make test-ripple` uses C parser/inspection checks and Unix pseudo-terminals to
exercise real editing, paste, history persistence, Ctrl-C cancellation and
execution interruption, EOF, `/load`, and terminal restoration. It also checks
colored nested previews, `/stack`, cursor editing and wrapping with SGR prompts,
plain stream output, `NO_COLOR`, and the absence of color bytes in stored history.
`make test` includes exact character-list tests for the diagnostic highlighter.
The existing
allocation-counter test also exercises 100 successful and failing submissions
against one persistent environment. `test-ripple` also runs
`test-ripple-reclamation`: it drives the actual controller and host through 100
cycles of successful, failing, and multiline submissions in each of two sessions,
with periodic binding inspection, checking stable interpreter allocations and
session scope/frame boundaries.
The file-error paths include repeated rejection of source containing NUL bytes.
Its terminal substitute makes those checks independent of Readline allocations.
Expected error diagnostics are shown only if this regression fails.
Native calls, parsing, and Readline internals
are not promised to have bounded execution latency.

`make test-input` is included in `make test`. It checks parser errors, prefix
splicing order, long flat lists/strings, independent cloned sequences, and
retained lexical metadata. Terminal regressions also cover malformed/NUL-bearing
files, recall of invalid source, multiline history (including `#123` source
lines), legacy history import, rejected symlinks/FIFOs,
private file modes, and preservation of previous history on save failure.

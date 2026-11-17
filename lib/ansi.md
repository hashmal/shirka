# ANSI styling

Load explicitly after the standard prelude (the interpreter already loads the
prelude). This library is not automatically imported:

```shirka
"lib/ansi.shk" with
"hello" [:red :bold] ansi-style puts
```

`ansi-style` consumes `string commands` and returns one ordinary character list.
It performs no output. Both outer arguments must be lists; every string element
must be a Character and every command a QuotedSymbol. Unknown commands, executable
Symbols, malformed encoded fragments, and invalid types abort catchably through
normal Shirka errors. Other stack values are preserved. `ansi/` words are internal
implementation helpers, not public API.

An empty command list returns the validated character list unchanged, including
any ESC bytes; it performs no ANSI scanning. With nonempty commands, supported
input is ordinary text plus complete fragments produced by this library. Text,
including whitespace and newlines, is preserved. Commands retain their order;
later conflicting colors win. Duplicates are allowed.

## Vocabulary

All eight colors have four forms, for example `:red`, `:bg-red`, `:bright-red`,
and `:bg-bright-red`:

| Color | Foreground | Background | Bright foreground | Bright background |
| --- | --- | --- | --- | --- |
| black | 30 | 40 | 90 | 100 |
| red | 31 | 41 | 91 | 101 |
| green | 32 | 42 | 92 | 102 |
| yellow | 33 | 43 | 93 | 103 |
| blue | 34 | 44 | 94 | 104 |
| magenta | 35 | 45 | 95 | 105 |
| cyan | 36 | 46 | 96 | 106 |
| white | 37 | 47 | 97 | 107 |

| Command | Enable | Selective cleanup |
| --- | --- | --- |
| `:bold` | 1 | 22 |
| `:dim` | 2 | 22 (encoded as 22;22) |
| `:italic` | 3 | 23 |
| `:underline` | 4 | 24 |
| `:blink` | 5 | 25 |
| `:reverse` | 7 | 27 |
| `:hidden` | 8 | 28 |
| `:strikethrough` | 9 | 29 |
| `:overline` | 53 | 55 |

Foreground closes with 39, background with 49. There are no reset requests,
default-color requests, aliases, indexed colors, or RGB forms.

## Nesting and wire format

```shirka
"before " "inner" [:blue] ansi-style ++ " after" ++ [:red] ansi-style puts
"dim " "bold" [:bold] ansi-style ++ " dim again" ++ [:dim] ansi-style puts
"one" [:red] ansi-style "two" [:blue] ansi-style ++ " plain" ++ puts
```

The first example produces this exact sequence (ESC written visibly):

```text
ESC[31m before ESC[34m inner ESC[39m ESC[31;31m after ESC[39m
```

Spaces separating the items above are explanatory; only the spaces in the source
strings are emitted. Effective foreground is red for `before `, blue for `inner`,
red for ` after`, and default afterward. The intermediate restoration is essential.

The wire encoding uses only valid SGR sequences, with reserved structural roles:

- A single enable parameter opens a category and pushes its request.
- A selective reset closes the newest request in its category. Repeated requests
  are closed separately, in reverse order. Bold closes with `22`; dim closes with
  `22;22`. Both physically clear intensity, but remain distinguishable structurally.
- A repeated enable, such as `31;31` or `1;1`, is a restoration marker. It sets
  exactly that attribute; it does not open another structural scope.

When wrapping a fragment, the scanner removes previous restoration markers and
recomputes them using the active requests at each close. Color/other attribute
closes restore their category. Either intensity close restores both active bold
and active dim, since 22 clears both. Thus closing inner bold preserves outer dim,
and vice versa. Background never changes when foreground closes. Closing an
already-active attribute re-enables its enclosing instance.

No automatic full reset (`0` or an empty SGR parameter) is emitted. Cleanup only
addresses affected categories, with the unavoidable shared intensity category
for bold/dim. This is not terminal-state discovery: unknown styles active before
the entire constructed fragment cannot be restored. For example, styling bold
may clear an externally established dim state at cleanup. Only styles represented
by enclosing library fragments can be restored reliably.

Nonempty styling rejects unrecognized ESC sequences, including arbitrary ANSI
cursor control and noncanonical combined SGR parameters. A manually embedded
sequence identical to the reserved encoding is indistinguishable from library
output and is interpreted structurally. This encoding is a contract, not proof
of provenance. Do not edit, split, or truncate encoded fragments; concatenate
complete fragments and ordinary text. Final active requests must match the
wrapper's initial requests. Arbitrary ANSI programs are unsupported.

The library is deterministic: no TTY checks, environment reads, `NO_COLOR`
handling, or capability negotiation. Callers decide whether to apply styling.
Terminal rendering/support varies, especially blink, hidden, italic, and overline.
Processing uses Shirka's copying lists and may be expensive for large fragments.

## Octal character escapes

The parser accepts one to three octal digits (`0`–`7`) after a backslash in both
strings and character literals. Values must fit in a byte, 0–255 (`000`–`377`).
It consumes at most three digits; a following digit belongs to the next token or
string character. A non-octal character ends a shorter escape. Examples:

```shirka
"\033[31m"  -- ESC followed by [31m
'\033       -- one ESC Character
"\0334"    -- ESC followed by the character 4
"\0x"      -- NUL followed by x
```

`\400` and `\777` fail safely. `\8` remains an unknown escape. A trailing bare
backslash is incomplete; an unfinished string containing `\03` is also incomplete.
A standalone character literal `'\03` is complete (one-to-three digits are valid).
Existing named escapes retain their meanings. Octal input is byte-oriented, not
Unicode code-point notation.

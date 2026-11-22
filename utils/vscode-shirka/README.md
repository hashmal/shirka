# Shirka for Visual Studio Code

Language support for `.shk` files, based on the interpreter's `parser.c`.
There is no language server, runtime JavaScript, or build step.

## Features

- TextMate highlighting for lists, prefix expressions, symbols, bindings,
  numbers, booleans, comments, strings, and character literals. Native and
  prelude words are highlighted as conveniences; they are not reserved words.
- Matching, autoclosing, and surrounding for `[]`, `()`, and double quotes.
  Brackets and quotes are not autoclosed inside comments or strings.
  Character literals receive string token types so `'[` does not open a list.
  Apostrophes are deliberately never paired: `'a` is a complete character.
- Toggle line comments using the editor's normal shortcut (`--`). Shirka has
  no block comments, so no block-comment delimiters are invented.
- Best-effort indentation after an unmatched opening delimiter, outdent before
  closing delimiters, and indentation after a standalone `(=> name)` heading.
- Indentation-based folding, plus optional `-- #region` / `-- #endregion`
  markers (ordinary Shirka comments).

## Snippets

Use completion suggestions or **Insert Snippet** in a Shirka document:

| Prefix | Expansion |
| --- | --- |
| `def`, `=>` | `(=> name)` followed by an indented quotation |
| `def-inline` | `[ body ] => name` |
| `reserve`, `->` | `-> name` |
| `restore`, `<-` | `<- name` |
| `prefix` | `(operation)` followed by a quotation |

Tab navigates placeholders. The mnemonic aliases work even when punctuation
completion is inconvenient. Only the interpreter's ASCII arrows are supported.

## Try it locally

Open **this directory** in VS Code and press F5 using **Run Shirka extension**.
In the new Extension Development Host, open any Shirka `.shk` file.

To install a packaged copy instead, from this directory:

```sh
npx @vscode/vsce package
code --install-extension vscode-shirka-0.1.1.vsix
```

Packaging needs Node/npm; using the installed extension does not. No extension
has been published to the Marketplace by this project setup.

## Tests

```sh
npm ci
npm test
```

The development-only dependencies are VS Code's TextMate and Oniguruma engines.
Tests check scopes, character-literal token types, language configuration,
indentation patterns, and the repository's examples, libraries, and suites.
Node/npm are needed only for extension development, not the Shirka interpreter.

The **Test Shirka extension** F5 configuration additionally runs editor checks
for actual autoclosing, surrounding, bracket navigation, comment toggling, and
indentation. These tests temporarily set editor options to predictable values and restore
them afterward. They do not modify source files.

## Limits

This is lexical highlighting, not binding analysis or parser diagnostics.
The identifier rules target the parser's ASCII spelling; locale-dependent
non-ASCII `ctype` behavior is not modeled. Strings can span lines. A dangling
character prefix is highlighted only through its line, to avoid coloring the
rest of an unfinished document as a character.

Indentation is line-based and approximate for multiple nested delimiters or
multiline literals. Folding follows indentation rather than computing exact
bracket ranges. Prefix parentheses reorder tokens; the editor matches their
physical delimiters without attempting to model that execution behavior.

Apostrophes within identifiers (such as `foo'`) and numbers ending in a decimal
point follow the parser. Dots are excluded from identifiers: `record.x.y` is a
field path with independently highlighted separators and properties. Quoted
paths retain symbol highlighting, and binding targets accept the same path
syntax. `.1` and `-.0` are invalid; numeric fractions require a leading digit.
Leading-dot paths such as `.x` and `.child.x` select stack fields and use the
same property scopes. Binding targets accept `-> .x`, `=> .method`, and `<- .x`.

## Highlighting scopes

Punctuation-only identifiers such as `!`, `!?`, `<<`, and custom `@@` words
use `keyword.operator.shirka`. Mixed names such as `foo?` and `$parse`
remain identifiers; quoted symbols such as `:!` keep their symbol highlighting.
Targets after `=>` use `entity.name.function.definition.shirka`; targets after
`->` and `<-` use `entity.name.variable.binding.shirka`, including when the
name itself is punctuation or a builtin. These are lexical roles, not claims
about runtime binding resolution. The active color theme chooses their colors.

# Lexical scoped lists: experimental implementation

This branch implements the snapshot model in [the RFC and Annex A](lexical-scoping-rfc.md).
It replaces dynamic lookup, including for tail calls. There is no caller fallback,
tracing collector, general reference type, or implicit receiver.

## Observable rules

A source list literal captures the current lexical dictionary when evaluated,
not when parsed. All visible bindings are captured, regardless of which symbols
occur in the sequence. A capture preserves reserve/operation/native binding kinds.

```shirka
10 -> x
[x] => read-x
20 -> x
read-x                    -- pushes 10

[-> increment [increment +]] => make-adder
10 make-adder => add-ten
5 add-ten                 -- pushes 15
```

Every invocation gets a fresh working dictionary with the stored dictionary as
its lexical ancestor. Assignments replace bindings in that invocation, without
modifying the stored operation. Captures are snapshots, not mutable cells.
`<- x` transfers a local value, or copies an inherited value, and hides the name
from subsequent lookup in that invocation. It never reveals an older reserve.
Both `->` and `=>` replace existing bindings; existing captures remain unchanged.

A missing name is an error even if a caller supplies it. Ordinary forward
references and mutual recursion are unsupported. Define dependencies first.

## Nested lists and generated code

Parsed lists have an internal `literal` flag; they are still user-visible Lists.
When a literal is evaluated, its entire nested literal structure is contextualized.
Each nested dictionary has an owning lexical ancestor, separate from the outer
sequence. Siblings can retain the same ancestor snapshot.

```shirka
42 -> x
[[x] []] -> branches
branches uncons >< << !    -- pushes 42; extraction preserves the branch context

[20 -> x [x]] => outer
outer !                   -- pushes 20; the literal captures invocation locals
```

The flag distinguishes those two cases. A nested literal retains its origin
while contained in executable literal structure: reaching it during execution
creates a fresh instance from the current working scope. `uncons` clears that
flag on an extracted list. `cons` clears it on an inserted list. Thus an existing
scoped value moved into instruction position remains that value, and is not
silently recaptured. Cloning preserves provenance and captured snapshots.

`cons`/`uncons` preserve the receiving/remainder dictionary. Symbols themselves
carry no context. Existing list values embedded through manipulation retain
their own dictionaries; instantiation skips them. `$parse` captures its outer
sequence at the call and contextualizes nested literals by the same rule:

```shirka
42 -> x
[] :x unquote cons !       -- pushes 42
"x" $parse !              -- pushes 42
```

Creating the empty container *before* reserving `x` would not capture the later
binding. Editing a sequence never merges dictionaries. Prelude concatenation
keeps the destination container's context; its existing list-building operations
are unchanged. List equality currently compares sequence values only, not lexical
dictionaries or operation self names. Inspection/printing also shows the sequence.

## Dotted dictionary access

List creation captures bindings but does not execute instructions. In particular,
`[1 -> x 2 -> y] -> record` stores code; those assignments have not run and do not
create fields. Executing that list later creates invocation-local bindings and
does not change its stored dictionary. Dotted lookup never runs code to populate
missing fields. There is no separate scope-construction operation.

Populate a list's dictionary directly using ordinary dotted assignment:


```shirka
[] -> record
42 -> record.answer
record.answer             -- pushes 42
[] -> record.child
21 -> record.child.x
record.child.x            -- pushes 21
[2 *] => record.double
21 record.double          -- pushes 42
<- record.child.x         -- restores 21 and hides that field
```

Lookup and assignment use the same effective lexical dictionaries as execution.
Intermediate components must hold lists and are selected without execution.
Only the final binding receives normal reserve/operation behavior. Missing
intermediate objects are errors; they are not created implicitly. Assignment
clones the root value, copies shared dictionaries along the path, then publishes
it. Copies of records and existing captured branches retain their old values.
The root binding's operation/value kind is preserved during a field update.

Dots are reserved path separators, not identifier characters. A path is one or
more ordinary identifiers joined by adjacent dots, for example `record.child.x`.
The same grammar applies to quoted paths (`:record.x`) and binding targets
(`-> record.x`, `=> record.method`, `<- record.x`). A single leading dot selects
the record on the stack, as described below. Trailing or consecutive dots,
numeric-only components, and whitespace after a dot are parse errors. `x .y` is
two operations (push `x`, then select `.y`), not one named path. `.` and `..`
remain invalid operation names. Decimal numbers such as
`0.5`, `-0.5`, and `1.` retain their numeric meaning; `.1` and `-.0` are invalid
because the numeric grammar requires a leading digit. Strings and Character
literals can still contain dots.

The parser validates each path component separately, then serializes the full
path into the existing Symbol/QuotedSymbol representation. This preserves
quotation and generated-code operations without introducing a new value type;
it does not make the dot part of an atomic identifier again. Keys selected from
a path contain only that component's name.

### Stack-relative fields

```shirka
(!)[1 -> x 2 -> y []]  -- leave a record on the stack
.x puts               -- print 1; the record remains
42 -> .x              -- consume 42 and update that record
.x puts               -- print 42
[.x 2 *] => .double    -- install an operation; keep the record
.double puts          -- print 84; this body also leaves the record
```

The stack effects are `.x (record -- record value)` and
`-> .x (record value -- record)`. Assignment consumes the value first, selecting
the record immediately beneath it. `=> .method` follows the same rule for its
quotation and stores an operation binding. `<- .x` restores the value while
keeping the record and hiding the field. Nested paths such as `.child.x` preserve
the outer record; missing intermediate fields are not created implicitly.

Reads share named-path lookup; writes share named-path traversal and copy-on-write
updates. A stack-relative write changes only the stack value's dictionary, not
another reserve or capture from which that value was copied. Its sequence and
position on the stack remain unchanged. Quoted paths (`:.x`) and generated code
use the same rules. Missing/non-list receivers and missing read fields are
catchable errors. Receiver validation precedes consuming assignment operands.

The final binding retains normal value/operation behavior. Selecting an operation
does not add a receiver argument or protect the record from its body: that body
can deliberately consume or rearrange the record using normal stack operations.
List creation and field lookup remain non-executing for ordinary value fields;
there is no lazy initialization.

Ancestor metadata cannot be assigned as a user field. Overlapping dotted
assignments use the old source
value; they cannot relink an owner inside itself. There is no live prototype or
automatic `self`/receiver binding for methods.

## Named recursion

`=> f` records `f` as the operation's self name. At invocation, a complete copy
of that operation is installed under `f` in the fresh working scope, overriding
any captured `f`. Its captured dictionary precedes the invocation; it does not
point back to that working scope. Nested escaping closures can capture this
ordinary owning entry safely. Local assignment may replace it.

```shirka
[ >> 0 > (if) [ [1 - countdown] [<<] ] ] => countdown
10000 countdown
```

Ordinary copying preserves the self name. Installing the copied list using a new
`=>` assigns a new self name (the final component for a dotted definition). The
body's symbols are not renamed; a body still referring to its old name needs
that binding in its capture. This is deliberately not automatic alpha-renaming.
Recursion copies the instruction sequence per invocation; it is not a new shared
code-body representation.

## Ownership and execution

- `skL` is a reference-counted dictionary snapshot, backed by `skH`, with one
  owning lexical ancestor. The hash table itself still borrows its ancestor.
- `context` is a mutable invocation handle. Its `parent` is only the execution
  cleanup chain; name lookup never traverses that chain.
- A capture retains a snapshot. Before writing a shared dictionary, `skL_write`
  copies its local entries and keeps the same lexical ancestor. It does not link
  the replacement to its old version, so uncaptured version history is released.
- Dictionary copies clone value sequences but share captured snapshots. No
  published dictionary is changed in place while a capture retains it. New
  values point only at already-completed snapshots, excluding ownership cycles.
- Hidden-name entries stop ancestor lookup after restoration. Native entries
  hold static function pointers; ordinary entries own Shirka values.
- A tail call releases the old invocation and creates the callee's lexical scope
  while reusing the execution frame. A root `scoping=0` host/include evaluation
  keeps its working namespace; a final callee still gets an invocation scope.
- Error unwinding releases instructions and invocation handles; escaping values
  keep only their owning snapshot references. Global symbol interning remains.

Captured dictionary mutation is interpreter-controlled copy-on-write. There is no
public arbitrary ancestor setter. C code borrowing snapshot maps must not mutate
them directly. Capturing everything can retain unrelated values; cloning local
maps and list sequences can be expensive. Reference-count destruction can cascade.
Allocation failure while constructing interpreter snapshots is fatal, consistent
with the core's existing object allocation policy; standalone hashmap allocation
APIs continue returning failure.

## Libraries, includes, and ripple

`with` explicitly evaluates loaded source in the current working namespace;
file definitions capture the environment at their own literal creation. This
retains the existing explicit include/export behavior without caller lookup for
subsequent calls. The host's top-level evaluation and ripple submissions use the
same current-namespace facility. `$parse` is not an implicit context rebinder for
existing lists. `try` result containers capture the caller at construction too.
Ripple's native input/inspection text carries an empty dictionary: external
character data must not capture the controller's transient step and retain its
submission history. Explicit `$parse` supplies the appropriate language context.

Test assertions are now defined at library load time, before suite creation.
The private comparison helper receives its quotation explicitly. This selects
the RFC's simplest lexical-test option rather than introducing context injection
or dynamic variables. Ripple's forward reference from dispatch to stack display
has been removed by defining its dependency first. Stored ripple operations
capture prior definitions and are not changed by subsequent submissions.

## Verification

`make test` includes `test/lexical.shk`: snapshots, escaping closures, no dynamic
fallback, nested branches, existing-value insertion, generated code, dotted
updates/restoration, copying, direct recursion, and escaping recursive closures.
`make test-reclamation` counts allocations in the core, hash tables, and lexical
snapshots, including repeated success/failure and escaping closure cleanup.
`make test-lexical-no-tail` (also in `make test`) runs the lexical suite with
tail optimization disabled in a temporary build.
`make test-ripple` covers persistent submissions, error recovery, terminal editing,
and allocation balance. Build with `-DSK_NO_TAIL` to disable tail optimization;
name resolution must remain the same.

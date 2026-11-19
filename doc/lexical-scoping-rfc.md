**Status:** exploratory proposal. The examples describe proposed behavior, not necessarily what the current interpreter supports.

The central idea is:

> A list carries the bindings available when it is created. Executing that list uses those bindings, rather than the bindings of whoever happens to execute it.
> 

This retains Shirka’s existing relationship between lists and executable sequences. There is no separate closure type: **an ordinary list can carry the context necessary to execute its contents**.

The proposal uses **snapshot captures**, consistent with Shirka’s current move/copy semantics. Reference counting or structural sharing could optimize those semantics later without changing their meaning.

## 1. What is a scoped list?

A scoped list has two parts:

- Its ordered sequence of values.
- Its dictionary of named bindings.

The dictionary can contain captured reserves, user-defined fields, and operations. These are the same kind of named storage.

For example:

```
42 -> x
[x 2 *] -> computation
```

The list stored in `computation` contains the executable sequence `x 2 *`, together with a binding of `x` to `42`.

Whether the program eventually executes that list is irrelevant. A data list and an executable list have the same representation and capture rules.

The dictionary must preserve the distinction between a reserve that pushes a value and an operation that executes its value. That distinction already matters to Shirka; adding a dictionary does not remove it.

## 2. Capture happens when a list literal is evaluated

The relevant event is **runtime creation of a list**, not the parser encountering `[`.

Consider:

```
(=> make-adder) [
    -> increment
    [increment +]
]
```

When Shirka defines `make-adder`, its outer list captures the surrounding context.

When `make-adder` subsequently executes, it consumes an `increment` from the stack. Evaluating the inner literal then creates a list that captures that invocation’s `increment`.

```
10 make-adder => add-ten
5 add-ten
-- Stack: 15
```

The inner list remains usable after `make-adder` returns.

This requires the interpreter to distinguish an **unevaluated list literal inside executable code** from an **already-created list value**. The distinction need not become a new user-visible type, but its implementation cannot be ignored.

Moving, copying, or executing an already-created list must not silently capture a new environment.

## 3. Captures are snapshots

A captured binding retains the value it had at capture time.

```
10 -> x
[x] => read-x
20 -> x

read-x
-- Pushes 10.
```

Likewise, restoring the original reserve does not invalidate the capture:

```
10 -> x
[x] => read-x
<- x

read-x
-- Still pushes 10.
```

Snapshot semantics provide a useful guarantee:

> Later changes to the surrounding scope do not change an existing list’s meaning.
> 

They also impose a limitation: capturing a reserve does not create a shared mutable variable. Two lists capturing `x` do not acquire a common mutable cell.

An implementation may share immutable storage internally. User-visible behavior must remain equivalent to independent copies.

## 4. Execution creates a fresh working scope

Executing a scoped list creates an invocation-local working scope, logically initialized from its dictionary.

Within that invocation:

```
42 -> x
```

stores `x` in the working scope, replacing any existing binding of that name.

It does **not** permanently modify the stored executable list.

For example:

```
(=> calculate) [
    42 -> temporary
    temporary 2 *
]
```

Each call gets its own `temporary`. Recursive calls also receive independent working scopes.

This is an important refinement of “definitions go into the currently executing list”:

> Definitions go into the current execution’s instance of the list’s scope.
> 

Otherwise, every operation would implicitly become a stateful object, and recursive calls would overwrite one another’s locals.

A nested list created during the invocation captures this working scope, including newly defined locals.

### Restoring captured bindings

If the working scope contains a captured `x`, then `<- x` moves that invocation’s copy onto the stack and removes its binding from that invocation.

It does not remove `x` from the stored list or from another invocation.

After `<- x`, `x` is absent from that working scope. A subsequent lookup fails unless a new binding is introduced.

## 5. Name resolution has a definite stopping point

During execution, a name is resolved in the current working scope.

That scope already contains the captured bindings and any local definitions made during execution. There is no need to search the caller’s scope.

If the name is absent, lookup fails.

```
[x] => read-x
42 -> x

read-x
-- Undefined-name error, assuming x did not exist when [x] was created.
```

In particular:

> Failed lexical lookup must not silently fall back to dynamic lookup.
> 

Such a fallback would make the same operation succeed or fail depending on its caller, precisely where its captures are incomplete.

For the simplest prototype, capture all visible bindings when constructing a list. This avoids an immediate free-variable analysis and supports generated code.

However, if captured bindings are user-visible dictionary entries, capturing only syntactically referenced names later would be a **language change**, not merely an optimization.

## 6. Scoped dictionaries are also ordinary data

Proposed dotted syntax accesses the same dictionary used to initialize execution scopes:

```
[] -> settings
42 -> settings.answer
settings.answer
-- Pushes 42.
```

Dotted assignment modifies the scoped list stored in that reserve.

```
10 -> x
[x] -> computation
20 -> computation.x
```

If `computation` is subsequently executed, its `x` resolves to `20`.

This makes the captured context inspectable and editable. It also means lexical scoping guarantees stability against changes to the **surrounding environment**, not against deliberate modification of the list’s own dictionary.

With value semantics:

```
computation -> other
30 -> other.x
```

must leave `computation.x` unchanged. Copy-on-write would be a possible implementation.

### Executable fields

Dotted lookup behaves like ordinary name lookup, except that it selects the dictionary explicitly. The binding’s reserve/operation distinction determines whether the stored value is pushed or executed.

```
42 -> record.answer
record.answer
-- Pushes 42.

[2 *] => record.double
21 record.double
-- Pushes 42.
```

A list stored as a normal reserve remains data:

```
[2 *] -> record.instructions
record.instructions
-- Pushes the list.
```

Consequently, inspecting a field through dotted lookup can execute code. Retrieving an operation’s stored value without executing it would require an explicit inspection operation.

For chained paths such as `record.x.y`, intermediate components select stored scoped-list values without invoking them. Only the final component receives normal reserve/operation behavior. A missing intermediate field, or one whose stored value is not a scoped list, produces an error.

Executing a field does not automatically bind its containing object as a receiver:

```
[] -> record
42 -> record.answer
[record.answer] => record.method
```

Here, the method captures the value of `record` available when its list is created. It does not acquire a live reference to the record into which it is subsequently installed. Automatic receiver binding would require an additional language rule.

## 7. Constructing executable lists remains ordinary list manipulation

An executable symbol does not carry its own lexical context. The **list containing it** supplies the context when executed.

For example:

```
42 -> x
[] :x unquote cons -> code
```

Here:

1. `[]` creates a scoped list capturing the current bindings.
2. `:x unquote` produces the executable symbol `x`.
3. `cons` inserts that symbol without changing the list’s dictionary.

Executing `code` therefore finds `x = 42`.

By contrast:

```
[] -> code
42 -> x
<- code :x unquote cons -> code
```

does not retroactively capture `x`. Assuming it was originally absent, executing this list fails until its dictionary is explicitly supplied with an `x`.

```
42 -> code.x
```

This gives generated code a predictable rule:

> Editing a list’s sequence does not implicitly recapture its surroundings.
> 

### What happens when lists are combined?

The dictionary belongs to the list container, not to each linked-list node.

Proposed rules:

- `cons` preserves the receiving list’s dictionary.
- Removing an element preserves the remainder’s dictionary.
- An extracted symbol does not carry the source list’s context.
- An extracted nested scoped list retains its own dictionary.
- Concatenation preserves the destination list’s dictionary; it does not silently merge dictionaries.

Thus, moving executable symbols between lists can change their meaning. That is an explicit consequence of list-based lexical context.

Automatic dictionary merging would require conflict rules and could expose bindings unexpectedly. It should be a separate operation if wanted.

## 8. Nested lists capture when they are created

Consider:

```
10 -> x

(=> outer) [
    20 -> x
    [x]
]
```

Calling `outer` returns a list whose `x` is `20`.

The inner literal captures the executing outer scope, including the replacement binding of `x`.

However, inserting an existing scoped list into another list does not replace its captures. Executing a list in a new location also does not rebind it.

This distinction matters for metaprogramming:

- Creating a fresh literal captures the current context.
- Manipulating an existing value preserves its context.
- Explicit dictionary edits change that context.

The implementation must preserve these distinctions through list construction and quotation operations, rather than guessing from whether a list “looks executable.”

## 9. Recursion needs an explicit language rule

The familiar pattern:

```
[ ... f ... ] => f
```

cannot obtain its recursive binding from an ordinary snapshot: `f` is defined only after the list has been created.

With current dynamic lookup, the later binding can be found at execution time. Lexical snapshots remove that mechanism.

### Proposed exception: a named operation knows its own name

When `=> f` creates an operation, it records `f` as that operation’s self-name.

On invocation, the working scope receives a special self-binding for `f`, referring to the operation being invoked. This replaces any captured binding of `f`; a later explicit local definition can replace the self-binding in that invocation.

The self-binding can be implemented as a symbolic marker rather than an owning pointer back to the list. Recursion therefore need not create an ownership cycle.

A nested list that captures this self-binding must retain a usable representation of the operation; it cannot retain a pointer to a temporary execution frame.

This rule makes direct recursion convenient, but it has consequences:

- Recursive calls keep targeting that operation even if an outer `f` is later rebound.
- Copying the operation must preserve its recursive identity.
- Reinstalling it under another name needs a documented policy.
- Ordinary forward references and mutual recursion remain unsupported.

Mutual recursion would need a further mechanism, such as an explicitly constructed group of definitions. I would defer that feature rather than introduce general late lookup as an accidental workaround.

## 10. Dynamic test helpers stop being implicitly available

Suppose `test/run` defines assertion operations locally and then executes a suite created elsewhere.

Under this proposal, the suite cannot see those assertions merely because `test/run` is its caller.

That is a genuine loss compared with the current design: dynamic scoping provides a concise way to establish a temporary execution vocabulary.

There are three plausible responses:

| Approach | Consequence |
| --- | --- |
| Define assertion operations before suite creation | Ordinary lexical capture; simplest model. |
| Have `test/run` inject assertions into a copy of the suite’s dictionary | Preserves the temporary vocabulary through explicit context modification. |
| Add explicitly dynamic bindings | Supports caller-provided services, but adds a second lookup mechanism. |

For an initial scoped-list experiment, I favor **explicit dictionary injection**.

The suite remains an ordinary scoped list. The runner prepares its context and executes it. The runner can use ordinary assignment to replace existing captures, or explicitly reject conflicting names before injection.

Injection affects that suite’s context. It does not recursively rewrite the captures of already-created helper operations or nested list values.

This preserves scope manipulation as a language-level capability without making every caller’s locals visible.

## 11. Tail calls must preserve these semantics

A tail call must behave like an ordinary call with respect to bindings.

For example:

```
(=> target) [secret]

(=> caller) [
    42 -> secret
    target
]
```

If `secret` was absent when `target` was created, calling `caller` must fail. Tail-call optimization must not accidentally make the caller’s `secret` visible.

An implementation may reuse an execution frame, but it must replace its working scope with the callee’s scope rather than accumulate bindings.

A list returned by the caller must retain its captured values independently of that frame.

This provides a concrete test criterion:

> Turning tail-call optimization on or off must not change name resolution.
> 

## 12. Benefits and limitations

Compared with current dynamic scoping, this proposal provides:

- Operations whose free names have predictable meanings.
- Escaping executable lists that retain their needed bindings.
- Caller locals that cannot accidentally satisfy a callee’s missing names.
- An ordinary, editable representation of execution context.
- A clear semantic target for fixing tail-call scope reuse.

The costs are equally concrete:

- Temporary caller-provided vocabularies require explicit preparation.
- Forward references and mutual recursion need additional rules.
- Captures are snapshots, not shared mutable variables.
- Copying all visible bindings can be expensive.
- Moving executable symbols between lists can change their meaning.
- Exposing captures as dictionary fields provides no privacy.
- Recursive operation identity requires a small exception to ordinary capture.
- Runtime literal creation needs more care than treating every parsed list as an already-created value.

For a first prototype, I would keep the contract narrow: **snapshot captures, fresh invocation scopes, no caller fallback, explicit dictionary editing, and a defined self-recursion rule**. Those choices are enough to explore whether scoped lists give Shirka the desired expressive power without first committing to shared mutable environments, prototype inheritance, or a new memory-management model.

---

## Annex A. Hierarchical dictionaries and captured snapshots

**Status:** proposed refinement of the scope representation. This annex preserves the RFC’s snapshot and copy-or-move semantics. It does not propose shared mutable variables or general-purpose reference values.

### A.1. One dictionary, two sources of bindings

A scoped list consists of a sequence and a dictionary. Its dictionary may have one optional **lexical ancestor**: another dictionary consulted when a name is absent locally.

A dictionary can therefore provide:

- Local entries: explicitly assigned fields or bindings introduced during an invocation.
- Captured entries: bindings reachable through its lexical ancestor.

For name lookup, both sources form one effective scope. A local entry takes precedence over an ancestor entry of the same name. Each entry retains its reserve/operation distinction, so finding an operation executes it under the normal name-resolution rules, including dotted access.

This refines the earlier description of capture as copying entries into a list’s dictionary. Capture must preserve the same observable bindings, but it need not physically duplicate those entries. An ancestor snapshot can supply them instead.

The ancestor is interpreter-managed metadata, not an ordinary user field. Assigning a field named `parent`, for example, does not change it. This proposal does not expose arbitrary ancestor reassignment.

### A.2. The ancestor is lexical, not dynamic

The ancestor records the context of list creation, rather than the context of a later caller.

```shirka
10 -> x
[x puts] => show-x
20 -> x
show-x
-- Prints 10.
```

`show-x` retains access to the captured version containing `x = 10`. Replacing the surrounding binding creates a new logical scope version; it does not modify that capture.

Walking through ancestor dictionaries is therefore not dynamic scoping. A lexical chain and a dynamic chain can use similar lookup code, but they connect different contexts.

If lookup reaches the end of the lexical chain without finding a name, it fails. It does not continue through the caller’s scopes.

### A.3. Captured dictionaries are immutable snapshots

A dictionary reachable by a capture cannot subsequently be modified in place in a way visible to that capture.

A working scope may be mutable while exclusively owned. Once captured, however, its existing state must be preserved. Later assignments produce a new logical version. Deep copying, copy-on-write, or persistent dictionaries are implementation alternatives; they must implement the same value semantics.

For example:

```shirka
42 -> x
[x] -> saved
```

Let `E0` be the scope snapshot containing `x` before `saved` is assigned. The list captures `E0`. Assigning `saved` produces a new working version, `E1`:

```text
E1 contains saved → list → captured E0
```

`E0` does not contain the new binding of `saved`. In particular, assignment must not mutate `E0` after the list has captured it.

A new version need not use the previous version as its lexical ancestor. Dictionary version history and lexical ancestry are different relationships. An implementation must not retain every historical version merely to implement assignment.

### A.4. Nested branch lists can share their context

Consider:

```shirka
42 -> x
[[x puts] []]
```

When the outer literal is instantiated and pushed onto the stack, its nested branch literals must also become usable scoped-list values. They can share one captured dictionary snapshot instead of each copying all visible bindings.

One possible arrangement is:

```text
outer sequence contains branch C and branch D

outer dictionary B → captured context A0 { x: 42, ... }
branch dictionary C → B
branch dictionary D → B
```

Here `B` is a stable snapshot of the outer list’s dictionary. It does not own the outer sequence: the list value owns its sequence and dictionary as separate components. Consequently, a branch can retain `B` without retaining the entire outer list or pointing back to itself through the sequence.

Extracting and executing branch C finds `x` through `C → B → A0`. No caller lookup and no copying of `x` into C is required.

If the outer list is later edited, its new dictionary version does not retroactively change C or D. Their ancestor remains the version captured when they were created. A live object/prototype relationship would be a different feature.

### A.5. Literal creation still differs from executing an existing value

Ancestor dictionaries solve context storage and sharing. They do not eliminate the need to define when an instance is created.

```shirka
10 -> x

(=> make-code) [
    42 -> x
    [x puts]
]
```

Calling `make-code` creates an invocation scope. Its reservation replaces that invocation’s `x`. When execution reaches `[x puts]` as a literal instruction, a fresh instance captures that working scope and therefore retains `x = 42`.

The following events remain distinct:

- Encountering a list as a literal instruction creates an instance with the current lexical context.
- Reading a reserved list preserves the stored value’s context.
- Extracting a nested list with ordinary data operations preserves its context.
- Executing an existing scoped list uses its context and creates an invocation scope; it does not capture its caller.

When literal instantiation includes nested literal structure, the interpreter must contextualize those nested literals consistently with A.4. It must not recursively rewrite the dictionaries of arbitrary existing scoped-list values inserted through data manipulation.

This is a remaining representation decision: the implementation must identify literal structure versus embedded existing values, or specify an explicit structural instantiation rule for generated code. Ancestor pointers alone do not resolve that distinction, and this annex does not claim that they do. Whichever rule is selected must also explain what happens when metaprogramming moves a list into an instruction position.

For `$parse`, the intended contract is:

```shirka
"x puts" $parse !
-- Equivalent to evaluating [x puts] ! at this point.
```

The parsed outer sequence captures at the `$parse` call. Nested literals must follow the same instantiation rules as source literals.

### A.6. Invocation-local definitions and restoration

Executing a list creates a fresh working dictionary whose lexical ancestor supplies the list’s stored effective scope. Assignments belong to that invocation, not to the stored executable value.

```shirka
(=> calculate) [
    42 -> temporary
    temporary 2 *
]
```

Repeated calls do not accumulate `temporary` in `calculate`’s stored dictionary. A list created inside a call may retain the relevant snapshot after the call returns.

Hierarchical storage introduces an important implementation detail for restoration:

```shirka
<- x
```

After moving the effective value of `x` onto the stack, lookup must treat `x` as absent from that invocation. Merely deleting a local entry would incorrectly expose the captured entry underneath it.

An implementation can record a **hidden-name marker** for `x`: lookup stops at that marker and reports an undefined name. A later assignment replaces the marker. Flattening the effective dictionary before modification is another possible implementation.

This is a storage detail, not a stack of user-visible reservations. Replacing or restoring a binding does not reveal a previous binding.

### A.7. Why ownership cycles are excluded

The required invariant is broader than “ancestor pointers form a tree”:

> The complete owning graph—including sequences, dictionary entries, and ancestor links—must remain acyclic.

The following rules enforce that model:

1. Captured snapshots are immutable.
2. Ordinary copying creates an independent logical value, even if immutable storage is shared internally.
3. Updating a shared value creates a new version rather than modifying storage retained by an existing value.
4. Ancestors are established by scope construction; user assignments cannot relink them arbitrarily.
5. Moving a value cannot place an owning container inside its own descendant.

A useful implementation discipline is to construct values from already completed, acyclic values and publish the new snapshot only when construction is complete. Existing published values never gain a pointer back to the newly constructed owner. An in-place optimization is valid only when it preserves that property.

The earlier example is safe:

```shirka
42 -> x
[x] -> saved
```

The new scope owns the list, and the list owns an older snapshot. The older snapshot cannot be changed to own the new list.

Dotted assignment follows the same rule:

```shirka
record.x -> record.x.y
```

The source is obtained before the destination is changed. The new `y` contains the old value of `record.x`, not a reference to the newly modified value. An overlapping move of an ancestor into its own descendant must instead be rejected or given explicitly specified snapshot behavior; blindly relinking pointers is invalid.

### A.8. Reclamation and ordinary lifetimes

With an acyclic owning graph, reference counting can reclaim scopes without a cycle collector:

- Returning from a call releases its working scope.
- An escaping list retains the snapshots it needs.
- Dropping the last such list releases those snapshots, unless another value still owns them.
- Sibling lists can share an ancestor; dropping one does not invalidate the other.
- Tail calls release the caller’s execution ownership. Only snapshots retained by surviving values remain alive.

A parent reference must be owning, or otherwise have an equivalent lifetime guarantee. A raw pointer into an execution frame would become invalid when that invocation returns. Weak references alone cannot implement escaping lexical captures.

These rules prevent cycle-induced leaks; they do not guarantee low memory use. Capturing an entire visible scope can retain large values that the code never reads. A surviving child can also retain an ancestor containing unrelated fields. This is reachable retention, and may be expensive even though eventual reclamation works correctly.

Likewise, snapshots can include earlier values under a name that is subsequently replaced. That retention is intentional when an existing capture still needs those values. In the absence of such captures, dictionary bookkeeping should not keep obsolete versions alive indefinitely.

Reference-count destruction can cascade through a large graph. This proposal provides deterministic ownership rules, not bounded destruction latency.

### A.9. Recursion remains a separate obligation

A direct owning entry from an operation’s captured dictionary back to that operation would violate the acyclic model. The RFC’s special self-binding therefore cannot be implemented as an ordinary strong reference cycle.

It may be represented symbolically, but escaping nested lists that capture it still need a valid lifetime and execution rule. This annex does not settle that representation. Recursive calls and captures of recursive operations require specific tests before the implementation can claim the acyclicity invariant holds.

Until that mechanism is established, the reclamation argument applies to the ordinary scoped-list graph, not automatically to every proposed recursion feature.

### A.10. Resulting design boundary

Hierarchical dictionaries offer a compact representation of local fields and lexical captures. Nested branches can share a context, escaping lists can retain it, and ordinary invocation-local definitions remain independent.

The essential constraint is that an ancestor denotes a stable captured value, not a mutable execution frame or a live containing object. This preserves Shirka’s copy-or-move model while allowing internal sharing. The remaining design work concerns literal instantiation in generated code and recursive self-bindings, rather than the legitimacy of hierarchical lexical lookup itself.

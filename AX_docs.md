# Ax Language Reference

**Complete specification for the Ax scripting language.**
Written so any developer can implement a fully compatible Ax interpreter from scratch — on Windows, Linux, or any other platform.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Quick Start](#2-quick-start)
3. [Lexical Structure](#3-lexical-structure)
4. [Types](#4-types)
5. [Variables](#5-variables)
6. [Operators & Precedence](#6-operators--precedence)
7. [Control Flow](#7-control-flow)
8. [Functions](#8-functions)
9. [Built-in Statements & Functions](#9-built-in-statements--functions)
10. [String Coercion Rules](#10-string-coercion-rules)
11. [Truthiness](#11-truthiness)
12. [Scoping](#12-scoping)
13. [Error Handling](#13-error-handling)
14. [Formal Grammar (EBNF)](#14-formal-grammar-ebnf)
15. [Limits Reference](#15-limits-reference)
16. [Implementation Notes (Porting Guide)](#16-implementation-notes-porting-guide)
17. [Code Examples](#17-code-examples)

---

## 1. Overview

Ax is a small, dynamically-typed interpreted scripting language. It is designed to be:

- **Simple** — the complete interpreter is ~600 lines of C99 across 4 files
- **Safe** — hard limits on all resources; no heap allocation, no crashes
- **Embeddable** — no dependencies beyond a C standard library subset

Ax scripts have the `.ax` file extension. The interpreter is a classic three-stage pipeline:

```
source text  →  lexer  →  tokens  →  parser  →  AST  →  evaluator  →  output
```

### Running Ax scripts

On **AswdOS**:
```
ax /ROOT/hello.ax          // direct ax command
run /ROOT/hello.ax         // run dispatches .ax files automatically
```

In the **AX Code IDE**: open a `.ax` file, press the **Run** button. Output appears in the terminal.

On a **Windows/Linux port**: see Section 16.

---

## 2. Quick Start

```ax
// Hello, world
print "Hello, world!"

// Variables
let x = 10
let y = 20
print x + y

// User input
print "What is your name?"
let name = input()
print "Hello, " + name + "!"

// Function
fn square(n) {
    return n * n
}
print square(7)
```

Save as `hello.ax`, run with `ax hello.ax`. Each `print` statement outputs one line.

---

## 3. Lexical Structure

### 3.1 Character set

Ax source files are plain ASCII text. Any byte outside printable ASCII is silently ignored by the lexer.

### 3.2 Whitespace

Spaces, tabs, and carriage returns (`\r`) are ignored everywhere except inside string literals.

### 3.3 Newlines

`\n` (LF) is the **statement separator**. A newline token (`TOK_NEWLINE`) is emitted after each logical line except:
- Consecutive newlines (only one separator emitted)
- Newlines immediately after `{` (suppressed — the block body starts fresh)

This means you cannot split a single statement across multiple lines.

### 3.4 Comments

```ax
// This is a comment — everything after // until end of line is ignored
let x = 5  // inline comment
```

Block comments (`/* ... */`) are **not supported**.

### 3.5 Identifiers

```
identifier  ::= [a-zA-Z_] [a-zA-Z0-9_]*
```

Identifiers are case-sensitive. `myVar`, `MyVar`, and `MYVAR` are three different names.

**Reserved keywords** — these cannot be used as identifiers:
```
let  if  else  while  fn  return  print  sys  input  true  false
```

### 3.6 Integer literals

```
integer  ::= [0-9]+
```

Base-10 only. No hex, no octal, no binary, no underscores. Range: 32-bit signed (`-2147483648` to `2147483647`). Negative numbers are written as the unary minus applied to a positive literal: `-42`.

There is no float or decimal literal.

### 3.7 String literals

```
string  ::= '"' [^"\n]* '"'
```

Strings are delimited by double quotes. They cannot span multiple lines. There are no escape sequences — `\n`, `\t`, etc. are not interpreted; they are stored literally. A string cannot contain a double-quote character.

### 3.8 Boolean literals

```
true
false
```

### 3.9 Operators and punctuation

```
=    ==   !=   <    >    <=   >=
+    -    *    /    %
&&   ||   !
(    )    {    }    ,
```

All operators are ASCII. There is no `.` operator (no member access), no `[` `]` (no arrays), no `&` or `|` (no bitwise ops).

---

## 4. Types

Ax is **dynamically typed** — values carry their type at runtime. There are exactly four types:

### `int`
A 32-bit signed integer (`int32_t`). The only numeric type. No floats.

```ax
let a = 42
let b = -7
let c = 1000000
```

### `string`
An immutable sequence of characters. Internally stored in a string pool as a null-terminated C string. Max combined string data: 16,384 bytes.

```ax
let s = "hello"
let t = "world"
let u = s + " " + t    // "hello world"
```

### `bool`
Either `true` or `false`.

```ax
let flag = true
let other = false
```

### `nil`
The absence of a value. Functions that don't explicitly return produce `nil`. Uninitialized contexts produce `nil`.

```ax
fn no_return() {
    // no return statement
}
let x = no_return()    // x is nil
print x                // prints: nil
```

### Type identity

There is no `typeof` or type-checking operator. Types are implicit. Operations on wrong types produce runtime errors (e.g., `-"hello"` is an error because unary minus requires an integer).

---

## 5. Variables

### 5.1 Declaration

```ax
let name = expression
```

`let` declares a new variable in the current scope and initializes it. You **must** always initialize with `let` — there is no uninitialized declaration.

```ax
let x = 10
let greeting = "hello"
let done = false
let nothing = nil        // explicitly set to nil (valid)
```

### 5.2 Assignment

```ax
name = expression
```

Assigns a new value to an existing variable. The variable **must already exist** (declared with `let`). Assigning to an undeclared name is a runtime error.

```ax
let count = 0
count = count + 1        // ok — count exists
total = 100              // ERROR: 'total' was never declared
```

Variables can change type freely:
```ax
let x = 42
x = "now I'm a string"   // ok — dynamic typing
```

### 5.3 Naming rules

- Start with a letter (`a-z`, `A-Z`) or underscore (`_`)
- Followed by any letters, digits (`0-9`), or underscores
- Case-sensitive
- Cannot be a keyword

Valid: `x`, `myVar`, `_count`, `MAX_SIZE`, `i2`
Invalid: `2fast` (starts with digit), `my-var` (hyphen), `fn` (keyword)

---

## 6. Operators & Precedence

### 6.1 Precedence table

Operators are listed from **lowest** to **highest** binding:

| Level | Operator(s) | Type | Associativity |
|-------|-------------|------|---------------|
| 1 | `\|\|` | Logical OR | Left |
| 2 | `&&` | Logical AND | Left |
| 3 | `== != < > <= >=` | Comparison | Left (no chaining) |
| 4 | `+ -` | Addition, subtraction | Left |
| 5 | `* / %` | Multiply, divide, modulo | Left |
| 6 | unary `-` `!` | Negate, logical NOT | Right (prefix) |
| 7 | `(expr)` literals, calls | Primary | — |

Use parentheses to override: `(a + b) * c`.

### 6.2 Arithmetic operators

All arithmetic operates on **integers only**. Applying arithmetic to non-integer types produces a runtime error (except `+` on strings — see 6.5).

| Operator | Meaning | Example |
|----------|---------|---------|
| `+` | Addition (or string concat) | `3 + 4` → `7` |
| `-` | Subtraction | `10 - 3` → `7` |
| `*` | Multiplication | `6 * 7` → `42` |
| `/` | Integer division (truncates toward zero) | `7 / 2` → `3` |
| `%` | Modulo | `10 % 3` → `1` |
| `-x` | Unary negation | `-5` → `-5` |

**Division by zero** is a runtime error. Both `/` and `%` check.

```ax
print 10 / 3     // 3 (truncated)
print -7 / 2     // -3 (truncated toward zero)
print 10 % 3     // 1
```

### 6.3 Comparison operators

Comparisons return a `bool`. They compare the **integer field** of both operands.

| Operator | Meaning |
|----------|---------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |

```ax
print 5 == 5     // true
print 3 != 4     // true
print 10 > 20    // false
```

**Important**: String equality is not directly supported. `"abc" == "abc"` compares internal string pool offsets, which may or may not match. Do not rely on string `==`. To compare strings, compare lengths or use a workaround.

Comparison chains are **not** supported. `a < b < c` is parsed as `(a < b) < c`, which compares a bool against c — almost certainly not what you want. Use `a < b && b < c` instead.

### 6.4 Logical operators

| Operator | Meaning | Notes |
|----------|---------|-------|
| `&&` | Logical AND | Returns bool. Both sides always evaluated. |
| `\|\|` | Logical OR | Returns bool. Both sides always evaluated. |
| `!x` | Logical NOT | Returns bool. |

**There is no short-circuit evaluation.** Both operands of `&&` and `||` are always evaluated before the logical operation. This means `x != 0 && func(x)` will call `func(x)` even if `x == 0`.

```ax
let a = true
let b = false
print a && b     // false
print a || b     // true
print !a         // false
```

`!x` applied to any value: truthiness check (see Section 11), then invert.

### 6.5 String concatenation

`+` performs string concatenation when **either operand is a string**. The non-string operand is automatically coerced:

- `int` → decimal string (`42` → `"42"`)
- `bool` → `"true"` or `"false"`
- `nil` → `"nil"`

```ax
let name = "Alice"
let age = 30
print "Name: " + name           // "Name: Alice"
print "Age: " + age             // "Age: 30"
print "Done: " + true           // "Done: true"
print 1 + 2                     // 3 (integer, no string involved)
print "1" + 2                   // "12" (string concat because "1" is a string)
```

---

## 7. Control Flow

### 7.1 `if` / `else`

```ax
if condition {
    // then branch
}

if condition {
    // then branch
} else {
    // else branch
}
```

- Condition is any expression. See Section 11 for truthiness.
- Braces `{ }` are **required** — no single-statement shorthand.
- `else if` is not a keyword. Chain with `else { if ... { } }`:

```ax
if x < 0 {
    print "negative"
} else {
    if x == 0 {
        print "zero"
    } else {
        print "positive"
    }
}
```

### 7.2 `while`

```ax
while condition {
    // body
}
```

- Evaluates condition before each iteration.
- Exits when condition is falsy (0 / false / nil).
- **Hard limit: 1,000,000 iterations.** The interpreter stops and continues after the loop. This prevents infinite loops from freezing the OS.
- There is no `break` or `continue`.

```ax
let i = 0
while i < 5 {
    print i
    i = i + 1
}
// prints 0, 1, 2, 3, 4
```

### 7.3 No `for` loop

Ax has no `for` loop. Use `while` with a counter variable.

```ax
// Equivalent of: for (int i = 0; i < 10; i++)
let i = 0
while i < 10 {
    print i
    i = i + 1
}
```

### 7.4 No `break` or `continue`

Use a flag variable as a workaround:

```ax
let found = false
let i = 0
while i < 100 {
    if found == false {
        if data == target {
            found = true
        }
    }
    i = i + 1
}
```

---

## 8. Functions

### 8.1 Definition

```ax
fn name(param1, param2, ...) {
    // body
    return value
}
```

- `fn` keyword, followed by the name, then a parenthesized parameter list.
- Zero or more parameters, separated by commas.
- Body is always a block `{ }`.
- `return expr` exits the function and returns a value.
- Bare `return` (no expression) returns `nil`.
- Falling off the end of a function (no `return`) also returns `nil`.

```ax
fn add(a, b) {
    return a + b
}

fn greet(name) {
    print "Hello, " + name + "!"
    // implicit return nil
}
```

### 8.2 Calling

As an expression (in `let`, `=`, `print`, argument to another call):
```ax
let result = add(3, 4)
print add(10, 20)
```

As a statement (call for side effects, return value discarded):
```ax
greet("world")
```

### 8.3 Arguments

- Arguments are passed **by value** (copied).
- The number of arguments must match the number of parameters. Passing too few leaves parameters as whatever is in that slot — **there is no argument count checking**; extra params get whatever random value happens to be there. Always match arg counts.
- Arguments are evaluated left-to-right before the function is entered.

### 8.4 Return values

```ax
fn max(a, b) {
    if a > b {
        return a
    }
    return b
}

let m = max(10, 20)
print m    // 20
```

`return` immediately exits the function — remaining statements in the body are skipped.

### 8.5 Recursion

Functions can call themselves or each other. Maximum call depth is **32**. Deeper recursion produces a runtime error `call stack overflow`.

```ax
fn factorial(n) {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}
print factorial(10)    // 3628800
```

### 8.6 Definition order matters

Functions must be **defined before they are called**. The interpreter evaluates top-level statements in order. A call to an undefined function is a runtime error.

```ax
// WRONG — greet not defined yet
greet("Alice")
fn greet(name) { print "Hello " + name }

// CORRECT — define first
fn greet(name) { print "Hello " + name }
greet("Alice")
```

**Exception**: mutual recursion is possible if both functions are defined before either is called:
```ax
fn is_even(n) {
    if n == 0 { return true }
    return is_odd(n - 1)
}
fn is_odd(n) {
    if n == 0 { return false }
    return is_even(n - 1)
}
print is_even(4)    // true
```

### 8.7 Redefining functions

Defining a function with a name that already exists silently replaces the previous definition. No error.

```ax
fn foo() { return 1 }
fn foo() { return 2 }    // replaces previous foo
print foo()              // 2
```

### 8.8 No first-class functions

Functions cannot be stored in variables, passed as arguments, or returned from functions. `fn` is a declaration-only statement.

---

## 9. Built-in Statements & Functions

### 9.1 `print`

```ax
print expression
```

**Statement** (not a function). Evaluates the expression and prints the result followed by a newline (`\n`). Works on any type:

| Type | Output |
|------|--------|
| `int` | Decimal string, e.g. `42`, `-7` |
| `string` | The string content |
| `bool` | `true` or `false` |
| `nil` | `nil` |

```ax
print 42            // 42
print "hello"       // hello
print true          // true
print nil           // nil
print 3 + 4         // 7
print "x = " + 5    // x = 5
```

No format specifiers. No trailing space. Always exactly one newline at the end.

### 9.2 `input()`

```ax
let line = input()
```

**Expression** (returns a value). Reads one line of text from standard input (keyboard). Blocks until the user presses Enter. Returns the line as a `string`, **not including the newline**.

```ax
print "Enter your name:"
let name = input()
print "Hello, " + name + "!"
```

`input()` takes no arguments. The parentheses are required: `input()` not `input`.

### 9.3 `sys`

```ax
sys "shell command string"
```

**Statement**. Executes a shell command. The argument must be a `string` expression (not a function call result or variable holding an int). The command string is split on spaces into `argv`; the first token is the command name.

On **AswdOS**, this runs the built-in shell command (same as typing it in the terminal). Examples: `sys "ls"`, `sys "ls /ROOT"`, `sys "date"`.

On a **Windows port**, map this to `system()` or `CreateProcess`.
On a **Linux/macOS port**, map to `system()` or `execvp`.

```ax
sys "ls /ROOT"          // list files in /ROOT
sys "date"              // print current date/time
```

The return value of `sys` is always discarded. There is no way to capture command output into an Ax variable.

---

## 10. String Coercion Rules

Coercion only happens automatically during `+` when one operand is a `string`. All other operations require exact types.

### Coercion table

| Type | Coerced to string as |
|------|----------------------|
| `int` | Decimal representation, e.g. `42` → `"42"`, `-3` → `"-3"` |
| `bool` | `true` → `"true"`, `false` → `"false"` |
| `nil` | `"nil"` |
| `string` | Unchanged |

### Rules

1. If **both** operands of `+` are integers → integer addition.
2. If **either** operand of `+` is a string → coerce the other, then concatenate.
3. No coercion happens for `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`.
4. `print` internally coerces any type to its string form for display but does not change the variable's type.

```ax
print 1 + 2           // 3      (int + int)
print "1" + 2         // "12"   (string + int → concat)
print 1 + "2"         // "12"   (int + string → concat)
print true + " flag"  // "true flag"
print nil + "!"       // "nil!"
```

---

## 11. Truthiness

Conditions in `if` and `while` evaluate any expression, then check truthiness.

| Type | Truthy if |
|------|-----------|
| `int` | Value is non-zero |
| `bool` | Value is `true` |
| `nil` | Never truthy (always false) |
| `string` | String pool offset is non-zero (non-empty string usually truthy — but **do not rely on this**; compare with expected values explicitly instead) |

```ax
let x = 0
if x { print "truthy" } else { print "falsy" }   // falsy

let y = 1
if y { print "truthy" } else { print "falsy" }   // truthy

if nil { print "truthy" } else { print "falsy" } // falsy
if false { print "truthy" } else { print "falsy" } // falsy
if true { print "truthy" }                        // truthy
```

The logical `!` operator inverts truthiness and always returns a `bool`.

---

## 12. Scoping

### 12.1 Global scope

All variables declared at the top level of a file live in the global scope (depth 0). They are visible throughout the file, including inside functions (unless shadowed by a local `let`).

```ax
let globalVar = 100

fn printGlobal() {
    print globalVar    // can read global
}
printGlobal()          // prints 100
```

### 12.2 Function scope

Each function call creates a new scope. Variables declared with `let` inside a function body are local to that call:

```ax
fn test() {
    let x = 99
    print x
}
test()
// print x   // ERROR — x is not in scope here
```

### 12.3 No closure / upvalue capture

Functions do **not** capture their enclosing scope. A function defined inside another function (not directly supported by Ax syntax) would not capture variables. In practice, Ax functions can only access:

1. Their own parameters
2. Variables they declared with `let` in their own body
3. Global variables declared at the top level

### 12.4 Block scope (`if`, `while`)

**`if` and `while` blocks do NOT create new scopes.** Variables declared with `let` inside `if` or `while` are in the **same scope as the containing function/global**:

```ax
if true {
    let x = 5
}
print x    // 5 — x is still visible! (no block scoping for if/while)
```

This is a known design characteristic of Ax. Scope is created **only** by function calls, not by `{` blocks.

### 12.5 Variable lookup order

Variables are looked up by linear search of the variable table. The first matching `name` with `active = true` is used, regardless of scope depth. There is no shadowing between two active variables of the same name in different scopes — the most recently declared one will be found first, since newer entries are at higher indices.

---

## 13. Error Handling

### 13.1 Error output

Errors are printed to the terminal/console in the format:

```
[ax] line N: message
```

Where `N` is the line number (1-based) from the source file. Some runtime errors (variable table full, undefined variable) report line 0 when the line context isn't available at evaluation time.

### 13.2 Error behavior

- **Fatal**: all errors immediately halt the interpreter. There is no `try/catch`, no recovery.
- The `g_lang_error` flag is set to 1 and propagates through all stages.
- Partial output may have already appeared on screen before the error.

### 13.3 Error list

**Lexer errors:**
| Message | Cause |
|---------|-------|
| *(token limit exceeded, silent)* | More than 4096 tokens in one file |

**Parser errors:**
| Message | Cause |
|---------|-------|
| `expected identifier after 'let'` | `let` not followed by a name |
| `expected '='` | Missing `=` in `let` statement |
| `expected '{'` | Block opener missing |
| `expected '}'` | Block not closed |
| `expected '('` | Missing `(` after function name in call or definition |
| `expected ')'` | Missing `)` in call or definition |
| `expected ','` | Missing `,` between parameters |
| `expected parameter name` | Non-identifier in parameter list |
| `expected function name` | `fn` not followed by an identifier |
| `unexpected token in expression` | Unparseable expression |
| `unexpected statement` | Unrecognized statement start |
| `expected '=' or '(' after identifier` | Identifier not followed by assignment or call |

**Runtime errors:**
| Message | Cause |
|---------|-------|
| `undefined variable` | Read or assign to undeclared name |
| `undefined function` | Call to undeclared function |
| `variable table full` | More than 128 active variables |
| `too many functions` | More than 32 function definitions |
| `call stack overflow` | Recursion deeper than 32 |
| `division by zero` | `/` or `%` with right operand = 0 |
| `'-' requires integer` | Unary minus on non-integer |
| `sys requires a string` | `sys` given non-string expression |
| `cannot open: <path>` | `lang_run_file` could not read file |
| *(string pool overflow)* | Total string data exceeds 16,384 bytes |
| *(node limit exceeded)* | AST exceeds 2,048 nodes |

---

## 14. Formal Grammar (EBNF)

```ebnf
(* Top level *)
program         ::= stmt_list EOF

stmt_list       ::= { newline } { stmt { newline } }

stmt            ::= let_stmt
                  | assign_stmt
                  | print_stmt
                  | sys_stmt
                  | if_stmt
                  | while_stmt
                  | fn_def
                  | return_stmt
                  | call_stmt

(* Statements *)
let_stmt        ::= "let" IDENT "=" expr

assign_stmt     ::= IDENT "=" expr

print_stmt      ::= "print" expr

sys_stmt        ::= "sys" expr

if_stmt         ::= "if" expr block [ "else" block ]

while_stmt      ::= "while" expr block

fn_def          ::= "fn" IDENT "(" [ param_list ] ")" block

param_list      ::= IDENT { "," IDENT }

return_stmt     ::= "return" [ expr ]

call_stmt       ::= IDENT "(" [ arg_list ] ")"

(* Blocks *)
block           ::= "{" { newline } stmt_list { newline } "}"

(* Expressions — ordered by precedence, lowest to highest *)
expr            ::= or_expr

or_expr         ::= and_expr { "||" and_expr }

and_expr        ::= cmp_expr { "&&" cmp_expr }

cmp_expr        ::= add_expr [ cmp_op add_expr ]

cmp_op          ::= "==" | "!=" | "<" | ">" | "<=" | ">="

add_expr        ::= mul_expr { add_op mul_expr }

add_op          ::= "+" | "-"

mul_expr        ::= unary_expr { mul_op unary_expr }

mul_op          ::= "*" | "/" | "%"

unary_expr      ::= "-" unary_expr
                  | "!" unary_expr
                  | primary

primary         ::= INTEGER
                  | STRING
                  | "true"
                  | "false"
                  | "input" "(" ")"
                  | IDENT "(" [ arg_list ] ")"
                  | IDENT
                  | "(" expr ")"

arg_list        ::= expr { "," expr }

(* Terminals *)
IDENT           ::= [a-zA-Z_] [a-zA-Z0-9_]*
INTEGER         ::= [0-9]+
STRING          ::= '"' [^"\n]* '"'
newline         ::= "\n"
EOF             ::= end of input

(* Comments: // ... \n  — stripped before parsing *)
```

**Notes on the grammar:**
- Newlines are significant as statement separators. A statement must be on one line.
- `cmp_expr` is intentionally non-repeating (single comparison per expression; chaining is not supported at the grammar level — it works left-to-right but compares booleans against values, which is almost never useful).
- `else` is parsed greedily: after any `if` block, the parser looks ahead for an optional `else`.

---

## 15. Limits Reference

| Resource | Limit | Why |
|----------|-------|-----|
| Source file max size | 16,384 bytes | Static read buffer |
| Token array | 4,096 tokens | Static array |
| AST nodes | 2,048 nodes | Static array |
| String pool | 16,384 bytes | Static buffer; 16-bit offsets |
| Variables (total, all scopes) | 128 | Static variable table |
| User-defined functions | 32 | Static function table |
| Call depth (recursion) | 32 | Stack overflow protection |
| `while` iterations | 1,000,000 | Prevents OS freeze on infinite loops |
| Line number tracking | uint16\_t → up to 65,535 lines | Lexer line counter |
| Max string intermediate result | 512 bytes | `g_str_tmp` coercion buffer |

For a Windows/desktop port, these limits can be raised freely. They exist only because AswdOS has no heap allocator and 64 MB of total RAM for the whole OS.

---

## 16. Implementation Notes (Porting Guide)

This section describes how the Ax interpreter is structured so you can implement a clean port.

### 16.1 Architecture overview

```
lang_run_file(path)    ← public API
lang_run_str(src, len) ← public API
    │
    ├── lang_eval_reset()    clear all state
    ├── lang_lex(src, len)   → fills g_toks[], g_tok_count
    ├── lang_parse()         → fills g_nodes[], returns root int16
    └── lang_eval(root)      → executes, prints output
```

All state is global. Running a second script completely resets everything.

### 16.2 String pool

```c
char     g_sp[16384];   // pool storage
uint16_t g_sp_end;      // next free offset (starts at 1; offset 0 = empty string)

uint16_t sp_add(const char *s, int len)  // append, return offset
const char *sp_get(uint16_t off)         // retrieve pointer
```

- Offset 0 is the empty string (`g_sp[0] = '\0'`).
- `sp_add` copies bytes into the pool and returns the start offset.
- Strings are referenced by 16-bit offsets everywhere (in tokens, nodes, variables).
- On a heap-based port, you can replace this with `strdup` and store `char *` directly.

### 16.3 Token structure

```c
typedef struct {
    tok_t    type;   // token kind (enum)
    int32_t  ival;   // integer value (for TOK_INT)
    uint16_t sval;   // string pool offset (for TOK_STR, TOK_IDENT)
    uint16_t line;   // source line number
} lang_token_t;
```

### 16.4 AST node structure

```c
typedef struct {
    uint8_t  type;   // node kind (enum: N_INT, N_IF, etc.)
    int16_t  left;   // left child index (-1 = none)
    int16_t  right;  // right child index (-1 = none)
    int16_t  extra;  // third child (used by N_IF for else branch)
    int32_t  ival;   // integer value (N_INT) or operator token (N_BINOP)
    uint16_t sval;   // string pool offset (N_STR, N_IDENT, N_LET, etc.)
} lang_node_t;
```

Nodes reference each other by index into `g_nodes[]`, not by pointer. `-1` means "no child".

**Node type meanings:**

| Node | `left` | `right` | `extra` | `ival` | `sval` |
|------|--------|---------|---------|--------|--------|
| `N_INT` | — | — | — | integer value | — |
| `N_STR` | — | — | — | — | pool offset |
| `N_BOOL` | — | — | — | 0=false, 1=true | — |
| `N_IDENT` | — | — | — | — | pool offset (name) |
| `N_BINOP` | left operand | right operand | — | operator token enum | — |
| `N_NEG` | operand | — | — | — | — |
| `N_NOT` | operand | — | — | — | — |
| `N_BLOCK` | statement node | next N\_BLOCK | — | — | — |
| `N_LET` | init expr | — | — | — | pool offset (name) |
| `N_ASSIGN` | value expr | — | — | — | pool offset (name) |
| `N_PRINT` | expr | — | — | — | — |
| `N_SYS` | expr | — | — | — | — |
| `N_INPUT` | — | — | — | — | — |
| `N_CALL` | first N\_ARG | — | — | — | pool offset (fn name) |
| `N_ARG` | expr | next N\_ARG | — | — | — |
| `N_IF` | condition | then block | else block (or -1) | — | — |
| `N_WHILE` | condition | body block | — | — | — |
| `N_FNDEF` | first N\_PARAM | body block | — | — | pool offset (fn name) |
| `N_PARAM` | — | next N\_PARAM | — | — | pool offset (param name) |
| `N_RETURN` | expr (or -1) | — | — | — | — |

### 16.5 Value representation

```c
typedef enum { VAL_NIL = 0, VAL_INT, VAL_STR, VAL_BOOL } val_t;

typedef struct {
    val_t    type;
    int32_t  i;      // VAL_INT value, or VAL_BOOL (0/1)
    uint16_t s;      // VAL_STR: string pool offset
} lang_val_t;
```

On a heap-based port, replace `uint16_t s` with `char *s` (heap-allocated or strdup'd strings).

### 16.6 Variable table

```c
typedef struct {
    uint16_t   name;    // pool offset of variable name
    lang_val_t val;     // current value
    uint8_t    depth;   // scope depth when declared
    uint8_t    active;  // 1 = in use
} var_entry_t;

var_entry_t g_vars[128];
int         g_scope_depth;  // current nesting level (0 = global)
```

- `var_let(name, val)` — create new entry at current depth
- `var_assign(name, val)` — find existing entry (any depth), update it
- `var_find(name)` — linear search, returns first active match
- `scope_enter()` — `g_scope_depth++`
- `scope_leave()` — deactivate all variables at current depth, then `g_scope_depth--`

### 16.7 Function table

```c
typedef struct {
    uint16_t name;    // pool offset of function name
    int16_t  params;  // first N_PARAM node index
    int16_t  body;    // first N_BLOCK node index
} fn_entry_t;

fn_entry_t g_fns[32];
int        g_fn_count;
```

Functions are stored as references into the AST (params and body are node indices). They are not re-parsed on each call — the AST persists for the entire run.

### 16.8 Platform-specific replacements

When porting, replace these three OS-specific hooks:

| Ax built-in | AswdOS impl | Windows/Linux replacement |
|-------------|-------------|--------------------------|
| `print val` | `console_writeln(str)` | `printf("%s\n", str)` |
| `input()` | `input_readline(buf, size)` | `fgets(buf, size, stdin)` then strip `\n` |
| `sys "cmd"` | `commands_dispatch(argc, argv)` | `system(cmd)` or `CreateProcess` |

Also replace:
- `vfs_cat(path, buf, size)` → `fopen` + `fread` + `fclose`
- `u32_to_dec(v, buf, size)` → `snprintf(buf, size, "%u", v)`
- `str_copy`, `str_cat`, `str_len`, `str_ncmp` → `strcpy`, `strcat`, `strlen`, `strncmp`
- `console_write(s)` / `console_writeln(s)` → `fputs(s, stdout)` / `puts(s)`

### 16.9 Running the interpreter

```c
// Initialize (once per script run)
g_sp_end     = 1;
g_sp[0]      = '\0';
g_tok_count  = 0;
g_node_count = 0;
g_lang_error = 0;
lang_eval_reset();   // clears vars, fns, return state

// Pipeline
lang_lex(source, length);
if (g_lang_error) return;

int16_t prog = lang_parse();
if (g_lang_error) return;

lang_eval(prog);
```

All state is reset at the start of each run. You can run multiple scripts sequentially in the same process.

---

## 17. Code Examples

### Hello, World

```ax
print "Hello, world!"
```

---

### Variables and Arithmetic

```ax
let a = 10
let b = 3

print a + b    // 13
print a - b    // 7
print a * b    // 30
print a / b    // 3 (integer division)
print a % b    // 1 (modulo)
```

---

### String Concatenation

```ax
let first = "Hello"
let second = "world"
let full = first + ", " + second + "!"
print full    // Hello, world!

let count = 5
print "Count is: " + count    // Count is: 5
```

---

### If / Else

```ax
let x = 42

if x > 100 {
    print "big"
} else {
    if x > 10 {
        print "medium"
    } else {
        print "small"
    }
}
// prints: medium
```

---

### While Loop — Counting

```ax
let i = 1
while i <= 5 {
    print i
    i = i + 1
}
// prints 1 through 5
```

---

### While Loop — Sum

```ax
let sum = 0
let i = 1
while i <= 100 {
    sum = sum + i
    i = i + 1
}
print "Sum 1..100 = " + sum    // Sum 1..100 = 5050
```

---

### FizzBuzz

```ax
let i = 1
while i <= 20 {
    if i % 15 == 0 {
        print "FizzBuzz"
    } else {
        if i % 3 == 0 {
            print "Fizz"
        } else {
            if i % 5 == 0 {
                print "Buzz"
            } else {
                print i
            }
        }
    }
    i = i + 1
}
```

---

### Factorial (Recursive)

```ax
fn factorial(n) {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

print factorial(1)     // 1
print factorial(5)     // 120
print factorial(10)    // 3628800
```

---

### Fibonacci (Recursive)

```ax
fn fib(n) {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

let i = 0
while i <= 10 {
    print fib(i)
    i = i + 1
}
// 0 1 1 2 3 5 8 13 21 34 55
```

---

### User Input and Greeting

```ax
print "What is your name?"
let name = input()
print "Hello, " + name + "! Welcome to AswdOS."

print "Enter a number:"
let n = input()
print "You entered: " + n
```

> **Note**: `input()` always returns a `string`. To use it as an integer you must avoid arithmetic with it, or implement a string-to-int converter function.

---

### String-to-Integer Conversion (Manual)

Since there is no `int(s)` built-in, here is a function that converts a non-negative decimal string:

```ax
fn str_to_int(s) {
    // NOTE: input() returns a string; this converts "123" -> 123
    // Works for non-negative integers only
    // Uses the trick: build result digit by digit
    // Limitation: no way to read individual characters in Ax
    // So this function is a placeholder showing the approach.
    // In a real implementation, add a charcode() built-in.
    return 0    // stub
}
```

> Ax has no character indexing or `charcode` built-in. For scripts that need numeric user input, design them to work with string comparisons or loop counters instead.

---

### Simple Counter with Input

```ax
fn count_up(max) {
    let i = 0
    while i < max {
        print i
        i = i + 1
    }
}

count_up(5)
```

---

### Sys Command Example (AswdOS)

```ax
// Run shell commands from Ax
sys "ls /ROOT"
sys "date"
sys "ax /ROOT/OTHER.AX"    // run another Ax script!
```

On a Windows port, `sys` maps to `system()`:
```c
// Equivalent C implementation of N_SYS node on Windows:
system(sp_get(lv.s));   // lv.s = string pool offset of command
```

---

### Power Function

```ax
fn power(base, exp) {
    let result = 1
    let i = 0
    while i < exp {
        result = result * base
        i = i + 1
    }
    return result
}

print power(2, 10)    // 1024
print power(3, 5)     // 243
```

---

### Max of Three

```ax
fn max2(a, b) {
    if a > b { return a }
    return b
}

fn max3(a, b, c) {
    return max2(a, max2(b, c))
}

print max3(7, 2, 9)    // 9
print max3(1, 1, 1)    // 1
```

---

### Boolean Logic Demo

```ax
let t = true
let f = false

print t && t    // true
print t && f    // false
print f || t    // true
print f || f    // false
print !t        // false
print !f        // true

// Note: && and || evaluate BOTH sides always (no short-circuit)
```

---

### Nested Function Calls

```ax
fn double(n) { return n * 2 }
fn triple(n) { return n * 3 }
fn add(a, b) { return a + b }

print add(double(3), triple(4))    // add(6, 12) = 18
```

---

## Appendix A: Differences from Other Languages

| Feature | Ax | Python | JavaScript |
|---------|----|----|---|
| Types | int, string, bool, nil | dynamic, many types | dynamic, many types |
| Integer type | 32-bit signed only | arbitrary precision | 64-bit float |
| Float support | None | Yes | Yes |
| String indexing | No | Yes | Yes |
| Arrays/lists | No | Yes | Yes |
| Objects/dicts | No | Yes | Yes |
| `for` loop | No (use `while`) | Yes | Yes |
| `break`/`continue` | No | Yes | Yes |
| Short-circuit `&&`/`\|\|` | No (both sides evaluated) | Yes | Yes |
| Closures | No | Yes | Yes |
| First-class functions | No | Yes | Yes |
| Block scoping (`if`/`while`) | No (only function scope) | Yes | Yes (`let`/`const`) |
| Exceptions | No | Yes | Yes |
| Modules/imports | No | Yes | Yes |
| Recursion limit | 32 | ~1000 (default) | ~10000 |
| Loop limit | 1,000,000 iterations | None | None |

---

## Appendix B: Known Gotchas

1. **No short-circuit evaluation** — `a != 0 && divide(10, a)` will call `divide(10, 0)` even when `a == 0`.

2. **`if`/`while` blocks don't create scope** — `let` inside an `if` block persists after the block.

3. **Functions must be defined before called** — unlike some languages, there is no hoisting.

4. **No string comparison** — `"abc" == "abc"` compares pool offsets, not content. Result is implementation-defined.

5. **`input()` always returns string** — there is no built-in way to convert it to an integer.

6. **No way to print without newline** — `print` always adds a newline at the end.

7. **1,000,000 iteration limit on `while`** — loops that legitimately need more iterations will silently stop.

8. **Maximum 128 variables** — this is a global count, not per-scope. Heavy use of local variables in recursive functions will exhaust this quickly.

9. **No modulo on negative numbers** — `(-7) % 3` follows C truncation semantics: result is `-1`, not `2`. This differs from Python's modulo.

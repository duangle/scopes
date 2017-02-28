About Bangra
============

Bangra is a minimalist, interpreted, mixed functional/imperative general purpose
programming language inspired by Scheme, Python and C.

The core interpreter is written in under 6k lines of C++ code, and exports the
fewest features necessary to allow the language to expand itself to a
comfortable level. These features are:

* A polymorphic value of type ``Any`` implemented as a 16-byte sized fat pointer.
* A ``Symbol`` type that maps strings to cached identifiers.
* An **annotation system** that permits to attach a file name, line number and
  column index in the form of an ``Anchor`` to any value.
* A flat, C-compatible **type system** that implements overloadable operations for
  booleans, all signed and unsigned integer types, floats, doubles, arrays,
  vectors, tuples, pointers, structs, unions, enums, function pointers, slices,
  tables, lists and strings.
* A **lexer and parser** for Bangra-style symbolic expressions, which use whitespace
  indentation to delimit code blocks, but also support a restricted form of
  traditional Lisp/Scheme syntax. The parser outputs a tree of cons cells
  annotated with file, line number and column.
* A **foreign function interface** (FFI) based on libffi that permits calling into
  C libraries without requiring any conversions, thanks to the C-compatible
  type system.
* A **clang-based C importer** that generates type libraries from include files on
  the fly.
* A **fully embedded** build of **LLVM** that can be accessed via FFI.
* A **programmable macro preprocessor** that expands symbolic expressions and
  requires only two builtin macros: ``fn/cc`` and ``syntax-extend``.
  These are sufficient to bootstrap the rest of the language.

  The macro preprocessor supports hooks to preprocessing arbitrary lists and
  symbols. This feature is used to support special syntax without having to
  expand the parser.
* A **translator** that generates flow nodes from fully expanded symbolic expressions.

  Flow nodes are functions stored in **control flow form** (CFF), a novel intermediate
  format which requires continuations as its only primitive and eases
  specialization and optimization considerably.
  For more information see
  `A Graph-Based Higher-Order Intermediate Representation <http://compilers.cs.uni-saarland.de/papers/lkh15_cgo.pdf>`_
  by Leissa et al.
* A small ``eval-apply`` style **interpreter loop** that executes flow nodes.
* A small set of built-in functions exported as globals.
* A loader that permits attaching a payload script to the main executable.
* Initialization routines for the root environment.

With these features, the runtime environment is loaded which bootstraps the
remaining elements of the language in several stages:

* Various utility functions, types and macros.
* A global hook to recognize and translate **infix notation**, **dot notation**
  and **symbol prefixes**.
* A read-eval-print (REPL) **console** for casual use.

As Bangra is in alpha stage, some essential features are still missing. These
features will be added in future releases:

* Better error reporting.
* An on-demand **specializer** that translates flow nodes to fast machine code
  using LLVM. The specializer will feature closure elimination and memory tracking.
* A **garbage collection** mechanism for the interpreter. Right now Bangra
  doesn't free any memory at all.
* Debugging support for gdb.

As designer of Bangra, I hope that, over time, you will find Bangra use- and
delightful, instructive, sometimes even entertaining, as you experiment with,
toy around with, learn about, and engineer new programs, utilities, languages,
infrastructures, games and toys written with and for Bangra.


About Scopes
============

Scopes is a general purpose programming language and compiler infrastructure
specifically suited for short turnaround prototyping and development of high
performance applications in need of multi-stage compilation at runtime.

The project was started as an alternative to C++ for programming computer games
and related tools, specifically for solutions that depend heavily on live
procedural generation or aim to provide good modding support.

The compiler is written in about 15k lines of C++ code, supports a LLVM
as well as a SPIR-V backend, and exports a minimal runtime environment. The
remaining features are bootstrapped from within the language.

The language is expression based, but primarily imperative. The syntactical
style marries concepts from Scheme and Python, describing source code with
S-expressions but delimiting blocks by indentation rather than braces. Closures
are supported as a zero-cost abstraction. The type system is strongly statically
typed but fully inferred, therefore every function is a template. Both nominal
and structural typing are supported. Type primitives roughly match C level,
but are aimed to be expandable without limitations. The memory model is
compatible to C/C++ and utilizes simple unmanaged stack and heap memory.

Scopes provides many metaprogramming facilities such as programmable macros,
deterministic/guided folding of constant expressions, metadata necessary for
basic step-by-step debugging as well as inspection of types, constants,
intermediate code, optimized output and disassembly. The environment is suitable
for development of domain specific languages to describe configuration files,
user interfaces, state machines or processing graphs.

Scopes embeds the clang compiler infrastructure and is therefore fully C
compatible. C libraries can be imported and executed at compile- and runtime
without overhead and without requiring special bindings.

The SCIL (Scopes Compiler Intermediate Language) is suitable for painless
translation to SSA forms such as LLVM IR and SPIR-V, of which both are
supported. The SPIR-V compiler can also emit GLSL shader code on the fly.

The Scopes compiler fundamentally differs from C++ and other traditional AOT
(ahead of time) compilers, in that the compiler is designed to remain on-line
at runtime so that functions can be recompiled when the need arises, and
generated machine code can adapt to the instruction set present on the target
machine. This also diminishes the need for a build system. Still, Scopes is
**not** a JIT compiler. Compilation is always explicitly initiated by the user.

About Bangra
============

Bangra is a general purpose programming language and compiler infrastructure 
specifically suited for short turnaround prototyping and development of high 
performance applications in need of multi-stage compilation at runtime.

The project was started as an alternative to C++ for programming computer games
and related tools, specifically for solutions that depend heavily on live
procedural generation or aim to provide good modding support.

The compiler is written in about 10k lines of C++ code on top of a LLVM 
back-end, and exports a minimal runtime environment. The remaining features are
bootstrapped from within the language.

The language is expression based, but primarily imperative. The syntactical
style marries concepts from Scheme and Python, describing source code with
S-expressions but delimiting blocks by indentation rather than braces. Closures
are supported as a zero-cost abstraction. The type system is strongly statically
typed but fully inferred, supporting both nominal and structural typing.
Therefore, every function is a template. Type primitives roughly match C level,
but are aimed to be expandable without limitations. The memory model is
compatible to C/C++ and utilizes simple unmanaged stack and heap memory.

Bangra provides many metaprogramming facilities such as programmable macros,
deterministic/guided folding of constant expressions, metadata necessary for
basic step-by-step debugging as well as inspection of types, constants, 
intermediate code, optimized output and disassembly. The environment is suitable
for development of domain specific languages to describe configuration files, 
user interfaces, state machines or processing graphs.

Bangra embeds the clang compiler infrastructure and is therefore fully C 
compatible. C libraries can be imported and executed at compile- and runtime
without overhead and without requiring special bindings.

The Bangra IL (intermediate language) is suitable for painless translation to 
SSA forms such as LLVM IR and SPIR-V, of which the former is already supported.
A future version of Bangra will target Vulkan-enabled GPUs as well.

The Bangra compiler fundamentally differs from C++ and other traditional AOT 
(ahead of time) compilers, in that the compiler is designed to remain on-line 
at runtime so that functions can be recompiled when the need arises, and
generated machine code can adapt to the instruction set present on the target 
machine. This also diminishes the need for a build system. Still, Bangra is
**not** a JIT compiler. Compilation is always explicitly initiated by the user.

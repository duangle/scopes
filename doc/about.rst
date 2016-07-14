About Bangra
============

Dear User,

Bangra (named after Bhangra, the Punjabi dance and music style) is a programming
infrastructure built around the idea that statically compiled programs should
have the ability to extend themselves at both compile- and runtime, and the
power to define for themselves what runtime actually means.

A Bangra program can compile and launch itself right out of the compiler, or
generate a new binary that does or does not reuse Bangra's runtime compiler
services. The idea behind this is that every compiled program should have the
same generative power as the original compiler, and the ability to locally
extend and transform this compiler *as it is compiling*, in any way its user
sees fit.

Bangra achieves this with a thin layer implemented on top of LLVM, shipped
as a shared library, that constructs LLVM IR from symbolic expressions and adds
the necessary metaprogramming idioms to enable programs

* to execute generated machine code during the compile process,
* to hook and radically transform the process in which expressions are
  generated and understood,
* and to generate and compile new code at runtime.

Bangra is intended to be suitable for use cases such as

* System programming of expandable kernels, game engines and other realtime performance
  dependent programs, a task typically managed with static languages like
  C, C++ or Rust.
* Scripting of simple maintenance tools and inhouse utilities, a task typically
  assigned to dynamic languages like Python, Ruby, Perl or Bash.
* Rapid prototyping that elegantly scales from an agile, largely type-free
  context to an optimized statically typed high performance context.
* Live programming which needs to compile and execute new machine code as the
  program is running.
* Designing editors and IDE's that intimately interact with the code being
  written and edited, offline, during debugging and at runtime.
* Engineering of plugin and modding systems that give the user full control
  over a program without having to operate dedicated development and build tools.
* Development of new static and dynamic languages and idioms, functional or
  imperative, universal and domain specific, for tinkering and productive use,
  and for all shades inbetween those extremes, which can be freely mixed to
  serve the context in which they are required.
* Development of translation layers for closed language specifications such as
  Javascript and GLSL, permitting to write shaders or websites with
  symbolic expressions and metaprogramming idioms.
* Automated transformation of source code for the purpose of refactoring, porting,
  annotation or emulation.

Bangra is not

* A language. Bangra IR is meant as a way to bootstrap languages, and the
  Bangra language implemented on top of it provides all means for developing
  new idioms and even completely different syntax. You are not stuck
  with a one size fits all solution; instead, the design trusts that *only you*
  know best what you need.
* A standard runtime. The Bangra core will always be minimal, and the community
  is encouraged to find its own purpose oriented standards, for which there is
  certainly more than one optimal solution.
* A framework that shackles its user to certain principles and guidelines with
  no means of escape. Instead, it is a tool meant to bootstrap the programming
  process with the greatest possible freedom, and to aid its user to get to
  results elegantly, without circumvention or misappropriation.

Bangra is still in early development, so not all promises can be fulfilled
at this point in time, but it is this authors conviction that the fundament
is sufficiently clean and fertile enough to permit all stated goals to be reached.

Much thanks is owed to the many developers at Apple who made LLVM and Clang a
reality, without which this project would have become an insurmountable task.
The extent to which we owe these people our ongoing happiness will only become
clear in many productive decades to come.

I hope that you will find Bangra useful, delightful, instructive, sometimes even
entertaining, as you experiment with, toy around with, learn about, and engineer
new programs, utilities, languages, infrastructures, games and toys written with
and for Bangra.

Sincerely,

Leonard Ritter

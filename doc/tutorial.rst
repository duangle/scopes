The Scopes Tutorial
===================

This tutorial does not attempt to cover every single feature, but focuses
on Scopes' most noteworthy features to give you a good idea of the 
language’s flavor and style. After reading it, you will be able to read and 
write Scopes modules and programs.

Using the Scopes Live Compiler
------------------------------

After downloading and unpacking the latest release of Scopes, the easiest way
to start it is to simply launch the executable shipped with the archive. It
is usually located in the root directory, and on Unix compatible systems
it can simply be started from the terminal with:

..  code-block:: none

    $ ./scopes

On Windows, and on systems where Scopes has been installed system-wide, it can
be started from the command line without the preceding dot:

..  code-block:: none

    > scopes

Interactive Console
```````````````````

When ``scopes`` is launched without arguments, it enters an interactive
read-eval-print loop (REPL), also called a console. Here's an example:

..  code-block:: none

    $ ./scopes
      \\\
       \\\
     ///\\\
    ///  \\\  Scopes 0.7 (debug build, Jul 22 2017, 10:51:02)
    $0 ▶

Simple expressions can be written on a single line, followed by hitting the 
return key::

    $0 ▶ print "hello world"
    hello world
    $0 ▶ 

Multiline expressions can be entered by trailing the first line with a space
character, and exited by entering nothing on the last line::

    $0 ▶ print#put a space here
    ....     "yes"
    ....     "this"
    ....     "is"
    ....     "dog"
    ....     
    yes this is dog
    $0 ▶ 

Entering a value binds it to the name indicated by the prompt, and can then
be reused::

    $0 ▶ 3
    $0 = 3
    $1 ▶ print $0
    3
    $1 ▶ 

A special keyboard shortcut (``Control-D`` on Unix, ``Control-Z`` on Windows)
at the prompt exits the program. You can also exit the program by typing
``exit 0`` followed by hitting the return key.

Launcher
````````

Most of the time you would like to use Scopes to compile and execute your own 
written Scopes programs. This is simply done by appending the name of the 
Scopes file you would like to launch to the executable::

    $ scopes path/to/my/program.sc

A Fistful of Scopes
-------------------

Many of the examples in this tutorial include comments, even those entered at
the console. Comments in Scopes start with a hash character ``#`` and extend 
to the first line starting with a character at a lower indentation.

Some examples::

    # this is the first comment
    print "hey!" # and this is a second comment
                   and a third, continuing on the same indentation
    let str = "# hash characters inside string quotes don't count as comments"

Using Scopes as a Calculator
````````````````````````````

Scopes is not only a fully-fledged compiler infrastructure, but also works 
nicely as a comfy calculator::

    $0 ▶ 1 + 2 + 3
    $0 = 6
    $1 ▶ 23 + 2 * 21
    $1 = 65
    $2 ▶ (23 + 2 * 21) / 5
    $2 = 13.0
    $3 ▶ 8 / 5 # all divisions return a floating point number
    $3 = 1.6

Integer numbers like ``6`` or ``65`` have type `i32`, real numbers with a
fractional part like ``13.0`` or ``1.6`` have type `f32`.

Division always returns a real number. On the off-chance that you want an
integer result without the fractional part, use the floor division operator 
`//`::

    $0 ▶ 23 / 3 # regular division returns a real
    $0 = 7.66667
    $1 ▶ 23 // 3 # floor division returns an integer
    $1 = 7
    $2 ▶ 23 % 3 # modulo returns the remainder
    $2 = 2
    $3 ▶ $1 * 3 + $2 # result * divisor + remainder
    $3 = 23

Binding Names
`````````````

Notice how the last example leveraged the auto-memorization function of the
console to bind any result to a name for reuse. But we can also make use of
`define` to bind values to specific names::

    $0 ▶ define width 23
    $0 ▶ define height 42
    $0 ▶ width * height
    $0 = 966
    
On a side note: `define` does not bind to free variables, which is not a problem
in interactive mode. Scopes' main mechanism to bind computation results to names
is `let`, which on the console can only be used in contiguous blocks::

    $0 ▶ let width = 23#don't forget to enter space here
    .... let height = 42
    .... width * height
    .... 
    $0 = 966

If a name isn't bound to anything, using it will give you an error, which is
useful when you've just mistyped it::

    $0 ▶ define color "red"
    $0 ▶ colour
    <string>:1:1: error: use of undeclared identifier 'colour'. Did you mean 'color'?

Strings
```````

Life can be tedious and boring at times. Why not perform some string operations 
to pass the time? We start with some light declarations of string literals::

    $0 ▶ "make it so" # every string is wrapped in double quotes
    $0 = "make it so"
    $1 ▶ "\"make it so!\", he said" # nested quotes need to be escaped
    $1 = "\"make it so!\", he said"
    $2 ▶ "'make it so!', he said" # single quotes are no problem though
    $2 = "'make it so!', he said"
    $3 ▶ "1. make it so 
    .... 2. ???
    .... 3. profit!" # defining a multi-line string
    .... 
    $3 = "1. make it so\n2. ???\n3. profit!"
    
In the interactive console output, the output string is enclosed in quotes and
special characters are escaped with backslashes, to match the way the string
has been declared. Sometimes this might look a little different from the input,
but the strings are equivalent. The `print` function produces a more readable
output that produces the intended look::

    $0 ▶ print "make it so"
    make it so
    $0 ▶ print "\"make it so!\", he said"
    "make it so!", he said
    $0 ▶ print "1. make it so 
    .... 2. ???
    .... 3. profit!"
    .... 
    1. make it so
    2. ???
    3. profit!
    
Sometimes it's necessary to join several strings into one. Strings can be
joined with the `..` operator::

    $0 ▶ "Sco" .. "pes" .. "!" # joining three strings together
    $0 = "Scopes!"
    $1 ▶ .. "Sco" "pes" "!" # using prefix notation
    $1 = "Scopes!"

The inverse operation, slicing strings, can be performed with the `slice`
operation::

    $0 ▶ "scopes" # bind the string we're working on to $0
    $0 = "scopes"
    $1 ▶ slice $0 1 # slice starting at the second character
    $1 = "copes"
    $2 ▶ slice $0 1 5 # slice four letters from the center
    $2 = "cope"
    $3 ▶ slice $0 0 -1 # a negative index selects from the back
    $3 = "scope"
    $4 ▶ slice $0 -2 # get the last two characters
    $4 = "es"
    $5 ▶ slice $0 2 3 # get the center character
    $5 = "o"

One way to remember how slices work is to think of the indices as pointing 
*between* characters, with the left edge of the first character numbered 0. Then
the right edge of the last character of a string of *n* characters has index *n*, 
for example:

..  code-block:: none

     +---+---+---+---+---+---+
     | S | c | o | p | e | s |
     +---+---+---+---+---+---+
     0   1   2   3   4   5   6
    -6  -5  -4  -3  -2  -1    

If we're interested in the byte value of a single character from a string, we
can use the `@` operator, also called the at-operator, to extract it::

    $0 ▶ "abc" @ 0
    $0 = 97:i8
    $1 ▶ "abc" @ 1
    $1 = 98:i8
    $2 ▶ "abc" @ 2
    $2 = 99:i8
    $3 ▶ "abc" @ -1 # get the last character
    $3 = 99:i8

The `countof` operation returns the byte length of a string::

    $2 ▶ countof "six"
    $2 = 3:usize
    $3 ▶ countof "three"
    $3 = 5:usize
    $4 ▶ countof "five"
    $4 = 4:usize

A Mild Breeze of Programming
````````````````````````````

Many calculations require repeating an operation several times, and of course
Scopes can also do that. For instance, here is one of the typical examples
for such a task, computing the first few numbers of the fibonacci sequence::

    $0 ▶ let [loop] a b = 0 1 
    .... if (b < 10)
    ....     print b
    ....     loop b (a + b)
    ....     
    1
    1
    2
    3
    5
    8

This example introduces several new features.

* The first line performs a *named* assignment, which assigns a label to it
  (in this example, named ``loop``, but any name is fine) so we can jump back 
  (see the fourth line), bind new values to those names, and perform the same 
  following operations again: in short, to build a loop. 
* The first line also performs multiple assignments at the same time. ``a`` is
  initially bound to ``0``, while ``b`` is initialized to ``1``. When we jump
  to this assignment again in line four, ``a`` will be bound to ``b``, while
  ``b`` will be bound to the result of calculating ``(a + b)``.
* In the second line, we perform a *conditional operation*. That is, the 
  indented block formed by lines three and four is only executed if the 
  expression ``(b < 10)`` evaluates to `true`. In other words: we are going
  to be performing the loop as long as ``b`` is smaller than ``10``.
* Scopes offers a set of comparison operators for all basic types. You can 
  compare any two numbers using `<` (less than), `>` (greater than), 
  `==` (equal to), `<=` (less than or equal to), `>=` (greater than or equal to)
  and `\!=` (not equal to).
* The body of the conditional block is indented: indentation is Scopes' way of
  grouping statements. At the console, you have to type a tab or four spaces for 
  each indented line. In practice you will prepare more complicated input for 
  Scopes with a text editor; all decent text editors have an auto-indent 
  facility. Note that each line within a basic block must be indented by the 
  same amount.

Controlling Flow
----------------

Let's get a little deeper into ways you can structure control flow in Scopes.

`if` Expressions
````````````````

You have seen a small bit of `if` in that fibonacci example. `if` is your
go-to solution for any task that requires the program to make decisions.
Another example::

    $0 ▶ prompt "please enter a word: "
    please enter a word: bang
    $0 = "bang"
    $1 ▶ if ($0 < "n") 
    ....     print "early in the dictionary, good choice!"
    .... elseif ($0 == "scopes")
    ....     print "oh, a very good word!"
    .... elseif ($0 == "")
    ....     print "that's no word at all!"
    .... else
    ....     print "late in the dictionary, nice!"
    ....     
    early in the dictionary, good choice!

You can also use `if` to decide on an expression::

    $0 ▶ print "you chose" 
    ....     if true
    ....         "poorly"
    ....     else
    ....         "wisely"
    ....         
    you chose poorly

Defining Functions
``````````````````

Let's generalize the fibonacci example from earlier to a function that can
write numbers from the fibonacci sequence up to an arbitrary boundary::

    $0 ▶ fn fib (n) # write Fibonacci series up to n
    ....     let [repeat] a b = 0 (unconst 1)
    ....     if (a < n)
    ....         io-write! (repr a)
    ....         io-write! " "
    ....         repeat b (a + b)
    ....     io-write! "\n"
    ....     
    $0 = fib(n)▶?:Label
    $1 ▶ fib 2000 # call the function we just defined
    0 1 1 2 3 5 8 13 21 34 55 89 144 233 377 610 987 1597 

The keyword `fn` introduces a function definition. It must be followed by an
optional name and a list of formal parameters. All expressions that follow
form the body of the function and it's good taste to indent them.

Executing (also called *applying*) a function binds the passed arguments to its
formal parameters and performs the actions within the function with that
argument standing in.

In this example, ``n`` is bound to ``2000``, all instances of ``n`` in the body
of ``fib`` are replaced with ``2000``, and therefore the loop is executed until
the condition ``a < 2000`` is `true`.

You may notice a small change to the loop variables here, passing ``(unconst 1)``
instead of simply ``1``. Conceptually, `unconst` seems to have no effect when
examined::

    $0 ▶ 23
    $0 = 23
    $1 ▶ unconst 23
    $1 = 23

But when we use the `dump` operation to look at values as they appear to the
compiler as it *proves* that expressions are properly typed and folds constant
expressions, we see a significant difference::

    $0 ▶ dump 23
    <string>:1:1: dump: 23
    $0 = 23
    $1 ▶ dump (unconst 23)
    <string>:1:1: dump: <unknown>:i32
    $1 = 23

The Scopes compiler is guaranteed to inline constant values and fold constant
expressions wherever they occur, which can become a problem with loops that run
for many iterations or have non-constant exit conditions. Let's see what happens
when we remove `unconst` from ``fib``::

    $0 ▶ fn fib (n) 
    ....     let [repeat] a b = 0 1
    ....     if (a < n)
    ....         io-write! (repr a)
    ....         io-write! " "
    ....         repeat b (a + b)
    ....     io-write! "\n"
    ....     
    $0 = fib(n)▶?:Label
    $1 ▶ fib 2000 # this one still works
    0 1 1 2 3 5 8 13 21 34 55 89 144 233 377 610 987 1597 
    $2 ▶ fib 2147483647 # but look here
    <string>:1:1: in <string>
        fib 2147483647
    <string>:4:19: in anonymous function
        io-write! (repr a)
    /home/lritter/devel/duangle/scopes/core.sc:96:1: error: instance limit reached 
        while unrolling named recursive function. Use less constant arguments.

we see that `unconst` regulates which expressions are evaluated at compile time
and which aren't.







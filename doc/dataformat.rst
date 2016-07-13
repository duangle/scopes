The Interchange Format
======================

This chapter outlines the syntax of Bangra source code at the data interchange
format level from the perspective of nesting arbitrary lists. No programming
happens at this stage, it's just data formatting, which means that the format
can also act as a replacement for other interchange formats like XML and JSON.

Element Types
-------------

The Bangra parser has been designed for minimalism and recognizes only five element
types:

* Symbols
* Strings
* Integers
* Reals
* Lists

All further understanding of types is done with additional parsing at later stages
that depend on language context and evaluation scope.

Comments
^^^^^^^^

Line comments are skipped by the lexer. A comment token is recognized by its
``#`` prefix and scanned until the next newline character. Here are some
examples for valid comments::

    #mostly harmless
    # 123
    #"test"
    #(an unresolved tension
    ## #(another comment)# ##

Strings
^^^^^^^

Strings describe sequences of unsigned 8-bit characters in the range of 0-255 and
are stored as zero-terminated string arrays. A string begins and ends with
``"`` (double quotes).  The ``\`` escape character can be used to include quotes
in a string and describe unprintable control characters such as `\\n` (return)
and `\\t` (tab). The parser parses strings as-is, so UTF-8 encoded strings will
be copied over verbatim, and return characters will be preserved, allowing
strings to span multiple lines.

Here are some examples for valid strings::

    "single-line string, 'double' quotations"
    "multi-
    line
    string"
    "return: \n, tab: \t, backslash: \\, double quote: \"."

Symbols
^^^^^^^

Like strings, a symbol describes a sequence of 8-bit characters, but uses
whitespace as delimiter and is often understood on a language level as a label
or bindable name. Symbols may contain any character from the UTF-8 character set
except whitespace and any character from the set ``#;()[]{},``. A symbol always
terminates when one of these characters is encountered. Any symbol that parses
as a number is also excluded. Like strings, symbols are stored as
zero-terminated string arrays and share almost all string API functions.

Here are some examples for valid symbols::

    # classic underscore notation
    some_identifier _some_identifier
    # hyphenated
    some-identifier
    # mixed case
    SomeIdentifier
    # fantasy operators
    &+ >~ >>= and= str+str
    # numbered
    _42 =303

Numbers
^^^^^^^

Numbers come in two forms: integers and reals. The parser understands integers
in the range -(2^63) to 2^64-1 and records them as signed 64-bit values. Reals
are floating point numbers parsed and stored as IEEE 754 binary64 values, which
guarantees integer precision up to 52-bit.

Here are some examples for valid numbers::

    # positive and negative integers
    0 +23 42 -303 12 -1 -0x20
    # positive and negative reals
    0.0 1.0 3.14159 -2.0 0.000003 0xa400.a400
    # reals in scientific notation
    1.234e+24 -1e-12
    # special reals
    +inf -inf nan


Lists
^^^^^

Lists are the only nesting type, scoped by the bracket pairs ``()``, ``[]``
and ``{}``. A list is stored as a pointer to its first element, and elements
are chained in a single link that terminates with a null pointer. Empty lists
are stored as null pointers.

Lists can be empty or contain an unlimited number of elements, separated by
whitespace. They typically describe expressions in Bangra.

Here are some examples for valid lists::

    # empty list
    ()
    # list containing a symbol, a string, an integer, a real, and an empty list
    (print "hello world" 303 3.14 ())
    # three nesting lists
    ((()))

Naked & Coated Lists
--------------------

Every Bangra source file is parsed as a top level list, where the head element
is the language name.

The classic notation (what we will call *coated notation*) uses the syntax known
to `Lisp <http://en.wikipedia.org/wiki/Lisp_(programming_language)>`_ and
`Scheme <http://en.wikipedia.org/wiki/Scheme_(programming_language)>`_ users
as *restricted* `S-expressions <https://en.wikipedia.org/wiki/S-expression>`_::

    (my-lang # A header that selects the interpreter to be invoked

    # there must not be any tokens outside the parentheses guarding the
    # top level list.

    # nested lists as nested expressions:
    (print (.. "Hello World"))

    # some languages use the last element as return value.
    # scripts usually return null.
    null)

As a modern alternative, Bangra offers a *naked notation* where the scope of
lists is implicitly balanced by indentation, an approach used by
`Python <http://en.wikipedia.org/wiki/Python_(programming_language)>`_,
`Haskell <http://en.wikipedia.org/wiki/Haskell_(programming_language)>`_,
`YAML <http://en.wikipedia.org/wiki/YAML>`_,
`Sass <http://en.wikipedia.org/wiki/Sass_(stylesheet_language)>`_ and many
other languages.

This source parses as the same list in the coated example::

    my-lang # a single element on a single line remains unwrapped.

    # nesting is implied by indentation.
    # a sub paragraph continues the list.
    print
        # multiple elements on a single line without sub-paragraph are wrapped
        # in a list.
        .. "Hello World"

    null

Mixing Modes
^^^^^^^^^^^^

Naked lists can contain coated lists, but coated lists can
only contain other coated lists::

    # compute the value of (1 + 2 + (3 * 4)) and print the result
    (print
        (+ 1 2
            (3 * 4)))

    # the same list in naked notation.
    # indented lists are spliced into the parent list:
    print
        + 1 2
            3 * 4

    # any part of a naked list can be coated
    print
        + 1 2 (3 * 4)

    # but a coated list can not contain naked parts
    print
        (+ 1 2
            3 * 4) # parsed as (+ 1 2 3 * 4), a syntax error

    # correct version:
    print (+ 1 2 (3 * 4))

Because it is more convenient for users without specialized editors to write
in naked notation, and balancing parentheses can be challenging for beginners,
the author suggests to use coated notation sparingly and in good taste.
Purists and enthusiasts may however prefer to keep only the top level naked,
as in most Lisp-like languages, and work exclusively with coated lists
otherwise.

Therefore Bangra's reference documentation describes all available symbols in
coated notation, while code examples make ample use of naked notation.

Block Comments
--------------

In addition to ``# single line comments``, Bangra recognizes and strips a special
kind of multi-line comment.

A list beginning with a symbol that starts with a ``///`` (triple slash)
describes a block comment. Block comments have to remain syntactically
consistent. Here are some examples for valid block comments::

    # block comments in coated notation
    (///this comment
        will be removed)
    (///
        # commenting out whole sections
        (function ()
            true)
        (function ()
            false))

    # block comments in naked notation
    ///this comment
        will be removed

    ///
        # commenting out whole sections
        function ()
            true
        function ()
            false

List Separators
---------------

Both naked and coated lists support a special control character, the list
separator `;` (semicolon). Known as statement separator in other languages,
it wraps all elements from the previous `;` or beginning of list in a new list,
and allows to reduce the amount of required parentheses in complex trees.

Here are some examples::

    # in coated notation
    (print a; print (a;b;); print c;)
    # parses as
    ((print a) (print ((a) (b))) (print c))

    # in naked notation
    print a;
        print b; print c;
            print d;
    # parses as
    ((print a) (print b) ((print c) (print d)))

There's a caveat though: if trailing elements aren't terminated with `;`,
they're not going to be wrapped::

    # in coated notation
    print a; print (a;b;); print c
    # parses as
    ((print a) (print ((a) (b))) print c)

Pitfalls of Naked Notation
--------------------------

As naked notation giveth the user the freedom to care less about parentheses,
it also taketh away. In the following section we will discuss the few
small difficulties that can arise and how to solve them efficiently.

Single Elements
^^^^^^^^^^^^^^^

Special care must be taken when single elements are defined, which are not to
be wrapped in lists.

Here is a coated list describing an expression printing the number 42::

    (print 42)

The naked equivalent declares two elements in a single line, which are implicitly
wrapped in a single list::

    print 42

A single element on its own line is always taken as-is::

    print           # (print
        42          #       42)

What happens when we want to call functions without arguments? Consider this example::

    # a coated list in a new scope, describing an expression
    # printing a new line, followed by the number 42
    (do
        (print)
        (print 42))

A naive naked transcription would probably look like this, and be very wrong::

    do
        # suprisingly, the new line is never printed, why?
        print
        print 42

Translating the naked list back to coated reveals what is going on::

    (do
        # interpreted as a symbol, not as an expression
        print
        (print 42))

The straightforward fix to this problem would be to explicitly wrap the single
element in parentheses::

    do
        (print)
        print 42

Nudists might however want to use the list separator `;` (semicolon) which
forces the line to be wrapped in a list and therefore has the same effect::

    do
        print;
        print 42

Wrap-Around Lines
^^^^^^^^^^^^^^^^^

There are often situations when a high number of elements in a list
interferes with best practices of formatting source code and exceeds the line
column limit (typically 80 or 100).

In coated lists, the problem is easily corrected::

    # import many symbols from an external module into the active namespace
    (import-from "OpenGL"
        glBindBuffer GL_UNIFORM_BUFFER glClear GL_COLOR_BUFFER_BIT
        GL_STENCIL_BUFFER_BIT GL_DEPTH_BUFFER_BIT glViewport glUseProgram
        glDrawArrays glEnable glDisable GL_TRIANGLE_STRIP)

The naked approach interprets each new line as a nested list::

    # produces runtime errors
    import-from "OpenGL"
        glBindBuffer GL_UNIFORM_BUFFER glClear GL_COLOR_BUFFER_BIT
        GL_STENCIL_BUFFER_BIT GL_DEPTH_BUFFER_BIT glViewport glUseProgram
        glDrawArrays glEnable glDisable GL_TRIANGLE_STRIP

    # coated equivalent of the term above; each line is interpreted
    # as a function call and fails.
    (import-from "OpenGL"
        (glBindBuffer GL_UNIFORM_BUFFER glClear GL_COLOR_BUFFER_BIT)
        (GL_STENCIL_BUFFER_BIT GL_DEPTH_BUFFER_BIT glViewport glUseProgram)
        (glDrawArrays glEnable glDisable GL_TRIANGLE_STRIP))

It comes easy to just fix this issue by putting each element on a separate line,
which is not the worst solution::

    # correct solution using single element lines
    import-from "OpenGL"
        glBindBuffer
        GL_UNIFORM_BUFFER
        glClear
        GL_COLOR_BUFFER_BIT
        GL_STENCIL_BUFFER_BIT
        GL_DEPTH_BUFFER_BIT
        glViewport
        glUseProgram
        glDrawArrays
        # comments should go on a separate line
        glEnable
        glDisable
        GL_TRIANGLE_STRIP

A terse approach would be to make use of the ``\`` (splice-line) control
character, which is only available in naked notation and splices the line
starting at the next token into the active list::

    # correct solution using splice-line, postfix style
    import-from "OpenGL" \
        glBindBuffer GL_UNIFORM_BUFFER glClear GL_COLOR_BUFFER_BIT \
        GL_STENCIL_BUFFER_BIT GL_DEPTH_BUFFER_BIT glViewport glUseProgram \
        glDrawArrays glEnable glDisable GL_TRIANGLE_STRIP

Unlike in other languages, ``\`` splices at the token level rather than the
character level, and can therefore also be placed at the beginning of nested
lines, where the parent is still the active list::

    # correct solution using splice-line, prefix style
    import-from "OpenGL"
        \ glBindBuffer GL_UNIFORM_BUFFER glClear GL_COLOR_BUFFER_BIT
        \ GL_STENCIL_BUFFER_BIT GL_DEPTH_BUFFER_BIT glViewport glUseProgram
        \ glDrawArrays glEnable glDisable GL_TRIANGLE_STRIP

Tail Splicing
^^^^^^^^^^^^^

While naked notation is ideal for writing nested lists that accumulate
at the tail::

    # coated
    (a b c
        (d e f
            (g h i))
        (j k l))

    # naked
    a b c
        d e f
            g h i
        j k l

...there are complications when additional elements need to be spliced back into
the parent list::

    (a b c
        (d e f
            (g h i))
        j k l)

A simple but valid approach would be to make use of the single-element rule again
and put each tail element on a separate line::

    a b c
        d e f
            g h i
        j
        k
        l

But we can also reuse the splice-line control character to this end::

    a b c
        d e f
            g h i
        \ j k l

Left-Hand Nesting
^^^^^^^^^^^^^^^^^

When using infix notation, conditional blocks or functions producing functions,
lists occur that nest at the head level rather than the tail::

    ((((a b)
        c d)
            e f)
                g h)

While this list tree is easy to describe in coated notation, it can not be
described in pure naked notation. Though these situations seldom occur, a mix
of multiple techniques may act as a compromise::

    # equivalent structure
    (a b; c d) e f;
        \ g h

A more complex tree which also requires splicing elements back into the parent
list can be realized with the same combo of line comment and splice-line
control character::

    # coated
    (a
        ((b
            (c d)) e)
        f g
        (h i))

    # naked
    a
        b (c d);
            e
        \ f g
        h i

While this example demonstrates the versatile usefulness of splice-line and
list separator, less seasoned users may prefer to express similar trees in
partial coated notation instead.

As so often, the best format is the one that fits the context.

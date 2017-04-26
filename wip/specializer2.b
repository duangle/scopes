
#### test #####

fn pow2 (x)
    * x x

fn pow (x n)
    if (n == 0) 1
    elseif ((n % 2) == 0) (pow2 (pow x (n / 2)))
    else (x * (pow x (n - 1)))

assert
    (pow 2 5) == 32

fn pow2-int (x : int)
    pow2 x

dump-flow pow2-int

fn flow-key (flow) (Symbol (repr (flow-uid flow)))
fn parameter-key (param)
    Symbol
        ..
            repr
                flow-uid
                    parameter-flow param
            \ " "
            repr
                parameter-index param

print
    parameter-key
        va@ 1
            flow-parameters (closure-flow pow2-int)

print
    Frame none (closure-flow pow2) 2




#
    we begin with value inlining?

    transform a full list of arguments + frame into another full list of arguments,
    until the list can no longer be transformed; the resulting list of arguments
    is the residual program.

    the input list has an incomplete format; when it has a parameter instead of
    a value, and that parameter is unbound, it will not be specialized.

    the idea here is that the "builtin" functions always receive their arguments
    unevaluated to figure out which ones are constant and which ones are not.

    best case: full evaluation
    cont func args -> none cont result
    * when the cont is known, it can be inlined
    *



    also need to expand varargs to individual parameters

    also, return continuation can be called with different types - must
    specialize argument passed by caller.

    start inlining with a flow body definition, not with a flow header?

    specializing a flow node for both of
        * a given frame
        * a given set of typed variables and constants for arguments
    where the continuation argument is either untyped or none

    yields either a flow node with
        * inlined and/or typed arguments
        * a continuation argument that is one of
            * none
            * parameter of type closure[argtype, ...]
    or a value?

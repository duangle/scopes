
fn do_something ()
    assert false

fn mul-zero-aware (a b)
    if (((constant? a) and (a == 0)) or ((constant? b) and (b == 0)))
        0
    else
        mul a b

fn foo (a b)
    while ((mul-zero-aware a b) != 0)
        do_something;

fn foo1 (b)
    foo 0 b

# should not loop forever
foo1 5;


let object =
    (qualifier (quote test.object)) closure

# attribute accessor for closures
set-type-symbol! object (quote @)
    fn closure-at (obj name)
        let cl = (cast closure obj)
        let env = (closure-frame cl)
        let f = (closure-flow cl)
        let values... = (frame-values env f)
        loop-for i param in (enumerate (va-iter (flow-parameters f)))
            if
                (i > 0) and ((parameter-name param) ==? name)
                let value = (va-arg i values...)
                break value
            else
                continue
        else
            # has base class?
            loop-for i param in (enumerate (va-iter (flow-parameters f)))
                if
                    (i > 0) and ((parameter-name param) ==? (quote base))
                    let value = (va-arg i values...)
                    # search in base class
                    closure-at value name
                else
                    continue


# closure prints itself as object
set-type-symbol! object (quote repr)
    fn closure-repr (obj styler)
        let cl = (cast closure obj)
        let env = (closure-frame cl)
        let f = (closure-flow cl)
        let values... = (frame-values env f)
        loop-for i param in (enumerate (va-iter (flow-parameters f)))
            with
                s =
                    .. (string (flow-name f))
                        styler Style.Operator "{"
            if (i == 0)
                # skip continuation argument
                continue s
            else
                continue
                    .. s
                        ? (i == 1) "" " "
                        string (parameter-name param)
                        styler Style.Operator "="
                        repr (va-arg i values...) styler
        else
            s .. (styler Style.Operator "}")

# "type" check:
fn is? (obj cls)
    (closure-flow (cast closure obj)) ==? (closure-flow cls)

# `recur` is an auto-generated name bound to the function itself.
  Since functions are always captured as closures when passed as arguments,
  recur is bound to a frame in which its parameters are already bound.
  We also cast recur to object type so we get the new functions.
fn Point (x y)
    cast object recur

fn point-add (a b)
    Point
        a.x + b.x
        a.y + b.y

let p =
    point-add
        Point 23 42
        Point 10 20

assert
    is? p Point

print p



fn inner-func (x y)
    let z = (unconst (x + y))
    locals;

fn inner-func2 (x y)
    do
        let x y
        let z = (unconst (x + y))
        locals;

let scope = (inner-func 2 3)
assert (scope.x == 2)
assert (scope.y == 3)
assert (scope.z == 5)
let scope = (inner-func2 2 3)
assert (scope.x == 2)
assert (scope.y == 3)
assert (scope.z == 5)


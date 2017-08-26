
let S =
    CStruct "TheCoolStruct"
        x = f32
        y = f32
        z = f32

let s =
    S 1 2 3

assert
    and
        s.x == 1
        s.y == 2
        s.z == 3

fn begin-arg ()
let val =
    fn (f)
        f;

fn append-val (prevf x)
    fn (f)
        prevf
            fn ()
                return x (f)

let val = (append-val val 1)
let val = (append-val val 2)
let val = (append-val val 3)

print (val begin-arg)

struct AnotherStruct
    x : i32
    y : i32
    z : i32
    method 'apply-type (cls x y z)
        'structof cls
            x = x
            y = y
            z = z
    method 'sum (self)
        + self.x self.y self.z

let q =
    AnotherStruct 3 0 4

assert
    ('sum q) == 7

assert
    and
        q.x == 3
        q.y == 0
        q.z == 4


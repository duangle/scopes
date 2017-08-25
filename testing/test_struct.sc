
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


syntax-extend
    let t k = syntax-scope none
    let [loop] last-key = k
    let key value =
        Scope-next t (Any last-key)
    if (not (('typeof key) == Nothing))
        print (cast Symbol key) (repr value)
        loop key
    syntax-scope



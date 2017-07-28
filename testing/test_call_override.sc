
fn pointer-call-override (self ...)
    dump self ...
    rawcall self ...

#dump-label pointer-call-override
#dump-label
    typify pointer-call-override (pointer (function void string)) string

let oldsym ok = (type@ pointer 'call)
set-type-symbol! pointer 'call pointer-call-override
print "done"
if ok
    set-type-symbol! pointer 'call oldsym
else
    delete-type-symbol! pointer 'call

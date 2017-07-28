
fn pointer-call-override (self ...)
    dump self ...
    rawcall self ...

#dump-label pointer-call-override
#dump-label
    typify pointer-call-override (pointer (function void string)) string

set-type-symbol! pointer 'call pointer-call-override

print "done"

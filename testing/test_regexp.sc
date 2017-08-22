
assert (string-match? "^(t.st)?(t.st)?(t.st)?$" "tisttosttust")
assert (not (string-match? "^(t.st)?(t.st)?(t.st)?$" "tisttozt"))

let s = (unconst "^(gl(.+)|GL(.+))$")
let loop (i) = (unconst 0)
if (i < 20000)
    assert (string-match? s "GL_UNIFORM_BUFFER_EXT")
    assert (string-match? s "GL_CURRENT_MATRIX_INDEX_ARB")
    assert (string-match? s "GL_DOT4_ATI")
    loop (i + 1)

#do
    let lib =
        import-c "regexp.c" "
        typedef struct Reprog Reprog;
        typedef struct Resub Resub;

        Reprog *regcomp(const char *pattern, int cflags, const char **errorp);
        int regexec(Reprog *prog, const char *string, Resub *sub, int eflags);
        void regfree(Reprog *prog);

        enum {
            /* regcomp flags */
            REG_ICASE = 1,
            REG_NEWLINE = 2,

            /* regexec flags */
            REG_NOTBOL = 4,

            /* limits */
            REG_MAXSUB = 16
        };

        struct Resub {
            unsigned int nsub;
            struct {
                const char *sp;
                const char *ep;
            } sub[REG_MAXSUB];
        };" '()

    let regcomp regexec regfree Resub = lib.regcomp lib.regexec lib.regfree lib.Resub

    fn print-match (m)
        let resub = (alloca Resub)
        let result = 
            regexec m ("tisttesttust" as rawstring) resub 0
        if (result == 0)
            let count = (load (getelementptr resub 0 'nsub))
            let sub = (getelementptr resub 0 1 0)
            let loop (i) = (tie-const count 0:u32)
            if (i == count)
                return;
            let sp ep = 
                (load (getelementptr sub i 'sp))
                (load (getelementptr sub i 'ep))
            let word = (string-new sp ((ptrtoint ep usize) - (ptrtoint sp usize)))
            print i "=" word
            loop (i + 1:u32)
        else
            print "no match"
        #print (load (getelementptr x 0 0))

    let m =
        regcomp ("(t.st)?(t.st)?(t.st)?" as rawstring) 0 (inttoptr 0 (pointer (pointer i8) 'mutable))

    print-match m

    regfree m


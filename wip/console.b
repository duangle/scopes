# a REPL console for bangra

let void* =
    pointer void
let NULL =
    bitcast void*
        uint64 0

function null? (x)
    (bitcast uint64 x) == 0

function make-lib (cdefs)
    let lib = (tableof)
    for k v in cdefs
        set-key! lib k
            ? ((typeof v) < tuple)
                external (splice v)
                v
        repeat;
    lib

let lib =
    make-lib
        import-c "linenoise.h"
            tupleof
                \ "-I" (.. interpreter-dir "/clang/lib/clang/3.9.1/include")
            "
            #include <stddef.h>

            typedef struct linenoiseCompletions {
              size_t len;
              char **cvec;
            } linenoiseCompletions;

            typedef void(linenoiseCompletionCallback)(const char *, linenoiseCompletions *);
            typedef char*(linenoiseHintsCallback)(const char *, int *color, int *bold);
            typedef void(linenoiseFreeHintsCallback)(void *);
            void linenoiseSetCompletionCallback(linenoiseCompletionCallback *);
            void linenoiseSetHintsCallback(linenoiseHintsCallback *);
            void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *);
            void linenoiseAddCompletion(linenoiseCompletions *, const char *);

            char *linenoise(const char *prompt);
            void linenoiseFree(void *ptr);
            int linenoiseHistoryAdd(const char *line);
            int linenoiseHistorySetMaxLen(int len);
            int linenoiseHistorySave(const char *filename);
            int linenoiseHistoryLoad(const char *filename);
            void linenoiseClearScreen(void);
            void linenoiseSetMultiLine(int ml);
            void linenoisePrintKeyCodes(void);
            "

# make a copy of the globals table
let scope =
    ..
        globals;
        tableof;
loop
    with
        k = 1
    let cmd =
        lib.linenoise (rawstring "B> ")
    if (not (null? cmd))
        lib.linenoiseHistoryAdd cmd
        let cmdstr =
            (string cmd) .. "\n"
        repeat
            try
                let expr =
                    list-parse cmdstr
                let f =
                    eval expr scope
                let result = (f)
                if ((typeof result) != void)
                    let id =
                        symbol (.. "$" (string k))
                    print id "=" (repr result)
                    set-key! scope id result
                    k + 1
                else
                    k
            except (e)
                print e
                k



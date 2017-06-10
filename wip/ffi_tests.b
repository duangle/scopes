
#

    external sinf : float <- float
    external sin : double <- double

    print
        sinf 0.5
        sin (double 0.5)

dump
    import-c "linenoise.h" "
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
        \ "-I" (.. interpreter-dir "/clang/lib/clang/3.9.1/include")

bang

__function printf
    __functype i32 (__pointer-type i8) ...
__function sin
    __functype double double

__call printf
    __gep "value = %f!\n"
        __const-int i32 0
        __const-int i32 0
    __call sin
        __const-real double 0.5

__call printf
    __gep "Hello World!\n"
        __const-int i32 0
        __const-int i32 0

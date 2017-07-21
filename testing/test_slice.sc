
# all code below is typechecked and statically compiled of course

set-type-symbol! bool 'not
    fn (self)
        ^ self true

# attribute access
# prints the name of the function
print
    . bool not
    . true not

# a method call
  prints 'false'
print
    (. bool not) true

# same operation, but with sugar: applies a symbol to a value
print
    'not true

# by prepending/removing an apostrophe, we can now conveniently
  select between function and method
print
    not true


#
    fn next (x)
        slice x 1

    compile
        typify next list
        'dump-function




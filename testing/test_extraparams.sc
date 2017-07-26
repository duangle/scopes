
# forwarding extra params can in certain situations lead to a LLVM validation
  error when a call passes more arguments than the declaration permits.

  i have encountered the error numerous times but always quickly worked around
  it because i had better things to do, and now that i want to track the error
  i have forgotten how to reproduce it.

  so this file is for when the problem occurs again.

  the fix should be to truncate call arguments in the solver

print "TODO"



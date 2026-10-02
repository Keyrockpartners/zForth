#!/usr/bin/env bash
# Local variable frames: locals l@ l! endlocals.
# Each t runs its input on every binary in $BINS (see lib.sh).
. "$(dirname "$0")/../lib.sh"
T_SQUEEZE=1

# parameters become locals; extra locals start as pushed zeros
t ': sq 1 locals 0 l@ 0 l@ * endlocals ; 3 sq . -4 sq .' '9 16'
t ': f2 0 3 locals 0 l@ 1 l@ + 2 l! 2 l@ 0 l@ - endlocals ; 5 7 f2 .' '7'
t ': args 3 locals 0 l@ . 1 l@ . 2 l@ . endlocals ; 10 20 30 args' '10 20 30'
t ': nolocals 0 locals 42 endlocals ; nolocals .' '42'
t '1 2 3 : keep 1 locals endlocals ; keep . .' '2 1'
# recursion and nesting
t ': fact 1 locals 0 l@ 2 < if 1 else 0 l@ 1 - fact 0 l@ * fi endlocals ; 10 fact . 1 fact .' '3628800 1'
t ': fib 1 locals 0 l@ 2 < if 0 l@ else 0 l@ 1 - fib 0 l@ 2 - fib + fi endlocals ; 20 fib .' '6765'
t ': inner 1 locals 0 l@ 10 * endlocals ; : outer 1 locals 0 l@ inner 0 l@ + endlocals ; 4 outer .' '44'
# independence from >r and loops
t ': g 1 locals 99 >r 0 l@ r> + 0 l@ + endlocals ; 1 g .' '101'
t ': h 0 1 locals 5 0 do i 0 l@ + 0 l! loop 0 l@ endlocals ; h .' '10'
t ': ex 1 locals 0 l@ 0 < if 0 endlocals exit fi 0 l@ endlocals ; -3 ex . 7 ex .' '0 7'
t ': junk 1 locals 1 >r 2 >r 3 >r endlocals 5 ; 0 junk .' '5'
# two-cell values occupy two slots
t ': dadd 0 0 4 locals 0 l@ 1 l@ 0 l@ 1 l@ d+ endlocals ; 1700000000000. dadd d.' '3400000000000'
t ': fsum 2 locals 0 l@ 1 l@ f+ endlocals ; 1.5 2.25 fsum f.' '3.75'
# errors, and recovery after them
t '0 l@' 'stdin:1: outside memory'
t 'endlocals' 'stdin:1: rstack underrun'
t ': bad 2 locals 2 l@ endlocals ; 1 2 bad' 'stdin:1: outside memory'
t ': badn 1 locals -1 l@ endlocals ; 5 badn' 'stdin:1: outside memory'
t ': badw 1 locals 9 3 l! endlocals ; 5 badw' 'stdin:1: outside memory'
t '5 -1 locals' 'stdin:1: invalid size'
t '1 2 5 locals' 'stdin:1: dstack underrun'
t ': deep 1 locals 0 l@ deep endlocals ; 1 deep' 'stdin:1: rstack overrun'
t $': leak 1 locals 0 l@ 0 / endlocals ;\n5 leak\n0 l@\n1 2 + .' 'stdin:2: division by zero stdin:3: outside memory 3'
t $': leftopen 1 locals ;\n5 leftopen\n0 l@' 'stdin:3: outside memory'

summary frames

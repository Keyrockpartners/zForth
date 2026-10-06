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
# l& gives a local's address in the return-stack window: a frame can hold
# arrays and structures, read and written with the memory words
t ': a 5 1 locals 0 l& @c . 9 0 l& !c 0 l@ . endlocals ; a' '5 9'
t ': b 0 0 0 3 locals 0 l& 12 7 fill 0 l& @u8 . 2 l& 3 + @u8 . 1 l@ . endlocals ; b' '7 7 117901063'
t ': inner 0 1 locals 42 0 l& !c 0 l@ endlocals ; : outer 7 1 locals inner . 0 l@ endlocals ; outer .' '42 7'
t ': d 1 0 2 locals 0 l& 1 l& 4 move 1 l@ endlocals ; d .' '1'
t ': s 0 0 0 3 locals 0 l& 12 65 fill 0 l& 12 tell endlocals ; s' 'AAAAAAAAAAAA'
t ': e 0 1 locals 2 l& endlocals ; e' 'stdin:1: outside memory'
t '0x60000000 100000 + @c' 'stdin:1: outside memory'
# recursion keeps each call's frame memory apart
t ': r 0 2 locals 0 l@ 1 l! 0 l@ 0 > if 0 l@ 1 - r fi 1 l@ . endlocals ; 3 r' '0 1 2 3'
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
# returning with the frame open jumps to the local's value (5) as an
# address, odd, so not code: outside memory; what matters is that the
# next line starts with no frame
t $': leftopen 1 locals ;\n5 leftopen\n0 l@' 'stdin:2: outside memory stdin:3: outside memory'

summary frames

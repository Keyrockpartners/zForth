#!/usr/bin/env bash
# Double-precision floats.
# Each t runs its input on every binary in $BINS (see lib.sh).
. "$(dirname "$0")/../lib.sh"

# literals
t '1.5d0 df. 6.02d23 df. -2.25d-3 df. 1d6 df. .5d0 df. 1D2 df. -1.5d+2 df.' '1.5 6.02e+23 -0.00225 1000000 0.5 100 -150'
t '1.5d0 . .' '1073217536 0'
t '1d' 'stdin:1: not a word'
t '1.5d0d0' 'stdin:1: not a word'
t '1e3d0' 'stdin:1: not a word'
t 'd0' 'stdin:1: not a word'
t '.d0' 'stdin:1: not a word'
t '1d-' 'stdin:1: not a word'
t '0x1d0 .' '464'
# precision beyond single floats
t '0.1d0 0.2d0 df+ df. 1d0 3d0 df/ df. 16777217d0 df. 16777217 s>f f.' '0.3 0.333333333333333 16777217 1.67772e+07'
# arithmetic
t '1.5d0 2.25d0 df+ df. 1.5d0 2.25d0 df- df. 1.5d0 2d0 df* df. 7d0 2d0 df/ df.' '3.75 -0.75 3 3.5'
t '2d0 dfsqrt df. 1d0 0d0 df/ df. -1d0 0d0 df/ df.' '1.4142135623731 inf -inf'
# conversions
t '1.5d0 df>s . -1.5d0 df>s . 1d10 df>s . -1d10 df>s . 0d0 0d0 df/ df>s .' '1 -1 2147483647 -2147483648 0'
t '7 s>df 2d0 df/ df. 1700000000000. d>df df. -5. d>df df.' '3.5 1700000000000 -5'
t '1.7d12 df>d d. -2.9d0 df>d d. 1d30 df>d d. -1d30 df>d d. 0d0 0d0 df/ df>d d.' '1700000000000 -2 9223372036854775807 -9223372036854775808 0'
t '1.5 f>df df. 1.5d0 df>f f. 0.1 f>df df.' '1.5 1.5 0.100000001490116'
# rounding
t '1.5d0 dffloor df. -1.5d0 dffloor df. 1.2d0 dfceil df. 2.5d0 dfround df. -2.5d0 dfround df. -1.7d0 dftrunc df.' '1 -2 2 3 -3 -1'
# comparisons
t '1d0 2d0 df< . 2d0 1d0 df< . 1d0 1d0 df= . 0d0 -0d0 df= . 2d0 1d0 df> . 1d0 1d0 df<= . 1d0 2d0 df>= .' '-1 0 -1 -1 -1 -1 0'
t '0d0 0d0 df/ 2dup df< . 0d0 0d0 df/ 1d0 df<= .' '0 0'
t '-0.5d0 df0< . 0d0 df0= . -0d0 df0= . 0.5d0 df0< .' '-1 -1 -1 0'
t '1.5d0 dfnegate df. -2.5d0 dfabs df. 1d0 2d0 dfmin df. 1d0 2d0 dfmax df.' '-1.5 2.5 1 2'
# memory, constants, compiled literals, fmt
t '3.25d0 dfconstant dk dk df. dfvariable dv dv df@ df. 2.5d0 dv df! dv df@ df.' '3.25 0 2.5'
t 'dfvariable q 1d0 q df! q @c . q cell + @c .' '0 1072693248'
t ': g 1.5d0 2.5d0 df+ ; g df. g df.' '4 4'
t '1.5d0 7 fmt" %lg %d"' '1.5 7'

summary dfloat

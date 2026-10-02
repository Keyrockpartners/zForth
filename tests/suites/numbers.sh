#!/usr/bin/env bash
# Integers, unsigned words, single floats, number literals, fmt.
# Each t runs its input on every binary in $BINS (see lib.sh).
. "$(dirname "$0")/../lib.sh"

# signed integers, wraparound, division edge cases
t '7 2 / . -7 2 / . 7 -2 / . -7 2 % . 7 3 % .' '3 -3 -3 -1 1'
t '2147483647 1 + . -2147483648 1 - . 65536 65536 * .' '-2147483648 2147483647 0'
t '-2147483648 -1 / . -2147483648 -1 % .' '-2147483648 0'
t '5 0 /' 'stdin:1: division by zero'
t '5 0 %' 'stdin:1: division by zero'
t '1 2 < . 2 1 < . -5 3 < . -1 0 < . 3 3 <= . 4 3 >= .' '-1 0 -1 -1 -1 -1'
t '3 7 min . 3 7 max . -3 7 min .' '3 7 -3'
# comparisons across the extremes, where "- <0" used to overflow
t '-2147483648 1 < . 2147483647 -1 < . -2147483648 2147483647 < . 2147483647 -2147483648 < .' '-1 0 -1 0'
t '1 -2147483648 > . -1 2147483647 > . 2147483647 -2147483648 > . -2147483648 2147483647 > .' '-1 0 -1 0'
t '-2147483648 2147483647 <= . 2147483647 -2147483648 <= . 2147483647 -2147483648 >= . -2147483648 -2147483648 >= .' '-1 0 -1 -1'
t '-2147483648 2147483647 min . 2147483647 -2147483648 min . -2147483648 2147483647 max . 2147483647 -2147483648 max .' '-2147483648 -2147483648 2147483647 2147483647'
# shifts
t '1 4 << . -8 1 >> . -8 1 rshift . 1 31 << .' '16 -4 2147483644 -2147483648'
t '1 32 << . 1 -1 << . -1 40 >> . 5 40 >> . -1 32 rshift .' '0 0 -1 0 0'
# unsigned
t '-1 u. 0 u. 2147483648 u.' '4294967295 0 2147483648'
t '-1 1 u< . 1 -1 u< . 1 2 u< . -1 1 u> . 3 3 u<= . 3 3 u>= .' '0 -1 -1 -1 -1 -1'
t '-1 2 u/ . -1 10 umod . 7 2 u/ .' '2147483647 5 3'
t '5 0 u/' 'stdin:1: division by zero'
t '-1 1 umin . -1 1 umax u.' '1 4294967295'
# literals
t '0xFFFFFFFF . 4294967295 . 0x7fffffff . -0x10 . 0x1E . +5 .' '-1 -1 2147483647 -16 30 5'
t '-2147483648 . 010 .' '-2147483648 10'
t '4294967296' 'stdin:1: not a word'
t '-2147483649' 'stdin:1: not a word'
t '0x100000000' 'stdin:1: not a word'
t '1.2.3' 'stdin:1: not a word'
t '1e' 'stdin:1: not a word'
t '0x' 'stdin:1: not a word'
# floats
t '1.5 f. -2.0 f. 1e3 f. .5 f. 5.0 f. 2.5e-3 f.' '1.5 -2 1000 0.5 5 0.0025'
t '1.5 2.25 f+ f. 1.5 2.25 f- f. 1.5 2.0 f* f. 7.0 2.0 f/ f.' '3.75 -0.75 3 3.5'
t '2.0 fsqrt f. 1.0 0.0 f/ f. -1.0 0.0 f/ f.' '1.41421 inf -inf'
t '1.5 f>s . -1.5 f>s . 1e10 f>s . -1e10 f>s . 0.0 0.0 f/ f>s .' '1 -1 2147483647 -2147483648 0'
t '7 s>f 2.0 f/ f. -1 u>f f. -1 s>f f.' '3.5 4.29497e+09 -1'
t '1.5 ffloor f. -1.5 ffloor f. 1.2 fceil f. 2.5 fround f. -2.5 fround f. -1.7 ftrunc f. -1.5 floor f.' '1 -2 2 3 -3 -1 -2'
t '1.0 2.0 f< . 2.0 1.0 f< . 1.0 1.0 f= . 0.0 -0.0 f= . 2.0 1.0 f> . 1.0 1.0 f<= . 1.0 2.0 f>= .' '-1 0 -1 -1 -1 -1 0'
t '0.0 0.0 f/ dup f< . 0.0 0.0 f/ 1.0 f<= .' '0 0'
t '-0.5 f0< . 0.0 f0= . -0.0 f0= . 0.5 f0< .' '-1 -1 -1 0'
t '1.5 fnegate f. -2.5 fabs f. 1.0 2.0 fmin f. 1.0 2.0 fmax f.' '-1.5 2.5 1 2'
t '1.5 fdup f+ f. 1.0 2.0 fswap f- f. 3.5 fconstant pi pi f. fvariable fv 2.5 fv f! fv f@ f.' '3 1 3.5 2.5'
t '1.5 variable v v @ f. 0.25 v ! v @ f.' '1.5 0.25'
t '1.5 -1 7 fmt" %g %u %d"' '1.5 4294967295 7'
# fmt: C printf conventions
t '3.14159 dup dup dup fmt" %.2f|%f|%e|%g"' '3.14|3.141590|3.141590e+00|3.14159'
t '255 255 255 255 fmt" %x %X %08x %#x"' 'ff FF 000000ff 0xff'
t '-5 -5 5 -5 -1 fmt" [%5d][%-5d][%+d][%05d][%u]"' '[   -5][-5   ][+5][-0005][4294967295]'
t 's" abc" 2dup 2dup 2dup fmt" [%s][%5s][%-5s][%.2s]"' '[abc][  abc][abc  ][ab]'
t '65 fmt" %c%%" 1 2 fmt" %d%d"' 'A%12'
t '1700000000000. -5. 0xFFFFFFFFFFFFFFFF. fmt" %lu %ld %lx"' '1700000000000 -5 ffffffffffffffff'
t '1.5d0 0.1d0 fmt" %.3lf %.17lg"' '1.500 0.10000000000000001'
t '5 fmt" %q|%n|%lc|%" .' '%q|%n|%lc|%5'
t '1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 fmt" %d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d"' 'stdin:1: unknown error'
t '1.5 .' '1069547520'
# sin stays a Linux helper, now on floats
t '0.0 sin f. 1.5707963 sin f.' '0 1'

summary numbers

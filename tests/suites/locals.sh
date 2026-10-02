#!/usr/bin/env bash
# Named locals {: :}, to, and 2l@ 2l!.
# Each t runs its input on every binary in $BINS (see lib.sh).
. "$(dirname "$0")/../lib.sh"
T_SQUEEZE=1

# declarations
t ': hyp {: a b | c :} a a * b b * + to c c ; 3 4 hyp . 5 12 hyp .' '25 169'
t ': o {: a b c :} a . b . c . ; 1 2 3 o' '1 2 3'
t ': z {: | x y :} x . y . ; z' '0 0'
t ': f {: a b -- sum :} a b + ; 2 3 f .' '5'
t ': cm {: a -- b c d :} a ; 9 cm .' '9'
t ': none {: :} 42 ; none .' '42'
t '1 2 : keep {: a :} ; 3 keep . .' '2 1'
# to, exit, recursion, loops, nesting, shadowing
t ': t1 {: a :} 10 to a a ; 1 t1 .' '10'
t ': e {: n :} n 0 < if 0 exit fi n ; -5 e . 7 e .' '0 7'
t ': e {: n :} n 0 < if 0 exit fi n ; : many 100 0 do -1 e drop loop ; many 1 2 + .' '3'
t ': fact {: n :} n 2 < if 1 else n 1 - fact n * fi ; 10 fact .' '3628800'
t ': sumto {: n | s :} n 0 do i s + to s loop s ; 5 sumto .' '10'
t ': in2 {: x :} x 10 * ; : out2 {: x :} x in2 x + ; 4 out2 .' '44'
t ': s {: dup :} dup dup + ; 4 s . 5 dup + .' '8 10'
t ': a1 {: q :} q ; q' 'stdin:1: not a word'
t ': br {: a :} [ 1 2 + ] literal a + ; 4 br .' '7'
t ': fl {: x y :} x y f+ ; 1.5 2.5 fl f.' '4'
# two-cell locals
t ': dsum {: d: x d: y :} x y d+ ; 1700000000000. 5. dsum d.' '1700000000005'
t ': dt {: d: x :} 7. to x x ; 1. dt d.' '7'
t ': dfm {: d: a d: b :} a b df* ; 1.5d0 2d0 dfm df.' '3'
t ': mix {: a d: b c | d: w :} b to w a . w d. c . ; 1 2. 3 mix' '1 2 3'
t ': q2 0 0 2 locals 5. 0 2l! 0 2l@ endlocals ; q2 d.' '5'
t ': q3 3 locals 0 2l@ d. 2 l@ . endlocals ; 1700000000000. 9 q3' '1700000000000 9'
t ': q4 1 locals 0 2l@ endlocals ; 5 q4' 'stdin:1: outside memory'
t ': q5 1 locals 1. 0 2l! endlocals ; 5 q5' 'stdin:1: outside memory'
# errors and recovery
t '{: a :}' 'stdin:1: compile-only word'
t '5 to x' 'stdin:1: compile-only word'
t ': bad1 {: a :} {: b :} ;' 'stdin:1: bad locals declaration'
t ': bad2 {: a ;' 'stdin:1: bad locals declaration'
t ': bad3 {: | x | y :} ;' 'stdin:1: bad locals declaration'
t ': bad4 {: a d: :} ;' 'stdin:1: bad locals declaration'
t ': bad5 {: d: | x :} ;' 'stdin:1: bad locals declaration'
t ': bad6 5 to nothere ;' 'stdin:1: not a word'
t ': bad7 {: a :} 5 to nothere ;' 'stdin:1: not a word'
t ': bad9 {: n00000000000000000001 n00000000000000000002 n00000000000000000003 n00000000000000000004 n00000000000000000005 n00000000000000000006 :} ;' 'stdin:1: bad locals declaration'
t $': bad2 {: a ;\n: ok {: a :} a ; 3 ok .' 'stdin:1: bad locals declaration 3'
t $': bad7 {: a :} 5 to nothere ;\n: ok2 a ;' 'stdin:1: not a word stdin:2: not a word'

summary locals

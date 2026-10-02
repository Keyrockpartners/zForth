#!/usr/bin/env bash
# The ZGo runtime words in forth/zgo.zf, on the RAM build loading bs.zf and
# zgo.zf, and on a ROM image with both baked in (run.sh builds it).
. "$(dirname "$0")/../lib.sh"
T_SQUEEZE=1
BINS=${ZF_ZGO_BINS:-"$BUILD/asan/zforth forth/bs.zf forth/zgo.zf|$BUILD/asan/zgo/zforth-rom"}

# labels: forward and backward jumps, conditional jumps, reuse
t ': a znew 0 znew 1 0 zlabel 1 dup 10 < zgoto0 0 dup . 1+ zgoto 1 zlabel 0 drop ; a' '0 1 2 3 4 5 6 7 8 9'
t ': b znew 0 znew 1 0 begin dup 10 < zgoto0 0 dup 3 = if 1+ zgoto 1 fi dup 7 = if zgoto 0 fi dup . zlabel 1 1+ again zlabel 0 drop ; b' '0 1 2 5 6'
t ': c znew 0 znew 1 1 zgoto0 0 5 . zgoto 1 zlabel 0 6 . zlabel 1 ; c' '5'
t ': d znew 3 1 if zgoto 3 fi 2 . zlabel 3 9 . znew 3 0 zgoto0 3 7 . zlabel 3 ; d' '9'
t ': e znew 4 zgoto 4 zgoto 4 zgoto 4 1 . zlabel 4 2 . ; e' '2'
t ': f znew 31 zgoto 31 zlabel 31 3 . ; f' '3'
t ': g znew 32 ;' 'stdin:1: index out of range'
# z" with escapes and UTF-8
t ': s z" h\xc3\xa9\t|\n\"q\\" tell ; s' $'hé\t| "q\\'
t ': s2 z" \xC3\xA9x\r" swap drop . ; s2 : s3 z" " swap drop . ; s3' '4 0'
t 'z" top level" tell z" ünï" swap drop .' 'top level5'
t ': s4 z" a b  c" swap drop . ; s4' '6'
# zbool, zslice, zcopy, zcnt
t ': bb 1 zbool tell 0 zbool tell ; bb' 'truefalse'
t 'z" hello" 1 4 1 zslice tell z" hello" 0 5 1 zslice tell z" hello" 5 5 1 zslice swap drop .' 'ellhello0'
t 'z" hello" 2 6 1 zslice' 'stdin:1: index out of range'
t 'z" hello" 3 2 1 zslice' 'stdin:1: index out of range'
t 'z" hello" -1 2 1 zslice' 'stdin:1: index out of range'
t '1000 100 2 4 4 zslice . .' '2 1008'
t '8 buffer: b b 8 46 fill b 8 z" abc" 1 zcopy . b 4 tell' '3 abc.'
t '8 buffer: b b 8 46 fill b 2 z" abc" 1 zcopy . b 4 tell' '2 ab..'
t '5 0 zcnt . 5 1 zcnt .' '5 -1'

summary zgort

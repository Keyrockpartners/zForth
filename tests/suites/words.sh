#!/usr/bin/env bash
# General words added for everyday use (ext.zf), the move, fill,
# frame and ?bounds primitives, and byte-exact strings.
# Each t runs its input on every binary in $BINS (see lib.sh).
. "$(dirname "$0")/../lib.sh"
T_SQUEEZE=1

# standard names
t '1 2 nip . 1 2 tuck . . . 1 2 3 -rot . . .' '2 2 1 2 2 1 3'
t '5 negate . -5 abs . 5 abs . 0 invert . 6 3 and . 6 3 or . 6 3 xor . 7 3 mod . 1 4 lshift .' '-5 5 5 -1 2 7 5 1 16'
t '0 0= . 1 0= . -1 0< . 1 0< . 0 0<> . 3 0<> . 3 0> . 1 2 <> . 2 2 <> .' '-1 0 -1 0 0 -1 -1 -1 0'
t ': t 7 >r r@ r@ + rdrop ; t .' '14'
t 'here 65 over c! c@ . s" ab" type' '65 ab'
t ': tt 1 if 7 . then 0 if 8 . else 9 . then ; tt' '7 9'
# begin while repeat
t ': w 0 begin dup 5 < while dup . 1+ repeat drop ; w' '0 1 2 3 4'
t ': w0 0 begin dup 0 < while 1+ repeat . ; w0' '0'
t ': nest 0 begin dup 2 < while 0 begin dup 2 < while over . dup . 1+ repeat drop 1+ repeat drop ; nest' '0 0 0 1 1 0 1 1'
# move and fill, including overlap and length 0
t 'here 10 65 fill here 10 tell' 'AAAAAAAAAA'
t 'here 6 65 fill 66 here 1+ c! here here 2 + 4 move here 6 tell' 'ABABAA'
t 'here 6 65 fill 66 here 4 + c! here 2 + here 4 move here 6 tell' 'AABABA'
t '8 buffer: b b 3 65 fill b b 1+ 0 move b 3 tell s" xyz" b swap move b 3 tell' 'AAAxyz'
t '8 buffer: b 8 buffer: c s" abcdef" b swap move c 8 46 fill b c 6 cmove c 8 tell' 'abcdef..'
t '1 2 -1 move' 'stdin:1: invalid size'
# strings
t 's" abc" s" abc" str= . s" abc" s" abd" str= . s" ab" s" abc" str= . s" " s" " str= .' '-1 0 0 -1'
t 's" abc" s" abd" compare . s" abd" s" abc" compare . s" ab" s" abc" compare . s" abc" s" abc" compare . s" ab" s" a" compare .' '-1 1 -1 0 1'
t 's" a" s" é" compare . s" é" s" a" compare .' '-1 1'
t 's" abc" s" ABC" compare . s" " s" a" compare . s" " s" " compare .' '1 -1 0'
t 's" ab" -1 s" ab" str=' 'stdin:1: invalid size'
# slice and copy: arrays as addr len of elements of size bytes
t 's" hello" 1 4 1 slice tell s" hello" 0 5 1 slice tell s" hello" 5 5 1 slice . drop' 'ellhello0'
t '100 6 2 5 4 slice . .' '3 108'
t 's" abc" 0 4 1 slice' 'stdin:1: index out of range'
t 's" abc" 2 1 1 slice' 'stdin:1: index out of range'
t 's" abc" -1 2 1 slice' 'stdin:1: index out of range'
t '8 buffer: b b 8 46 fill b 8 s" xyz" 1 copy . b 8 tell' '3 xyz.....'
t '8 buffer: b b 8 46 fill b 2 s" xyz" 1 copy . b 4 tell' '2 xy..'
t '8 buffer: b b 8 46 fill b 2 s" abcdef" 3 copy . b 8 tell' '2 abcdef..'
t '1 2 3 -1 1 copy' 'stdin:1: invalid size'
# compiled code: 16-bit units, lit16 and lit at the boundaries, calls
# beyond a unit's reach through call, compile, and jump operands
t ': l 32767 . -32768 . 32768 . -32769 . 0 . -1 . 0x7FFFFFFF . ; l' '32767 -32768 32768 -32769 0 -1 2147483647'
t ': near 7 . ; 140000 allot : far 8 . ; : both near far ; both' '7 8'
t $': nn 5 . ; : mk \' nn compile, ; immediate : u mk mk ; u' '5 5'
t ': odd s" abc" tell s" de" tell 1 . ; odd : ev s" " swap drop . 2 . ; ev' 'abcde1 0 2'
t 'here 0 ,j here 40 + over !j @j here 40 + = .' '-1'
t 'here 0 ,j @j .' '0'
t 'here 0 ,j here 100000 + swap !j' 'stdin:1: outside memory'
t 'here 0 ,j here 1+ swap !j' 'stdin:1: outside memory'
t 'prim-count 100 > . prim-count 1024 < .' '-1 -1'
t ': z 1 if 2 else 3 fi . ; z : zz 0 begin 1+ dup 3 = until . ; zz' '2 3'
# a lit16 and l@ l! 2l@ 2l! + @@ !! after it compile as one primitive,
# never across a jump target (begin, if, here)
t ': p 3 + 70000 + -2 + ; 1 p . var v : s 1234 v 1 !! v 1 @@ . -5 v 0 !! v 0 @@ . ; s' '70002 1234 -5'
t ': ll 2 locals 0 l@ . 1 l@ . 9 1 l! 1 l@ . endlocals ; 4 6 ll' '4 6 9'
t ': dl 2 locals 0 2l@ d. 5. 0 2l! 0 2l@ d. endlocals ; 7. dl' '7 5'
t ': h 1 locals 0 begin l@ dup . 9 > if endlocals exit fi 10 0 l! 0 again ; 5 h' '5 10'
t ': h2 1 locals 0 [ here drop ] l@ . endlocals ; 3 h2' '3'
# the stack pointers are kept in registers while words run: dsp and rsp
# read and written from a word see them
t ': dd 7 8 9 dsp @ . ; dd . . .' '3 9 8 7'
t ': dz 1 2 3 dropall 5 . ; dz dsp @ .' '5 0'
t ': rs rsp @ ; : rs2 rs rs - . ; rs2' '0'
# index: the address of an element, checked; a size before it fuses
t '1000 5 0 4 index . 1000 5 4 4 index . 1000 5 2 1 index .' '1000 1016 1002'
t '1000 5 5 4 index' 'stdin:1: index out of range'
t '1000 5 -1 4 index' 'stdin:1: index out of range'
t ': ix 3 index ; 100 4 3 ix . 100 4 4 ix' 'stdin:1: index out of range 109'
t ': s3 s" abcde" 3 1 index c@ emit ; s3' 'd'
# common words as primitives
t '1 2 over . . . 1 2 nip . 3 4 2dup . . . . 5 6 7 2drop .' '1 2 1 2 4 3 4 3 5'
t '1 1+ . -1 1+ . 0x7FFFFFFF 1+ . 1 2 != . 2 2 != . 2 1 > . 1 2 > . -1 1 > .' '2 0 -2147483648 -1 0 -1 0 0'
t '3 5 min . 3 5 max . -3 5 min . -3 -5 max .' '3 5 -3 -3'
t 'var v 1234567 v ! v @ . 7 v ! v @ . h @ here = .' '1234567 7 -1'
t '1 2 3 2drop 2drop' 'stdin:1: dstack underrun'
# UTF-8 survives s", ." and key; long strings compile correctly
t 's" hé!" tell s" hé!" swap drop .' 'hé!4'
t ': u s" hé!" tell ; u : v ." ünï€" ; v' 'hé!ünï€'
t ': long s" aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab" ; long swap drop . long + 1- c@ .' '138 98'
# words and comments with UTF-8 names
t ': grüße 42 ; grüße . ( ünïcödé comment ) 1 .' '42 1'

# 64-bit bitwise and shifts
t '0xFF00FF00FF00FF00. 0x0FF00FF00FF00FF0. d& ud. 0xF0. 0x0F. d| ud. 3. 5. d^ ud. 0. dinvert d.' '1080880403494997760 255 6 -1'
t '1. 4 dlshift ud. 1. 40 dlshift ud. 1. 63 dlshift ud. 1. 64 dlshift ud. 1. -1 dlshift ud. 5. 0 dlshift ud.' '16 1099511627776 9223372036854775808 0 0 5'
t '0x8000000000000000. 40 drshift ud. 0x8000000000000000. 4 drshift ud. -1. 64 drshift ud. -1. 32 drshift ud.' '8388608 576460752303423488 0 4294967295'
t '-1. 4 darshift d. -256. 40 darshift d. -1. 70 darshift d. 256. 4 darshift d. -4294967296. 32 darshift d. 0x7FFFFFFFFFFFFFFF. 62 darshift d.' '-1 -1 -1 16 -1 1'
# unsigned and 64-bit conversions
t '3000000000.0 f>u u. 3e9 f>u u. 7.9 f>u u. 3000000000 u>df df. 4000000000. ud>df df.' '3000000000 3000000000 7 3000000000 4000000000'
t '18446744073709551615. ud>df df. 9223372036854775809. ud>df df. 1.8446744073709550d19 df>ud ud. 1d19 df>ud ud.' '1.84467440737096e+19 9.22337203685478e+18 18446744073709549568 10000000000000000000'
t '-3.5 f>d d. -7. d>f f. 1.5e10 f>ud ud. 3d9 df>u u.' '-3 -7 15000000512 3000000000'
# 64-bit integers to single floats round once, not through a double
t '1152921573326323713. d>f 1152921573326323712. d>f = . 9223372586610589697. ud>f 9223372586610589696. ud>f = .' '0 0'
t '-7. d>f f. 18446744073709551615. ud>f 1.8446744e19 f= .' '-7 -1'

# frame: arguments then zeroed slots
t ': x 2 4 frame 0 l@ . 1 l@ . 2 l@ . 3 l@ . endlocals ; 7 8 x 9 .' '7 8 0 0 9'
t ': y 0 3 frame 2 l@ . 1 2 l! 2 l@ . endlocals ; y y' '0 1 0 1'
t ': r dup 0 > if 1 2 frame 0 l@ 1- r 0 l@ . endlocals else drop fi ; 3 r' '1 2 3'
t '1 3 2 frame' 'stdin:1: invalid size'
# ?bounds
t '0 1 ?bounds . 9 10 ?bounds . 0 0x7FFFFFFF ?bounds .' '0 9 0'
t '10 10 ?bounds' 'stdin:1: index out of range'
t '-1 10 ?bounds' 'stdin:1: index out of range'
t '0 0 ?bounds' 'stdin:1: index out of range'
t ': ix 4 ?bounds ; 3 ix . 4 ix 1 2 + .' 'stdin:1: index out of range 3'

# abort
t ': ab 1 . abort 2 . ; ab 3 .' 'stdin:1: aborted 1'
t $': ab2 abort ;\nab2\n4 .' 'stdin:2: aborted 4'

summary words

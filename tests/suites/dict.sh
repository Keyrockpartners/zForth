#!/usr/bin/env bash
# Dictionary basics: bs.zf self-tests, variables, the store guard, and the
# marker / empty checkpoints. Each t runs on every binary in $BINS.
. "$(dirname "$0")/../lib.sh"
T_SQUEEZE=1

# loading bs.zf prints nothing when memaccess.zf's self-tests pass
t '' ''
t '1 2 + . 9 variable q q @ . 2 q ! q @ .' '3 9 2'

# variable reserves room for any value; var stays correct past 16 KB
t '0 variable x 1 variable y 1000000 x ! x @ . y @ .' '1000000 1'
t '20000 allot var z 5 z ! z @ .' '5'
t '8 buffer: b2 1 b2 7 + !u8 b2 7 + @u8 .' '1'

# the dictionary only grows for stores at or below here
t '5 100000000 !' 'stdin:1: outside memory'
t '5 here 100 + ! here 100 + @ .' '5'
t '100000 allot 7 here 9 - ! here 9 - @ .' '7'

# marker: removes itself and later words, keeps the data stack, frees its memory
t $'1 2 marker m : hi 5 . ; hi 3 m . . .\nhi\nm' 'stdin:2: not a word stdin:3: not a word 5 3 2 1'
t 'here marker m m here = . here marker m m marker m m here = .' '-1 -1'
t $'marker m : r m 99 . ; r\n1 2 + .\nr' 'stdin:3: not a word 3'
t '9 variable keep marker m 4 keep ! m keep @ .' '4'

# empty: the checkpoint at the end of bs.zf; clears the stacks, is reusable
t '1 2 empty .' 'stdin:1: dstack underrun'
t $': half 1 2 empty\n: hi 7 . ; hi\nhalf' 'stdin:3: not a word 7'
t $': hi 7 . ;\nempty\nhi\n: hi 8 . ; hi empty empty 1 2 + .' 'stdin:3: not a word 8 3'

summary dict

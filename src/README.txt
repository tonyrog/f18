
Some cool tricks:

- computed jump when  address on stack:

push ;

- same as above but with ex: co-routine jump

push ex

- 
call:xxxx ;  => jump:xxxx

Direction port ioreg numbers

        8 7654 3210
_D_U    1 0000 0101  0
_D__    1 0001 0101  1
_DLU    1 0010 0101  2
_DL_    1 0011 0101  3
___U    1 0100 0101  4

__LU    1 0110 0101  6
__L_    1 0111 0101  7
RD_U    1 1000 0101  8
RD__    1 1001 0101  9
RDLU    1 1010 0101  A
RDL_    1 1011 0101  B
R__U    1 1100 0101  C
R___    1 1101 0101  D
R_LU    1 1110 0101  E
R_L_    1 1111 0101  F

: zero ( -- 0 ) dup dup or ;

: nowhere 155 ;
: right ( x -- x )  80 + ;
: down  ( x -- x ) -40 + ;
: left  ( x -- x )  20 +  ;
: up    ( x -- x ) -10 + ;

route codes:
with 2 bits we can get upto 9 hops of data transfers in a word

00 = R
01 = D
10 = L
11 = U

This code segment is normall y first in a stream:

\ ------- BOOT LOADER ------
@p push . .
<number-of-instructions>
@p !+ unext .
\ --------------------------

Or it could be a router like this one:

\ ------- BOOT/ROUTER ------
@p push @p .
<number-of-instructions>
<start-address>
a! . . .
@p !+ unext .
\ --------------------------


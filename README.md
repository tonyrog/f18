# F18

A simulator of the Green Array F18A chip.

The simulator is a bit different than the chip since it
interprets the input from the io registers rather than 
just expect a binary stream of 18 bit input.

This makes it super simple to write programs for the 
F18A simulator since you can feed it straight in.

## Build

    cd src
    make

## Run 

Have a look in the test directory and run the examples by

    cd test
    ../bin/f18 < add.f18
    
or
    cd test
    ../bin/f18 < mult.f18

    ...

## Options 

    -v     verbose   (if debug compiled)
    -t     trace     (if debug comipled)
    -d     delay     Set delay between instructions
    -l     VxH       processor layout (max 8x18) default is 1x1!!!

Specially the last flag is worth mentioning, -l 3x4 starts 12 thread
with about 1 page of node data and 4 pages of stack each. 
On a mac 8x18 open a bit too many socket pairs, simulating the io 
registers used for communication between nodes.
That is memory consumption is 2.8M when running all 8x18 (144) nodes.

## Remarks

The processor is interesting in a number of ways, but the way
you can just bootstrap the thing from basically nothing is 
spectacular.
Worth mentioning is also the selective receive, by using various
io registers.


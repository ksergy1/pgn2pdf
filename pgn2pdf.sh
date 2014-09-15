#!/bin/bash

if [ $# -le 1 ]; then
    echo "usage:    $0 <input.pgn> [start-num end-num]"
    echo "usage: or $0 <input.pgn> [end-num] // start_num = 0"
    echo "usage: or $0 <input.pgn> // start_num = 0, end_num = INT_MAX"
    echo "start-num and end-num - 0-based"
    exit 0;
fi;

if [ $# -eq 2 ]; then
    startNum=0
    endNum=$2
fi;

if [ $# -eq 3 ]; then
    startNum=$2
    endNum=$3
fi;

OUT_DIR=`basename $1 .pgn`

mkdir $OUT_DIR

echo "Convert games in pgn to directory of games"
./pgn2dir.bin $1 $OUT_DIR $2

for i in `ls $OUT_DIR`; do
    TEX_FILE=`basename $i .pgn`.tex
    echo -e "\n\n\n================ Convert $i to $TEX_FILE ===========\n\n\n"
    dos2unix $OUT_DIR/$i
    ./pgn2pdf.bin $OUT_DIR/$i result/$TEX_FILE
    echo -e "\n\n\n================ Convert $TEX_FILE pdf ===========\n\n\n"
    cd result
    pdflatex $TEX_FILE
    cd ..
done;


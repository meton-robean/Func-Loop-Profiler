1，编译程序,生成可执行img

gcc test.c -o test

2，生成symbol文件:symbol.txt

readelf -s test | grep FUNC > symbol.txt

3,生成edge 文件:edgcnt.out

pin -t edgcnt.so -- test

4,生成callgraph log文件

python callgraph.py -t 2 symbol.txt edgcnt.out

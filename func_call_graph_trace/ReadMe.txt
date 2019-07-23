 1.编译pvtrave:
  在根目录运行如下：
 $ make
 $ make install
 
 2.测试C语言的call graph功能，进入test目录
 $ $ gcc -g -finstrument-functions test.c instrument.c -o test
 $ ./test
 $ ../pvtrace test
 $ dot -Tjpg graph.dot -o graph.jpg
 
 
 note:
 该版本支持C语言函数，C++函数的话函数名没有demangle

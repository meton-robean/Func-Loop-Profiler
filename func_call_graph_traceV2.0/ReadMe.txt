 1.编译pvtrave:
  在根目录运行如下：
 $ make
 $ make install
 
 2.测试C语言的call graph功能，进入test目录
 $ $ gcc -g -finstrument-functions test.c instrument.c -o test
 $ ./test
 $ ../pvtrace test
 $ dot -Tjpg graph.dot -o graph.jpg
 
 3.测试C++例子的call graph 功能，进入cpptest目录
 $ g++ -g -finstrument-functions instrument.cpp test.cpp  -o test
 $ ./test
 $ ../pvtrace test
 $ dot -Tjpg graph.dot -o graph.jpg
 
 ------------------------------
 note:
v1.0 该版本支持C语言函数，C++函数的话函数名没有demangle

v2.0 支持C++例子,详见cpptest/

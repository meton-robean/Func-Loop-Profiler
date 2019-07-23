# Func-Loop-Profiler
基于PIn动态插桩框架的函数-循环嵌套关系分析工具： function-loop call graph profiler using Pin

/* 
 * 2018-03-10
 * description: a pin tool to detection loops of program using dynamic BBLs tracing technique
 * v3.4 version :   a. ignore system call
                      b. in func_call_graph_traceV2.0/ dir, we use gcc compiler to get function call graph.
                      c. solve the bug that function call graph have some problem when encouter function ptr
                      d. add more detailed annotations

 problem: 1.the last iteration 's instruction can not recognized and count
            2. dynamic instrument have high overhead

*/ 

/*test file compile:   eg.   gcc -g test_file.c -o test 
 *compile:             eg.   make PIN_ROOT=~/application/pin-3.5/ OBJDIR=./obj/ ./obj/LoopProfv3.4.so
 *run pintool:        eg.   pin -t ./obj/LoopProfv3.4.so -o log.out -- ./test
 *loop call graph visualization: eg,   ./callgraph_v2.py addr2line test log.out callgraph.png
*/

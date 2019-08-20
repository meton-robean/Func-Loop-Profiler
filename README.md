# Func-Loop-Profiler
基于PIn动态插桩框架的函数-循环嵌套关系分析工具： function-loop call graph profiler using Pin

#### Version 3.4     
 a. ignore system call  
 b. in func_call_graph_traceV2.0/ dir, we use gcc compiler to get function call graph.   
 c. solve the bug that function call graph have some problem when encouter function ptr   
 d. add more detailed annotations  



#### Usage:  
 test file compile:   eg.   gcc -g test_file.c -o test   
 compile:             eg.   make PIN_ROOT=~/application/pin-3.5/ OBJDIR=./obj/ ./obj/LoopProfv3.4.so  
 run pintool:         eg.   pin -t ./obj/LoopProfv3.4.so -o log.out -- ./test  (测试程序的名字要是test)
 loop call graph visualization: eg.   ./callgraph_v2.py addr2line test log.out callgraph.png  


#### Test:  
using gzip binary file to zip the zip_example.txt:   
first, rename gzip binary file as 'test'   
'''
pin -t ./obj/LoopProfv3.4.so -o log.out -- ./test zip_example.zip  
'''  
#### Result:   
![loop call graph](https://github.com/meton-robean/Func-Loop-Profiler/blob/master/callgraph1.png)  

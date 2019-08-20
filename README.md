# Func-Loop-Profiler
基于PIn动态插桩框架的函数-循环嵌套关系分析工具： function-loop call graph profiler using Pin  
Unofficial implementation of paper: [Identifying Potential Parallelism via Loop-centric Profiling](https://dl.acm.org/citation.cfm?id=1242554)
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
then run: 
```bash
 pin -t ./obj/LoopProfv3.4.so -o log.out -- ./test zip_example.zip 
```

#### Result:   
##### Loop Flat Profile:   
```bash
*********Loop Flat Profile*********
[1]--------loop at: gzip.c:977(401d99)----------
-> loop_head: 401d99
-> entries: 1
-> trip_count: 15
-> iterations: 15
-> self_ins: 15 (0.0202271%)
-> total_ins: 15 (0.0202271%)
[2]--------loop at: gzip.c:984(401e36)----------
-> loop_head: 401e36
-> entries: 1
-> trip_count: 15
-> iterations: 15
-> self_ins: 15 (0.0202271%)
-> total_ins: 15 (0.0202271%)
[3]--------loop at: gzip.c:986(401e5c)----------
-> loop_head: 401e5c
-> entries: 1
-> trip_count: 23
-> iterations: 23
-> self_ins: 212 (0.285876%)
-> total_ins: 212 (0.285876%)
[4]--------loop at: gzip.c:1440(401f32)----------
....
....

```

##### Loop Call Graph:  
```bash
----------------------------------------------------------------------------------------
[17]							405fe2	    deflate	   [caller]
							401921	      __cyg_profile_func_exit	 [callee]
							4075b2	      flush_block	 [callee]
							4018f1	      __cyg_profile_func_enter	 [callee]
	  23		3159 (4%)    	3303 (4%)    	40629a	      deflate.c:675 [sub_loop]
----------------------------------------------------------------------------------------
[18]							406313	    lm_init	   [caller]
							401921	      __cyg_profile_func_exit	 [callee]
							405c62	      fill_window	 [callee]
							4059cf	      file_read	 [callee]
							4018f1	      __cyg_profile_func_enter	 [callee]
----------------------------------------------------------------------------------------
[19]							40647a	    gen_codes	   [caller]
							401921	      __cyg_profile_func_exit	 [callee]
							4018f1	      __cyg_profile_func_enter	 [callee]
	  296		20289 (27%)    	37326 (50%)    	4064f9	      trees.c:594 [sub_loop]
	  56		336 (0%)    	336 (0%)    	4064b2	      trees.c:581  [sub_loop]
	  259		2017 (2%)    	2044 (2%)    	4064fe	      trees.c:590  [sub_loop]
----------------------------------------------------------------------------------------
[20]							406534	    init_block	   [caller]
							401921	      __cyg_profile_func_exit	 [callee]
							4018f1	      __cyg_profile_func_enter	 [callee]
	  36		144 (0%)    	144 (0%)    	406581	      trees.c:414  [sub_loop]
	  58		232 (0%)    	232 (0%)    	406569	      trees.c:413  [sub_loop]
	  570		2280 (3%)    	2280 (3%)    	406551	      trees.c:412  [sub_loop]
----------------------------------------------------------------------------------------
```
   
   
##### Graph Visualization:  
![loop call graph](https://github.com/meton-robean/Func-Loop-Profiler/blob/master/callgraph1.png)  

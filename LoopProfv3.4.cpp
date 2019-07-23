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

/*test file compile: eg.   gcc -g test_file.c -o test 
 *compile:             eg.   make PIN_ROOT=~/application/pin-3.5/ OBJDIR=./obj/ ./obj/LoopProfv3.4.so
 *run pintool:        eg.   pin -t ./obj/LoopProfv3.4.so -o log.out -- ./test
 *loop call graph visualization: eg,   ./callgraph_v2.py addr2line test log.out callgraph.png
*/



#include "pin.H"
#include <asm/unistd.h>
#include <iostream>
#include <fstream>
#include <list>
#include <sstream>
#include <vector>
#include <stack>
#include <map>
#include <time.h>
/*
 * when DEBUG=1,print debug message ,0 not print debug info
*/
#define DEBUG  0

/*
 *doing loop profile,will print flat loop proile ,
 * loop call graph table (with which can do call graph visuallization using callgraph_v2.py script)
*/
#define LOOP_PROF  


/*
 * only print bbl flow of program, not doing loop profile
*/
//#define ONLY_GET_BBL_FLOW 

#define LOCKED    1
#define UNLOCKED  !LOCKED

static UINT32       _lockAnalysis = !LOCKED; /* unlock -> without sym */
static UINT16       _tabExeNum[0x10000];  //the exe  times of bbl
static std::string  _tabStr[0x10000];  //store the disassembling code of instruction
static bool          isbblhead[0x10000] ; // mark bbl's head addr
static UINT32       _Insnum[0x10000];  //ins number of bbl
static bool  _is_indirect_call=0;    //mark the indirect calling bbl
static UINT64       icount = 0;        // account for total bbls'ins number
  
/* for debug information printing when DEBUG=1 */
#define DBG(s)  if (DEBUG){ s;}

//get the value of %eip
#define FETCH_EIP(_eip)     do{\
    asm volatile( \
        "movl %%eip, %0 \n" \
        : "=r" (_eip) \
    ); \
}while(0)

//get the value of %ebp和%esp
#define FETCH_SREG(_ebp, _esp)     do{\
    asm volatile( \
        "movl %%ebp, %0 \n" \
        "movl %%esp, %1 \n" \
        : "=r" (_ebp), "=r" (_esp) \
    ); \
}while(0)

/*print ebp esp's addr */
#define PRINT_SREG(_ebp, _esp)     do{\
    printf("[%s]: EBP      = 0x%08x\n", __FUNCTION__, _ebp); \
    printf("[%s]: ESP      = 0x%08x\n", __FUNCTION__, _esp); \
}while(0)

UINT64 get_dot_text_sec_addr();
UINT64 get_elf_symbol_addr(string symbol);

/* two core data struct struct func and struct loopc   */

typedef struct func
{
  UINT64 func_addr;
  // functions called directly (not within any loop in this function) by this function
  list<struct func> callee_list;
  // a set of out-most loops within this function
  list<struct loopc> outmost_loop_list;
  bool operator==(struct func fc) const 
  {
      return fc.func_addr == func_addr;
  }
}func_t;

typedef struct loopc
{
  UINT64 loopc_addr;
  // children loops that are embedded directly in this loop (without inclusion of grand-children loops
  list<struct loopc> outmost_loop_list;
  // functions called directly in this loop (without inclusion of functions called inside children loops)
  list<struct func> callee_list;
  bool operator==(struct loopc lpc) const
   {
      return lpc.loopc_addr == loopc_addr;
  }
}loop_t;


/*BblPathInfo struct: store bbl information */
typedef struct 
{
   //other accounting information;
   UINT64 head_addr;      // the first ins's addr of this bbl
   UINT64 tail_addr;      // the last ins's addr of this bbl
   UINT64 self_ins;       // self ins, not including sub loop's ins
   UINT64 total_ins;      // total instructions that this bbl accumulates (include sub loop's instruction)
   bool isloophead;      // is loop head 
   UINT64 belongto;      // the bbl belongs to which loop (loop head addr)
   bool   isCallBbl;     // is calling bbl 
   UINT64 call_target;    // calling bbl's call traget function  addr

}BblPathInfo;

/*fake function stackframe we maintain,
  including frame index and the path of bbls limited in this stackframe  */
typedef struct 
{
  UINT64 index;
  list<BblPathInfo > path;

}StackFrame;


/*struct that stores loop information */
typedef struct 
{
  // Loop accounting variable for iterations
  // self/total instructions ,and children
  UINT64 loop_head;
  UINT64 iter_num;
  UINT64 entry_num;
  UINT64 self_ins;
  UINT64 total_ins;
  

}LoopInfo;

typedef struct 
{
   UINT64 belongtoWhichLayer;
    UINT64 loop_head; 

}Loop_child;

std::map<UINT64, LoopInfo*> hash_loops;
std::map<UINT64, list<Loop_child> > hash_loop_child;
std::map<UINT64, func_t> func_map;
std::map<UINT64, loop_t> loopc_map;

list<StackFrame> callstack;

UINT32 pre_sp=0;
UINT32 cur_sp=0;

/*count total ins number */
VOID docount(UINT32 c) 
{ 
    icount += c;

}

/* mark loophead in current stack frame  */
bool mark_loophead_bbl( StackFrame *frame, UINT64 bblhead)
{
   list<BblPathInfo>::iterator it;
   //BblPathInfo * l=loop_head_bpi;
   if(frame->path.empty())
   {
     return 0;
   }
   
   DBG(printf("in func mark_loophead_bbl(): after mark, print all element of frame: \n"); );
   
   for(it=frame->path.begin();it!=frame->path.end();it++)
   {
       if(it->head_addr==bblhead)
       {
          it->isloophead=1;
       }
   }
   DBG(
   for(it=frame->path.begin();it!=frame->path.end();it++)
   {
       std::cout<<std::hex<<it->head_addr<<" "<<std::dec<<it->isloophead<<endl;
   }
   );
   return 1;
}

/* check if current stack frame path contains this bbl head addr ,if yes,means a loop */
bool findBPI(StackFrame *frame, UINT64 bblhead)
{
  if(frame->path.empty())
  {
    return NULL;
  }

    list<BblPathInfo >::iterator iter;
    for(iter=frame->path.begin(); iter !=frame->path.end(); iter++)
    {
        if(iter->head_addr==bblhead )
        {
          return 1;
        }

    }
    return 0;
}


bool isHashLoopChildContain(UINT64 loop_parent,UINT64 loop_child)
{
  list<Loop_child>::iterator it;
  for(it=hash_loop_child[loop_parent].begin();it!=hash_loop_child[loop_parent].end();it++)
  {
      if(it->loop_head==loop_child)
      {
        return 1;
      }

  }
  return 0;
}

/*check if global func map contain spec callee function  */
bool isfuncMapCalleeListContain(UINT64 caller_addr,UINT64 callee_addr)
{
    list<struct func>::iterator it;

    for(it=func_map[caller_addr].callee_list.begin();it!=func_map[caller_addr].callee_list.end();it++)
    {
       if(it->func_addr==callee_addr)
       {
          return 1;
       }
    }
    return 0;
}
/*check if loop map contain spec callee function  */
bool isloopcMapCalleeListContain(UINT64 caller_addr,UINT64 callee_addr)
{
    list<struct func>::iterator it;

    for(it=loopc_map[caller_addr].callee_list.begin();it!=loopc_map[caller_addr].callee_list.end();it++)
    {
       if(it->func_addr==callee_addr)
       {
          return 1;
       }
    }
    return 0;
}

/* check if loop map contain spec sub loop */
bool isloopcMapLoopListContain(UINT64 top_level_loop, UINT64 sub_loop)
{
  list<struct loopc>::iterator it;

  for(it=loopc_map[top_level_loop].outmost_loop_list.begin();
               it!=loopc_map[top_level_loop].outmost_loop_list.end();it++)
  {
     if(it->loopc_addr==sub_loop)
     {
         return 1;
     }
  }
  return 0;
}


/* check if func map contain spec sub loop  */
bool isfuncMapLoopListContain(UINT64 top_level_loop, UINT64 sub_loop)
{
  list<struct loopc>::iterator it;

  for(it=func_map[top_level_loop].outmost_loop_list.begin();
               it!=func_map[top_level_loop].outmost_loop_list.end();it++)
  {
     if(it->loopc_addr==sub_loop)
     {
         return 1;
     }
  }
  return 0;
}
/*core function for accouting self ins and total ins , 
          construct func map and loop map , construct function callstack   */
void doAccounting(StackFrame *frame, UINT64 bblhead,UINT64 ins_num_bbl)
{
  DBG(printf("in func doAccounting(): start to do accounting \n"); );
  UINT64 loop_head_addr=bblhead;

  BblPathInfo poped_bpi;
  BblPathInfo * current_bpi_ptr;
  current_bpi_ptr=&frame->path.back();
  current_bpi_ptr->belongto=loop_head_addr; //mark this bpi belongs to the loop_head_bpi
  while(!frame->path.empty()) //start to travere the path in reversed order
  {
    current_bpi_ptr=&frame->path.back();
    DBG(cout<<"in func doAccounting(): stack back element is : "<<std::hex<< current_bpi_ptr->head_addr
              <<"("<<std::dec<<current_bpi_ptr->isloophead<<")"<<" )"
              <<std::dec<<current_bpi_ptr->self_ins<<" "<<std::dec<<current_bpi_ptr->total_ins<<" " 
              <<"( belongto "<<std::hex<<current_bpi_ptr->belongto<<") "
              <<endl;);
    
    if((current_bpi_ptr->isCallBbl==1)
        && (!isloopcMapCalleeListContain(loop_head_addr,current_bpi_ptr->call_target)) )//add current loop 's callee to loopc_map
    {
        func f;
        f.func_addr=current_bpi_ptr->call_target;
        loopc_map[loop_head_addr].callee_list.push_back(f);
        DBG(cout<<"in doAccounting():111 add loop "<<std::hex<<loop_head_addr<<"'s callee:"<<current_bpi_ptr->call_target<<endl;);
    }
    if(current_bpi_ptr->isloophead==0) //not loop head 
    {

        poped_bpi= frame->path.back();
        current_bpi_ptr=NULL;
        frame->path.pop_back();
        DBG( cout<<"in func doAccounting(): "<<std::hex<<poped_bpi.head_addr<<" is poped from frame"<<endl;);
        current_bpi_ptr=&frame->path.back();
        if((current_bpi_ptr->isloophead)&&( current_bpi_ptr->head_addr==poped_bpi.belongto)  ) //encounted loop head of inner loop
        {
           
           hash_loops[current_bpi_ptr->head_addr]->iter_num+=1 ;
           //DBG(cout<<"in func doAccounting(): kkk  iter_num: "<<std::dec<< hash_loops[current_bpi_ptr->head_addr]->iter_num<<endl; );

           current_bpi_ptr->total_ins +=(poped_bpi.total_ins+ins_num_bbl);
           current_bpi_ptr->self_ins  +=(poped_bpi.self_ins+ins_num_bbl);
           
          //hash_loops[current_bpi_ptr->head_addr]->self_ins=current_bpi_ptr->self_ins;
          //hash_loops[current_bpi_ptr->head_addr]->total_ins=current_bpi_ptr->total_ins;

           break;//not pop the loophead bpi temperarily
        }
        else if( (current_bpi_ptr->isloophead)
                    &&( current_bpi_ptr->head_addr!=poped_bpi.belongto) )//outer loop encouters inner loop
        {
            
 
            hash_loops[current_bpi_ptr->head_addr]->entry_num+=1;
            hash_loops[current_bpi_ptr->head_addr]->iter_num+=1;
            hash_loops[current_bpi_ptr->head_addr]->self_ins+=current_bpi_ptr->self_ins;
            hash_loops[current_bpi_ptr->head_addr]->total_ins+=current_bpi_ptr->total_ins;
            DBG(cout<<"in doAccounting(): aaa in hash_loops: "<<std::hex<<current_bpi_ptr->head_addr<<":"
                      <<std::dec<<hash_loops[current_bpi_ptr->head_addr]->self_ins<<" "
                      <<std::dec<<hash_loops[current_bpi_ptr->head_addr]->total_ins
                      <<endl;  );
            //add sub loop to current loop's loopc_map
            if(!isloopcMapLoopListContain(loop_head_addr,current_bpi_ptr->head_addr))
            {
                loop_t lp;
                lp.loopc_addr=current_bpi_ptr->head_addr;
                loopc_map[loop_head_addr].loopc_addr=loop_head_addr; //#cmt
                loopc_map[loop_head_addr].outmost_loop_list.push_back(lp);
                
                DBG(cout<<"in doAccounting():222 add loop "<<std::hex<<loop_head_addr
                  <<"'s sub_loop:"<<current_bpi_ptr->head_addr<<endl;);
            }

            DBG(cout<<"in doAccounting(): aaa put poped-bbl( "<< std::hex<< poped_bpi.head_addr
                      <<" )info to curent bbl( "<<std::hex<<current_bpi_ptr->head_addr<<" )" <<endl;);
            current_bpi_ptr->isloophead=0;
            current_bpi_ptr->self_ins=poped_bpi.self_ins;
            current_bpi_ptr->total_ins+=poped_bpi.total_ins;
            current_bpi_ptr->belongto=poped_bpi.belongto;
            DBG(cout<<"in doAccounting: aaa "<<std::hex<<current_bpi_ptr->head_addr<<" :"
                       <<std::dec<< current_bpi_ptr->self_ins <<" "<<current_bpi_ptr->total_ins<<endl;);

        }
        else
        {

            current_bpi_ptr->total_ins +=poped_bpi.total_ins;
            current_bpi_ptr->self_ins+=poped_bpi.self_ins;
            current_bpi_ptr->belongto=poped_bpi.belongto; //pass the belongto to next bpi member of the same loop
            DBG(cout<<"in doAccounting(): bbb  put poped-bbl( "<< std::hex<< poped_bpi.head_addr
                      <<" )info to curent bbl( "<<std::hex<<current_bpi_ptr->head_addr<<" )" <<endl;);

        }

     }
     else //current_bpi_ptr is loop head 
     {

 
        if(current_bpi_ptr->head_addr!=loop_head_addr) //current bbl's loophead not belong to current loop head
        {
            DBG(printf("in func doAccounting(): ccc \n"); );
            poped_bpi=frame->path.back();
            hash_loops[poped_bpi.head_addr]->entry_num+=1;
            hash_loops[poped_bpi.head_addr]->iter_num+=1;
            hash_loops[poped_bpi.head_addr]->self_ins+=poped_bpi.self_ins;
            hash_loops[poped_bpi.head_addr]->total_ins+=poped_bpi.total_ins;
            
            //add sub loops to current loop's loopc_map
            if(!isloopcMapLoopListContain(loop_head_addr,current_bpi_ptr->head_addr))
            {
                loop_t lp2;
                lp2.loopc_addr=current_bpi_ptr->head_addr;
                loopc_map[loop_head_addr].outmost_loop_list.push_back(lp2); 
                DBG(cout<<"in doAccounting():333 add loop "<<std::hex<<loop_head_addr
                         <<"'s sub_loop:"<<current_bpi_ptr->head_addr<<endl;); 
            }
            //after update the hash_loops ,set this loophead bbl's "isloophead=0"(means it become normal bbl now)
            current_bpi_ptr->isloophead=0;
            current_bpi_ptr->self_ins=0;
            current_bpi_ptr->total_ins=ins_num_bbl;
            current_bpi_ptr->belongto=loop_head_addr;
        }
        else //current bbl is current loop head
        {
          DBG(printf("in func doAccounting(): ddd \n"); );
          hash_loops[current_bpi_ptr->head_addr]->iter_num+=1;
          if(2==hash_loops[current_bpi_ptr->head_addr]->iter_num)
          {

             current_bpi_ptr->self_ins+=ins_num_bbl;
             current_bpi_ptr->total_ins+=ins_num_bbl;

             //hash_loops[current_bpi_ptr->head_addr]->self_ins+=(current_bpi_ptr->self_ins);
             //hash_loops[current_bpi_ptr->head_addr]->total_ins+=(current_bpi_ptr->total_ins);
             DBG(cout<<"in doAccounting(): ddd-1 "<<std::dec<<current_bpi_ptr->self_ins<<" "
                      <<current_bpi_ptr->total_ins<<std::dec<<" "
                      <<hash_loops[current_bpi_ptr->head_addr]->iter_num
                      <<endl; );
             break;
          }
          current_bpi_ptr->self_ins+=ins_num_bbl;
          current_bpi_ptr->total_ins+=ins_num_bbl;
          //hash_loops[current_bpi_ptr->head_addr]->self_ins+=ins_num_bbl;
          //hash_loops[current_bpi_ptr->head_addr]->total_ins+=ins_num_bbl;
             DBG(cout<<"in doAccounting(): ddd-2 "<<std::dec<<current_bpi_ptr->self_ins<<" "
                       <<current_bpi_ptr->total_ins
                       <<" "<<std::dec<<hash_loops[current_bpi_ptr->head_addr]->iter_num
                       <<endl; );
          break;

        }
    }
  }
  return;
}

//Loop Detection Algorithm
void precessLoop(StackFrame *frame, UINT64 bblhead ,UINT64 ins_num_bbl )
{
  UINT64 loop_head_addr=bblhead;
  //DBG(cout<<"in func precessLoop(): meet a loophead BBL: "<<std::hex<< bblhead <<endl;);
  if(hash_loops[loop_head_addr]==NULL) //cmt this position of hash is NULL
  {
     DBG(cout<<"in precessLoop(): create new item in hash_loops[] for new loops: "<<std::hex<<bblhead<<endl;);
     LoopInfo *loopinfo=(LoopInfo*)malloc(sizeof(LoopInfo));
     hash_loops[loop_head_addr]=loopinfo;
     hash_loops[loop_head_addr]->loop_head=loop_head_addr;

     //hash_loops[loop_head_addr]->iter_num+=1;
     //hash_loops[loop_head_addr]->entry_num+=1;
     //DBG(cout<<"in precessLoop(): it has been iterated "<<hash_loops[loop_head_addr]->iter_num <<"times,now begin the second iteration"<<endl;);
  }
  //do instruction ,iterations,trip-count,entry accounting and do loop call graph assembling~
  doAccounting(frame, bblhead, ins_num_bbl);

}

/*alloc stackframe index num  */
int alloc_stack_frame_index()
{
    if(!callstack.empty())
    {
        UINT64 index=0;
        list<StackFrame>::iterator it;
        for(it=callstack.begin();it!=callstack.end();++it)
        {
           if(it->index>index)
           {
              index=it->index;
           }

        }
        return index+1;
    }
    return -1;
}



//check the function call stack each time meet a BBL
void adjustStack(UINT64 bblhead,UINT64 bbltail, UINT64 ins_num_bbl,
                        BOOL iscall, BOOL isIndirectCall, ADDRINT call_target,BOOL isSyscall, BOOL isRet)
{
    if(isSyscall)
    {
      DBG(cout<<"in adjustStack(): this bbl("<<std::hex<<bblhead<<")  do a system call "<<endl;);
    }
    //set previous indirect calling bbl's call_taget
    if( (1==_is_indirect_call)  )
    {
        StackFrame *fra_ptr=&callstack.back();
        UINT64 current_frame_id=fra_ptr->index;
        fra_ptr=NULL;
        list<StackFrame>::iterator it;
        for(it=callstack.begin();it!=callstack.end();++it)
        {
            if(it->index==(current_frame_id-1))
            {
                 BblPathInfo *tmp_bpi =&(it->path.back() );  
                 tmp_bpi->call_target=bblhead;  
                 DBG(cout<<" now previous indirect calling bbl "<<std::hex<< it->path.back().head_addr
                          <<" 's call target is "<<std::hex<<it->path.back().call_target
                           <<endl;);
                 tmp_bpi=NULL;      
            }
        }
        _is_indirect_call=0; //reset 0;   

    }  

    if((1==iscall)&&(0==isRet)&&(0==isSyscall)) //detect the call instruction
    {

      DBG(cout<<"in adjustStack():  enter new function: "<<std::hex<<call_target
                <<"(0 if is indirect call)"
                <<endl;);
      if(alloc_stack_frame_index()==-1) //if the first stack frame, the index is 1
      {
            StackFrame frame;
            frame.index=1;
            callstack.push_back(frame);
            DBG(cout<<"push new stack frame ,frame id is"<<frame.index<<endl;);
      }
      else  
      {     
           //detect the indirect call instruction
              if( (1==iscall)&&(1==isIndirectCall)&&(0==call_target) )
              {
                  _is_indirect_call=1;  //global_var
                  
                  //put the  indirect calling bbl into current callstack path
                  StackFrame *frame_ptr=&callstack.back();
                  BblPathInfo bpi;
                  bpi.head_addr=bblhead;
                  bpi.tail_addr=bbltail;
                  bpi.isloophead=0;
                  bpi.self_ins=ins_num_bbl;
                  bpi.total_ins=ins_num_bbl;
                  bpi.isCallBbl=1;
                  bpi.call_target=0; //indirect call's call_target is  set 0 temperarily 
                  frame_ptr->path.push_back(bpi);
                  frame_ptr=NULL; 
                  //create new stack frame for indirect calling bbl's call_target bbl
                  StackFrame frame;
                  frame.index=alloc_stack_frame_index();
                  DBG(cout<<"push new stack frame ,frame id is"<<frame.index<<endl;);
                  callstack.push_back(frame); 
                  return;

              }
          //detect the indirect call instruction
            //put the calling bbl into current callstack path
            StackFrame *frame_ptr=&callstack.back();
            BblPathInfo bpi;
            bpi.head_addr=bblhead;
            bpi.tail_addr=bbltail;
            bpi.isloophead=0;
            bpi.self_ins=ins_num_bbl;
            bpi.total_ins=ins_num_bbl;
            bpi.isCallBbl=1;
            DBG(cout<<"in adjustStack(): www "<<std::hex<<bpi.head_addr<<" is calling bbl"<<endl;);
            if(call_target>get_dot_text_sec_addr())//not system call
            {
              bpi.call_target=call_target;
            }
            else // call taget is system call ,so just ignore these bbl 
            {
                bpi.call_target=0;
                bpi.isCallBbl=0;
            }
            frame_ptr->path.push_back(bpi);
            frame_ptr=NULL;
            //create new stackframe(new path )for new function's bbls
            ////if call target is not  system call,create a new stack frame for non-system call function
            if(call_target>get_dot_text_sec_addr())
            {
              StackFrame frame;
              frame.index=alloc_stack_frame_index();
              callstack.push_back(frame);
              DBG(cout<<"push new stack frame ,frame id is"<<frame.index<<endl;);
              //create new func_t struct
               if(func_map[call_target].func_addr==0)
               {
                  func_t func;
                  func.func_addr=call_target;
                  func_map[call_target]=func;
                  DBG(cout<<"new caller: "<<std::hex<<func.func_addr<<" add to funcmap"<<endl;);
                  
               }
            }
      }
      return;
    }
    if(isRet==1) //a function returned
    {
        DBG(cout<<"in adjustStack(): start to return from function----"<<endl;);
        
        StackFrame *frame=&callstack.back();
        list<BblPathInfo> * path=&frame->path;
        BblPathInfo poped_bpi ;
        poped_bpi.head_addr=bblhead; //add the returning bbl to the current path
        poped_bpi.tail_addr=bbltail;
        poped_bpi.self_ins=ins_num_bbl;
        poped_bpi.total_ins=ins_num_bbl;
        poped_bpi.isCallBbl=0;
        poped_bpi.call_target=0;
        poped_bpi.isloophead=0;//#cmt
        path->push_back(poped_bpi);
        BblPathInfo * calling_bpi=&path->front();
        while(!path->empty())
        {
             poped_bpi=path->back();
             /* cmt here calling bbl means the first bbl of current frame path
                also means the function head bbl ~(the first bbl of function body~);
                it not means the calling bbl that calls this function~
             */
             if(calling_bpi->head_addr!=poped_bpi.head_addr)//not reach the calling bbl
             {
                 DBG(cout<<"in adjustStack(): "<<"aaa"<<endl;);
                 if(!poped_bpi.isloophead) //not loop head
                 {
                    DBG(cout<<"in adjustStack(): "<<"aaa-1"<<endl;);
                    calling_bpi->self_ins+=poped_bpi.self_ins;
                    calling_bpi->total_ins+=poped_bpi.total_ins;
                    path->pop_back();

                 }
                 else
                 {  // is loop head
                    //before the top-level loophead bbl is poped , hash_loops is updated
                    DBG(cout<<"in adjustStack(): "<<"aaa-2"<<endl;);
                    hash_loops[poped_bpi.head_addr]->iter_num+=1;
                    hash_loops[poped_bpi.head_addr]->entry_num+=1; //#cmt

                    hash_loops[poped_bpi.head_addr]->loop_head=poped_bpi.head_addr;
                    hash_loops[poped_bpi.head_addr]->self_ins+=poped_bpi.self_ins;
                    hash_loops[poped_bpi.head_addr]->total_ins+=poped_bpi.total_ins;
                     DBG(cout<<"in adjustStack(): in hash_loops: "<<std::hex<<poped_bpi.head_addr<<":"
                               <<std::dec<<hash_loops[poped_bpi.head_addr]->self_ins<<" "
                               <<std::dec<<hash_loops[poped_bpi.head_addr]->total_ins
                               <<endl;  
                         );
                     //remember the top-level loop info in this stack frame for following call graph assembling 
                     // Loop_child loop_child;
                     // loop_child.belongtoWhichLayer=frame->index-1;
                     // loop_child.loop_head=poped_bpi.head_addr;
                     // _toplevel_loop_tmp_stack.push_back(loop_child);

                     //pass top-level loop to func_t struct's loop list
                     if(!isfuncMapLoopListContain(calling_bpi->head_addr,poped_bpi.head_addr))
                     {
                         loop_t lp3;
                         lp3.loopc_addr=poped_bpi.head_addr;
                         func_map[calling_bpi->head_addr].outmost_loop_list.push_back(lp3);
                         DBG(cout<<"in adjustStack():444 add func "<<std::hex<<calling_bpi->head_addr
                                   <<"'s sub_loop:"<<poped_bpi.head_addr<<endl;);
                     }
                    // pass top-level loophead bbl info to caling bbl
                     calling_bpi->self_ins+=0;
                     calling_bpi->total_ins+=poped_bpi.total_ins;
                    //pop the top-level loop head bbl
                     path->pop_back();
                 }

                 if(poped_bpi.isCallBbl) //is callee's calling bbl 
                 {
                   if(!isfuncMapCalleeListContain(calling_bpi->head_addr, poped_bpi.call_target))
                   {
                      func_t f2;
                      f2.func_addr=poped_bpi.call_target;
                      func_map[calling_bpi->head_addr].callee_list.push_back(f2);
                      DBG(cout<<"in adjustStack():555 add func "<<std::hex<<calling_bpi->head_addr
                             <<"'s callee:"<<poped_bpi.call_target<<endl;);
                   }
                 }
            }
            else  //reach the calling bbl
            {

                if(calling_bpi->isCallBbl&&calling_bpi->call_target)
                {
                  if(!isfuncMapCalleeListContain(calling_bpi->head_addr,calling_bpi->call_target))
                  {
                    func_t f3;
                    f3.func_addr=calling_bpi->call_target;
                    func_map[calling_bpi->head_addr].callee_list.push_back(f3);
                  }
                }
                DBG(cout<<"in adjustStack(): "<<"returning from calling bpi: "<<std::hex<<calling_bpi->head_addr <<endl;);
                calling_bpi=NULL;
                path->pop_back(); //pop the last bbl (calling bbl)
                path=NULL;
                frame=NULL;
                callstack.pop_back();//pop the current stack frame
                if(!callstack.empty())
                {
                    
                    path=&callstack.back().path; // function return ,and pointer is point to previous fuction stack
                    BblPathInfo *back=&path->back();
                    //add the poped calling bbl info to previous path back element
                     // _tmp_calling_bpi=poped_bpi;
                    back->self_ins+=poped_bpi.self_ins;
                    back->total_ins+=poped_bpi.total_ins;
                    back=NULL;
                    path=NULL;

                    DBG(cout<<"in adjustStack(): "<<"bbb-1 add to privous calling bbl"<<endl;);
                }
                if(callstack.empty())
                {
                  StackFrame frame;
                  frame.index=1;
                  DBG(cout<<"create new stack frame,id=1"<<endl;);
                  callstack.push_back(frame);
                }
                
                return;
            }
         }

    }

    return;
}

/* called each time Bbl is executed */
void precessBbl(UINT64 bblhead, UINT64 bbltail,UINT64 ins_num_bbl,
                       BOOL iscall, BOOL isIndirectCall, ADDRINT call_target, BOOL isSyscall ,BOOL isRet )
{
  //INS head_ins=BBL_InsHead(bbl);
  //INS tail_ins=BBL_InsTail(bbl);
  //UINT64 bblhead = INS_Address(head_ins); //cmt the head addr of bbl
  //UINT64 bbltail=INS_Address(tail_ins);

   /* check if the address is in a stack area */
  if (bblhead > 0x700000000000)
  {
    return;
  }
 
  if (_tabExeNum[bblhead ^ 0x400000] == 0xffff)
  {
    return;
  }
  // if this bbl is system call ,just ignore it,we don't account for these kind of bbl
  if(bblhead<get_dot_text_sec_addr())
  {
    return;
  }

   DBG(std::cout<<"in func precessBbl(): meet a BBL "<<std::hex<<bblhead
                  <<"("<<std::dec<<ins_num_bbl<<" "<<std::dec<<ins_num_bbl
                  <<endl; );
   
   docount(ins_num_bbl);//count for total ins for this program

   adjustStack(bblhead,bbltail,ins_num_bbl, iscall, isIndirectCall, call_target,isSyscall,isRet);


   StackFrame *frame=&callstack.back();
   if(iscall) //calling bbl has been put into path,so not put again
   {
      return;
   }
   if(isRet) 
   {
      return;
   }
  if(findBPI(frame,bblhead) ) //cmt if frame contains this bblhead,find a loop head
  {
      //loop detected;do instruction accounting and pop BBLs above this BBL
      DBG(std::cout<<"in func precessBbl(): this BBL has contained in frame: "<<std::hex<<bblhead<<endl; );
      //// create loop_t struct and add loop_t struct to loopc_map
      //// loop_t loopc;
      //// loopc.loopc_addr=bblhead;
      //// loopc_map[bblhead]=loopc;

      if(mark_loophead_bbl(frame,bblhead ))//mark the loop head in current path (isloophead=1)
      {
        
        precessLoop(frame,bblhead,ins_num_bbl);
      }
      
  }
  else //cmt new bpi is created
  {
    //cmt acounting the self_ins of this bbl and store self_ins to bpi*
    //UINT64 ins_num_bbl=BBL_NumIns(bbl); 
    BblPathInfo newBpi;
    newBpi.head_addr=bblhead;
    newBpi.tail_addr=bbltail;
    newBpi.self_ins=ins_num_bbl;
    newBpi.total_ins=ins_num_bbl;
    newBpi.isCallBbl=0;
    newBpi.call_target=0;
    newBpi.isloophead=0;

    frame->path.push_back(newBpi);

    DBG(printf("in func precessBbl(): push this BBL to path frame \n"); );
  }
}


INT32 Usage()
{
    std::cerr << "LoopProf test" << std::endl;
    return -1;
}

/*collect bbl's head ins information  */
VOID insCallBack_bbl_head(UINT64 insAddr, string insDis,UINT32 ins_num)
{
  if (_lockAnalysis)
    return ;

  if (insAddr > 0x700000000000)
    return;
 
  if (_tabExeNum[insAddr ^ 0x400000] == 0xffff)
    return;
 
  _tabExeNum[insAddr ^ 0x400000] += 1;
  _tabStr[insAddr ^ 0x400000] = insDis;
  isbblhead[insAddr^0x400000]=1;
  _Insnum[insAddr^0x400000]=ins_num;
}

/*collect bbl's tail ins information  */
VOID insCallBack_bbl_tail(UINT64 insAddr, std::string insDis, UINT32 ins_num)
{
  if (_lockAnalysis)
    return ;

  if (insAddr > 0x700000000000)
    return;
 
  if (_tabExeNum[insAddr ^ 0x400000] == 0xffff)
    return;
 
  _tabExeNum[insAddr ^ 0x400000] += 1;
  _tabStr[insAddr ^ 0x400000] = insDis;
  isbblhead[insAddr^0x400000]=0;
  _Insnum[insAddr^0x400000]=ins_num;

}

/*get programe's bbl flow information  */
void get_bbl_flow(TRACE trace, VOID *v)
{
      // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to docount before every bbl, passing the number of instructions
        // BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)docount, IARG_UINT32, BBL_NumIns(bbl), IARG_END);
         INS head_ins=BBL_InsHead(bbl) ;//get the head of bbl
         INS tail_ins=BBL_InsTail(bbl);
         INS_InsertCall(
                  tail_ins, IPOINT_BEFORE, (AFUNPTR)insCallBack_bbl_tail,
                  IARG_ADDRINT, INS_Address(tail_ins),
                  IARG_PTR, new string(INS_Disassemble(tail_ins)),IARG_UINT32, BBL_NumIns(bbl),
                  IARG_END);
         INS_InsertCall(
                  head_ins, IPOINT_BEFORE, (AFUNPTR)insCallBack_bbl_head,
                  IARG_ADDRINT, INS_Address(head_ins),
                  IARG_PTR, new string(INS_Disassemble(head_ins)),IARG_UINT32, BBL_NumIns(bbl),
                  IARG_END);

    }
}

/* Instrument function */
VOID Trace(TRACE trace, VOID *v)
{
   
   #ifdef ONLY_GET_BBL_FLOW
        get_bbl_flow(trace,v);
   #endif

   #ifdef  LOOP_PROF
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // // Insert a call to docount before every bbl, passing the number of instructions
        // // BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)docount, IARG_UINT32, BBL_NumIns(bbl), IARG_END);
         INS head_ins=BBL_InsHead(bbl) ;//get the head of bbl
         INS tail_ins=BBL_InsTail(bbl);
         UINT64 ins_num_bbl=BBL_NumIns(bbl);
         BOOL iscall=0;
         BOOL isRet=0;
         BOOL isIndirectCall=0;
         BOOL isSyscall=0;  // this flag is not used yet
         ADDRINT call_target=0;
          // if(INS_IsDirectCall(tail_ins)) //#cmt previous method
          // {
          //   iscall=1;
          //   if(!INS_IsSyscall(tail_ins)) //not syscall    //#cmt 
          //   {
          //       //DBG(cout<<"in Trace(): this bbl tail is call ins"<<endl;);
          //       call_target=INS_DirectBranchOrCallTargetAddress(tail_ins);
          //       isSyscall=0;

          //   }
          //   else // is syscall
          //   {
          //       call_target=INS_DirectBranchOrCallTargetAddress(tail_ins);
          //       isSyscall=1;
          //       DBG(cout<<"in Trace(): system call!!!"<<endl;);

          //   }
          // }
          if(INS_IsDirectBranchOrCall(tail_ins) )
          {
              if(INS_IsCall(tail_ins))
              {
                  iscall=1;
                  isIndirectCall=0;
                  call_target=INS_DirectBranchOrCallTargetAddress(tail_ins);
                  DBG(cout<<"direct call"<<endl;);
              }
          }
          if(INS_IsIndirectBranchOrCall(tail_ins))
          {
              
              if(INS_IsCall(tail_ins))
              {
                  DBG(cout<<"indirect call"<<endl;);
                  iscall=1;
                  isIndirectCall=1;
                  // UINT64 a=INS_Address(head_ins);
                  // if(0x4049c3==a)
                  // {
                  //     call_target=0x40553e;
                  // }
              }
          }

          if(INS_IsRet(tail_ins) )
          {
            isRet=1;
          }


          UINT64 bblhead = INS_Address(head_ins); //cmt the head addr of bbl
          UINT64 bbltail=INS_Address(tail_ins);
          BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)precessBbl, IARG_UINT64,bblhead,
                                                 IARG_UINT64,bbltail, 
                                                 IARG_UINT64,ins_num_bbl,
                                                 IARG_BOOL,iscall,
                                                 IARG_BOOL,isIndirectCall,
                                                 IARG_ADDRINT,call_target,
                                                 IARG_BOOL,isSyscall,
                                                 IARG_BOOL,isRet,
                                                 IARG_END);

    }
    #endif

}

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "log.out", "specify output file name");

VOID unlockAnalysis(void)
{
  _lockAnalysis = UNLOCKED;
}

VOID lockAnalysis(void)
{
  _lockAnalysis = LOCKED;
}




//string split function
vector< string> string_split( string str, string pattern)
{
  vector<string> ret;
  if(pattern.empty()) return ret;
    size_t start=0,index=str.find_first_of(pattern,0);
  while(index!=str.npos)
  {
    if(start!=index)
      ret.push_back(str.substr(start,index-start));
      start=index+1;
      index=str.find_first_of(pattern,start);
  }
  if(!str.substr(start).empty())
    ret.push_back(str.substr(start));
  return ret;
}

/* mapping addr to name or line number using gnu tool addr2line */
string addr2line_name(string addr, int type) // type 0 for function name; 1 for line number
{ 
    //cout<<addr<<endl;
    char cmd[1024];
    list<string> list; 
    sprintf(cmd,"addr2line -e test -f -C %s",addr.c_str());
    FILE *pp = popen(cmd, "r"); //建立管道
    // if (!pp) {
    //     return -1;
    // }
    char tmp[1024]; //设置一个合适的长度，以存储每一行输出
    while (fgets(tmp, sizeof(tmp), pp) != NULL) {
        if (tmp[strlen(tmp) - 1] == '\n') {
            tmp[strlen(tmp) - 1] = '\0'; //去除换行符
        }
        list.push_back(tmp);
        //cout<<tmp<<endl;
    }
    pclose(pp); //关闭管道
    if(type==1)
    {
        return list.front();
    }
    else
    {
           string a=list.back();
           vector<string> result=string_split(a,"//");
           vector<string> result2=string_split(result.back(),"(");
           // for(int i=0;i<result.size();i++)
           // {
           //   cout<<result[i]<<endl;
           // }
          return result2.front();
    }
    
}

/* get test elf file 's .text addr' */
UINT64 get_dot_text_sec_addr() 
{ 
    //cout<<addr<<endl;
    char cmd[1024];
    list<string> list; 
    sprintf(cmd,"nm -a test |grep -w .text");
    FILE *pp = popen(cmd, "r"); //建立管道
    // if (!pp) {
    //     return -1;
    // }
    char tmp[1024]; //设置一个合适的长度，以存储每一行输出
    while (fgets(tmp, sizeof(tmp), pp) != NULL) {
        if (tmp[strlen(tmp) - 1] == '\n') {
            tmp[strlen(tmp) - 1] = '\0'; //去除换行符
        }
        list.push_back(tmp);
        //cout<<tmp<<endl;
    }
    pclose(pp); //关闭管道

    string a=list.back();
    vector<string> result=string_split(a," ");
    // for(int i=0;i<result.size();i++)
    // {
    //  cout<<result[i]<<endl;
    // }
      UINT64 h;
      std::stringstream ss;
      ss << std::hex << result.front();
      ss >> std::hex >> h;
      //cout<<std::hex<<h<<endl;
      //cout<<std::dec<<h<<endl;
     return h;
}


UINT64 get_elf_symbol_addr(string symbol) 
{ 
    //cout<<addr<<endl;
    char cmd[1024];
    list<string> list; 
    sprintf(cmd,"nm -a test |grep -w %s",symbol.c_str());
    FILE *pp = popen(cmd, "r"); //建立管道
    // if (!pp) {
    //     return -1;
    // }
    char tmp[1024]; //设置一个合适的长度，以存储每一行输出
    while (fgets(tmp, sizeof(tmp), pp) != NULL) {
        if (tmp[strlen(tmp) - 1] == '\n') {
            tmp[strlen(tmp) - 1] = '\0'; //去除换行符
        }
        list.push_back(tmp);
        //cout<<tmp<<endl;
    }
    pclose(pp); //关闭管道

    string a=list.back();
    vector<string> result=string_split(a," ");
    // for(int i=0;i<result.size();i++)
    // {
    //  cout<<result[i]<<endl;
    // }
     UINT64 h;
     std::stringstream ss;
      ss << std::hex << result.front();
      ss >> std::hex >> h;
      //cout<<std::hex<<h<<endl;
      //cout<<std::dec<<h<<endl;
     return h;
}

/*print out bbl flow information we collect  */
void print_bbl_head_tail()
{
  UINT32 i;
  cout<<"******************bbl flow info table******************* "<<endl;
  std::cout << "Addr\tExeNum\tType\tIns_Num\tDisass" << std::endl;
  for (i = 0; i < 0x10000; i++)
  {
      if(_tabExeNum[i] &&isbblhead[i]==1)
           std::cout << std::hex << (0x400000 + i) << "\t" 
                   <<std::dec<<_tabExeNum[i]<< "\t"
                       <<"head"<<"\t"<<std::dec<<_Insnum[i]<<"\t"
                          <<_tabStr[i] << std::endl;

      if(_tabExeNum[i] &&isbblhead[i]==0)
           std::cout << std::hex << (0x400000 + i) << "\t" 
                   <<std::dec<<_tabExeNum[i]<< "\t"
                       <<"tail"<<"\t"<<std::dec<<_Insnum[i]<<"\t"
                          <<_tabStr[i] << std::endl;
      //std::cout<<"---------------------------------------------------"<<std::endl;
  }
}


//print loop flat profile chart
void print_loops_info()
{
   cout<<"total instructions: "<<std::dec<<icount<<endl;
   //std::cout<<" "<<endl;
   std::cout<<"*********Loop Flat Profile*********"<<std::endl;
   std::map<UINT64,LoopInfo *>::iterator it;
   int index=1;
   for(it = hash_loops.begin(); it != hash_loops.end(); ++it)
   { 
     stringstream ss;
     ss<<std::hex<<it->first;
     printf("[%d]", index);
     std::cout<< "--------"<<"loop at: "<<addr2line_name(ss.str(),2) <<"("
                              <<std::hex<<it->first<<")"<<"----------"<<std::endl;
     std::cout<< "-> loop_head: " << std::hex<<it->second->loop_head << std::endl;
     std::cout << "-> entries: " << std::dec<<it->second->entry_num<< std::endl;
     if(it->second->entry_num!=0)
     {
        std::cout<<"-> trip_count: "<<std::dec<<(it->second->iter_num)/(it->second->entry_num)<<std::endl;
     }
     std::cout << "-> iterations: " << std::dec<<it->second->iter_num << std::endl;
     std::cout << "-> self_ins: " << std::dec<<it->second->self_ins <<" ("<<std::dec<<(it->second->self_ins)*100.0/(icount)<<"%)"<< std::endl;
     std::cout<< "-> total_ins: " << std::dec<<it->second->total_ins << " ("<<std::dec<<(it->second->total_ins)*100.0/(icount)<<"%)"<< std::endl;
     index++;
   }  
   std::cout<<"************************************"<<std::endl;
   cout<<"note: loops in total : "<<std::dec<<index-1<<endl;
   cout<<""<<endl;
}

void print_loop_children_info()
{

  std::cout<<" "<<endl;
  std::cout<<"*********Loop Call Graph*********"<<std::endl;
  std::map<UINT64,list<Loop_child> >::iterator it;
  list<Loop_child>::iterator list_it;

  for(it=hash_loop_child.begin();it!=hash_loop_child.end();++it)
  {
      if(it->second.back().loop_head!=0)
      {
          cout<<" loop("<<std::hex<<it->first<<")'s children: "<<endl;
          list<Loop_child> loop_child_list=it->second;

          for(list_it=loop_child_list.begin(); list_it!=loop_child_list.end();++list_it)
          {
             cout<<"-> "<<std::hex<<list_it->loop_head<<endl;
          }  
      }
  }
}

/* */
list<UINT64> 
callee_search_in_func_map(map<UINT64,func_t> func_map, UINT64 callee_addr)
{
    map<UINT64,func_t>::iterator func_it;
    list<UINT64> result_list;
    result_list.clear();
    for(func_it=func_map.begin();func_it!=func_map.end();func_it++ )
    {

        if(func_it->first==0x4003c0 ||func_it->first==0x400490 )
        {
            continue;
        }
        if((!func_it->second.callee_list.empty()) )
        {
              list<struct func>::iterator func_list_it;
              list<struct func> func_list = func_it->second.callee_list;
              for(func_list_it=func_list.begin();func_list_it!=func_list.end();func_list_it++)
              {
                 if(func_list_it->func_addr==callee_addr)
                 {
                    result_list.push_back(func_it->first);
                 }

              }
        }

    }
    return result_list; 
}

void del_invalid_func2func_edges()
{
    map<UINT64,func_t>::iterator func_it;
    //map<UINT64,loop_t>::iterator loopc_it; 
    for(func_it=func_map.begin();func_it!=func_map.end();func_it++ )
    {
          if(func_it->first==0x4003c0 ||func_it->first==0x400490 ) //not print the caller 4003c0 400490
          {
            continue;
          }
          if((!func_it->second.outmost_loop_list.empty()) )
          {

              list<struct loopc>::iterator loopc_list_it;
              list<struct loopc> loopc_list = func_it->second.outmost_loop_list;
              for(loopc_list_it=loopc_list.begin();loopc_list_it!=loopc_list.end();loopc_list_it++)//search outmost_loop_list
              {   
                  list<struct func> lp_callee_list =loopc_map[loopc_list_it->loopc_addr].callee_list;
                  list<struct func>::iterator f_it;
                  for(f_it=lp_callee_list.begin();f_it!=lp_callee_list.end();f_it++)
                  {
                      UINT64 callee_head=f_it->func_addr;
                      list<UINT64> otherCaller_list= callee_search_in_func_map(func_map ,callee_head);
                      list<UINT64>::iterator iter;
                      for(iter=otherCaller_list.begin();iter!=otherCaller_list.end();iter++) //go through otherCaller_list
                      {
                          UINT64 a=(loopc_list_it->loopc_addr)-callee_head;
                          UINT64 b=(*iter)-callee_head;
                          if(a>b)
                          {
                              func_t fc;
                              fc.func_addr=callee_head;
                              func_map[*iter].callee_list.remove(fc);

                          }
                          else
                          {
                              func_t fc;
                              fc.func_addr=callee_head;
                              loopc_map[loopc_list_it->loopc_addr].callee_list.remove(fc);
                          }
                      }
                  }
              }
          }
    }

}

list<UINT64> loop_search_in_func_map(map<UINT64,func_t> func_map, UINT64 subLoop_addr)
{
     map<UINT64,func_t>::iterator func_it;
    list<UINT64> result_list;
    result_list.clear();
    for(func_it=func_map.begin();func_it!=func_map.end();func_it++ )
    {

        if(func_it->first==0x4003c0 ||func_it->first==0x400490 )
        {
            continue;
        }
        if((!func_it->second.outmost_loop_list.empty()) )
        {
              list<struct loopc>::iterator loop_list_it;
              list<struct loopc> loopc_list = func_it->second.outmost_loop_list;
              for(loop_list_it=loopc_list.begin();loop_list_it!=loopc_list.end();loop_list_it++)
              {
                 if(loop_list_it->loopc_addr==subLoop_addr)
                 {
                    result_list.push_back(func_it->first);
                 }

              }
        }

    }
    return result_list;    
}

void del_invalid_func2loop_edges()
{
    map<UINT64,func_t>::iterator func_it;
    //map<UINT64,loop_t>::iterator loopc_it; 
    for(func_it=func_map.begin();func_it!=func_map.end();func_it++ )
    {
          if(func_it->first==0x4003c0 ||func_it->first==0x400490 ) //not print the caller 4003c0 400490
          {
            continue;
          }
          if((!func_it->second.outmost_loop_list.empty()) )
          {

              list<struct loopc>::iterator loopc_list_it;
              list<struct loopc> loopc_list = func_it->second.outmost_loop_list;
              for(loopc_list_it=loopc_list.begin();loopc_list_it!=loopc_list.end();loopc_list_it++)//search outmost_loop_list
              {   
                  list<struct loopc> lp_sublp_list =loopc_map[loopc_list_it->loopc_addr].outmost_loop_list;
                  list<struct loopc>::iterator lp_it;
                  for(lp_it=lp_sublp_list.begin();lp_it!=lp_sublp_list.end();lp_it++)
                  {
                      UINT64 subLoop_head=lp_it->loopc_addr;
                      list<UINT64> otherCaller_list= loop_search_in_func_map(func_map ,subLoop_head);
                      list<UINT64>::iterator iter;
                      for(iter=otherCaller_list.begin();iter!=otherCaller_list.end();iter++) //go through otherCaller_list
                      {
                          UINT64 a=(loopc_list_it->loopc_addr)-subLoop_head;
                          UINT64 b=(*iter)-subLoop_head;
                          if(a>b)
                          {
                              loop_t lp;
                              lp.loopc_addr=subLoop_head;
                              func_map[*iter].outmost_loop_list.remove(lp);


                          }
                          else
                          {
                              loop_t lp;
                              lp.loopc_addr=subLoop_head;
                              loopc_map[loopc_list_it->loopc_addr].outmost_loop_list.remove(lp);
                          }
                      }
                  }
              }
          }
    }   

}

list<UINT64> loop_search_in_loopc_map(map<UINT64,loop_t> loopc_map, UINT64 top_level_loop_addr ,UINT64 subLoop_addr)
{
    map<UINT64,loop_t>::iterator loopc_it;
    list<UINT64> result_list;
    result_list.clear();
    for(loopc_it=loopc_map.begin();loopc_it!=loopc_map.end();loopc_it++ )
    {
        if(loopc_it->first==top_level_loop_addr)
        {
          continue;
        }

        if((!loopc_it->second.outmost_loop_list.empty()) )
        {
              list<struct loopc>::iterator loop_list_it;
              list<struct loopc> loopc_list = loopc_it->second.outmost_loop_list;
              for(loop_list_it=loopc_list.begin();loop_list_it!=loopc_list.end();loop_list_it++)
              {
                 if(loop_list_it->loopc_addr==subLoop_addr)
                 {
                    result_list.push_back(loopc_it->first);
                 }

              }
        }

    }
    return result_list; 
}

void del_invalid_loop2loop_edges()
{
    //map<UINT64,func_t>::iterator func_it;
    map<UINT64,loop_t>::iterator loopc_it; 
    for(loopc_it=loopc_map.begin();loopc_it!=loopc_map.end();loopc_it++ )
    {
          if((!loopc_it->second.outmost_loop_list.empty()) )
          {
              list<struct loopc>::iterator loopc_list_it;
              list<struct loopc> loopc_list = loopc_it->second.outmost_loop_list;
              for(loopc_list_it=loopc_list.begin();loopc_list_it!=loopc_list.end();loopc_list_it++)//search outmost_loop_list
              {   
                  list<struct loopc> lp_sublp_list =loopc_map[loopc_list_it->loopc_addr].outmost_loop_list;
                  list<struct loopc>::iterator lp_it;
                  for(lp_it=lp_sublp_list.begin();lp_it!=lp_sublp_list.end();lp_it++)
                  {
                      UINT64 subLoop_head=lp_it->loopc_addr;
                      list<UINT64> otherLoop_list= loop_search_in_loopc_map(loopc_map ,loopc_list_it->loopc_addr, subLoop_head);
                      list<UINT64>::iterator iter;
                      for(iter=otherLoop_list.begin();iter!=otherLoop_list.end();iter++) //go through otherCaller_list
                      {
                          UINT64 a=(loopc_list_it->loopc_addr)-subLoop_head;
                          UINT64 b=(*iter)-subLoop_head;
                          if(a>b)
                          {
                              loop_t lp;
                              lp.loopc_addr=subLoop_head;
                              loopc_map[*iter].outmost_loop_list.remove(lp);


                          }
                          else
                          {
                              loop_t lp;
                              lp.loopc_addr=subLoop_head;
                              loopc_map[loopc_list_it->loopc_addr].outmost_loop_list.remove(lp);
                          }
                      }
                  }
              }
          }
    } 
}

/*
    delete invalid edges before print call graph and visualization
*/
void del_invalid_edges()
{
   del_invalid_func2func_edges();
   del_invalid_func2loop_edges();
   del_invalid_loop2loop_edges();

}

void print_call_graph()
{
  del_invalid_edges();
  std::cout<<" "<<endl;
  std::cout<<"*********************************Loop Call Graph**********************************************"<<std::endl;
  map<UINT64,func_t>::iterator func_it;
  map<UINT64,loop_t>::iterator loopc_it;
  int index=1;
  std::cout << "index     iteration     self_ins       total_ins       address      name\t" << std::endl;
  //print the func_map~
  cout<<""<<endl;
  for(func_it=func_map.begin();func_it!=func_map.end();func_it++ )
  {
    if(func_it->first==0x4003c0 ||func_it->first==0x400490 ) //not print the caller 4003c0 400490
    {
      continue;
    }
    if((!func_it->second.callee_list.empty()) ||(!func_it->second.outmost_loop_list.empty()) )
    {
       stringstream ss;
       ss<<std::hex<<func_it->first;
       printf("[%d]",index );
       cout<<"\t\t\t\t\t\t\t"<<std::hex<<func_it->first<<"\t    "<<addr2line_name(ss.str(),1)<<"\t"<<"   [caller]"<<endl;
       list<struct func>::iterator list_it;
       list<struct func> func_list = func_it->second.callee_list;
       for(list_it=func_list.begin();list_it!=func_list.end();list_it++)//print callee_list
       {
          stringstream ss;
          ss<<std::hex<<list_it->func_addr;
          cout<<"\t\t\t\t\t\t\t"<<list_it->func_addr<<"\t      "<<addr2line_name(ss.str(),1)<<"\t [callee]"<<endl;


       }

       list<struct loopc>::iterator loopc_list_it;
       list<struct loopc> func_loopc_list =func_it->second.outmost_loop_list;//print outmost_loop_list
       for(loopc_list_it=func_loopc_list.begin();loopc_list_it!=func_loopc_list.end();loopc_list_it++ )
       {
          stringstream ss;
          ss<<std::hex<<loopc_list_it->loopc_addr;
          cout<<"\t  "<<std::dec<<hash_loops[loopc_list_it->loopc_addr]->iter_num<<"\t"
              <<"\t"<<std::dec<<hash_loops[loopc_list_it->loopc_addr]->self_ins<<" ("<<std::dec<<(hash_loops[loopc_list_it->loopc_addr]->self_ins)*100/(icount)<<"%)"
              <<"    \t"<<std::dec<<hash_loops[loopc_list_it->loopc_addr]->total_ins<<" ("<<std::dec<<(hash_loops[loopc_list_it->loopc_addr]->total_ins)*100/(icount)<<"%)"
              <<"    \t"<<std::hex<<loopc_list_it->loopc_addr<<"\t      "<<addr2line_name(ss.str(),2)<<" [sub_loop]"<<endl;

       }
       cout<<"----------------------------------------------------------------------------------------"<<endl;

       //cout<<""<<endl;
       index++;
    }
    
  }
  
    //print the loopc_map~
   cout<<""<<endl;
   cout<<"----------------------------------------------------------------------------------------"<<endl;
   for(loopc_it=loopc_map.begin();loopc_it!=loopc_map.end();loopc_it++ )
   {
     if( (!loopc_it->second.outmost_loop_list.empty()) ||(!loopc_it->second.callee_list.empty()))
    {
         //DBG(cout<<"in print_call_graph():top_level_loop is "<<std::hex<<loopc_it->first<<endl;);
         stringstream ss;
         ss<<std::hex<<loopc_it->first;
         printf("[%d]",index );
         cout<<"\t  "<<std::dec<<hash_loops[loopc_it->first]->iter_num<<"     "
              <<"\t  "<<std::dec<<hash_loops[loopc_it->first]->self_ins<<" ("<<std::dec<<(hash_loops[loopc_it->first]->self_ins)*100/(icount)<<"%)"
              <<"\t  "<<std::dec<<hash_loops[loopc_it->first]->total_ins<<" ("<<std::dec<<(hash_loops[loopc_it->first]->total_ins)*100/(icount)<<"%)"
              <<"\t"<<std::hex<<loopc_it->first<<"\t    "<<addr2line_name(ss.str(),2)<<"     [top_level_loop]"<<endl;
         list<struct loopc>::iterator list_it;
         list<struct loopc> loopc_list = loopc_it->second.outmost_loop_list;
         for(list_it=loopc_list.begin();list_it!=loopc_list.end();list_it++)
         {
            //DBG(cout<<"in print_call_graph(): sub_loop is: "<<std::hex<<list_it->loopc_addr<<endl;);
            stringstream ss;
            ss<<std::hex<< list_it->loopc_addr;
            cout<<"\t  "<<std::dec<<hash_loops[list_it->loopc_addr]->iter_num<<"\t"
                <<"\t  "<<std::dec<<hash_loops[list_it->loopc_addr]->self_ins<<" ("<<std::dec<<(hash_loops[list_it->loopc_addr]->self_ins)*100/(icount)<<"%)"
                <<"\t  "<<std::dec<<hash_loops[list_it->loopc_addr]->total_ins<<" ("<<std::dec<<(hash_loops[list_it->loopc_addr]->total_ins)*100/(icount)<<"%)"
                <<"\t"<<std::hex<<list_it->loopc_addr<<"\t      "<<addr2line_name(ss.str(),2)<<" [sub_loop]"<<endl;
            
         }

         list<struct func>::iterator func_list_it;
         list<struct func> loopc_callee_list =loopc_it->second.callee_list;
         for(func_list_it=loopc_callee_list.begin();func_list_it!=loopc_callee_list.end();func_list_it++)
         {
              stringstream ss;
              ss<<std::hex<<func_list_it->func_addr;
              //cout<<"-> callee: "<<std::hex<<func_list_it->func_addr<<endl;
              cout<<"\t\t\t\t\t\t\t"<<std::hex<<func_list_it->func_addr<<"\t      "<<addr2line_name(ss.str(),1)<<"\t [callee]"<<endl;
         }
         cout<<"----------------------------------------------------------------------------------------"<<endl;
         index++;
     }

   }

   cout<<""<<endl;
}

//create call graph log.out for following visualization using Graphziv~
void print_call_graph_log_for_visualization()
{
  ofstream OutFile;
  OutFile.open(KnobOutputFile.Value().c_str());
  //std::cout<<" "<<endl;
  map<UINT64,func_t>::iterator func_it;
  map<UINT64,loop_t>::iterator loopc_it;
  //print the func_map~
  //cout<<""<<endl;
  for(func_it=func_map.begin();func_it!=func_map.end();func_it++ )
  { if(func_it->first==0x4003c0 ||func_it->first==0x400490 ) //not print the caller 4003c0 400490
    {
      continue;
    }
    if((!func_it->second.callee_list.empty()) ||(!func_it->second.outmost_loop_list.empty()) )
    {
       //cout<<"--caller: "<<std::hex<<func_it->first<<"------------"<<endl;
       list<struct func>::iterator list_it;
       list<struct func> func_list = func_it->second.callee_list;
       for(list_it=func_list.begin();list_it!=func_list.end();list_it++)
       {
          //cout<<"-> callee: "<<std::hex<<list_it->func_addr<<endl;
          //create call graph log.out for following visualization using Graphziv~
          OutFile<<std::hex<<func_it->first<<"-1"<<"-0%-\\0%"<<"--"<<std::hex<<list_it->func_addr<<"-1"<<"-0%-\\0%"<<endl;
       }

       list<struct loopc>::iterator loopc_list_it;
       list<struct loopc> func_loopc_list =func_it->second.outmost_loop_list;
       for(loopc_list_it=func_loopc_list.begin();loopc_list_it!=func_loopc_list.end();loopc_list_it++ )
       {
            //cout<<"-> sub_loop: "<<std::hex<<loopc_list_it->loopc_addr<<endl;
            OutFile<<std::hex<<func_it->first<<"-1-0%-\\0%--"<<std::hex<<loopc_list_it->loopc_addr<<"-2-"
                     <<std::dec<<hash_loops[loopc_list_it->loopc_addr]->self_ins*100/icount<<"%-"
                     <<std::dec<<hash_loops[loopc_list_it->loopc_addr]->total_ins*100/icount<<"%"
                     <<endl;
       }
       //cout<<""<<endl;
    }
  }

   //print the loopc_map~
   //cout<<""<<endl;
   for(loopc_it=loopc_map.begin();loopc_it!=loopc_map.end();loopc_it++ )
   {
     if( (!loopc_it->second.outmost_loop_list.empty()) ||(!loopc_it->second.callee_list.empty()))
    {
         //cout<<"--loop: "<<std::hex<<loopc_it->first<<"------------"<<endl;
         list<struct loopc>::iterator list_it;
         list<struct loopc> loopc_list = loopc_it->second.outmost_loop_list;
         for(list_it=loopc_list.begin();list_it!=loopc_list.end();list_it++)
         {
            //cout<<"-> sub_loop: "<<std::hex<<list_it->loopc_addr<<endl;
            OutFile<<std::hex<<loopc_it->first<<"-2-"<<std::dec<<hash_loops[loopc_it->first]->self_ins*100/icount<<"%-"<<hash_loops[loopc_it->first]->total_ins*100/icount<<"%"
                  <<"--"
                  <<std::hex<<list_it->loopc_addr<<"-2-"<<std::dec<<hash_loops[list_it->loopc_addr]->self_ins*100/icount<<"%-"<<hash_loops[list_it->loopc_addr]->total_ins*100/icount<<"%"
                  <<endl;
         }

         list<struct func>::iterator func_list_it;
         list<struct func> loopc_callee_list =loopc_it->second.callee_list;
         for(func_list_it=loopc_callee_list.begin();func_list_it!=loopc_callee_list.end();func_list_it++)
         {
              //cout<<"-> callee: "<<std::hex<<func_list_it->func_addr<<endl;
              OutFile<<std::hex<<loopc_it->first<<"-2-"<<std::dec<<hash_loops[loopc_it->first]->self_ins*100/icount<<"%-"<<hash_loops[loopc_it->first]->total_ins*100/icount<<"%"
              <<"--"
              <<std::hex<<func_list_it->func_addr<<"-1-0%-\\0%"<<endl;
         }
     }

   }

   OutFile.close();
   //cout<<""<<endl;
   cout<<"Done!generate "<<KnobOutputFile.Value().c_str()
        <<". for farther visualization,please use callgraph_v2.py script."<<endl;
   //cout<<"start to generate call graph visual picture........."<<endl;
   //system("./callgraph_v2.py addr2line test log.out callgraph.png");

}

VOID Fini(INT32 code, VOID *v)
{

  #ifdef ONLY_GET_BBL_FLOW
   print_bbl_head_tail();
  #endif


  #ifdef LOOP_PROF
    print_loops_info();
    //print_loop_children_info();
    print_call_graph();
    print_call_graph_log_for_visualization();

    // clock_t ends=clock();
    // cout <<"Running Time : "<<(double)(ends - start)*1000/ CLOCKS_PER_SEC << endl;
  #endif

}






/*cmt  main entry of program  */
int main(int argc, char *argv[])
{
    PIN_InitSymbols();
    if(PIN_Init(argc, argv))
    {
        return Usage();
    }
    //Sets the disassembly syntax to Intel format. (Destination on the left)
    PIN_SetSyntaxIntel();

    // start=clock();

    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();

    // clock_t ends=clock();
    // cout <<"Running Time : "<<(double)(ends - start)*1000/ CLOCKS_PER_SEC << endl;
    
    return 0;
}


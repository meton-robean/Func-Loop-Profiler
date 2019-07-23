//StackReg.c
#include <stdio.h>

//获取函数运行时寄存器%ebp和%esp的值
#define FETCH_SREG(_ebp, _esp)     do{\
    asm volatile( \
        "movl %%ebp, %0 \n" \
        "movl %%esp, %1 \n" \
        : "=r" (_ebp), "=r" (_esp) \
    ); \
}while(0)
//也可使用gcc扩展register void *pvEbp __asm__ ("%ebp"); register void *pvEsp __asm__ ("%esp");获取，
// pvEbp和pvEsp指针变量的值就是FETCH_SREG(_ebp, _esp)中_ebp和_esp的值

#define PRINT_ADDR(x)     printf("[%s]: &"#x" = 0x%08x\n", __FUNCTION__, &x)
#define PRINT_SREG(_ebp, _esp)     do{\
    printf("[%s]: EBP      = 0x%08x\n", __FUNCTION__, _ebp); \
    printf("[%s]: ESP      = 0x%08x\n", __FUNCTION__, _esp); \
}while(0)

void func1();
void func2();
void func3();

int tmp_esp1,tmp_esp2;
int ebp,esp;
void func1()
{

    //printf("-----------in func1-----------\n");
    FETCH_SREG(ebp, esp);
    //PRINT_SREG(ebp,esp);
    tmp_esp1=esp;
    func2();
    //func2(ebp,esp);
}

void func2()
{
    //printf("-----------in func2-----------\n");
    FETCH_SREG(ebp, esp);
    //PRINT_SREG(ebp,esp);
    tmp_esp2=esp;
    if(tmp_esp1>tmp_esp2)
    {
        printf("enter another function: %s\n",__FUNCTION__ );
    }
    func3();
}

void func3()
{
    //printf("-----------in func3-----------\n");
    tmp_esp1=tmp_esp2;
    FETCH_SREG(ebp, esp);
    tmp_esp2=esp;
    if(tmp_esp1>tmp_esp2)
    {
        printf("enter another function: %s\n",__FUNCTION__ );
    }
    //PRINT_SREG(ebp,esp);
}
int main(void)
{
    //int _ebp,_esp;
    func1();
    return 0;
}
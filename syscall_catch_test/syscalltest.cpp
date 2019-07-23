#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include "pin.H"

using namespace std;
/*
Retrieves the value of registers with the current context.
*/
void getContext(CONTEXT *ctxt)
{
	fprintf(stdout, "rax: 0x%lx\nrbx: 0x%lx\nrcx: 0x%lx\nrdx: 0x%lx\nrsp: 0x%lx\nrbp: 0x%lx\nrsi: 0x%lx\nrdi: 0x%lx\nr8: 0x%lx\nr9: 0x%lx\n",
	PIN_GetContextReg(ctxt, REG_RAX),
	PIN_GetContextReg(ctxt, REG_RBX),
	PIN_GetContextReg(ctxt, REG_RCX),
	PIN_GetContextReg(ctxt, REG_RDX),
	PIN_GetContextReg(ctxt, REG_RSP),
	PIN_GetContextReg(ctxt, REG_RBP),
	PIN_GetContextReg(ctxt, REG_RSI),
	PIN_GetContextReg(ctxt, REG_RDI),
	PIN_GetContextReg(ctxt, REG_R8),
	PIN_GetContextReg(ctxt, REG_R9));
}

/*
Retrieves the arguments of a system call.
*/
void getSyscallArgs(CONTEXT *ctxt, SYSCALL_STANDARD std)
{
	for (int i = 0; i < 5; i++) {
		ADDRINT scargs = PIN_GetSyscallArgument(ctxt, std, i);
		fprintf(stdout, "arg%d: 0x%lx\n", i, scargs);
	}
}

/*
Retrieves the arguments of the sendto and recvfrom system calls. Dereferences then increments
the bufptr pointer to grab the value at each byte in the buffer.
*/
void getSyscallArgsVal(CONTEXT *ctxt, SYSCALL_STANDARD std)
{
	ADDRINT buf = PIN_GetSyscallArgument(ctxt, std, 1);
	ADDRINT len = PIN_GetSyscallArgument(ctxt, std, 2);
	int buflen = (int)len;
	char *bufptr = (char *)buf;
	fprintf(stdout, "buffer start: 0x%lx\n", buf);
	fprintf(stdout, "length: %d\n", buflen);

	for (int i = 0; i < buflen; i++, bufptr++) {
		fprintf(stdout, "%c", *bufptr);
	}
	fprintf(stdout, "\n");
}

/*
Entry function before system call execution. Checks all system call numbers but hooks
sendto and recvfrom.
*/
void syscallEntryCallback(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
	ADDRINT scnum = PIN_GetSyscallNumber(ctxt, std);
	if(scnum==1)
	{
		cout<<"system call found! write sysnum is"<<std::dec<<scnum<<endl;
	}
	
	if (scnum == __NR_sendto)
	{
		fprintf(stdout, "systemcall sendto: %lu\n", scnum);
		getSyscallArgsVal(ctxt, std);

	} else if (scnum == __NR_recvfrom)
	{
		fprintf(stdout, "systemcall recvfrom: %lu\n", scnum);
		getSyscallArgsVal(ctxt, std);
	}
}

/*
Exit function after system call execution. Grabs the system call return value.
*/
void syscallExitCallback(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
	//ADDRINT retval = PIN_GetSyscallReturn(ctxt, std);
	//fprintf(stdout, "retval: %lu\n", retval);
}

int Usage()
{
	fprintf(stdout, "../../../pin -t obj-intel64/syscalltest.so -- sample program");
	return -1;
}

int32_t main(int32_t argc, char *argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    fprintf(stdout, "call PIN_AddSyscallEntryFunction\n");
    PIN_AddSyscallEntryFunction(&syscallEntryCallback, NULL);

    fprintf(stdout, "call PIN_AddSyscallExitFunction\n");
    PIN_AddSyscallExitFunction(&syscallExitCallback, NULL);

    fprintf(stdout, "call PIN_StartProgram()\n");
    PIN_StartProgram();

    return(0);
}
/********************************************************************
 * File: instrument.c
 *
 * Instrumentation source -- link this with your application, and
 *  then execute to build trace data file (trace.txt).
 *
 * Author: M. Tim Jones <mtj@mtjones.com>
 * Website: https://www.ibm.com/developerworks/cn/linux/l-graphvis/


 * Usage:-------------------------------------------------
 $ $ g++ -g -finstrument-functions instrument.cpp test.cpp  -o test
 $ ./test
 $ pvtrace test
 $ dot -Tpng -Nshape=box -Nfontsize=10 -Gcharset=latin1  graph.dot -o graph.png
 */

#include <stdio.h>
#include <stdlib.h>

extern "C"{
	/* Function prototypes with attributes */
	void main_constructor( void )
		__attribute__ ((no_instrument_function, constructor));

	void main_destructor( void )
		__attribute__ ((no_instrument_function, destructor));

	void __cyg_profile_func_enter( void *, void * ) 
		__attribute__ ((no_instrument_function));

	void __cyg_profile_func_exit( void *, void * )
		__attribute__ ((no_instrument_function));
};

static FILE *fp;


void main_constructor( void )
{
  fp = fopen( "trace.txt", "w" );
  if (fp == NULL) exit(-1);
}


void main_deconstructor( void )
{
  fclose( fp );
}


void __cyg_profile_func_enter( void *this_fn, void *callsite )
{
  fprintf(fp, "E%p\n", this_fn);
}


void __cyg_profile_func_exit( void *this_fn, void *callsite )
{
  fprintf(fp, "X%p\n", this_fn);
}


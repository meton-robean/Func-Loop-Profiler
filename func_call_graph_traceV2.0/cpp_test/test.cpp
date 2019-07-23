#include<stdio.h>
#include<stdlib.h>
// #include"hello.h"
#include<list>
#include<iostream>
using namespace std;


int k=3;
int x=0;
int y=0;
int z=0;
void f0()
{
   int x;
   for(int i=1;i<=2;i++)
   {
       x++;

   }

}

void f1()
{
   int x;
   for(int i=1;i<=3;i++)
   {
       x++;

   }

}

void f3()
{


   for(int i=1;i<=4;i++)
   {
        x++;

       for(int j=1;j<=3;j++)
       {
             y++;

             for(int k=1;k<=3;k++)
             {
                 z++;
                 
             }
       }

   }
}


void f2_r()
{
   k--;
   //f0();
   if(k>0)
   {
      f2_r();
   }

}

int p=3;
void f4()
{

    if(k>0)
    {
       x++;
       p--;

    }else{
      p++;
    }

}

int main(void)
{

  list<int> lista;
  // lista.push_back(1);

  cout<<"hello"<<endl;

  for(int i=1;i<=2;i++)
  {

    x++;
    y++;
  }

  for(int i=1;i<=3;i++)
  {
     printf("hello2\n");
     printf("hello4\n");
     f0();
  }
  printf("hello3\n");

  
  for(int i=0;i<3;i++)
  {
      f4();
  }
   //f0();
   f2_r();
   f3();
   // f5();
   return 0;
}

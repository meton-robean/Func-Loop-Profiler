#include<sys/types.h>  
#include<sys/stat.h>  
#include<fcntl.h>  
#include<stdio.h>  
#include<string.h>  
// //定义flags:只写，文件不存在那么就创建，文件长度戳为0  
// #define FLAGS O_WRONLY | O_CREAT | O_TRUNC  
// //创建文件的权限，用户读、写、执行、组读、执行、其他用户读、执行  
// #define MODE S_IRWXU | S_IXGRP | S_IROTH | S_IXOTH  
  
int main(void)  
{  
  // const char* pathname;  
  // int fd;//文件描述符  
  // char pn[100];  
  // printf("输入路径名，小于30个字符\n");  
  // scanf("%s", pn);  
  // printf("%s", pn);  
  // //gets(pn);//字符串的输入用gets,请记住  
  // pathname = pn;  
  // if ((fd = open(pathname, FLAGS, MODE)) == -1) {  
  //   printf("open file error");  
  //   return 0;  
  // }  
  // printf("open file successful\n");  
  // printf("fd = %d", fd);

  printf("hello,world\n");  
  return 0;  
} 
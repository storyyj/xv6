#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define STDIN 0
#define MAXLEN 512
#define MAXARG 32  // max exec arguments

int main(int argc,char *argv[])
{
    //buf用来存放标准输入接收的命令，cmd用来存放xargs后面紧跟的命令，params用来存放命令参数
    char buf[MAXLEN];
    char *cmd;
    char *params[MAXARG];
    int n;
    if(argc<2)
    {
        fprintf(2,"too few args\n");
        exit(1);
    }
    if(argc+1>MAXARG)
    {
        fprintf(2,"too many args\n");
        exit(1);
    }
    cmd=argv[1];  //get cmd
    for(int i=1;i<argc;i++)  //get parames,这里由于exec的特性，所以argv[1]中的cmd也需要放入params数组中
    {
        params[i-1]=argv[i];
    }
    //接下来开始将标准输入转换为命令行参数,因为可能有多个命令用\n换行符分隔，所以这里要用while循环，依次获取并执行
    while(1)
    {
        int index=0;
        while(1)
        {
            n=read(STDIN,&buf[index],1);  //每次只读取一个字节的数据，直到读取到\n停止
            if(n==0)
            {
                //标准输入的命令都已读取完毕，跳出循环
                break;
            }
            if(n<0)
            {
                fprintf(2,"read error\n");
                exit(1);
            }
            if(buf[index]=='\n')
            {
                break;
            }
            index++;
        }
        if(index==0)
        {
            break;
        }
        buf[index]='\0';
        params[argc-1]=buf;
        //memset(&buf,0,MAXLEN);  为什么这里加上这一句就不行了呢？
        if(fork()==0)
        {
            exec(cmd,params);
            fprintf(2,"exec error");
            exit(1);
        }
        else{
            wait(0);
        }
    }
    return 0;


}
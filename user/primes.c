#include "kernel/types.h"
#include "user/user.h"
#define MAXNUM 36

void prime(int read_p,int write_p)
{
    char nums[MAXNUM];
    //初始化一个char类型数组，存放从父进程写入管道的数据，read的第二个参数是一个指针地址，而数组首位就是一个指针，所以这里不需要&取地址符
    read(read_p,nums,MAXNUM);
    int target=0;
    //遍历整个nums数组，当前进程取出最小的还未判断的第一个数即为素数
    for(int i=0;i<MAXNUM;i++)
    {
        if(nums[i]=='0')
        {
            target=i;
            break;
        }
    }
    //如果target=0，说明所有数均已判断完毕，退出程序即可
    if(target==0)
    {
        exit(0);
    }
    printf("prime %d\n",target);
    nums[target]='1';
    //遍历数组中所有的数，把为target倍数的筛掉
    for(int i=0;i<MAXNUM;i++)
    {
        if(i%target==0)
        {
            nums[i]='1';
        }
    }
    int pid=fork();
    if(pid>0)
    {
        write(write_p,nums,MAXNUM);
        wait(0);
    }
    if(pid==0)
    {
        prime(read_p,write_p);
        wait(0);
    }

}
int main(int argc, char *argv[]) 
{
    //创建一个数组，用于判断质数
    //下标i表示要判断的数字，num[i]=1表示已经判断,num[i]=0表示未判断
    char nums[MAXNUM];
    //数组的初始化
    memset(nums,'0',sizeof(nums));
    int p[2];
    pipe(p);
    int pid=0;
    pid=fork();
    //父进程将nums数组写入管道中
    if(pid>0)
    {
        //01毋须判断是否为质数
        nums[0]='1';
        nums[1]='1';
        write(p[1],nums,MAXNUM);
        wait(0);

    }
    if(pid==0)
    {
        prime(p[0],p[1]);
        wait(0);
    }
    exit(0);
}

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

int find(char *path,char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    int findSuccess=0;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return -1;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return -1;
    }
    //接下来要获取指定文件夹下的所有文件以及文件夹的绝对路径
    strcpy(buf,path);
    p=buf+strlen(buf);
    *p++='/';
    /*循环获取该文件描述符指向的所有目录项，若和target为同名文件直接打印
    若是文件夹，则递归调用find函数
    if(de.inum==0)代表了此文件夹无文件
    continue语句的作用是跳过本次循环体中剩下尚未执行的语句，立即进行下一次的循环条件判定，可以理解为只是中止(跳过)本次循环，接着开始下一次循环。*/
    
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0)
        {
            continue;
        }
        if(!strcmp(de.name,".")||!strcmp(de.name,".."))
        {
            continue;
        }
        memmove(p,de.name,DIRSIZ);
        //在buf中加上0作为结束标志
        *(p+DIRSIZ)=0;
        if(stat(buf,&st)<0)
        {
            fprintf(2, "find: cannot stat %s\n", path);
            return -1;
        }
        switch(st.type)
        {
            case T_FILE:
                if(!strcmp(de.name,target))
                {
                    printf("%s\n",buf);
                    findSuccess=1;
                }
                break;
            case T_DIR:
                findSuccess=find(buf,target);
                break;
        }
    }
    close(fd);
    return(findSuccess);
}
int main(int argc,char *argv[])
{
    int findSuccess;
    if(argc<2||argc>3)
    {
        fprintf(2,"usage:find path target...\n");
        exit(1);
    }
    if(argc==2){
        findSuccess=find(".",argv[1]);
    }
    if(argc==3){
        findSuccess=find(argv[1],argv[2]);
    }
    if(findSuccess==0)
    {
        printf("no target can be find\n");
    }
    return 0;
}
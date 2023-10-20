#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

/*
 * create a kernel page table for the process.
 * Ukp=user kernel pagetable
 */
pagetable_t
kvminitForUkp()
{
  pagetable_t ukp = (pagetable_t) kalloc();
  memset(ukp, 0, PGSIZE);

  // uart registers
  kvmmapForUkp(UART0, UART0, PGSIZE, PTE_R | PTE_W, ukp);

  // virtio mmio disk interface
  kvmmapForUkp(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W, ukp);

  // CLINT
  kvmmapForUkp(CLINT, CLINT, 0x10000, PTE_R | PTE_W, ukp);

  // PLIC
  kvmmapForUkp(PLIC, PLIC, 0x400000, PTE_R | PTE_W, ukp);

  // map kernel text executable and read-only.
  kvmmapForUkp(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X, ukp);

  // map kernel data and the physical RAM we'll make use of.
  kvmmapForUkp((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W, ukp);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmapForUkp(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X, ukp);
  return ukp;
}

void
kvmmapForUkp(uint64 va, uint64 pa, uint64 sz, int perm, pagetable_t ukp)
{
  if(mappages(ukp, va, sz, pa, perm) != 0)
    panic("kvmmap");
}


// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");
  // walk函数为给定va找到其对应的PTE
  for(int level = 2; level > 0; level--) {

    /*#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)
    这段代码是一个宏定义，它的作用是从虚拟地址中提取出页表的索引。
    其中，PXSHIFT(level)是一个函数宏，它的作用是计算出第level级页表索引在虚拟地址中的偏移量。
    PXMASK是一个掩码，用于提取出虚拟地址中的页表索引。
    因此，这段代码的意思是：从虚拟地址中提取出第level级页表的索引。*/

    pte_t *pte = &pagetable[PX(level, va)];
    /* 根据当前level，对va进行移位和掩码操作，得到当前level页表中的对应PTE条目
    level=2时，向右移出12+2*9=30位，经掩码后得到9位level=2页表的PTE编号
    level=1时，向右移出12+9=21位，经掩码后得到9位level=1页表的PTE编号
    然后将该pte条目的后面10位flags和PTE_V按位与，
    如果该pte条目的VALID位为1，则该表达式非0，提取其物理空间，对应一个页的首地址
    说明这个页是存在的，若为0，则为该页分配一块新的空间*/
    if(*pte & PTE_V) {
      //#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
      // 将最右10位标志位移出，补充12位全0的偏移位，原44位PPN保留，得到指向下一层页表的物理地址
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // 这里的alloc为walk的传入参数，默认为1.对应PTE不存在，且alloc被置位，则为该PTE指向的下一层页表分配一页
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// mappages函数的作用是创建一个PTE条目，用来完成从虚拟地址va到物理地址pa的映射
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  //PGROUNDDOWN为往下4K地址对齐，相当于可以把一个地址转换为比此地址低的4K地址
  a = PGROUNDDOWN(va);  //算出4k对齐后的首虚拟地址和末虚拟地址
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    //walk代码可以遍历页表的PTE条目，当第三个参数alloc设置为1时，也可以用来给不存在的PTE条目建立映射并创建
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

//和mappages一模一样, 只不过不再panic remapping, 直接强制复写
int
u2kmappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  //PGROUNDDOWN为往下4K地址对齐，相当于可以把一个地址转换为比此地址低的4K地址
  a = PGROUNDDOWN(va);  //算出4k对齐后的首虚拟地址和末虚拟地址
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    //walk代码可以遍历页表的PTE条目，当第三个参数alloc设置为1时，也可以用来给不存在的PTE条目建立映射并创建
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    // if(*pte & PTE_V)
    //   panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

//递归打印三级页表的所有PTE条目,首先传入的SATP寄存器中的第一张页表的物理地址
void
vmprintRecur(pagetable_t pagetable,int level)
{
  static char* dot[]=
  {
    "",
    "..",
    ".. ..",
    ".. .. .."
  };
  for(int i=0;i<512;i++)
  {
    pte_t pte=pagetable[i];
    if(pte&PTE_V){
      printf("%s%d: pte %p pa %p\n",dot[level],i,pte,PTE2PA(pte));
      if((pte&(PTE_R|PTE_W|PTE_X))==0)
      {
        //若这三个标志位没有被置为，则说明为间接层页表，需要递归调用该函数打印下一层页表。
        uint64 child=PTE2PA(pte);  //用于接收下一层页表的物理地址，但是下面要用类型强制转换
        vmprintRecur((pagetable_t)child,level+1);
      }
    }
  }
}
void vmprint(pagetable_t pagetable){
    printf("page table %p\n", pagetable);
    vmprintRecur(pagetable, 1);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  //uint64 sz;                   // Size of process memory (bytes)
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  //父进程fork创建子进程后，一页一页的将父进程的用户页表拷贝给子进程。
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    
    //将物理内存地址pa对应的数据拷贝到新分配的内存中
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

int
u2kPageCopy(pagetable_t up,pagetable_t ukp,uint64 begin,uint64 end)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  begin=PGROUNDDOWN(begin);
  for(i=begin;i<end;i+=PGSIZE)
  {
    if((pte=walk(up,i,0))==0)
    {
      panic("u2kPageCopy:pte not exist in up");
    }
    if((*pte&PTE_V)==0)
    {
      panic("u2kPageCopy:pte not valid");
    }
    pa=PTE2PA(*pte);
    flags=PTE_FLAGS(*pte)&(~PTE_U);  //清除flags的U位
    if(u2kmappages(ukp,i,PGSIZE,pa,flags)!=0)
    {
      goto err;
    }
  }
  return 0;
err:
  uvmunmap(ukp, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
}

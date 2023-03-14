// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  long long pageCount;
} kmem[NCPU];


static struct spinlock dynamiclock;

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
    kmem[i].pageCount = 0;
    kmem[i].freelist = 0;
  }
  initlock(&dynamiclock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// success 1 fail -1
static int dynamicAlloc(int cpuid) {
  static int incount = 0;
  incount ++;
  if (!holding(&kmem[cpuid].lock)) {
    panic("error call dynamic alloc! no lock\n");
  }
  if (kmem[cpuid].pageCount != 0) {
    printf("error pagecount : %d, incount : %d, cpuid : %d\n", kmem[cpuid].pageCount, incount, cpuid);
    panic("error call dynamic alloc! pageCount not zero!!\n");
  }
  long long maxPageCount = 0;
  int maxCpuId = -1;
  for (int i = 0; i < NCPU; i++) {
    if (i == cpuid) continue ;
    acquire(&kmem[i].lock);
    if (kmem[i].pageCount > maxPageCount) {
      if (maxCpuId != -1) {
        release(&kmem[maxCpuId].lock);
      }
      maxPageCount = kmem[i].pageCount;
      maxCpuId = i;
    } else 
      release(&kmem[i].lock);
  }
  if (maxPageCount == 0) return -1;
  struct run *r;
  r = kmem[maxCpuId].freelist;
  int i = 1;
  for (int I = maxPageCount / 2; i <= I; i++) {
    r = r->next;
  }
  kmem[cpuid].freelist = kmem[maxCpuId].freelist;
  kmem[maxCpuId].freelist = r->next;
  r->next = 0;
  kmem[cpuid].pageCount = i;
  kmem[maxCpuId].pageCount -= kmem[cpuid].pageCount;
  //printf("queue %d alloc %d pages to queue %d, res : %d pages\n", maxCpuId, kmem[cpuid].pageCount, cpuid, kmem[maxCpuId].pageCount);
  release(&kmem[maxCpuId].lock);
  return 1;
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpu = cpuid();
  pop_off();

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  kmem[cpu].pageCount ++;
  release(&kmem[cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu = cpuid();
  pop_off();

  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if (!r && dynamicAlloc(cpu) == 1) {
    r = kmem[cpu].freelist;
  }
  if(r) {
    kmem[cpu].freelist = r->next;
    kmem[cpu].pageCount --;
  }
    
  release(&kmem[cpu].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


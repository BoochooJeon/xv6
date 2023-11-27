// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define MAXENTRY 57334 

extern int PID[];
extern uint VPN[];
extern pte_t PTE_XV6[];

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// hash function
uint fnv1a_hash1(int pid, uint vpn) {
    const uint32_t fnv_prime = 0x811C9DC5;
    uint32_t hash = 0x01000193;

    // PID와 VPN을 결합하여 해시 계산
    hash = (hash ^ (uint32_t)pid) * fnv_prime;
    hash = (hash ^ (uint32_t)vpn) * fnv_prime;

    return (hash % MAXENTRY);
}


// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    //kfree(p);
	PID[(int)(V2P(p))/4096] = -1; //If PID[i] is -1, the physical frame i is freespace.
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

void kfree(int pid, char *v){

  uint kv, idx;
  //TODO: Fill the code that supports kfree

  //1. Find the corresponding physical address for given pid and VA
  uint vpn = VTX(v);
  idx = fnv1a_hash1(pid, vpn);

  //2. Initialize the PID[idx], VPN[idx], and PTE_XV6[idx]
  PID[idx] = -1;
  VPN[idx] = 0;
  PTE_XV6[idx] = 0; //  pa찾은다음에 PTE_P 마스킹 작업 진행해야함

  kv = idx * PGSIZE ;
  // 4MB 공간 남겨야함
  // kfree에 들어오는 가상주소, 물리주소
  //3. For memset, Convert the physical address for free to kernel's virtual address by using P2V macro

  memset(P2V((char *)kv), 1, PGSIZE); //TODO: You must perform memset for P2V(physical address);
}


//질문모음 
// 1. 할당 자체는 어케하는것인가? mem변수로 해야하나???
// 2. PTE_XV6[idx] = pa | perm \ PTE-P 는 proj Ppt에 나와있는 PTE에 해당하는 것인가?
// 3. PTE_KERN 은 어디에 쓰는것인가? // 
// 4. XV6_KERN 변수는 무엇인가? PTE_KERN의 오타인가? 
// 5. Kalloc이 P2V로 반환해서 주는데, isKernel = true인 경우만 그렇게 처리해줘야하는게 아닌가??? 
// 6. Kalloc freespace를 해시함수로 찾아야 하는이유? 해시함수로 Idx 찾고 PTE_XV6에 접근해서 PTE_P 여부 확인 맞나?
char*
kalloc(int pid, char *v)// 여기서 v는 va? pa?, va인듯 
{

  int idx;
 
  if(kmem.use_lock)
    acquire(&kmem.lock);

  //TODO: Fill the code that supports kalloc
  //1. Find the freespace by hash function // 왜 해시function으로 찾아야할지 이해가 안됨
  uint vpn = VTX(v);
  idx = fnv1a_hash1(pid, vpn);

  if((int)v == -1) {
    return (char*)P2V(idx * PGSIZE);
  }

  PID[idx] = pid;
  VPN[idx] = vpn;
  PTE_XV6[idx] = 0; //TODO: 적절한 값 넣기, collition handling

  bool is_alloc = false;
  if(!is_alloc) {
    return (char*)P2V(idx * PGSIZE);
  }

  //2. Consider the case that v is -1, which means that the caller of kalloc is kernel so the virtual address is decided by the allocated physical address (P2V) 
  //3. Update the value of PID[idx] and VPN[idx] (Do not update the PTE_XV6[idx] in this code!)
  //4. Return (char*)P2V(physical address), if there is no free space, return 0
  if(kmem.use_lock)
    release(&kmem.lock);
  return 0;
}




/*
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}
*/

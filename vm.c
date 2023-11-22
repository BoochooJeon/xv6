#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
extern uint PTE_XV6[];
extern uint PTE_KERN[];
extern int PID[];
extern uint VPN[];
pde_t *kpgdir;  // for use in scheduler()
static pde_t *null_pgdir; // used to force page faults

int first_setup = 0;

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.

static pte_t *
ittraverse(int pid, pde_t *pgdir, const void *va, int alloc) //You don't have to consider the pgdir argument
{
  //NOTE: 2. vm.c에서 ittraverse() 함수를 구현할 때, va가 KERNBASE보다 크다면, &XV6_KERN[(uint)(V2P(va)/PGSIZE)] 를 return 해주세요.
  // ittraverse() 코드에서 int alloc 변수에 대해서는 고려 안하셔도 됩니다.
  //PPT에서 memtest를 수행하라고 나와있는데, memtest대신 console에서 ls를 입력할때 hash collision이 발생하는 것을 캡쳐해서 보여주세요. PPT에서 memtest의 result format과 동일하게 출력해주셔야 합니다.
	uint idx; 
	//TODO: File the code that returns corresponding PTE_XV6[idx]'s address for given pid and VA
	//1. Handle two case: the VA is over KERNBASE or not.
  uint *va_ptr = (uint *)va;
  if(*va_ptr > KERNBASE) { // va가 맞는지 잘 모르겠음. *va여야하지않나?
    return &PTE_KERN[(uint)(V2P(va)/PGSIZE)]; 
  } else {
    // user va에 대해서 Page Table을 탐색해서 PTE를 리턴함
  }
	//2. For former case, return &PTE_KERN[(uint)V2P(physical address)];
	//3. For latter case, find the phyiscal address for given pid and va using inverted page table, and return &PTE_XV6[idx]
}

static pte_t *
k_walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;
  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc(0,(char*)-1)) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U; // V2P 는 저장용
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(int pid, int is_kernel, pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if (is_kernel)
      pte = k_walkpgdir(pgdir, a, 1);
    else
      pte = ittraverse(pid, pgdir, a, 1);
    if(pte == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(int is_kernel)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc(0,(char*)-1)) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  if (first_setup == 1 && is_kernel == 0) {
	  return pgdir;
  }
  if (first_setup == 0 && is_kernel == 0) first_setup = 1;
  
  for(k =  ; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(0, is_kernel, pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(0, pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm(1);
  null_pgdir = setupkvm(1); // XXX: Hmm
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(null_pgdir));  // switch to null pgdir to force a page fault
  //lcr3(V2P(kpgdir));
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc(1, (char*)0);
  memset(mem, 0, PGSIZE);
  mappages(1, 0, pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(int pid, pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = ittraverse(pid, pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(int pid, pde_t *pgdir, uint oldsz, uint newsz, uint flags)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc(pid, (char*)a);
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pid, pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pid, 0, pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U|flags) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pid, pgdir, newsz, oldsz);
      kfree(pid, mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.

int
deallocuvm(int pid, pde_t *pgdir, uint oldsz, uint newsz){
  pte_t *pte;
  uint a, pa;
  if(newsz >= oldsz)
    return oldsz;
  a = PGROUNDUP(newsz);
  //TODO: File the code that free the allocated pages by users
  //For range in (a <= va < oldsz), if there are some pages that the process allocates, call kfree(pid, v)
  return newsz; 
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(int pid, pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pid, pgdir, KERNBASE, 0);
  kfree(0, (char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(int pid, pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = ittraverse(pid, pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(int pid, int old_pid, pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm(0)) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = ittraverse(old_pid, pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc(pid,(void*)i)) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(pid, 0, d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(pid, mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(pid, d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(int pid, pde_t *pgdir, char *uva)
{
  pte_t *pte;
  pte = ittraverse(pid, pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(int pid, pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pid, pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

/*
 * Page fault handler can be called while console lock is acquired,
 * and when cprintf() is re-entered, the kernel panics.
 *
 * The LOG macro should be used while performing early debugging only
 * and it'll most likely cause a crash during normal operations.
 */
#define LOG 0
#define clprintf(...) if (LOG) cprintf(__VA_ARGS__)

// Returns physical page address from virtual address
static uint __virt_to_phys(int pid, int shadow, pde_t *pgdir, struct proc *proc, uint va)
{
  uint pa;
  pte_t *pte;
  
  if (shadow == 1){
	pde_t *pde = &pgdir[PDX(va)];
	pte_t *pgtable = (pte_t*)P2V(PTE_ADDR(*pde)); // P2V 읽을때 사옹
	pa = PTE_ADDR(pgtable[PTX(va)]) | OWP(va);
	return pa;
  } 
  //TODO: Fill the code that converts VA to PA for given pid
  //Hint: Use ittraverse!
  return pa;
}

static int __get_flags(int pid, pde_t *pgdir, struct proc *proc, uint va){
  //This function is used for obtaining flags for given va and pid
  uint flags;
  pte_t *pte;
  //TODO: Fill the code that gets flags in PTE_XV6[idx] 
  //Hint: use the ittraverse and macro!
  return flags;
}
// Same as __virt_to_phys(), but with extra log
static uint virt_to_phys(int pid, int shadow, const char *log, pde_t *pgdir, struct proc *proc, uint va)
{
  uint pa = __virt_to_phys(pid, shadow, pgdir, proc, va);

  clprintf("virt_to_phys: translated \"%s\"(%d)'s VA 0x%x to PA 0x%x (%s)\n", proc->name, proc->pid, va, pa, log);

  return pa;
}

void pagefault(void)
{
  struct proc *proc;
  pde_t *pde;
  pte_t *pgtab;
  uint va, flags = 0;

  clprintf("pagefault++\n");

  proc = myproc();

  // Get the faulting virtual address
  va = rcr2();
  clprintf("Page fault by process \"%s\" (pid: %d) at 0x%x\n", proc->name, proc->pid, va);

  // Print stock pgdir's translation result
  virt_to_phys(proc->pid, 0, "pgdir", proc->pgdir, proc, va);
  flags = __get_flags(proc->pid, proc->pgdir, proc, va);
  // Save PTE flags
  /*
  pde = &proc->pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
    flags = PTE_FLAGS(pgtab[PTX(va)]);
  }
  */

  // Remove existing shadow_pgdir mapping
  if (proc->last_va != 0 && proc->last_va != va) {
    pde = &proc->shadow_pgdir[PDX(proc->last_va)];
    if (*pde & PTE_P) {
      pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
      if (pgtab[PTX(proc->last_va)] & PTE_SBRK) {
        clprintf("\rClearing 0x%x", proc->last_va);
        pgtab[PTX(proc->last_va)] = 0;
      }
    }
  }

  // Map pgdir's page address to shadow_pgdir's page table
  pde = &proc->shadow_pgdir[PDX(va)];
  if (*pde & PTE_P) {
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    pgtab = (pte_t*)kalloc(0, (char*)-1);
    clprintf("Allocated pgtable at 0x%x\n", pgtab);
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  pgtab[PTX(va)] = PTE_ADDR(__virt_to_phys(proc->pid, 0, proc->pgdir, proc, va)) | flags;

  /*
   * Print shadow pgdir's translation result,
   * this should match with stock pgdir's translation result above!
   */
  virt_to_phys(proc->pid, 1, "shadow_pgdir", proc->shadow_pgdir, proc, va);
  
  proc->last_va = va;
  proc->page_faults++;

  // Load a bogus pgdir to force a TLB flush
  lcr3(V2P(null_pgdir));
  // Switch to our shadow pgdir
  lcr3(V2P(proc->shadow_pgdir));

  clprintf("pagefault--\n");
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.


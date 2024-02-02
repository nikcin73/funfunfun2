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
} kmem;

struct frame {
    enum {
        EMPTY, IDLE, KERNEL, USER
    } state;
    pte_t *pte;
    uint64 history;
};
#define NUM_PAGES 4096
struct frame frames[NUM_PAGES];

void
framesinit() {
    for (int i = 0; i < NUM_PAGES ;
    i++) {
        frames[i].state = KERNEL;
        frames[i].history = 0;
        frames[i].pte = 0;
    }
}

void *
F2PA(int frame) {
    return (void *) ((uint64) end + ((uint64) frame << 12));
}

int
PA2F(void *pa) {
    return (int) (((uint64) pa - (uint64) end) >> 12);
}


void
shiftrefbits() {
    for (int i = 0; i < NUM_PAGES ; i++) {
        if (frames[i].state == USER || frames[i].state==KERNEL) {
            pte_t *pte = frames[i].pte;
            if (!pte) continue;
            frames[i].history = (frames[i].history >> 1) | ((*pte & PTE_A) << 57);
            *pte &= ~PTE_A;
        }
    }
}


int victimstart=0;
int
getvictim() {
    int i=victimstart;
    int victim = -1;
    uint64 history = ~(0UL);
    do{
        if (frames[i].state == IDLE){
            printf("frame %d is idle\n",i);
            victim=i;
            break;
        }
        if (frames[i].state == USER) {
            if (victim == -1) victim = i;
            if (frames[i].history < history) {
                history = frames[i].history ;
                victim = i;
            }
        }
        i=(i+1)%NUM_PAGES;
    }while(i!=victimstart);
    if(victim>-1 && frames[victim].state!=IDLE)
        printf("frame %d has lowest history:%U\n",victim,frames[i].history);
    if(victim==victimstart) victimstart=(victimstart+1)%NUM_PAGES;
    return victim;
}

pte_t *getpte(int num) {
    return frames[num].pte;
}

void setframestate(int num, char s) {
    switch (s) {
        case 'K':
        case 'k':
            frames[num].state = KERNEL;
            break;
        case 'U':
        case 'u':
            frames[num].state = USER;
            break;
        case 'E':
        case 'e':
            frames[num].state = EMPTY;
            break;
        case 'I':
        case 'i':
            frames[num].state = IDLE;
            break;
        default:
            panic("setframestate");
    }
}

void setframepte(int num, pte_t *pte) {
    frames[num].pte = pte;
}

void
kinit() {
    framesinit();
    initlock(&kmem.lock, "kmem");
    freerange(end, (void *) PHYSTOP);
    printf("kinit: end=%U\n",end);
}

void
freerange(void *pa_start, void *pa_end) {
    char *p;
    p = (char *) PGROUNDUP((uint64) pa_start);
    for (; p + PGSIZE <= (char *) pa_end; p += PGSIZE){
        kfree(p);
    }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa) {
    struct run *r;

    if (((uint64) pa % PGSIZE) != 0 || (char *) pa < end || (uint64) pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 0, PGSIZE);

    r = (struct run *) pa;

    acquire(&kmem.lock);
    int num = PA2F(r);
    //printf("FREEING FRAME %d (&%U)\n",num,r);
    frames[num].history = 0;
    frames[num].state = EMPTY;
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}



// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

void *
kalloc(void) {
    struct run *r;
    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
        int num= PA2F(r);
        frames[num].history=~(0UL);
        frames[num].state=KERNEL;
    } else {
        int victim = getvictim();
        if (victim == -1) panic("kalloc: no idle/user pages in memory");
        uint32 entry = acquireentry();
        if (entry == -1) panic("kalloc: no free disk entries");
        pte_t *oldpte = frames[victim].pte;
        frames[victim].state = KERNEL;
        release(&kmem.lock);
        printf("&pte=%U swapping &%U (frame %d) to entry %u\n", oldpte, F2PA(victim), victim, entry);
        swapout((void *) F2PA(victim), entry);
        acquire(&kmem.lock);
        if (oldpte) {
            *oldpte &= ~PTE_V;
            *oldpte &= ~ENTRYMASK;
            *oldpte |= (entry << 10) | PTE_DISK;
        }
        r = (struct run *) F2PA(victim);
        frames[victim].history=~(0UL);
    }
    release(&kmem.lock);
    memset((char *) r, 0, PGSIZE); // fill with junk
    return (void *) r;
}

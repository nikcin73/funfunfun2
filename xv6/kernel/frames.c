#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

struct frame {
    char swap;
    pte_t *pte;
    pagetable_t pagetable;
    uint64 history;
};

struct frame frames[NFRAME];

struct spinlock framelock;

void framesinit() {
    initlock(&framelock, "framelock");
    for (int i = 0; i < NFRAME; i++) {
        frames[i].swap = 0;
        frames[i].history = 0;
        frames[i].pte = 0;
        frames[i].pagetable = 0;
    }
}

void
shifthistory() {
    for (int i = 0; i < NFRAME; i++) {
        pte_t *pte;
        if(!frames[i].swap || !(pte=frames[i].pte)) continue;
        frames[i].history = (frames[i].history >> 1) | ((*pte & PTE_A) << 57);
        *pte &= ~PTE_A;
    }
}

void setframeswap(int num, char s) {
    acquire(&framelock);
    if (num < 0 || num >= NFRAME) panic("setframeswap: invalid frame number");
    frames[num].swap = s;
    release(&framelock);
}

void setframepte(int num, pte_t *pte) {
    acquire(&framelock);
    if (num < 0 || num >= NFRAME) panic("setframepte: invalid frame number");
    frames[num].pte = pte;
    release(&framelock);
}

void setframetable(int num, void* pagetable) {
    acquire(&framelock);
    if (num < 0 || num >= NFRAME) panic("setframetable: invalid frame number");
    frames[num].pagetable = (pagetable_t)pagetable;
    release(&framelock);
}

char getframeswap(int num) {
    acquire(&framelock);
    if (num < 0 || num >= NFRAME) panic("getframeswap: invalid frame number");
    char swap=frames[num].swap;
    release(&framelock);
    return swap;
}

pte_t *getframepte(int num) {
    acquire(&framelock);
    if (num < 0 || num >= NFRAME) panic("getframepte: invalid frame number");
    pte_t* pte=frames[num].pte;
    release(&framelock);
    return pte;
}

void* getframetable(int num) {
    acquire(&framelock);
    if (num < 0 || num >= NFRAME) panic("getframetable: invalid frame number");
    pagetable_t pagetable=frames[num].pagetable;
    release(&framelock);
    return (void*)pagetable;
}

void freeframe(int num,char swap){
    acquire(&framelock);
    frames[num].pte=0;
    frames[num].pagetable=0;
    frames[num].history=0;
    frames[num].swap=swap;
    release(&framelock);
}

int getvictim() {
    int victim = -1;
    uint64 history = ~(0UL);
    for (int i = 0; i < NFRAME; i++) {
        if (frames[i].swap) {
            if (frames[i].swap == 2) {
                victim = i;
                break;
            }
            if (frames[i].history <= history) {
                history = frames[i].history;
                victim = i;
            }
        }
    }
    return victim;
}

void fillframe(int num) {

}

void *swapout() {
    acquire(&framelock);
    int victim = getvictim();
    if (victim == -1) {
        release(&framelock);
        return 0;
    }
    char oldswap = frames[victim].swap;
    frames[victim].swap = 0;
    release(&framelock);
    int entry = acquireentry();
    if (entry == -1) {
        frames[victim].swap = oldswap;
        return 0;
    }
    pte_t *oldpte = frames[victim].pte;
    frames[victim].pte = 0;
    frames[victim].pagetable = 0;
    frames[victim].history = ~(0UL);
    void *pa = F2PA(victim);
    swapdiskwrite(pa, entry);
    if(oldpte){
        *oldpte &= ~PTE_V;
        *oldpte &= ~ENTRYMASK;
        *oldpte |= ((uint32) entry << 10) | PTE_DISK;
    }
    sfence_vma();
    return pa;
}

//swapin: returns 1 if successful, 0 otherwise
int swapin(uint64 pagetable, uint64 va) {
    pte_t *pte = walk((pagetable_t) pagetable, va, 0);
    if (!pte || (*pte & PTE_DISK) == 0) {
        return 0;
    }
    void *pa = kalloc();
    if (!pa) {
        return 0;
    }
    int num = PA2F(pa);
    frames[num].pte = pte;
    int diskentry = getdiskentry(pte);
    swapdiskread(pa, diskentry);
    releaseentry(diskentry);
    *pte &= ~PTE_DISK;
    *pte &= ~ENTRYMASK;
    *pte |= PA2PTE((uint64) pa) | PTE_V;
    sfence_vma();
    return 1;
}
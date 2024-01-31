#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#define NUM_PAGES 4096
//PHYSICAL MEMORY=16MB, PAGE SIZE=4KB => PHYSICAL MEMORY=4K PAGES
uint64 refhistory[NUM_PAGES];
struct frame {
    enum {
        EMPTY, IDLE, KERNEL, USER
    } state;
    pte_t *pte;
};
struct frame frames[NUM_PAGES];

void
framesinit() {
    for (int i = 0; i < NUM_PAGES; i++) {
        frames[i].state = EMPTY;
        refhistory[i] = 0;
    }
}

void
shiftrefbits() {
    for (int i = 0; i < NUM_PAGES; i++) {
        if (frames[i].state == USER) {
            pte_t *pte = frames[i].pte;
            refhistory[i] = (refhistory[i] >> 1) | ((*pte & PTE_A) << 57);
            *pte &= ~PTE_A;
        }
    }
}

int
getvictim() {
    int victim = -1;
    uint64 history = ~(0UL);
    for (int i = 0; i < NUM_PAGES; i++) {
        if (frames[i].state == IDLE)
            return i;
        if (frames[i].state == USER) {
            if (refhistory[i] <= history) {
                history = refhistory[i];
                victim = i;
            }
        }
    }
    return victim;
}

pte_t *getpte(int num) {
    return frames[num].pte;
}

void setframestate(int num,char s){
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
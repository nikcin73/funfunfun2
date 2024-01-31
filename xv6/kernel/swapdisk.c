#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#define NUM_ENTRY ((1L<<14)/4)

//Every page in VM is placed in one swapdisk entry
//Swapdisk block is 4 times smaller than page, so one entry takes 4 successive blocks

const int DISKBLOCK=1L<<10;

struct {
    struct spinlock lock;
    char valid[NUM_ENTRY];
    int start;
} swapdisk;

void swapdiskinit(){
    initlock(&swapdisk.lock,"swapdisk");
    for(int i=0;i<NUM_ENTRY;i++) swapdisk.valid[i]=0;
    swapdisk.start=0;
}

uint32 acquireentry(){
    acquire(&swapdisk.lock);
    uint32 entry=swapdisk.start;
    do{
        if(!swapdisk.valid[entry]) break;
        entry=(entry+1)%NUM_ENTRY;
    }while(entry!=swapdisk.start);
    if(entry==swapdisk.start && swapdisk.valid[entry]) panic("swapdisk: no more free entries");
    swapdisk.valid[entry]=1;
    swapdisk.start=(entry+1)%NUM_ENTRY;
    release(&swapdisk.lock);
    return entry;
}

void releaseentry(uint32 entry){
    acquire(&swapdisk.lock);
    if(!swapdisk.valid[entry]) panic("swapdisk: tried to release free entry");
    swapdisk.valid[entry]=0;
    release(&swapdisk.lock);
}

void swapout(void* addr,uint32 entry){
    for(int i=0;i<4;i++){
        write_block(entry*4+i,(uchar*)((uint64)addr+i*DISKBLOCK),0);
    }
}

void swapin(void* addr,uint32 entry){
    for(int i=0;i<4;i++){
        read_block(entry*4+i,(uchar*)((uint64)addr+i*DISKBLOCK),0);
    }
}


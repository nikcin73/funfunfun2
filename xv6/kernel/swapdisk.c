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
} swapdisk;

void swapdiskinit(){
    initlock(&swapdisk.lock,"swapdisk");
    for(int i=0;i<NUM_ENTRY;i++) swapdisk.valid[i]=0;
}

int acquireentry(){
    acquire(&swapdisk.lock);
    int entry;
    for(entry=0;entry<NUM_ENTRY;entry++){
        if(!swapdisk.valid[entry]) break;
    }
    if(entry==NUM_ENTRY){
        release(&swapdisk.lock);
        return -1;
    }
    swapdisk.valid[entry]=1;
    release(&swapdisk.lock);
    return entry;
}

void releaseentry(int entry){
    acquire(&swapdisk.lock);
    if(!swapdisk.valid[entry]) panic("swapdisk: tried to release free entry");
    swapdisk.valid[entry]=0;
    release(&swapdisk.lock);
}

void swapdiskwrite(void* addr,int entry){
    for(int i=0;i<4;i++){
        write_block(entry*4+i,(uchar*)((uint64)addr+i*DISKBLOCK),0);
    }
}

void swapdiskread(void* addr,int entry){
    for(int i=0;i<4;i++){
        read_block(entry*4+i,(uchar*)((uint64)addr+i*DISKBLOCK),0);
    }
}


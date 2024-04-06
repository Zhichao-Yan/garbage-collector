#ifndef GC_H
#define GC_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>

enum flag{
  GC_MARK = 0x01,
  GC_ROOT = 0x02,
  GC_LEAF = 0x04
};

typedef struct gc_ptr{
  void *ptr;    // ptr to the allocation
  int flags;    // indicate that the allocation is a GC_ROOT or GC_LEAF or NULL
  size_t size;  // size of allocation
  size_t hash;  // store the hash value(the location of the slot where it should be at start)
  void (*dtor)(void*);  // destructor function
}gc_ptr_t;

typedef struct gc{
  void *bottom;               // stack bottom
  int paused;                 // paused or resume the garbage collector
  uintptr_t min_ptr, max_ptr; // range of heap(min_ptr:lowest address max_ptr:highest address)
 
  double sweep_factor;        // factor controls the threshold
  size_t items_cnt2;          // threshold of items number controls automatically sweeping

  gc_ptr_t *items;            // table list of allocations
  size_t slots_cnt;           // number of slots which equals to length of items
  double load_factor;         // items_cnt1,load_factor ==> slots_cnt
  size_t items_cnt1;          // number of gc_ptr_t items(allocations allocated) 

  size_t frees_cnt;           // number of allocation(unmarked) waiting to be freed
  gc_ptr_t *frees;            // allocations needed be freed 
}gc_t;



void gc_start(gc_t *gc, void *stk);
void gc_stop(gc_t *gc);

void gc_sweep(gc_t *gc);
void gc_run(gc_t *gc);

void *gc_alloc(gc_t *gc, size_t size);
void *gc_alloc_opt(gc_t *gc, size_t size, int flags, void (*dtor)(void *));
void *gc_calloc(gc_t *gc, size_t num, size_t size);
void *gc_calloc_opt(gc_t *gc, size_t num, size_t size, int flags, void(*dtor)(void*));
void *gc_realloc(gc_t *gc, void *ptr, size_t size);

void gc_pause(gc_t *gc);
void gc_resume(gc_t *gc);
void gc_free(gc_t *gc, void *ptr);

void gc_set_flags(gc_t *gc, void *ptr, int flags);
int gc_get_flags(gc_t *gc, void *ptr);
void gc_set_dtor(gc_t *gc, void *ptr, void (*dtor)(void *));
void (*gc_get_dtor(gc_t *gc, void *ptr))(void *);
size_t gc_get_size(gc_t *gc, void *ptr);

#endif
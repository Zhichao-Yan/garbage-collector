#include "gc.h"
#define GC_PRIMES_COUNT 24

static const size_t gc_primes[GC_PRIMES_COUNT] = {
    0, 1, 5, 11,
    23, 53, 101, 197,
    389, 683, 1259, 2417,
    4733, 9371, 18617, 37097,
    74093, 148073, 296099, 592019,
    1100009, 2200013, 4400021, 8800019};

/* gc starts */
void gc_start(gc_t *gc, void *stk)
{
    gc->bottom = stk;
    gc->paused = 0;
    gc->items_cnt1 = 0;
    gc->slots_cnt = 0;
    gc->items_cnt2 = 0;
    gc->frees_cnt = 0;
    gc->max_ptr = 0;
    gc->items = NULL;
    gc->frees = NULL;
    gc->min_ptr = UINTPTR_MAX;
    gc->load_factor = 0.9;
    gc->sweep_factor = 0.5;
}

/* sweep operation */
void gc_sweep(gc_t *gc)
{
    if (gc->items_cnt1 == 0)
    {
        return;
    }
    // sum up the total number of allocation needed to be freed
    gc->frees_cnt = 0;
    for (size_t i = 0; i < gc->slots_cnt; i++)
    {
        if (gc->items[i].hash == 0)
        {
            continue;
        }
        if (gc->items[i].flags & GC_MARK)
        {
            continue;
        }
        if (gc->items[i].flags & GC_ROOT)
        {
            continue;
        }
        gc->frees_cnt++;
    }
    gc->frees = malloc(sizeof(gc_ptr_t) * gc->frees_cnt);
    if (gc->frees == NULL) // if failed reallocing,then
    {
        return;
    }
    size_t k = 0;
    for (size_t i = 0; i < gc->slots_cnt; i++)
    {
        if (gc->items[i].hash == 0)
        {
            i++;
            continue;
        }
        if (gc->items[i].flags & GC_MARK)
        {
            i++;
            continue;
        }
        if (gc->items[i].flags & GC_ROOT)
        {
            i++;
            continue;
        }

        gc->frees[k++] = gc->items[i];
        memset(&gc->items[i], 0, sizeof(gc_ptr_t)); // clear items[i]
        /* move back slots forward */
        size_t j = i;
        while (1)
        {
            size_t index = (j + 1) % gc->slots_cnt;
            size_t h = gc->items[index].hash;
            if (h != 0 && gc_offset(gc, index, h) > 0)
            {
                memcpy(&gc->items[j], &gc->items[index], sizeof(gc_ptr_t));
                memset(&gc->items[index], 0, sizeof(gc_ptr_t));
                j = index;
            }
            else
                break;
        }
        gc->items_cnt1--; // decrease the number of allocation as free it
    }
    // turn all the marked into unmarked
    for (size_t i = 0; i < gc->slots_cnt; i++)
    {
        if (gc->items[i].hash == 0)
        {
            continue;
        }
        if (gc->items[i].flags & GC_MARK)
        {
            gc->items[i].flags &= ~GC_MARK;
        }
    }
    // since decrese the items_cnt1,try to shrink hashtable
    gc_adjust_slots(gc);
    gc->items_cnt2 = gc->items_cnt1 + (size_t)(gc->items_cnt1 * gc->sweep_factor) + 1;
    // destruct object before freeing it
    for (size_t i = 0; i < gc->frees_cnt; i++)
    {
        if (gc->frees[i].ptr)
        {
            if (gc->frees[i].dtor)
            {
                gc->frees[i].dtor(gc->frees[i].ptr);
            }
            free(gc->frees[i].ptr);
        }
    }
    free(gc->frees);
    gc->frees = NULL;
    gc->frees_cnt = 0;
}

/* mark allocation pointed by ptr */
static void gc_mark_ptr(gc_t *gc, void *ptr)
{
    // not between the range,so ptr isn't pointing to an allocation
    if ((uintptr_t)ptr < gc->min_ptr || (uintptr_t)ptr > gc->max_ptr)
    {
        return;
    }
    size_t i = gc_hash(ptr) % gc->slots_cnt; // the index where the ptr shoud be in hashtable
    size_t j = 0;
    while (1)
    {
        size_t h = gc->items[i].hash;
        if (h == 0 || j > gc_offset(gc, i, h)) // means there is no pointer == ptr pointing to an allocation
        {
            return;
        }
        // hashvalue != 0 && j = gc_offset(gc, i, h)
        if (ptr == gc->items[i].ptr) // finally find the items
        {
            if (gc->items[i].flags & GC_MARK) // already marked,then return
            {
                break;
            }
            else
            {
                gc->items[i].flags |= GC_MARK;    // if not,then mark it
                if (gc->items[i].flags & GC_LEAF) // it's a leaf, so there is no need to scan it
                {
                    break;
                }
                // scan the allocation
                for (size_t k = 0; k < gc->items[i].size / sizeof(void *); k++)
                {
                    void **p = gc->items[i].ptr;
                    gc_mark_ptr(gc, p[k]);
                }
                break;
            }
        }
        else
        {
            // other pointers have the same hash value as ptr but with different address
        }
        i = (i + 1) % gc->slots_cnt; // keep searching in slots
        j++;                         // means the search distance
    }
    return;
}
/* mark from stack */
static void gc_mark_stack(gc_t *gc)
{
    int x;
    void *bottom = gc->bottom;
    void *top = &x; // acquire the stack top pointer at the point of x definition
    if (bottom < top)
    {
        for (void *p = top; p >= bottom; p = (void *)((uintptr_t)p - sizeof(void *)))
        {
            gc_mark_ptr(gc, p);
        }
    }
    if (bottom > top)
    {
        for (void *p = top; p <= bottom; p = (void *)((uintptr_t)p + sizeof(void *)))
        {
            gc_mark_ptr(gc, p);
        }
    }
    return;
}
/* mark from heap */
static void gc_mark_heap(gc_t *gc)
{
    if (gc->items_cnt1 == 0) // no allocations need to be marked
    {
        return;
    }
    for (size_t i = 0; i < gc->slots_cnt; i++)
    {
        if (gc->items[i].hash == 0) // empty
        {
            continue;
        }
        if (gc->items[i].flags & GC_MARK) // already marked,so continue the next one
        {
            continue;
        }
        if (gc->items[i].flags & GC_ROOT) // if it is a garbage collection root
        {
            gc->items[i].flags |= GC_MARK;    // mark this allocation
            if (gc->items[i].flags & GC_LEAF) // it is a Leaf, no need to scan
            {
                continue;
            }
            // split items[i] into small chunks
            // and assume them as pointer pointed to other allocations
            for (size_t k = 0; k < gc->items[i].size / sizeof(void *); k++)
            {
                void **p = gc->items[i].ptr;
                gc_mark_ptr(gc, p[k]);
            }
            continue;
        }
    }
}
/* mark operation  */
static void gc_mark(gc_t *gc)
{
    void (*volatile mark_heap)(gc_t *) = gc_mark_heap;
    mark_heap(gc);
    jmp_buf env;                      // jmp_buf variable
    void (*volatile mark_stack)(gc_t *) = gc_mark_stack;
    memset(&env, 0, sizeof(jmp_buf)); // clear the jmp_buf env
    // env is a stack variable
    // setjmp will preserve the current program context(including register) into env
    // so this will spill the registers into stack memory
    setjmp(env);
    mark_stack(gc);
}

/* stop gc */
void gc_stop(gc_t *gc)
{
    gc_sweep(gc);
    free(gc->items);
    free(gc->frees);
}

/* an iteration of mark and sweep */
void gc_run(gc_t *gc)
{
    gc_mark(gc);
    gc_sweep(gc);
}

/* hash function */
static size_t gc_hash(void *ptr)
{
    uintptr_t ad = (uintptr_t)ptr;           // transform pointer to an integer
    return (size_t)((13 * ad) ^ (ad >> 15)); // do hash operation
}

// calculate the offset
static size_t gc_offset(gc_t *gc, size_t i, size_t h)
{
    // h - represent the original location of item
    // i - represent the current location of item
    // v = i - h represent the offset because of hash conflict
    long v = i - h;
    if (v < 0)
    {
        v = gc->slots_cnt + v;
    }
    return v;
}

/* insert gc_ptr_t into gc_items which is a hashtable actually */
static void gc_insert_item(gc_t *gc, void *ptr, size_t size, int flags, void (*dtor)(void *))
{
    // calculate the hash value with ptr as a key
    size_t i = gc_hash(ptr) % gc->slots_cnt; // Remainder operation
    size_t j = 0;
    gc_ptr_t item;
    item.ptr = ptr;
    item.flags = flags;
    item.size = size;
    item.dtor = dtor;
    item.hash = i; // the location of the slot where it should be at start
    while (1)
    {
        size_t h = gc->items[i].hash;
        // if h == 0,means find a slot
        if (h == 0)
        {
            gc->items[i] = item;
            break;
        }
        // if h != 0, but two items.ptr is same,then return back
        if (gc->items[i].ptr == item.ptr)
        {
            break;
        }
        size_t v = gc_offset(gc, i, h);
        // it is like insert item at index i and move items[i] backwards
        if (j >= v)
        {
            // switch gc->items[i] with item
            // start to find new location for items[i]
            gc_ptr_t tmp = gc->items[i];
            gc->items[i] = item;
            item = tmp;
            j = v;
        }
        i = (i + 1) % gc->slots_cnt; // Linear detection method
        j++;                         // j represents steps that i moves
    }
    return;
}

/*  since the size of slots changes
 *  there is a necessity to rehash
 * */
static void gc_rehash(gc_t *gc, size_t new_size)
{
    gc_ptr_t *old_items = gc->items; // old item lists
    size_t old_size = gc->slots_cnt; // old num of slots

    gc->slots_cnt = new_size; // renew the number of slots as new_size
    gc->items = calloc(gc->slots_cnt, sizeof(gc_ptr_t));
    if (gc->items == NULL) // if calloc failed,roll date back
    {
        gc->slots_cnt = old_size;
        gc->items = old_items;
        return 0;
    }

    for (size_t i = 0; i < old_size; i++)
    {
        if (old_items[i].hash != 0)
        {
            gc_insert_item(gc,
                       old_items[i].ptr, old_items[i].size,
                       old_items[i].flags, old_items[i].dtor);
        }
    }
    free(old_items);
    return;
}

/* choose the ideal slots size */ 
static size_t gc_ideal_size(gc_t *gc, size_t size)
{
    // load_factor is a factor which controls the load rate in slots
    // this will make sure that allocation numbers/slots size < load_factor
    size = (size_t)((double)(size + 1) / gc->load_factor);
    // find the first prime number in gc_primes which is larger than the size
    for (size_t i = 0; i < GC_PRIMES_COUNT; i++)
    {
        if (gc_primes[i] >= size)
        {
            return gc_primes[i];
        }
    }
    // failed to find right prime number,then make use of the largest prime number
    size_t last_value = gc_primes[GC_PRIMES_COUNT - 1];
    for (size_t i = 2;; i++)
    {
        if (last_value * i >= size)
        {
            return last_value * i;
        }
    }
    return 0;
}

/* adjust the size of slots whether expanding or shrinking 
 * as long as the ideal slots size changed
 */
static void gc_adjust_slots(gc_t *gc)
{
    // acquire an new ideal slots size according to items_cnt1
    size_t new_size = gc_ideal_size(gc, gc->items_cnt1);
    size_t old_size = gc->slots_cnt;
    if(new_size != old_size)
        gc_rehash(gc, new_size);
    return;
}

/* add items */
static void *gc_add_item(gc_t *gc, void *ptr, size_t size, int flags, void (*dtor)(void *))
{
    gc->items_cnt1++; // number of allocations allocated total
    /* adjust the range of heap because of adding a new allocation */
    gc->max_ptr = ((uintptr_t)ptr) + size > gc->max_ptr ? ((uintptr_t)ptr) + size : gc->max_ptr;
    gc->min_ptr = ((uintptr_t)ptr) < gc->min_ptr ? ((uintptr_t)ptr) : gc->min_ptr;
    gc_adjust_slots(gc); // since adding an item,so try to expand slots
    gc_insert_item(gc, ptr, size, flags, dtor);
    // automatically sweeping
    if (!gc->paused && gc->items_cnt1 > gc->items_cnt2)
    {
        gc_run(gc);
    }
    return ptr;
}

/* delete gc_ptr_t item which contains ptr from gc->items */
static void gc_delete_item(gc_t *gc, void *ptr)
{
    if (gc->items_cnt1 == 0)
    {
        return;
    }
    size_t i = gc_hash(ptr) % gc->slots_cnt;
    size_t j = 0;
    while (1)
    {
        size_t h = gc->items[i].hash;
        if (h == 0 || j > gc_offset(gc, i, h))   // didn't find it
        {
            return;
        }
        if (gc->items[i].ptr == ptr)
        {
            memset(&gc->items[i], 0, sizeof(gc_ptr_t));
            j = i;
            while (1)
            {
                size_t index = (j + 1) % gc->slots_cnt;
                size_t h = gc->items[index].hash;
                if (h != 0 && gc_offset(gc, index, h) > 0)
                {
                    memcpy(&gc->items[j], &gc->items[index], sizeof(gc_ptr_t));
                    memset(&gc->items[index], 0, sizeof(gc_ptr_t));
                    j = index;
                }
                else
                {
                    break;     // if h == 0 or h!=0&&gc_offset(gc, index, h)==0
                }
            }
            gc->items_cnt1--;
            return;
        }
        i = (i + 1) % gc->slots_cnt;
        j++;
    }
    gc_adjust_slots(gc);
    gc->items_cnt2 = gc->items_cnt1 + (size_t)(gc->items_cnt1 * gc->sweep_factor) + 1;
}


/* free the allocation pointed by ptr */
void gc_free(gc_t *gc, void *ptr)
{
    gc_ptr_t *p = gc_get_item(gc, ptr);
    if (p)
    {
        if (p->dtor)
        {
            p->dtor(ptr);
        }
        free(ptr);                  // free the memory allocation pointed by ptr
        gc_delete_item(gc, ptr);    // delete gc_ptr_t item from gc->items
    }
}

/* realloc allocation pointed by ptr with size bytes */
void *gc_realloc(gc_t *gc, void *ptr, size_t size)
{
    /* 
    *   if ptr isn't pointing to an allocation item in gc->items
    *   it is an undefined behavior,then it will return NULL
    *  */
    gc_ptr_t *p = gc_get_item(gc, ptr);
    if(p == NULL)
        return NULL;
    void *qtr = realloc(ptr, size);
    if (qtr == NULL)
    {
        if(ptr == NULL && size == 0)
        {
            // if ptr is NULL && size is 0
            // then realloc will do nothing and return NULL
            return NULL;
        }
        if(ptr != NULL && size == 0)
        {
            // realloc will free the allocation pointed by ptr
            // since ptr has been freed,then we just delete the gc_ptr_t from gc->items
            gc_delete_item(gc,ptr);
            return NULL;
        }
        /* 
        *   realloc failed because of no room for allocating
        *   the original memory block will not be released, 
        *   and you should continue to use it instead.
        * */
        return qtr;
    }else{
        if (ptr == NULL && size != 0)
        { 
            /* 
            *   if ptr is NULL but size is not 0
            *   then it allocate a new allocation without freeing ptr(NULL)
            *   then add the new gc_ptr_t item into gc->items
            *  */
            gc_add_item(gc, qtr, size, 0, NULL);
            return qtr;
        }
        if(ptr != NULL && size != 0)
        {
            /* 
            *   if find the allocation p in gc->items and qtr == ptr
            *   which means realloc just expanded the size at the same place
            *   just modify the allocation size
            *  */
            if (p && qtr == ptr)
            {
                p->size = size;
                return qtr;
            }
            /* 
            *   if find the allocation p in gc->items but qtr != p
            *   which means that realloc gave another different allocation
            *   so we need to remove ptr from gc->items and add qtr into gc->items
            *  */
            if (p && qtr != ptr)
            {
                int flags = p->flags;
                void (*dtor)(void *) = p->dtor;
                /* 
                *   previous memory allocation pointed by ptr
                *   will be automatically freed without explicitly calling free(ptr);
                *   we just need to remove corresponding gc_ptr_t from gc->items
                *  */
                gc_delete_item(gc, ptr);
                gc_add_item(gc, qtr, size, flags, dtor);
                return qtr;
            }
        }
    }
}

/* get the gc_ptr_t from gc->items according to ptr */
static gc_ptr_t *gc_get_item(gc_t *gc, void *ptr)
{
    size_t i = gc_hash(ptr) % gc->slots_cnt;
    size_t j = 0;
    while (1)
    {
        size_t h = gc->items[i].hash;
        if (h == 0 || j > gc_offset(gc, i, h))   // didn't find it
        {
            break;       
        }
        // j == gc_offset(gc, i, h)
        if (gc->items[i].ptr == ptr)
        {
            return &gc->items[i];
        }
        i = (i + 1) % gc->slots_cnt;
        j++;
    }
    return NULL;        // return NULL since there is no allocation pointed by ptr
}

/* alloc the size bytes of allocation */
void *gc_alloc(gc_t *gc, size_t size)
{
    return gc_alloc_opt(gc, size, 0, NULL);
}

/* alloc the size bytes of allocation with flags and dtor */
void *gc_alloc_opt(gc_t *gc, size_t size, int flags, void (*dtor)(void *))
{
    void *ptr = malloc(size);
    if (ptr != NULL)
    {
        ptr = gc_add_item(gc, ptr, size, flags, dtor);
    }
    return ptr;
}

/* alloc (num * size) bytes of allocation */
void *gc_calloc(gc_t *gc, size_t num, size_t size)
{
    return gc_calloc_opt(gc, num, size, 0, NULL);
}

/* alloc (num * size) bytes of allocation with flags and dtor */
void *gc_calloc_opt(gc_t *gc, size_t num, size_t size, int flags, void (*dtor)(void *))
{
    void *ptr = calloc(num, size);
    if (ptr != NULL)
    {
        ptr = gc_add_item(gc, ptr, num * size, flags, dtor);
    }
    return ptr;
}

/* pause the garbage collector */
void gc_pause(gc_t *gc)
{
    gc->paused = 1;
}

/* resume the garbage collector */
void gc_resume(gc_t *gc)
{
    gc->paused = 0;
}

/*  set the ptr allocation's destructor function */
void gc_set_dtor(gc_t *gc, void *ptr, void (*dtor)(void *))
{
    gc_ptr_t *p = gc_get_item(gc, ptr);
    if (p)
    {
        p->dtor = dtor;
    }
}
/*  set the ptr allocation's flag */
void gc_set_flags(gc_t *gc, void *ptr, int flags)
{
    gc_ptr_t *p = gc_get_item(gc, ptr);
    if (p)
    {
        p->flags = flags;
    }
}
/*  get the ptr allocation's flag */
int gc_get_flags(gc_t *gc, void *ptr)
{
    gc_ptr_t *p = gc_get_item(gc, ptr);
    if (p)
    {
        return p->flags;
    }
    return 0;
}
/*  get the ptr allocation's destructor function */
void (*gc_get_dtor(gc_t *gc, void *ptr))(void *)
{
    gc_ptr_t *p = gc_get_item(gc, ptr);
    if (p)
    {
        return p->dtor;
    }
    return NULL;
}
/*  get the ptr allocation's size */
size_t gc_get_size(gc_t *gc, void *ptr)
{
    gc_ptr_t *p = gc_get_item(gc, ptr);
    if (p)
    {
        return p->size;
    }
    return 0;
}
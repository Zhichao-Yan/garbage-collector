#include "gc.h"

static gc_t gc;

static void example_function()
{
    void *memory = gc_alloc(&gc, 1024);
}

int main(int argc, char **argv)
{
    // acquire the address of argc 
    // address of argc is the bottom of the stack
    // no matter you do how many function callings or deeper
    gc_start(&gc, &argc);   
    example_function();
    gc_stop(&gc);
    return 0;
}
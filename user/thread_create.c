#include "types.h"
#include "stat.h"
#include "user.h"

#define PAGESIZE  4096

//char stack[4096] __attribute__ ((aligned (4096)));

int thread_create (void (*fn) (void*), void *arg) {

  // allocate 1-page for new thread's stack.
  // TODO - how to make this page-aligned if the original heap was not page-aligned? 
  void* stack = malloc (sizeof(char)*PAGESIZE);
  if (stack == NULL ) {
    printf (1, "malloc failure \n");
    return -1;
  }
  int tid = 0;
  tid = clone ((void*)stack);
  if (tid < 0) {
    printf(1, "clone failure \n");
    return -1;
  }
  if (tid == 0) { //child thread
    // call function passed to thread_create
    //fn (arg);
    (fn)(arg); 
    // fn has finished, deallocate stack
    free(stack);
    //printf(1, "done freeing stack \n");
    exit ();
  }
  // return tid to parent. 
  return tid;
}

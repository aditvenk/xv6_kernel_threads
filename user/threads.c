#include "types.h"
#include "stat.h"
#include "user.h"

#define PAGESIZE  4096

//int stack[4096] __attribute__ ((aligned (4096)));
int x = 0;

void dummy() {
  printf(1, "this is dummy \n");
  return;
}

int main(int argc, char *argv[]) {
  
  void* stack = malloc (sizeof(char)*PAGESIZE);
  printf(1, "Stack is at %p\n", stack);
  // int tid = fork();
  int tid = clone(stack);

  if (tid < 0) {
    printf(2, "error!\n");
  } else if (tid == 0) {
    // child
    for(;;) {
      x++;
      sleep(100);
      if (x == 5)
        break;
    }
    dummy();
    exit();
  } else {
    // parent
    int a=0;
    for(;;) {
      a++;
      printf(1, "x = %d\n", x);
      sleep(100);
      if (a==10)
        break;
    }
    wait();
  }

  exit();
}

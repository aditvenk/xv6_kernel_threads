#include "types.h"
#include "stat.h"
#include "user.h"

void child (void* args) {
  printf(1, "hello, this is child! passed int = %x \n", *(int*)args);
  return;
}

int main (int argc, char** argv) {
  int a = 0xabcd;
  int rc = thread_create ( &child, (void*)&a);
  if (rc < 0) {
    printf (1, "thread create failed \n");
  }
  // parent
  printf(1, "thread id = %d \n", rc);
  join();
  /*
  int i;
  for (i=0;i<5;i++) {
    printf(1, "parent zzz \n");
    sleep(100);
  }
  */
  exit();
}



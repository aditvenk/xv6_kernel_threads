#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "x86.h"

#undef NULL
#define NULL ((void*)0)

#define PGSIZE (4096)

int ppid;
volatile uint newfd = 0;

#define assert(x) if (x) {} else { \
   printf(1, "%s: %d ", __FILE__, __LINE__); \
   printf(1, "assert failed (%s)\n", # x); \
   printf(1, "TEST FAILED\n"); \
   kill(ppid); \
   exit(); \
}

void worker(void *arg_ptr);

int
main(int argc, char *argv[])
{
   ppid = getpid();
   void *stack = malloc(PGSIZE*2);
   assert(stack != NULL);
   if((uint)stack % PGSIZE)
     stack = stack + (4096 - (uint)stack % PGSIZE);

   int fd = open("tmp", O_WRONLY|O_CREATE);
   assert(fd == 3);
   int clone_pid = clone(stack);
   if (clone_pid == 0) {
     worker(0);
   }
   assert(clone_pid > 0);
   while(!newfd);
   printf(1, "parent, newfd = %d \n", newfd);
   char buf[6];
   read(fd, buf, 6);
   printf(1, "read from fd : %s \n", buf);
   assert(write(newfd, "goodbye\n", 8) == 8);
   printf(1, "TEST PASSED\n");
   exit();
}

void
worker(void *arg_ptr) {
   assert(write(3, "hello\n", 6) == 6);
   printf(1, "worker, written into fd 3 \n");
   char buf[6];
   read(3, buf, 6);
   printf(1, "read from fd : %s \n", buf);
   xchg(&newfd, open("tmp2", O_WRONLY|O_CREATE));
   printf(1, "worker, opened newfd = %d \n", newfd);
   exit();
}

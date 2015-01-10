#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "sysfunc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return proc->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = proc->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;
  
  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(proc->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since boot.
int
sys_uptime(void)
{
  uint xticks;
  
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// adityav - adding syste calls clone, lock, unlock & join

int sys_clone (void) {
  // need to acquire the stack addr from parameter
  // expected arg is an addr (4B) --> use fetchint
  int stack_addr = 0;
  if(argint(0, &stack_addr) < 0)
    return -1;
  return clone((void*) stack_addr); //clone will return -1 if something goes wrong. 
}

int sys_lock (void) {

  int lk = 0;
  if (argptr(0, (void*)&lk, sizeof(lk)) < 0)
    return -1;
  acquire (&guard_lock);
  my_lock ((int*)lk, &guard_lock);
  release(&guard_lock);
  // if we arrive here, it means the lock has been acquired. 
  return 0;
}

int sys_unlock (void) {
  int lk = 0;
  
  if (argptr(0, (void*)&lk, sizeof(lk)) < 0)
    return -1;
  
  acquire (&guard_lock);
  my_unlock((int*)lk);
  release(&guard_lock);
  return 0;
}

int sys_join (void) {
  return join();
}


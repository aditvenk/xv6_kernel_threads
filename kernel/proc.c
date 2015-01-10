#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

struct spinlock thread_lock;
struct spinlock guard_lock;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&thread_lock, "thread_lock");
  initlock(&guard_lock, "guard_lock");
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->stack_top = -1; // adityav - initializing stack_top to illegal value. exec/fork/clone will set it to correct value
  p->pgdir_refcnt = -1; // adityav - initializing pg_dir refcnt to illegal value. exec/fork/initproc will set these to 1 when the pgdir is allocated. clone will increment this for the parent, child will continue to retain -1 as it shares the parent's addr space. 
  release(&ptable.lock);

  // Allocate kernel stack if possible.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  p = allocproc();
  acquire(&ptable.lock);
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  // adityav - setting pgdir_refcnt for initcode
  p->pgdir_refcnt = 1; //1 implies just one thread of execution in this addr space
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  acquire(&thread_lock);
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0) {
      release(&thread_lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0) {
      release(&thread_lock);
      return -1;
    }
  }
  proc->sz = sz;

  //update sz in all threads
  acquire(&ptable.lock);
  struct proc *p;
  int num_children;
  if (proc->pgdir_refcnt == -1) // this is child. 
  {
    // update in parent. 
    proc->parent->sz = proc->sz;
    num_children = (proc->parent->pgdir_refcnt - 1)-1; //other children of same parent.  
    if (num_children <= 0) //there is no other child of the same parent. 
      goto end;

    for(p = ptable.proc; p < &ptable.proc[NPROC] && num_children > 0; p++){
      if ( p!=proc && p->parent == proc->parent && p->pgdir_refcnt == -1) //another child of same parent
      { 
        p->sz = proc->sz;
        num_children--;
      }
    }
  }
  else { // not a child. 
    num_children = proc->pgdir_refcnt - 1; //need to update in these many children threads. 
    if (num_children <= 0) 
      goto end;
    for(p = ptable.proc; p < &ptable.proc[NPROC] && num_children > 0; p++){
      if (p->parent == proc && p->pgdir_refcnt == -1 ) {//child thread of proc
        p->sz = proc->sz;
        num_children--;
      }
    }
  }

  end:
  release(&ptable.lock);
  release(&thread_lock);
  switchuvm(proc);
  return 0;
}
// adityav - adding clone function. 
// Clone - clone an existing process into a new thread. The new thread will be a new process, but it will share same addr space as parent, except the stack.
int clone (void* stack)
{
  int pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  // child thread will share same addr space as parent, hence np->pgdir = proc->pgdir, with one exception : child has its own stack page in the addr passed to this function. so we should copy the parent's stack into this addr & point esp to top of stack. 
  np->pgdir = proc->pgdir;
  // increase the refcnt for this pgdir in parent. pgdir_refcnt in child will default -1.
  proc->pgdir_refcnt++;


  // child's stack-top will be in the addr given by parent + 1-page
  np->stack_top = (int)((char*) stack + PGSIZE); 

  // copy parent's stack to child's location
  // starting from parent's stack_top till parent's esp
  int num_bytes_on_stack = proc->stack_top - proc->tf->esp; 
  np->tf->esp = np->stack_top - num_bytes_on_stack;
  memmove ((void*)np->tf->esp, (void*)proc->tf->esp, num_bytes_on_stack);
  
  // no change to heap, child & parent must share global code & heap. 
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that clone returns 0 in the child.
  np->tf->eax = 0;
  np->tf->esp = np->stack_top - num_bytes_on_stack;
  np->tf->ebp = np->stack_top - (proc->stack_top - proc->tf->ebp); 
  
  /*
  int i; 
  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = (proc->ofile[i]);
  np->cwd = (proc->cwd);
  */
  
  int i; 
  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
  
 
  pid = np->pid;
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  return pid;
}



// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  // adityav - while forking, the child will have same stack-top as parent. 
  np->stack_top = proc->stack_top;
  // aditya - fork creates a new pgdir for child, so it's refcnt is initialized to 1. 
  np->pgdir_refcnt = 1;
  
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
 
  pid = np->pid;
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  iput(proc->cwd);
  proc->cwd = 0;

  acquire(&ptable.lock);

  // 
  if (proc->pgdir_refcnt == -1) { // looks like a child has called exit. Decrement parent's refcnt
    proc->parent->pgdir_refcnt --;
  }
  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      if (p->pgdir_refcnt == -1) //join should only handle child threads
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        // adityav - freevm will release the page-tables for this addr space. This is NOT okay if there are other threads operating out of this addr space. check pgdir_refcnt of thread. 

        if (p->pgdir_refcnt == 1) { // I'm all alone, ok to clear vm
          goto free;
        }
        else { // parent thread is about to be killed, but looks like child threads have not yet been killed. don't release memory. 
          cprintf("parent pid %d has called killed before killing children threads \n", pid);
          goto dontfree;
        }
      
    free:
        freevm(p->pgdir);
    dontfree:
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        // adityav - reset stack_top & pgdir if this is parent. 
        p->stack_top = -1;
        p->pgdir = 0; // if I had called freevm, this would already be 0, if not, i need to set here. 
        p->pgdir_refcnt = -1;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}


int
join(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      if (p->pgdir_refcnt != -1) // join should wait for only child threads, not child procs
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        // when child had called exit, it would have decremented parent's refcnt by 1. 
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        // adityav - reset stack_top & pgdir if this is parent. 
        p->stack_top = -1;
        p->pgdir = 0; // if I had called freevm, this would already be 0, if not, i need to set here. 
        p->pgdir_refcnt = -1;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void my_lock (int* lk, struct spinlock* g_lock) {
  
  // test the value of lk. if lk=0 --> free, else locked so go to sleep
  if (*lk == 0) {
    *lk = 1; // acquire lock 
    //cprintf("pid %d. acquired lk = %x, value of lk = %d \n", proc->pid, lk, *lk);  
    return; 
  }
  // lock is locked. 
  do {
    //cprintf("pid %d. going to sleep on lk = %x, value of lk = %d \n", proc->pid, lk, *lk);
    sleep ((void*)lk, g_lock);
  } while (*lk != 0);
  
  //cprintf("pid %d. woke up on lk = %x, value of lk = %d \n", proc->pid, lk, *lk);
  if (*lk != 0)
  {
    //cprintf("shit hit the roof. pid = %d, lock value = %d \n", proc->pid, *lk);
  }
  else
    *lk = 1; 
  // now i am awake. 
  return;
}

void my_unlock (int* lk) {

  if (*lk == 0 ) //already unlocked. return
    return;
  *lk = 0;
  //cprintf("pid %d. unlocked lk = %x, value of lk = %d \n", proc->pid, lk, *lk);
  wakeup((void*)lk);
  return;
}

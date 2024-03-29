#include "os.h"
#include "globals.h"

#define DEBUG 0

#if DEBUG
   static volatile int row = 1;
   static volatile int count = 1;
#endif

// global system_t variable to track register contents belonging to threads
static volatile system_t system_threads;
static uint8_t* os_start_garbage_base;
static uint8_t* os_start_garbage_end;

//This interrupt routine is automatically run every 10 milliseconds
ISR(TIMER0_COMPA_vect) {
   //The following statement tells GCC that it can use registers r18-r27, 
   //and r30-31 for this interrupt routine.  These registers (along with
   //r0 and r1) will automatically be pushed and popped by this interrupt 
   // routine.
   asm volatile ("" : : : "r18", "r19", "r20", "r21", "r22", "r23", "r24", \
                 "r25", "r26", "r27", "r30", "r31");                        

   volatile uint16_t *stack_ptr = 0x5D;
   volatile uint8_t old_id = system_threads.current_thread;
   volatile uint8_t new_id = system_threads.current_thread = get_next_thread();

   volatile thread_t *old_thread = &system_threads.thread_list[old_id];

   old_thread->tos = *stack_ptr - M_OFFSET - 1;
   old_thread->stack_usage = old_thread->base - old_thread->tos + 1;

   // TODO update any system output information here. Use |system_threads|

   context_switch(system_threads.thread_list[new_id].tos - 1, 
    *stack_ptr - PC_OFFSET);
}

//Call this to start the system timer interrupt
void start_system_timer() {
   TIMSK0 |= _BV(OCIE0A);  //interrupt on compare match
   TCCR0A |= _BV(WGM01);   //clear timer on compare match

   //Generate timer interrupt every ~10 milliseconds
   TCCR0B |= _BV(CS02) | _BV(CS00);    //prescalar /1024
   OCR0A = 156;             //generate interrupt every 9.98 milliseconds
}

/*
 * Switches between threads. |new_tp| is the new top of stack to point to. 
 * |old_tp| is the old top of stack pointed to. Prior to calling context_switch,
 * the stack pointer should be pointing to a valid spot within the stack 
 * associated to |old_tp|. Secondly, if the stack associated to |new_tp| is 
 * new and has just been initialized by create_thread(), then it is assumed that
 * interrupts need to be enabled again, and thus, context_switch will call 
 * sei(). The |tos| pointers for the thread_t objects associated to |new_tp| 
 * and |old_tp| will also be updated given that |new_tp| associates to the 
 * thread stack immediately after |old_tp|'s associating thread stack or if
 * |old_tp| and |new_tp| point to the same thread stack. 
 */
__attribute__((naked)) void context_switch(uint16_t* new_tp, uint16_t* old_tp) {
   asm volatile("clr r31");
   asm volatile("ldi r30, 0x5D");
   asm volatile("st z+, r22");
   asm volatile("st z, r23");

   asm volatile("push r2");
   asm volatile("push r3");
   asm volatile("push r4");
   asm volatile("push r5");
   asm volatile("push r6");
   asm volatile("push r7");
   asm volatile("push r8");
   asm volatile("push r9");
   asm volatile("push r10");
   asm volatile("push r11");
   asm volatile("push r12");
   asm volatile("push r13");
   asm volatile("push r14");
   asm volatile("push r15");
   asm volatile("push r16");
   asm volatile("push r17");
   asm volatile("push r28");
   asm volatile("push r29");

   asm volatile("ldi r30, 0x5D");
   asm volatile("st z+, r24");
   asm volatile("st z, r25");

   asm volatile("pop r29");
   asm volatile("pop r28");
   asm volatile("pop r17");
   asm volatile("pop r16");
   asm volatile("pop r15");
   asm volatile("pop r14");
   asm volatile("pop r13");
   asm volatile("pop r12");
   asm volatile("pop r11");
   asm volatile("pop r10");
   asm volatile("pop r9");
   asm volatile("pop r8");
   asm volatile("pop r7");
   asm volatile("pop r6");
   asm volatile("pop r5");
   asm volatile("pop r4");
   asm volatile("pop r3");
   asm volatile("pop r2");
   
   asm volatile("ret");
}

/*
 * Starts multithreading by enabling interrupts
 */
__attribute__((naked)) void thread_start(void) {
   sei(); //enable interrupts - leave as the first statement  
   asm volatile("mov r30,r2");
   asm volatile("mov r31,r3");
   asm volatile("mov r25,r5");
   asm volatile("mov r24,r4");
   asm volatile("mov r23,r7");
   asm volatile("mov r22,r6");
   asm volatile("ijmp");
}

void os_init() {
   int i;

   system_threads.current_thread = ETHREAD;
   for (i = 0; i < MAX_THREADS; i++)
      system_threads.thread_list[i].active = false;

   system_threads.active_threads_count = 0;
   system_threads.uptime = 0;
   cli();
}

void create_thread(uint16_t address, void *args, uint16_t stack_size) {
   int idx;
   thread_t *open_thread;
   uint8_t *byte_ptr;
   uint16_t thread_start_addr = thread_start;

   // look for an inactive thread
   for (idx = 0; idx < MAX_THREADS && 
    system_threads.thread_list[idx].active; idx++) 
      ;

   if (idx == MAX_THREADS) {
      print_string("Error: Maximum amount of threads has been reached");
      return;
   }

   open_thread = &system_threads.thread_list[idx];
   open_thread->thread_id = idx;
   open_thread->thread_pc = address;
   open_thread->stack_usage = INIT_SIZE;
   open_thread->stack_size = stack_size;
   
   // base represents the bottom of the stack and 
   // end represents the top of the stack
   open_thread->end = malloc(stack_size);
   open_thread->base = open_thread->end + stack_size - 1;
   open_thread->tos = open_thread->base;
   open_thread->active = true;

   ++system_threads.active_threads_count;

   // store thread_start() address, |address|, and |*arg| in big endian
   byte_ptr = &thread_start_addr;
   *open_thread->tos-- = *byte_ptr++;
   *open_thread->tos-- = *byte_ptr;
   byte_ptr = &address;
   *open_thread->tos-- = *byte_ptr++;
   *open_thread->tos-- = *byte_ptr;
   byte_ptr = args;
   
   for (idx = 0; idx < REMAINING; idx++)
      *open_thread->tos-- = *byte_ptr++;
   *open_thread->tos = *byte_ptr;
}

void os_start() {
   system_threads.current_thread = get_next_thread();

   if (system_threads.current_thread == ETHREAD) {
      print_string("Error: No active thread can be found");
      return;
   }

   start_system_timer();

   os_start_garbage_end = malloc(GARBAGE_SIZE);
   os_start_garbage_base = os_start_garbage_end + GARBAGE_SIZE - 1;

   context_switch(
    system_threads.thread_list[system_threads.current_thread].tos - 1, 
    os_start_garbage_base);
}

uint8_t get_next_thread() {
   uint8_t next;
   uint8_t save = system_threads.current_thread;

   if (system_threads.current_thread >= MAX_THREADS)
      system_threads.current_thread = MAX_THREADS - 1;

   for (next = (system_threads.current_thread + 1) % MAX_THREADS;
    next != system_threads.current_thread && 
    !system_threads.thread_list[next].active; 
    next = (next + 1) % MAX_THREADS) 
      ;

   if (next == system_threads.current_thread && 
    !system_threads.thread_list[next].active) {
      system_threads.current_thread = save;
      return ETHREAD;
   }

   return next;
}

system_t *get_system_stats() {
   return &system_threads;
}

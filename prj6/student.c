/*
 * student.c
 * Multithreaded OS Simulation for CS 2200, Project 6
 * Summer 2015
 *
 * This file contains the CPU scheduler for the simulation.
 * Name:Son Nguyen
 * GTID:snguyen48
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os-sim.h"



typedef enum {
    FIFO,
    STATIC_PRIORITY,
    ROUND_ROBIN        
}scheduler;

static scheduler schedule_method;
static int timeslice;

typedef struct {
    pcb_t *head;
    pcb_t *tail;
}ready_q;

static ready_q queue;
static pthread_cond_t q_not_empty;
static pthread_mutex_t queue_mutex;


/* Insert process to the ready queue according to its priority
 * Non-thread-safe
 */
void PriorityInsert(pcb_t *process) {
    
    if(queue.head == NULL && queue.tail == NULL) {
        process->next = NULL;
        queue.head = process;
        queue.tail = process;
    }
    else {
        pcb_t *iterator;
        iterator = queue.head;
        //If the first entry has lower priority then insert to the front
        if(iterator->static_priority < process->static_priority) {
            process->next = iterator;
            queue.head = process;
        }
        else {
            while(iterator->next != NULL && 
                  iterator->next->static_priority >= process->static_priority ) {
                iterator = iterator->next;
            }
            process->next = iterator->next;
            iterator->next = process;
        }
    }
}



/*
 * current[] is an array of pointers to the currently running processes.
 * There is one array element corresponding to each CPU in the simulation.
 *
 * current[] should be updated by schedule() each time a process is scheduled
 * on a CPU.  Since the current[] array is accessed by multiple threads, you
 * will need to use a mutex to protect it.  current_mutex has been provided
 * for your use.
 */
static pcb_t **current;
static pthread_mutex_t current_mutex;
static int size_of_current; //Added to save the size of current[] array 

/*
 * schedule() is your CPU scheduler.  It should perform the following tasks:
 *
 *   1. Select and remove a runnable process from your ready queue which 
 *	you will have to implement with a linked list or something of the sort.
 *
 *   2. Set the process state to RUNNING
 *
 *   3. Call context_switch(), to tell the simulator which process to execute
 *      next on the CPU.  If no process is runnable, call context_switch()
 *      with a pointer to NULL to select the idle process.
 *	The current array (see above) is how you access the currently running
 *	process indexed by the cpu id. See above for full description.
 *	context_switch() is prototyped in os-sim.h. Look there for more information 
 *	about it and its parameters.
 */
static void schedule(unsigned int cpu_id)
{
    pthread_mutex_lock(&queue_mutex);
   
    if(queue.head == NULL && queue.tail == NULL) {
        context_switch(cpu_id, NULL, -1);
        pthread_mutex_unlock(&queue_mutex);
        pthread_mutex_lock(&current_mutex);
        current[cpu_id] = NULL; // update current array
        pthread_mutex_unlock(&current_mutex);
    }
    else {
        pcb_t *candidate = queue.head;
        queue.head = queue.head->next;
        if(queue.head == NULL) {
            queue.tail = NULL;
        }
        pthread_mutex_unlock(&queue_mutex);
        
        candidate->state = PROCESS_RUNNING;
        
        if(schedule_method != ROUND_ROBIN) {
            context_switch(cpu_id, candidate, -1);
        }
        else {
            context_switch(cpu_id, candidate, timeslice);
        }
        
        pthread_mutex_lock(&current_mutex);
        current[cpu_id] = candidate;
        pthread_mutex_unlock(&current_mutex);
    }
}


/*
 * idle() is your idle process.  It is called by the simulator when the idle
 * process is scheduled.
 *
 * This function should block until a process is added to your ready queue.
 * It should then call schedule() to select the process to run on the CPU.
 */
extern void idle(unsigned int cpu_id)
{
    pthread_mutex_lock(&queue_mutex);
    while(queue.head == NULL){
        pthread_cond_wait(&q_not_empty, &queue_mutex);
    }
    pthread_mutex_unlock(&queue_mutex);
    schedule(cpu_id);
       
      
      

    /*
     * REMOVE THE LINE BELOW AFTER IMPLEMENTING IDLE()
     *
     * idle() must block when the ready queue is empty, or else the CPU threads
     * will spin in a loop.  Until a ready queue is implemented, we'll put the
     * thread to sleep to keep it from consuming 100% of the CPU time.  Once
     * you implement a proper idle() function using a condition variable,
     * remove the call to mt_safe_usleep() below.
     */
}


/*
 * preempt() is the handler called by the simulator when a process is
 * preempted due to its timeslice expiring.
 *
 * This function should place the currently running process back in the
 * ready queue, and call schedule() to select a new runnable process.
 */
extern void preempt(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t *candidate = current[cpu_id];
    candidate->state = PROCESS_READY;   
    pthread_mutex_unlock(&current_mutex);
    
    pthread_mutex_lock(&queue_mutex);    
    candidate->next = NULL;
    if(schedule_method == STATIC_PRIORITY) {
        PriorityInsert(candidate);
    }
    else {
        if(queue.head == NULL && queue.tail == NULL) {
            queue.head = candidate;
            queue.tail = candidate;
        }
        else {
            queue.tail->next = candidate;
            queue.tail = queue.tail->next;
        }
    }
    pthread_mutex_unlock(&queue_mutex);
    schedule(cpu_id);
    
}


/*
 * yield() is the handler called by the simulator when a process yields the
 * CPU to perform an I/O request.
 *
 * It should mark the process as WAITING, then call schedule() to select
 * a new process for the CPU.
 */
extern void yield(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_WAITING;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}


/*
 * terminate() is the handler called by the simulator when a process completes.
 * It should mark the process as terminated, then call schedule() to select
 * a new process for the CPU.
 */
extern void terminate(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}


/*
 * wake_up() is the handler called by the simulator when a process's I/O
 * request completes.  It should perform the following tasks:
 *
 *   1. Mark the process as READY, and insert it into the ready queue.
 *
 *   2. If the scheduling algorithm is static priority, wake_up() may need
 *      to preempt the CPU with the lowest priority process to allow it to
 *      execute the process which just woke up.  However, if any CPU is
 *      currently running idle, or all of the CPUs are running processes
 *      with a higher priority than the one which just woke up, wake_up()
 *      should not preempt any CPUs.
 *	To preempt a process, use force_preempt(). Look in os-sim.h for 
 * 	its prototype and the parameters it takes in.
 */
extern void wake_up(pcb_t *process)
{
    process->state = PROCESS_READY;
    process->next = NULL;
    
    pthread_mutex_lock(&queue_mutex);
  
    if(schedule_method !=STATIC_PRIORITY) {
        if(queue.head == NULL && queue.tail == NULL) {      
            queue.head = process;
            queue.tail = process;
            //Signal to whatever process waiting for q_not_empty conditional variable
            pthread_cond_signal(&q_not_empty); // This will invoke schedule
            pthread_mutex_unlock(&queue_mutex);
        }
        else {
            //Add current process to the end of the ready_q
            queue.tail->next = process;
            queue.tail = process;
            pthread_mutex_unlock(&queue_mutex);
        }
    }
    else {  
        PriorityInsert(process);
        pthread_mutex_unlock(&queue_mutex);

        //preempt the running process with lowest priority
        pthread_mutex_lock(&current_mutex);
        int i = 0;
        pcb_t *lowest_priority = current[0];
        int cpuid = 0; // store the cpuid of the lowest priority running process
        while(i < size_of_current) {
            //if the running process has lowest_priority
            if(lowest_priority == NULL ||
                current[i]->static_priority < lowest_priority->static_priority) {
                lowest_priority = current[i];
                cpuid = i;
            }
            i++;
        }
        pthread_mutex_unlock(&current_mutex);
        //we should have running process with lowest priority
        if(lowest_priority == NULL) { // All CPUs are idle
            pthread_cond_signal(&q_not_empty);
        }
        else if(lowest_priority->static_priority < process->static_priority) {
            force_preempt(cpuid); // This will invoke schedule
        }
    }
        

    
}


/*
 * main() simply parses command line arguments, then calls start_simulator().
 * You will need to modify it to support the -r and -p command-line parameters.
 */
int main(int argc, char *argv[])
{
    int cpu_count;

    /* Parse command-line arguments */
    if (argc < 2 || argc > 4 || (argc==3 && strcmp(argv[2],"-r") == 0))
    {
        fprintf(stderr, "CS 2200 Project 4 -- Multithreaded OS Simulator\n"
            "Usage: ./os-sim <# CPUs> [ -r <time slice> | -p ]\n"
            "    Default : FIFO Scheduler\n"
            "         -r : Round-Robin Scheduler\n"
            "         -p : Static Priority Scheduler\n\n");
        return -1;
    }
    else {
        cpu_count = atoi(argv[1]);
        if(argc == 2) {
            schedule_method = FIFO;
        }
        else if(strcmp(argv[2], "-r") == 0) {
            schedule_method = ROUND_ROBIN;
            timeslice = atoi(argv[3]);
        }
        else if(strcmp(argv[2],"-p") == 0) {
            schedule_method = STATIC_PRIORITY;                
        }
        else {
            return -1;
        }
    }    
    /* FIX ME - Add support for -r and -p parameters*/
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&q_not_empty, NULL);
    /* Allocate the current[] array and its mutex */
    current = malloc(sizeof(pcb_t*) * cpu_count);
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);

    /* Start the simulator in the library */
    start_simulator(cpu_count);

    return 0;
}



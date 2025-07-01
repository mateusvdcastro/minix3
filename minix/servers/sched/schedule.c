/* This file contains the scheduling policy for SCHED
 *
 * The entry points are:
 *   do_noquantum:        Called on behalf of process' that run out of quantum
 *   do_start_scheduling  Request to start scheduling a proc
 *   do_stop_scheduling   Request to stop scheduling a proc
 *   do_nice		  Request to change the nice level on a proc
 *   init_scheduling      Called from main.c to set up/prepare scheduling
 */
#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>
#include <minix/bitmap.h>
#include <minix/sysutil.h>
#include <stdlib.h>

static unsigned balance_timeout;

#define BALANCE_TIMEOUT	5 /* how often to balance queues in seconds */

static int schedule_process(struct schedproc * rmp, unsigned flags);

#define SCHEDULE_CHANGE_PRIO	0x1
#define SCHEDULE_CHANGE_QUANTUM	0x2
#define SCHEDULE_CHANGE_CPU	0x4

#define SCHEDULE_CHANGE_ALL	(	\
		SCHEDULE_CHANGE_PRIO	|	\
		SCHEDULE_CHANGE_QUANTUM	|	\
		SCHEDULE_CHANGE_CPU		\
		)

#define schedule_process_local(p)	\
	schedule_process(p, SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)
#define schedule_process_migrate(p)	\
	schedule_process(p, SCHEDULE_CHANGE_CPU)

#define CPU_DEAD	-1

#define cpu_is_available(c)	(cpu_proc[c] >= 0)

#define DEFAULT_USER_TIME_SLICE 200

/* processes created by RS are sysytem processes */
#define is_system_proc(p)	((p)->parent == RS_PROC_NR)

#define MAX_READY_PROCS NR_PROCS
static int ready_queue[MAX_READY_PROCS];
static int ready_front = 0;
static int ready_back = 0;

static unsigned cpu_proc[CONFIG_MAX_CPUS];

static void pick_cpu(struct schedproc * proc)
{
#ifdef CONFIG_SMP
	unsigned cpu, c;
	unsigned cpu_load = (unsigned) -1;
	
	if (machine.processors_count == 1) {
		proc->cpu = machine.bsp_id;
		return;
	}

	/* schedule sysytem processes only on the boot cpu */
	if (is_system_proc(proc)) {
		proc->cpu = machine.bsp_id;
		return;
	}

	/* if no other cpu available, try BSP */
	cpu = machine.bsp_id;
	for (c = 0; c < machine.processors_count; c++) {
		/* skip dead cpus */
		if (!cpu_is_available(c))
			continue;
		if (c != machine.bsp_id && cpu_load > cpu_proc[c]) {
			cpu_load = cpu_proc[c];
			cpu = c;
		}
	}
	proc->cpu = cpu;
	cpu_proc[cpu]++;
#else
	proc->cpu = 0;
#endif
}

/*===========================================================================*
 *				enqueue					     *
 *===========================================================================*/

void enqueue(int proc_nr) {
    // Verifica se a fila está cheia. Se o próximo 'back' for igual ao 'front',
    // a fila está cheia.
    if ((ready_back + 1) % MAX_READY_PROCS == ready_front) {
        printf("SCHED: WARNING: Fila de prontos está cheia! Processo %d não pode ser enfileirado.\n", proc_nr);
        return;
    }
    ready_queue[ready_back] = proc_nr;
    ready_back = (ready_back + 1) % MAX_READY_PROCS;
}

/*===========================================================================*
 *				dequeue					     * << Added function to dequeue a process from the ready queue
 *===========================================================================*/

int dequeue(void) {
    // Se front == back, a fila está vazia.
    if (ready_front == ready_back) {
        return -1; 
    }
    int proc_nr = ready_queue[ready_front];
    ready_front = (ready_front + 1) % MAX_READY_PROCS;
    return proc_nr;
}

/*===========================================================================*
 *				peek_queue				     * << Added function to peek at the front of the ready queue
 *===========================================================================*/

int peek_queue(void) {
    if (ready_front == ready_back) {
        return -1;
    }
    return ready_queue[ready_front];
}

/*===========================================================================*
 *				is_queue_empty				     * << Added function to check if the ready queue is empty
 *===========================================================================*/

int is_queue_empty(void) {
    return ready_front == ready_back;
}

/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

int do_schedule_next(message *m_ptr)
{
	int next_proc = peek_queue();  // Pega o próximo (sem remover)

	if (next_proc == -1) {
		// Nenhum processo para agendar
		return OK;
	}

	// Agenda o próximo processo
	return schedule_process(&schedproc[next_proc], SCHEDULE_CHANGE_ALL);
}

/*===========================================================================*
 *				do_stop_scheduling			     *
 *===========================================================================*/
int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int proc_nr_n, current_proc;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->m_lsys_sched_scheduling_stop.endpoint,
		    &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%d\n", m_ptr->m_lsys_sched_scheduling_stop.endpoint);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];

	// Marca processo como fora de uso
	rmp->flags = 0;

    // O processo que está parando DEVE ser o que estava na frente da fila.
    // Vamos verificar isso e removê-lo.
    current_proc = dequeue();

    if (current_proc != proc_nr_n && current_proc != -1) {
        // Isso indica um erro de estado grave no escalonador.
        printf("SCHED: WARNING: Processo %d parou, mas o processo %d estava na frente da fila!\n", 
                proc_nr_n, current_proc);
    }

	return do_schedule_next();
}

/*===========================================================================*
 *				do_start_scheduling			     *
 *===========================================================================*/
int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n, parent_nr_n;
	
	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || // Asserts only valid calls pass by
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr)) // Verifies if the message is from a valid source
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = sched_isemtyendpt(m_ptr->m_lsys_sched_scheduling_start.endpoint,
			&proc_nr_n)) != OK) {
		return rv;
	}
	rmp = &schedproc[proc_nr_n];

	/* Populate process slot */
	rmp->endpoint     = m_ptr->m_lsys_sched_scheduling_start.endpoint;
	rmp->parent       = m_ptr->m_lsys_sched_scheduling_start.parent;
	
	// Removed priority and time_slice from the message,
	// FCFS does not use them.

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;

	enqueue(proc_nr_n); // Add the process to the ready queue

	/* Schedule the process */
	if (peek_queue() == proc_nr_n) {
		if ((rv = schedule_process(rmp, SCHEDULE_CHANGE_ALL)) != OK) {
			printf("Sched: Error while scheduling process %d, kernel replied %d\n",
				rmp->endpoint, rv);
			return rv;
		}
	}

	if (rv != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into the "scheduler" field.
	 */

	m_ptr->m_sched_lsys_scheduling_start.scheduler = SCHED_PROC_NR;

	return OK;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
static int schedule_process(struct schedproc * rmp, unsigned flags)
{
	int err;
	int new_quantum, new_cpu;

	pick_cpu(rmp);

	new_quantum = -1; // Quantum padrão (infinito) para o algoritmo FCFS

	if (flags & SCHEDULE_CHANGE_CPU)
		new_cpu = rmp->cpu;
	else
		new_cpu = -1;

	if ((err = sys_schedule(rmp->endpoint, 0,
		new_quantum, new_cpu, 0)) != OK) {
		printf("PM: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, err);
	}

	return err;
}


/*===========================================================================*
 *				init_scheduling				     *
 *===========================================================================*/
void init_scheduling(void)
{
	int r;

	balance_timeout = BALANCE_TIMEOUT * sys_hz();

	if ((r = sys_setalarm(balance_timeout, 0)) != OK)
		panic("sys_setalarm failed: %d", r);
}
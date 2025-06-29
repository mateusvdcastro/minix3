/* Function prototypes. */

struct schedproc;

/* Existing prototypes... */

/* Round Robin specific functions */
void set_scheduling_algorithm(int algorithm);
int get_scheduling_algorithm(void);

/* Constants for scheduling algorithms */
#define SCHEDULING_ALGORITHM_DEFAULT 0
#define SCHEDULING_ALGORITHM_RR      1

/* Round Robin configuration */
#define RR_QUANTUM          100    /* Quantum in ticks - adjustable */
#define RR_USER_PRIORITY    USER_Q /* Fixed priority for Round Robin */
/* main.c */
int main(void);
void setreply(int proc_nr, int result);

/* schedule.c */
int do_noquantum(message *m_ptr);
int do_start_scheduling(message *m_ptr);
int do_stop_scheduling(message *m_ptr);
int do_nice(message *m_ptr);
void init_scheduling(void);
void balance_queues(void);

/* utility.c */
int no_sys(int who_e, int call_nr);
int sched_isokendpt(int ep, int *proc);
int sched_isemtyendpt(int ep, int *proc);
int accept_message(message *m_ptr);


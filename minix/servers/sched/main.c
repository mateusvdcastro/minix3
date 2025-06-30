/* This file contains the main program of the SCHED scheduler. It will sit idle
 * until asked, by PM, to take over scheduling a particular process.
 */

/* The _MAIN def indicates that we want the schedproc structs to be created
 * here. Used from within schedproc.h */
#define _MAIN

#include "sched.h"
#include "schedproc.h"

/* Declare some local functions. */
static void reply(endpoint_t whom, message *m_ptr);
static void sef_local_startup(void);
static int sef_cb_init_fresh(int type, sef_init_info_t *info);

struct machine machine;		/* machine info */

/*===========================================================================*
 *				main					     *
 *===========================================================================*/
int main(void)
{
	/* Main routine of the scheduler. */
	message m_in;	/* the incoming message itself is kept here. */
	int call_nr;	/* system call number */
	int who_e;	/* caller's endpoint */
	int result;	/* result to system call */
	int rv;

	/* SEF local startup. */
	sef_local_startup();

	/* This is SCHED's main loop - get work and do it, forever and forever. */
	while (TRUE) {
		int ipc_status;

		/* Wait for the next message and extract useful information from it. */
		if (sef_receive_status(ANY, &m_in, &ipc_status) != OK)
			panic("SCHED sef_receive error");
		who_e = m_in.m_source;	/* who sent the message */
		call_nr = m_in.m_type;	/* system call number */

		switch(call_nr) {
		case SCHEDULING_INHERIT:
		case SCHEDULING_START:
			result = do_start_scheduling(&m_in);
			break;
		case SCHEDULING_STOP:
			result = do_stop_scheduling(&m_in);
			break;
		default:
			result = no_sys(who_e, call_nr);
		}

		/* Send reply. */
		if (result != SUSPEND) {
			m_in.m_type = result;  		/* build reply message */
			reply(who_e, &m_in);		/* send it away */
		}
 	}
	return(OK);
}

/*===========================================================================*
 *				reply					     *
 *===========================================================================*/
static void reply(endpoint_t who_e, message *m_ptr)
{
	int s = ipc_send(who_e, m_ptr);    /* send the message */
	if (OK != s)
		printf("SCHED: unable to send reply to %d: %d\n", who_e, s);
}

/*===========================================================================*
 *			       sef_local_startup			     *
 *===========================================================================*/
static void sef_local_startup(void)
{
	/* Register init callbacks. */
	sef_setcb_init_fresh(sef_cb_init_fresh);
	sef_setcb_init_restart(SEF_CB_INIT_RESTART_STATEFUL);

	/* No signal callbacks for now. */

	/* Let SEF perform startup. */
	sef_startup();
}

/*===========================================================================*
 *		            sef_cb_init_fresh                                *
 *===========================================================================*/
static int sef_cb_init_fresh(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	int s;

	if (OK != (s=sys_getmachine(&machine)))
		panic("couldn't get machine info: %d", s);
	/* Initialize scheduling timers, used for running balance_queues */
	init_scheduling();

	return(OK);
}


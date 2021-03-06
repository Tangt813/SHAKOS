/*************************************************************************//**
 *****************************************************************************
 * @file   forkexit.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   Tue May  6 00:37:15 2008
 *****************************************************************************
 *****************************************************************************/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"


PRIVATE void cleanup(struct proc * proc);

/*****************************************************************************
 *                                do_exit
 *****************************************************************************/
/**
 * Perform the exit() syscall.
 *
 * If proc A calls exit(), then MM will do the following in this routine:
 *     <1> inform FS so that the fd-related things will be cleaned up
 *     <2> tell TASK_SYS (TODO)
 *     <3> free A's memory
 *     <4> set A.exit_status, which is for the parent
 *     <5> depends on parent's status. if parent (say P) is:
 *           (1) WAITING
 *                 - clean P's WAITING bit, and
 *                 - send P a message to unblock it
 *                   {P's wait() call is done}
 *                 - release A's proc_table[] slot
 *                   {A's exit() call is done}
 *           (2) not WAITING
 *                 - set A's HANGING bit
 *                   {things will be done at do_wait()::comment::<1>}
 *     <6> iterate proc_table[], if proc B is found as A's child, then:
 *           (1) make INIT the new parent of B, and
 *           (2) if INIT is WAITING and B is HANGING, then:
 *                 - clean INIT's WAITING bit, and
 *                 - send INIT a message to unblock it
 *                   {INIT's wait() call is done}
 *                 - release B's proc_table[] slot
 *                   {B's exit() call is done}
 *               else
 *                 if INIT is WAITING but B is not HANGING, then
 *                     - B will call exit() and things will be done at
 *                       do_exit()::comment::<5>::(1)
 *                 if B is HANGING but INIT is not WAITING, then
 *                     - INIT will call wait() and things will be doen at
 *                       do_wait()::comment::<1>
 *
 * TERMs:
 *     - HANGING: everything except the proc_table entry has been cleaned up.
 *     - WAITING: a proc has at least one child, and it is waiting for the
 *                child(ren) to exit()
 *     - zombie: say P has a child A, A will become a zombie if
 *         - A exit(), and
 *         - P does not wait(), neither does it exit(). that is to say, P just
 *           keeps running without terminating itself or its child
 *         - @see Linux manpage for more details:
 *             - man 2 exit
 *             - man 3 exit
 *             - man 2 wait
 * 
 * @param status  Exiting status for parent.
 * 
 *****************************************************************************/
PUBLIC void do_exit(int status)
{
	int i;
	int pid = mm_msg.source; /* PID of caller */
	int parent_pid = proc_table[pid].p_parent;
	struct proc * p = &proc_table[pid];
	/* struct proc * p_parent = &proc_table[parent_pid]; */

	/* tell FS, see fs_exit() */
	MESSAGE msg2fs;
	msg2fs.type = EXIT;
	msg2fs.PID = pid;
	send_recv(BOTH, TASK_FS, &msg2fs);

	/**
	 * @todo should also send a message to TASK_SYS to do some cleanup work.
	 *       e.g. if the proc is killed by another proc, TASK_SYS should
	 *            check if the proc happens to be SENDING a message, if so,
	 *            the proc should be removed from the sending queue.
	 * @see MINIX::src/kernel/system.c:do_xit()
	 */

	free_mem(pid);

	p->exit_status = status;

	if (proc_table[parent_pid].p_flags & WAITING) { /* parent is waiting */
		printl("{MM} ((--do_exit():: %s (%d) is WAITING, %s (%d) will be cleaned up.--))\n",
		       proc_table[parent_pid].name, parent_pid,
		       p->name, pid);
		/* dump_fd_graph("((--do_exit():: %s (%d) is WAITING, %s (%d) will be cleaned up.--))", */
		/*        proc_table[parent_pid].name, parent_pid, */
		/*        p->name, pid); */
		/* assert(proc_table[parent_pid].p_flags & RECEIVING); */
		printl("{MM} ((--do_exit():1: proc_table[parent_pid].p_flags: 0x%x--))\n",
		       proc_table[parent_pid].p_flags);
		/* dump_fd_graph("((--do_exit():1: proc_table[parent_pid].p_flags: 0x%x--))", */
		/*        proc_table[parent_pid].p_flags); */
		proc_table[parent_pid].p_flags &= ~WAITING;
		cleanup(&proc_table[pid]);
	}
	else { /* parent is not waiting */
		printl("{MM} ((--do_exit():: %s (%d) is not WAITING, %s (%d) will be HANGING--))\n",
		       proc_table[parent_pid].name, parent_pid,
		       p->name, pid);
		/* dump_fd_graph("((--do_exit():: %s (%d) is not WAITING, %s (%d) will be HANGING--))", */
		/*        proc_table[parent_pid].name, parent_pid, */
		/*        p->name, pid); */
		proc_table[pid].p_flags |= HANGING;
	}

	/* if the proc has any child, make INIT the new parent */
	for (i = 0; i < NR_TASKS + NR_PROCS; i++) {
		if (proc_table[i].p_parent == pid) { /* is a child */
			proc_table[i].p_parent = INIT; /* FIXME: make sure INIT always waits */
			printl("{MM} %s (%d) exit(), so %s (%d) is INIT's child now\n",
			       p->name, pid, proc_table[i].name, i);
			/* dump_fd_graph("%s (%d) exit(), so %s (%d) is INIT's child now", */
			/*        p->name, pid, proc_table[i].name, i); */
			printl("{MM} ((--do_exit():2: proc_table[INIT].p_flags: 0x%x--))\n",
			       proc_table[INIT].p_flags);
			/* dump_fd_graph("((--do_exit():2: proc_table[INIT].p_flags: 0x%x--))", */
			/*        proc_table[INIT].p_flags); */
			if ((proc_table[INIT].p_flags & WAITING) &&
			    (proc_table[i].p_flags & HANGING)) {
				/* assert(proc_table[INIT].p_flags & RECEIVING); */
				proc_table[INIT].p_flags &= ~WAITING;
				cleanup(&proc_table[i]);
				assert(0);
			}
			else {
				/* assert(0); */
			}
		}
	}
}

/*****************************************************************************
 *                                cleanup
 *****************************************************************************/
/**
 * Do the last jobs to clean up a proc thoroughly:
 *     - Send proc's parent a message to unblock it, and
 *     - release proc's proc_table[] entry
 * 
 * @param proc  Process to clean up.
 *****************************************************************************/
PRIVATE void cleanup(struct proc * proc)
{
	MESSAGE msg2parent;
	msg2parent.type = SYSCALL_RET;
	msg2parent.PID = proc2pid(proc);
	msg2parent.STATUS = proc->exit_status;
	send_recv(SEND, proc->p_parent, &msg2parent);

	proc->p_flags = FREE_SLOT;

	printl("{MM} ((--cleanup():: %s (%d) has been cleaned up.--))\n", proc->name, proc2pid(proc));
	/* dump_fd_graph("((--cleanup():: %s (%d) has been cleaned up.--))", proc->name, proc2pid(proc)); */
}

/*****************************************************************************
 *                                do_wait
 *****************************************************************************/
/**
 * Perform the wait() syscall.
 *
 * If proc P calls wait(), then MM will do the following in this routine:
 *     <1> iterate proc_table[],
 *         if proc A is found as P's child and it is HANGING
 *           - reply to P (cleanup() will send P a messageto unblock it)
 *             {P's wait() call is done}
 *           - release A's proc_table[] entry
 *             {A's exit() call is done}
 *           - return (MM will go on with the next message loop)
 *     <2> if no child of P is HANGING
 *           - set P's WAITING bit
 *             {things will be done at do_exit()::comment::<5>::(1)}
 *     <3> if P has no child at all
 *           - reply to P with error
 *             {P's wait() call is done}
 *     <4> return (MM will go on with the next message loop)
 *
 *****************************************************************************/
PUBLIC void do_wait()
{
	printl("{MM} ((--do_wait()--))");
	/* dump_fd_graph("((--do_wait()--))"); */
	int pid = mm_msg.source;

	int i;
	int children = 0;
	struct proc* p_proc = proc_table;
	for (i = 0; i < NR_TASKS + NR_PROCS; i++,p_proc++) {
		if (p_proc->p_parent == pid) {
			children++;
			if (p_proc->p_flags & HANGING) {
				printl("{MM} ((--do_wait():: %s (%d) is HANGING, "
				       "so let's clean it up.--))",
				       p_proc->name, i);
				/* dump_fd_graph("((--do_wait():: %s (%d) is HANGING, " */
				/*        "so let's clean it up.--))", */
				/*        p_proc->name, i); */
				cleanup(p_proc);
				return;
			}
		}
	}

	if (children) {
		/* has children, but no child is HANGING */
		proc_table[pid].p_flags |= WAITING;
		printl("{MM} ((--do_wait():: %s (%d) is WAITING for child "
		       "to exit().--))\n", proc_table[pid].name, pid);
		/* dump_fd_graph("((--do_wait():: %s (%d) is WAITING for child " */
		/*        "to exit().--))", proc_table[pid].name, pid); */
	}
	else {
		/* no child at all */
		printl("{MM} ((--do_wait():: %s (%d) has no child at all.--))\n",
		       proc_table[pid].name, pid);
		/* dump_fd_graph("((--do_wait():: %s (%d) is has no child at all.--))", */
		/*        proc_table[pid].name, pid); */
		MESSAGE msg;
		msg.type = SYSCALL_RET;
		msg.PID = NO_TASK;
		send_recv(SEND, pid, &msg);
	}
}

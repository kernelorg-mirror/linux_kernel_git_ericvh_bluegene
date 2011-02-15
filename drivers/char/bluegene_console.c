/*
 * Blue Gene Console over JTAG.
 *
 * (C) Copyright IBM Corp. 2003,2010
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses>.
 *
 * Author: Todd Inglett <tinglett@vnet.ibm.com>
 *
 *
 */

#include <linux/unistd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kbd_kern.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <linux/syscalls.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>

#include <asm/bluegene.h>
#include <asm/bluegene_ras.h>

static struct proc_dir_entry *proc_ras;         /* /proc/ras */
static struct proc_dir_entry *proc_ras_ascii;   /* /proc/ras_ascii */


/* ToDo: figure out what to do with bgprintf... */
#define bgprintf udbg_printf
#include <asm/udbg.h>


#ifdef CONFIG_ZEPTO_COMPUTENODE
/* console message output control */

#include <linux/zepto_debug.h>
#include <asm/bgp_personality.h>
static int enable_console;
#endif


#define BLUEGENE_MAJOR	229
#define BLUEGENE_MINOR	0


typedef struct _BG_CONSOLE {
	struct tty_struct*	tty;
	spinlock_t		ttyLock;
	struct tty_driver*	ttyDriver;
#define BG_OUTBOX_BUFF_SIZE 8192
	unsigned char		outboxBuff[BG_OUTBOX_BUFF_SIZE];
	spinlock_t		outboxLock;
#define BG_RAS_MAGIC_CHAR	((unsigned char) 0xff)
#define BG_RAS_TYPE_BINARY	((unsigned char) 0x82)
#define BG_RAS_TYPE_ASCII	((unsigned char) 0x88)
#define BG_OUTBOX_MSG_SIZE 248
	unsigned int 		outboxHead;
	unsigned int		outboxTail;
	unsigned int		outboxMsgAge;
#define BG_OUTBOX_MAX_AGE 36
	unsigned int		outboxRetry;
#define BG_OUTBOX_MAX_RETRY 2
	int			outboxMsgSent;
	struct task_struct*	kmboxdTask;
	/* Wait queue to wakeup kmboxd.  For now it runs strictly on timeout (polling),
	 * but in the future an interrupt or other means could wake it.
	 */
	wait_queue_head_t	wait;
} BG_CONSOLE;


static BG_CONSOLE bgc = {
	.tty = NULL,
	.ttyLock = SPIN_LOCK_UNLOCKED,
	.ttyDriver = NULL,
	.outboxLock = SPIN_LOCK_UNLOCKED,
	.outboxHead = 0,
	.outboxTail = 0,
	.outboxMsgAge = 0,
	.outboxRetry = 0,
	.outboxMsgSent = 0,
	.kmboxdTask = NULL,
};


#define BG_OUTBOX_HEAD_INCREMENT(i) bgc.outboxHead = (bgc.outboxHead + (i)) % BG_OUTBOX_BUFF_SIZE
#define BG_OUTBOX_TAIL_INCREMENT(i) bgc.outboxTail = (bgc.outboxTail + (i)) % BG_OUTBOX_BUFF_SIZE


/*  How many bytes of outbox buffer space are in use.  The caller must be */
/*  holding the outbox lock. */
static inline int __bgOutboxBufferUsed(void)
{
        int rc = 0;

        if (bgc.outboxHead <= bgc.outboxTail)
                rc = bgc.outboxTail - bgc.outboxHead;
        else
                rc = BG_OUTBOX_BUFF_SIZE - bgc.outboxHead + bgc.outboxTail;

        return rc;
}


/*  How many bytes of buffer space are in use. */
static inline int bgOutboxBufferUsed(struct tty_struct* tty)
{
        unsigned long flags;
        int rc;

        spin_lock_irqsave(&bgc.outboxLock, flags);
        rc = __bgOutboxBufferUsed();
        spin_unlock_irqrestore(&bgc.outboxLock, flags);

        return rc;
}


/*  How many bytes of outbox buffer space are unused.  The caller must be */
/*  holding the outbox lock. */
static inline int __bgOutboxBufferFree(void)
{
        int rc;

        if (bgc.outboxHead > bgc.outboxTail)
                rc = bgc.outboxHead - bgc.outboxTail;
        else
                rc = BG_OUTBOX_BUFF_SIZE - bgc.outboxTail + bgc.outboxHead;

        return rc;
}


/*  How many bytes of buffer space are free. */
static inline int bgOutboxBufferFree(struct tty_struct* tty)
{
        int rc;
        unsigned long flags;

        spin_lock_irqsave(&bgc.outboxLock, flags);
        rc = __bgOutboxBufferFree();
        spin_unlock_irqrestore(&bgc.outboxLock, flags);

        return rc;
}


/*  Append the specified data to the outbox buffer. */
static inline int __bgOutboxBufferAppend(unsigned char* data,
                                        unsigned int dataLen)
{
        int i = 0;

        while ((!dataLen && data[i]) || i < dataLen) {
                bgc.outboxBuff[bgc.outboxTail] = data[i++];
                if ((bgc.outboxTail + 1) % BG_OUTBOX_BUFF_SIZE != bgc.outboxHead)
                        bgc.outboxTail = (bgc.outboxTail + 1) % BG_OUTBOX_BUFF_SIZE;
                else
                        break;
        }

        return i;
}


/*  Remove the specified number of bytes from the outbox buffer. */
static inline int __bgOutboxBufferRemove(unsigned char* data,
                                         unsigned int dataLen)
{
        int i = 0;

        while (bgc.outboxHead != bgc.outboxTail && i < dataLen) {
                data[i++] = bgc.outboxBuff[bgc.outboxHead];
                bgc.outboxHead = (bgc.outboxHead + 1) % BG_OUTBOX_BUFF_SIZE;
        }

        return i;
}


/*  Search for the end of the line, starting at the specified index for the specified maximum length. */
/*  The end of a line is defined by the presence of a newline character or the RAS magic character or */
/*  the end of the buffer.  The number of bytes in the line are returned and 'index' is set to the */
/*  buffer index of the last character in the line.  If no line can be found zero is returned and */
/*  'index' is set to the buffer index of the last character examined.  The caller must ensure that */
/*  the outbox is locked. */
inline static int __bgOutboxBuffFindEOL(unsigned int* index, unsigned int maxLen) {
        int rc;
        int i = *index;
        int limit;
	int foundRAS = 0;

	 /*  Determine the limit of the search. */
	limit = (*index + maxLen - 1 < BG_OUTBOX_BUFF_SIZE - 1 ? *index + maxLen - 1 : BG_OUTBOX_BUFF_SIZE - 1);
	if (bgc.outboxTail > *index && limit > bgc.outboxTail -1)
		limit = bgc.outboxTail - 1;

         /*  Search for a newline. */
        while (i < limit && bgc.outboxBuff[i] != '\n') {
		if (bgc.outboxBuff[i] == BG_RAS_MAGIC_CHAR) {
			unsigned char nextChar = bgc.outboxBuff[(i+1) % BG_OUTBOX_BUFF_SIZE];

			if ((nextChar == BG_RAS_TYPE_BINARY || nextChar == BG_RAS_TYPE_ASCII) &&
			    (i+1) % BG_OUTBOX_BUFF_SIZE != bgc.outboxTail) {
				foundRAS = 1;
				break;
			}
		}
		i++;
	}
	if (bgc.outboxBuff[i] == '\n') {
		 /*  Found the end of a line. */
		rc = i - *index + 1;
		*index = i;
	} else if (foundRAS) {
		 /*  Ran into a RAS message so end the line. */
		rc = i - *index;;
		*index = i - 1;
	} else {
		 /*  Reached the search limit. */
                rc = 0;
                *index = i;
        }

        return rc;
}


/*  Send any buffered messages so long as the outbox is ready.  This function assumes that the caller is */
/*  holding the outbox buffer lock. */
int __bgFlushOutboxMsgs(void)
{
        int rc = 0;

         /*  Send buffered outbox messages as long as there is something to send and the mailbox is ready. */
        while (bgc.outboxHead != bgc.outboxTail && !bluegene_testForOutboxCompletion()) {
		unsigned char nextChar = bgc.outboxBuff[(bgc.outboxHead + 1) % BG_OUTBOX_BUFF_SIZE];

                 /*  We have a message to send.  Is it RAS or a console message? */
                if (bgc.outboxBuff[bgc.outboxHead] == BG_RAS_MAGIC_CHAR &&
		    (nextChar == BG_RAS_TYPE_BINARY || nextChar == BG_RAS_TYPE_ASCII) &&
		    (__bgOutboxBufferUsed() >= sizeof(bg_ras) + 2)) {
                         /*  Send a RAS message to the outbox. */
                        bg_ras ras;
                        int rc;

                         /*  Copy the RAS information out of the buffer into a form we can easily deal with. */
                        BG_OUTBOX_HEAD_INCREMENT(2);
                        rc = __bgOutboxBufferRemove((unsigned char*) &ras, sizeof(ras));

                         /*  Send the RAS. */
                        do {
                                if (nextChar == BG_RAS_TYPE_BINARY) {
                                         /*  Send binary RAS to the outox. */
                                        bgc.outboxMsgSent = !bluegene_writeRASEvent_nonBlocking(ras.comp, ras.subcomp, ras.code,
                                                                      ras.length / sizeof(int), (int*) ras.data);
                                } else if (nextChar == BG_RAS_TYPE_ASCII) {
                                         /*  Send ASCII RAS. */
                                        int sent = bluegene_writeRASString_nonBlocking(ras.comp, ras.subcomp, ras.code, ras.data);

                                        bgc.outboxMsgSent = (sent == 0 || sent == -2);
                                } else {
                                        bgprintf("Unknown RAS msg type %d\n", nextChar);
					break;
				}
                        } while (!bgc.outboxMsgSent && bgc.outboxRetry++ < BG_OUTBOX_MAX_RETRY);
                        if (!bgc.outboxMsgSent) {
                                bgprintf("Unable to send RAS (0x%02x 0x%02x 0x%02x\n", ras.comp, ras.subcomp, ras.code);
                                rc = -EIO;
                        }
                        bgc.outboxRetry = 0;
                } else {
			 /*  Send console messages. */
                        unsigned int EOL = bgc.outboxHead;
                        unsigned int msgLen = 0;
			unsigned int len;

                         /*  Group lines into an outbox-sized block of lines. */
			while (EOL != bgc.outboxTail && msgLen < BG_OUTBOX_MSG_SIZE &&
				(len = __bgOutboxBuffFindEOL(&EOL, BG_OUTBOX_MSG_SIZE - msgLen)) > 0) {
				 /*  Found another line.  Append it to the outbox message. */
				EOL = (EOL+1) % BG_OUTBOX_BUFF_SIZE;
				msgLen += len;
			}

                         /*  Determine if there are complete lines to print or if we should print a partial line. */
                        if (!msgLen) {
				unsigned int bytesAvailable = EOL - bgc.outboxHead + 1;

                                if (bytesAvailable == BG_OUTBOX_MSG_SIZE || bgc.outboxMsgAge++ >= BG_OUTBOX_MAX_AGE) {
                                         /*  Either we have a full outbox message or output is too old.  Send it now. */
                                        msgLen = bytesAvailable;
                                } else {
                                        rc = -EAGAIN;  // wait for more output
                                        break;
                                }
                        }

			 /*  Send any outbox message data. */
                        if (msgLen) {
                                bgc.outboxMsgSent = !bluegene_writeToMailboxConsole_nonBlocking(bgc.outboxBuff+bgc.outboxHead, msgLen);
                                if (bgc.outboxMsgSent || bgc.outboxRetry++ > BG_OUTBOX_MAX_RETRY) {
                                       	BG_OUTBOX_HEAD_INCREMENT(msgLen);
                                        bgc.outboxMsgAge = bgc.outboxRetry = 0;
                                        rc = (bgc.outboxMsgSent ? rc + 1 : -EIO);
                                } else {
                                        rc = -EAGAIN;
                                }
                        }
                }
        }

         /*  If a message was sent (now or during a past call) then check to see if the message has been */
         /*  taken so that we lower outbox attention ASAP. */
        if (bgc.outboxMsgSent && !bluegene_testForOutboxCompletion())
                bgc.outboxMsgSent = 0;

         /*  If there is something to send but the outbox wasn't ready then return -EWOULDBLOCK. */
        if (!rc && bgc.outboxHead != bgc.outboxTail)
                rc = -EWOULDBLOCK;

        return rc;
}


/*  Send any buffered messages so long as the outbox is ready.  This function locks the outbox before accessing it. */
inline int bgFlushOutboxMsgs(void)
{
        int rc;
        unsigned long flags;

        spin_lock_irqsave(&bgc.outboxLock, flags);
        rc = __bgFlushOutboxMsgs();
        spin_unlock_irqrestore(&bgc.outboxLock, flags);

        return rc;
}


/*  Add a console message to the outbox buffer. */
int bgWriteConsoleMsg(struct tty_struct* tty,
		      const unsigned char* msg,
                      int msgLen)
{
        int rc = 0;

	if (msgLen > 0) {
		unsigned long flags;

	         /*  Lock the outbox. */
        	spin_lock_irqsave(&bgc.outboxLock, flags);

	         /*  Copy the message to the buffer, wrapping around if necessary. */
        	rc = __bgOutboxBufferAppend((char*) msg, (unsigned int) msgLen);

	         /*  Unlock outbox. */
        	spin_unlock_irqrestore(&bgc.outboxLock, flags);
	}

        return rc;
}

static DEFINE_MUTEX(bgWriteConsoleBlockDirect_mutex);

/* Write a console msg in block mode. This function can be called from other kernel code. */
int bgWriteConsoleBlockDirect(const char* fmt,...)
{
    int rc = 0;
    va_list  args;
    static char buf[256];
    int len;

    mutex_lock(&bgWriteConsoleBlockDirect_mutex);
    va_start(args,fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    len = strlen(buf);

    if(len>0) bgWriteConsoleMsg(bgc.tty, buf,len);
    mutex_unlock(&bgWriteConsoleBlockDirect_mutex);

    return rc;
}


/*  Add a binary RAS event to the outbox buffer.  If the buffer is full this function flushes */
/*  outbox messages to free buffer space. */
int bgWriteRasEvent(unsigned int component,
                    unsigned int subcomponent,
                    unsigned int errCode,
                    unsigned int data[],
                    unsigned int dataLen)
{
        int rc = 1;
        unsigned long flags;
        bg_ras ras;

         /*  Lock the outbox buffer. */
        spin_lock_irqsave(&bgc.outboxLock, flags);

         /*  If insufficient buffer space exists then flush outbox messages until we free enough space. */
        while (__bgOutboxBufferFree() < sizeof(ras) + 2)
                __bgFlushOutboxMsgs();

         /*  Initialize the RAS structure. */
        ras.comp = component;
        ras.subcomp = subcomponent;
        ras.code = errCode;
        ras.length = (dataLen <= sizeof(ras.data) ? dataLen : sizeof(ras.data));
        memcpy(ras.data, (char*) data, ras.length);

         /*  Copy the RAS information to the outbox buffer. */
        bgc.outboxBuff[bgc.outboxTail] = BG_RAS_MAGIC_CHAR;
        BG_OUTBOX_TAIL_INCREMENT(1);
	bgc.outboxBuff[bgc.outboxTail] = BG_RAS_TYPE_BINARY;
	BG_OUTBOX_TAIL_INCREMENT(1);
        rc = __bgOutboxBufferAppend((unsigned char*) &ras, sizeof(ras));

         /*  Unlock the outbox buffer. */
        spin_unlock_irqrestore(&bgc.outboxLock, flags);

        return rc;
}


/*  Add an ASCII RAS event to the outbox buffer.  If the buffer is full this function flushes */
/*  outbox messages to free buffer space. */
int bgWriteRasStr(unsigned int component,
                  unsigned int subcomponent,
                  unsigned int errCode,
                  char*        str,
                  unsigned int strLen)
{
        int rc = 1;
        unsigned long flags;
        bg_ras ras;

         /*  Lock the outbox buffer. */
        spin_lock_irqsave(&bgc.outboxLock, flags);

         /*  If insufficient buffer space exists then flush outbox messages until we free enough space. */
        while (__bgOutboxBufferFree() < sizeof(ras) + 2)
                __bgFlushOutboxMsgs();

         /*  Initialize the RAS structure. */
        ras.comp = component;
        ras.subcomp = subcomponent;
        ras.code = errCode;
        if (!strLen || strLen > sizeof(ras.data))
                strLen = sizeof(ras.data)-1;
        for (ras.length = 0; *str && ras.length < strLen; str++, ras.length++)
                ras.data[ras.length] = *str;
	ras.data[ras.length] = '\0';

         /*  Copy the RAS information to the outbox buffer. */
        bgc.outboxBuff[bgc.outboxTail] = BG_RAS_MAGIC_CHAR;
        BG_OUTBOX_TAIL_INCREMENT(1);
        bgc.outboxBuff[bgc.outboxTail] = BG_RAS_TYPE_ASCII;
        BG_OUTBOX_TAIL_INCREMENT(1);
        rc = __bgOutboxBufferAppend((unsigned char*) &ras, sizeof(ras));

         /*  Unlock the outbox buffer. */
        spin_unlock_irqrestore(&bgc.outboxLock, flags);

        return rc;
}


static int bluegenecons_open(struct tty_struct *tty, struct file * filp)
{
	if (tty->count == 1) {
		bgc.tty = tty;
		tty->driver_data = &bgc;
	}

	return 0;
}

static void bluegenecons_close(struct tty_struct *tty, struct file * filp)
{
	if (tty && tty->count == 1) {
		bgc.tty = NULL;
	}

	return;
}


#define BLUEGENECONS_MAGIC_SYSRQ_KEY (15)       /* ^O */

static void bluegenecons_rcv(char *msg, int msglen)
{
	struct tty_struct *tty;
	unsigned long flags;
	static int sysrq_mode;

	spin_lock_irqsave(&bgc.ttyLock, flags);
	tty = bgc.tty;
	if (tty) {
		while (msglen) {
			int i;
			int count = tty_buffer_request_room(tty, msglen);

			for (i = 0; i < count; i++) {
				if (sysrq_mode) {
					handle_sysrq(msg[i], tty);
					sysrq_mode = 0;
				} else if (msg[i] == BLUEGENECONS_MAGIC_SYSRQ_KEY)
					sysrq_mode = 1;
				else
					tty_insert_flip_char(tty, msg[i], 0);
			}
			msglen -= count;
			msg += count;
			tty_flip_buffer_push(tty);
		}
	}
	spin_unlock_irqrestore(&bgc.ttyLock, flags);

	return;
}


/*
 * Mailbox polling kernel thread.
 *
 * This thread wakes up at intervals to check for inbound mailbox messages
 * and it will send waiting outbound messages if the outbound box is free.
 */
int kmboxd(void *arg)
{
	__set_current_state(TASK_RUNNING);
	do {
		int rc;

		 /*  If there is anything in the inbox read it now. */
		if (bluegene_testInboxAttention()) {
			static char buffer[512];
			int len;

			/* Fetch any input */
			len = bluegene_readFromMailboxConsole(buffer, sizeof(buffer));
			if (len > 0)
				bluegenecons_rcv(buffer, len);
		}

		 /*  Flush any console output that is buffered. */
		rc = bgFlushOutboxMsgs();

		 /*  If outbox buffer data was written then wake any TTY writer */
		 /*  that is waiting. */
                if (rc > 0 && bgc.tty) {
                        if ((bgc.tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
                            && bgc.tty->ldisc.ops->write_wakeup)
                                (bgc.tty->ldisc.ops->write_wakeup)(bgc.tty);
                 	wake_up_interruptible(&bgc.tty->write_wait);
                }

		wait_event_interruptible_timeout(bgc.wait, 0, msecs_to_jiffies(10));
	} while (!kthread_should_stop());

	return 0;
}


#ifdef CONFIG_MAGIC_SYSRQ

extern void ctrl_alt_del(void);

static int bluegene_do_sysrq(void* data)
{
	int key = (int) data;
	static char* env[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
                               "LD_LIBRARY_PATH=/lib:/usr/lib", NULL };

	switch(key) {
		case 'h' :
		{
			static char* argv[] = { "/etc/rc.shutdown", NULL };

			kernel_execve(argv[0], argv, env);
			printk(KERN_EMERG "Failure halting I/O node.  Attempting secondary method.\n");
			ctrl_alt_del();
			break;
		}

		case 'x' :
		{
			static char* argv[] = { "/etc/rc.reboot", NULL };

			kernel_execve(argv[0], argv, env);
			printk(KERN_EMERG "Failure rebooting I/O node.\n");
			break;
		}

		default :
			printk(KERN_EMERG "Unknown sysrq '%c'\n", key);
	}

 	return 0;
}


static void bluegene_handle_sysrq(int key, struct tty_struct *tty)
{
	struct task_struct* t = kthread_run(bluegene_do_sysrq, (void*) key, "Process System Request");

	if (IS_ERR(t)) {
		printk(KERN_EMERG "Failure creating sysrq '%c' thread.\n", (char) key);
		bgWriteRasStr(bg_comp_kernel, bg_subcomp_linux, bg_code_sysrq_thread_create_failure,
				"Failure creating sysrq thread.", 0);
		if (key == 'h')
			ctrl_alt_del();
	}

        return;
}

static struct sysrq_key_op bg_sysrq_halt_op = {
        .handler =        bluegene_handle_sysrq,
        .help_msg =       "Halt",
        .action_msg =     "Halt node"
};

static struct sysrq_key_op bg_sysrq_reboot_op = {
        .handler =        bluegene_handle_sysrq,
        .help_msg =       "Reboot",
        .action_msg =     "Reboot node"
};
#endif


static struct tty_operations bgcons_ops = {
	.open = bluegenecons_open,
	.close = bluegenecons_close,
	.write = bgWriteConsoleMsg,
	.write_room = bgOutboxBufferFree,
	.chars_in_buffer = bgOutboxBufferUsed,
};


/* Read interface not defined so we just return EOF */
static int bluegene_rasevent_read(char *page, char **start, off_t off,
                             int count, int *eof, void *data)
{
        return 0;
}


/* Write the event.  The user provides the payload...we provide the rest.
 */
static int bluegene_rasevent_write(struct file *file, const char *buffer,
                           unsigned long len, void *data)
{
        bg_ras ras;

         /*  Truncate the message if it is too large. */
        if (len > sizeof(ras))
                len = sizeof(ras);
        else if (len < ((unsigned long) &ras.data - (unsigned long) &ras))
                return -EIO;

        if (copy_from_user(&ras, buffer, len))
                return -EFAULT;
        else {
                if (!data)
                        bgWriteRasEvent(ras.comp, ras.subcomp, ras.code,
                                        (unsigned int*) ras.data, ras.length);
                else {
                         /*  ASCII detail data was written. */
                        if (!ras.length)
                                ras.data[0] = '\0';
                        bgWriteRasStr(ras.comp, ras.subcomp, ras.code,
                                        ras.data, ras.length);
                }
        }

        return len;
}


static inline char* entryName(char* path)
{
        char* lastSlash = NULL;

        while (*path) {
                if (*path == '/')
                        lastSlash = path + 1;
                path++;
        }

        return lastSlash;
}


static int __init bluegenecons_init(void)
{

	bgc.ttyDriver = alloc_tty_driver(1);
	if (!bgc.ttyDriver) {
		char* msg = "Failure allocating BlueGene console driver.";

		bgprintf(msg);
		bluegene_writeRASString(bg_comp_kernel, bg_subcomp_linux, bg_code_tty_alloc_failure, msg);
		return -EIO;
	}

	bgc.ttyDriver->owner = THIS_MODULE;
	bgc.ttyDriver->name = "bgcons";
	bgc.ttyDriver->name_base = 1;
	bgc.ttyDriver->major = BLUEGENE_MAJOR;
	bgc.ttyDriver->minor_start = BLUEGENE_MINOR;
	bgc.ttyDriver->type = TTY_DRIVER_TYPE_SYSTEM;
	bgc.ttyDriver->init_termios = tty_std_termios;
	bgc.ttyDriver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(bgc.ttyDriver, &bgcons_ops);

	if (tty_register_driver(bgc.ttyDriver)) {
		char* msg = "Failure registering BlueGene console driver";

		bgprintf(msg);
		bluegene_writeRASString(bg_comp_kernel, bg_subcomp_linux, bg_code_tty_reg_failure, msg);
		return -EIO;
	}

#ifdef CONFIG_MAGIC_SYSRQ
        /* Sysrq h is sent by the control system to halt an ION during free_block */
        register_sysrq_key('h', &bg_sysrq_halt_op);

        /* Sysrq x is sent by the control system when ION reboot is requested. */
        register_sysrq_key('x', &bg_sysrq_reboot_op);
#endif

	/* Kick off the kernel mailbox poll thread. */
	init_waitqueue_head(&bgc.wait);
	bgc.kmboxdTask = kthread_run(kmboxd, NULL, "kmboxd");
	if (IS_ERR(bgc.kmboxdTask)) {
		char* msg = "Failure creating mailbox processing thread.";

		bgprintf(msg);
		bluegene_writeRASString(bg_comp_kernel, bg_subcomp_linux, bg_code_mbox_thread_create_failure, msg);
		put_tty_driver(bgc.ttyDriver);
		return -EIO;
	}

	 /*  Create /proc RAS interfaces. */
	proc_ras = create_proc_entry(entryName(BG_RAS_FILE), S_IFREG | S_IRWXUGO, NULL);
	if (proc_ras) {
		proc_ras->nlink = 1;
		proc_ras->read_proc = (void*) bluegene_rasevent_read;
		proc_ras->write_proc = (void*) bluegene_rasevent_write;
		proc_ras->data = (void*) 0; // not ASCII message
	}
	proc_ras_ascii = create_proc_entry(entryName(BG_RAS_ASCII_FILE), S_IFREG | S_IRWXUGO, NULL);
	if (proc_ras_ascii) {
		proc_ras_ascii->nlink = 1;
		proc_ras_ascii->read_proc = (void*) bluegene_rasevent_read;
		proc_ras_ascii->write_proc = (void*) bluegene_rasevent_write;
		proc_ras_ascii->data = (void*) 1; // is ASCII message
	}

	return 0;
}

static void __exit bluegenecons_exit(void)
{
	if (proc_ras) {
		remove_proc_entry(proc_ras->name, NULL);
		proc_ras = NULL;
	}
	if (proc_ras_ascii) {
		remove_proc_entry(proc_ras_ascii->name, NULL);
		proc_ras_ascii = NULL;
	}

	return;
}

/*
 * Console write.
 */
static void bluegene_console_write(struct console *co, const char *b, unsigned count)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    if( !enable_console ) return; 
#endif
	if (count > 0)
		bgWriteConsoleMsg(bgc.tty, b, count);
}


static struct tty_driver *bluegene_console_device(struct console *c, int *ip)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    if( !enable_console ) return NULL;
#endif

	*ip = 0;
	return bgc.ttyDriver;
}


static struct console bgcons = {
        .name   = "bgcons",
        .write  = bluegene_console_write,
        .device = bluegene_console_device,
        .flags  = CON_PRINTBUFFER,
        .index  = 0,
};


#ifdef CONFIG_ZEPTO_COMPUTENODE
void zepto_enable_console(int i) { 
    enable_console = i; 
}

static int  zepto_enable_console_write(struct file *file, const char *buffer,
				       unsigned long len, void *data)
{
    char tmp[2];

    if( len > 2 ) len = 2;

    if(copy_from_user(tmp, buffer,len) == 0 ) {
	if( tmp[0] == '1' )  zepto_enable_console(1);
	else                 zepto_enable_console(0);
    } else {
	return -EFAULT;
    }

    return len;
}

int __init  zepto_enable_console_proc_init(void)
{
    struct proc_dir_entry *p_zepto_enable_console;
    p_zepto_enable_console = create_proc_entry("zepto_enable_console", S_IFREG|S_IRUGO|S_IWUGO, NULL );
    if( p_zepto_enable_console ) {
	p_zepto_enable_console->nlink = 1;
	p_zepto_enable_console->write_proc = zepto_enable_console_write; 
    } else {
	printk("Failed to register /proc/zepto_enable_console\n");
    }
    return 0;
}
__initcall(zepto_enable_console_proc_init);

#endif

int __init bluegene_console_init(void)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    char* optstr = "zepto_console_output=";
    int  zepto_console_output = 1; /* 0=disable 1=onenode 2=all */


    /* zepto_debug(1, "'%s' '%s'\n",saved_command_line,optstr); */
    /* FIXME: not sure cmd_line is truncated for some reason.
            so just using saved_command_line here but not sure
            this is right solution or not */
    if(strstr(saved_command_line, optstr) ) {
        char* p;
        p = strstr( saved_command_line, optstr );
        if( p && (strlen(p)-strlen(optstr))>0 ) {
            p=p+strlen(optstr);
            zepto_console_output=simple_strtoul(p,&p,0);
        }
    }

    enable_console = 0;
    if( zepto_console_output==1) {
        BGP_Personality_t bgpers;
        bluegene_getPersonality(&bgpers, sizeof(bgpers));
        if( bgpers.Network_Config.Rank == 0 ) enable_console = 1;
    } else if( zepto_console_output>=2) {
        enable_console = 1;
    }


/* #else */
/*     enable_console = 1; */
#endif
    register_console(&bgcons);

    return 0;
}


module_init(bluegenecons_init);
module_exit(bluegenecons_exit);
console_initcall(bluegene_console_init);

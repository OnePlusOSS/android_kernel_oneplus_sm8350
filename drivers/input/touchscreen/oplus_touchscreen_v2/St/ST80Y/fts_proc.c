// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include "fts.h"

#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "st-proc"
#else
#define TPD_DEVICE "st-proc"
#endif

#define DRIVER_TEST_FILE_NODE    "driver_test"    /* name of file node published */
#define CHUNK_PROC              1024  /* Max chunk of data printed on the sequential file in each iteration */
#define DIAGNOSTIC_NUM_FRAME    10    /* number of frames reading iterations during the diagnostic test */

/** @defgroup proc_file_code     Proc File Node
  * @ingroup file_nodes
  * The /proc/fts/driver_test file node provide expose the most important API
  * implemented into the driver to execute any possible operation into the IC \n
  * Thanks to a series of Operation Codes, each of them, with a different set of
  * parameter, it is possible to select a function to execute\n
  * The result of the function is usually returned into the shell as an ASCII
  * hex string where each byte is encoded in two chars.\n
  */

/* Bus operations */
#define CMD_READ                0x00    /* /< I2C/SPI read: need
										* to pass: byteToRead1
										* byteToRead0
										* (optional) dummyByte */
#define CMD_WRITE               0x01    /* /< I2C/SPI write:
										* need to pass: cmd[0]
										*  cmd[1]
										* cmd[cmdLength-1] */
#define CMD_WRITEREAD           0x02    /* /< I2C/SPI writeRead:
										* need to pass: cmd[0]
										* cmd[1]
										* cmd[cmdLength-1]
										* byteToRead1
										* byteToRead0 dummyByte */
#define CMD_WRITETHENWRITEREAD  0x03    /* /< I2C/SPI write then
										* writeRead: need to
										* pass: cmdSize1
										* cmdSize2 cmd1[0]
										* cmd1[1]
										* cmd1[cmdSize1-1]
										* cmd2[0] cmd2[1]
										* cmd2[cmdSize2-1]
										* byteToRead1
										* byteToRead0 */
#define CMD_WRITEU8UX           0x04    /* /< I2C/SPI writeU8UX:
										* need to pass: cmd
										* addrSize addr[0]
										* addr[addrSize-1]
										* data[0] data[1]  */
#define CMD_WRITEREADU8UX       0x05    /* /< I2C/SPI
										* writeReadU8UX: need
										* to pass: cmd addrSize
										* addr[0]
										* addr[addrSize-1]
										* byteToRead1
										* byteToRead0
										* hasDummyByte */
#define CMD_WRITEU8UXTHENWRITEU8UX          0x06        /* /< I2C/SPI writeU8UX
														* then writeU8UX: need
														* to pass: cmd1
														* addrSize1 cmd2
														* addrSize2 addr[0]
														* addr[addrSize1+addrSize2-1]
														* data[0] data[1]  */
#define CMD_WRITEU8UXTHENWRITEREADU8UX      0x07        /* /< I2C/SPI writeU8UX
														*  then writeReadU8UX:
														* need to pass: cmd1
														* addrSize1 cmd2
														* addrSize2 addr[0]
														* addr[addrSize1+addrSize2-1]
														*  byteToRead1
														* byteToRead0
														* hasDummybyte */
#define CMD_GETLIMITSFILE                   0x08        /* /< Get the Production
														* Limits File && print
														* its content into the
														* shell: need to pass:
														* path(optional)
														* otherwise select the
														* approach chosen at
														* compile time */
#define CMD_GETFWFILE                       0x09        /* /< Get the FW file
														* && print its content
														* into the shell: need
														* to pass: path
														* (optional) otherwise
														* select the approach
														* chosen at compile
														* time */
#define CMD_READCONFIG                      0x0B        /* /< Read The config
														* memory, need to pass:
														* addr[0] addr[1]
														* byteToRead0
														* byteToRead1 */


/* GUI utils byte ver */
#define CMD_READ_BYTE                   0xF0    /* /< Byte output
												* version of I2C/SPI
												* read @see CMD_READ */
#define CMD_WRITE_BYTE                  0xF1    /* /< Byte output
												* version of I2C/SPI
												* write @see CMD_WRITE */
#define CMD_WRITEREAD_BYTE              0xF2    /* /< Byte output
												* version of I2C/SPI
												* writeRead @see
												* CMD_WRITEREAD */
#define CMD_WRITETHENWRITEREAD_BYTE     0xF3    /* /< Byte output
												* version of I2C/SPI
												* write then writeRead
												* @see
												* CMD_WRITETHENWRITEREAD */
#define CMD_WRITEU8UX_BYTE              0xF4    /* /< Byte output
												* version of I2C/SPI
												* writeU8UX @see
												* CMD_WRITEU8UX */
#define CMD_WRITEREADU8UX_BYTE          0xF5    /* /< Byte output
												* version of I2C/SPI
												* writeReadU8UX @see
												* CMD_WRITEREADU8UX */
#define CMD_WRITEU8UXTHENWRITEU8UX_BYTE         0xF6    /* /< Byte output
														* version of I2C/SPI
														* writeU8UX then
														* writeU8UX @see
														* CMD_WRITEU8UXTHENWRITEU8UX */
#define CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE     0xF7    /* /< Byte output
														* version of I2C/SPI
														* writeU8UX  then
														* writeReadU8UX @see
														* CMD_WRITEU8UXTHENWRITEREADU8UX */
#define CMD_GETLIMITSFILE_BYTE                  0xF8    /* /< Byte output
														* version of Production
														* Limits File @see
														* CMD_GETLIMITSFILE */
#define CMD_GETFWFILE_BYTE                      0xF9    /* /< Byte output
														* version of FW file
														* need to pass: @see
														* CMD_GETFWFILE */

#define CMD_CHANGE_OUTPUT_MODE                  0xFF    /* /< Select the output
														* mode of the
														* scriptless protocol,
														* need to pass:
														* bin_output = 1 data
														* returned as binary,
														* bin_output = 0 data
														* returned as hex
														* string */

/* Core/Tools */
#define CMD_POLLFOREVENT            0x11    /* /< Poll the FIFO for
											* an event: need to
											* pass: eventLength
											* event[0] event[1]
											* event[eventLength-1]
											* timeToWait1
											* timeToWait0 */
#define CMD_SYSTEMRESET             0x12    /* /< System Reset */
#define CMD_CLEANUP                 0x13    /* /< Perform a system
											* reset && optionally
											* re-enable the
											* scanning, need to
											* pass: enableTouch */
#define CMD_POWERCYCLE              0x14    /* /< Execute a power
											* cycle toggling the
											* regulators */
#define CMD_READSYSINFO             0x15    /* /< Read the System
											* Info information from
											* the framebuffer, need
											* to pass: doRequest */
#define CMD_FWWRITE                 0x16    /* /< Write a FW
											* command: need to
											* pass: cmd[0]  cmd[1]
											* cmd[cmdLength-1] */
#define CMD_INTERRUPT               0x17    /* /< Allow to enable or
											* disable the
											* interrupts, need to
											* pass: enable (if 1
											* will enable the
											* interrupt) */
#define CMD_SETSCANMODE             0x18    /* /< set Scan Mode
											* need to pass:
											* scanType option */
#define CMD_SAVEMPFLAG              0x19    /* /< save manually a
											* value in the MP flag
											* need to pass: mpflag */

/* Frame */
#define CMD_GETFORCELEN             0x20    /* /< Get the number of
											* Force channels */
#define CMD_GETSENSELEN             0x21    /* /< Get the number of
											* Sense channels */
#define CMD_GETMSFRAME              0x23    /* /< Get a MS frame:
											* need to pass:
											* MSFrameType */
#define CMD_GETSSFRAME              0x24    /* /< Get a SS frame:
											* need to pass:
											* SSFrameType */
#define CMD_GETSYNCFRAME            0x25    /* /< Get a Sync Frame:
											* need to pass:
											* frameType */

/* Compensation */
#define CMD_REQCOMPDATA             0x30    /* /< Request Init data:
											* need to pass: type */
#define CMD_READCOMPDATAHEAD        0x31    /* /< Read Init data
											* header: need to pass:
											* type */
#define CMD_READMSCOMPDATA          0x32    /* /< Read MS Init data:
											* need to pass: type */
#define CMD_READSSCOMPDATA          0x33    /* /< Read SS Init data:
											* need to pass: type */
#define CMD_READTOTMSCOMPDATA       0x35    /* /< Read Tot MS Init
											* data: need to pass:
											* type */
#define CMD_READTOTSSCOMPDATA       0x36    /* /< Read Tot SS Init
											* data: need to pass:
											* type */

/* FW Update */
#define CMD_GETFWVER                0x40    /* /< Get the FW version
											* of the IC */
#define CMD_FLASHUNLOCK             0x42    /* /< Unlock the flash */
#define CMD_READFWFILE              0x43    /* /< Try to read the FW
											* file, need to pass:
											* keep_cx */
#define CMD_FLASHPROCEDURE          0x44    /* /< Perform a full
											* flashing procedure:
											* need to pass: force
											* keep_cx */
#define CMD_FLASHERASEUNLOCK        0x45    /* /< Unlock the erase
											* of the flash */
#define CMD_FLASHERASEPAGE          0x46    /* /< Erase page by page
											* the flash, need to
											* pass: keep_cx, if
											* keep_cx>SKIP_PANEL_INIT
											* Panel Init Page will
											* be skipped, if
											* >SKIP_PANEL_CX_INIT
											* Cx && Panel Init
											* Pages will be skipped
											* otherwise if
											* = ERASE_ALL all the
											* pages will be deleted */


/* MP test */
#define CMD_ITOTEST                 0x50    /* /< Perform an ITO
											* test */
#define CMD_INITTEST                0x51    /* /< Perform an
											* Initialization test:
											* need to pass: type */
#define CMD_MSRAWTEST               0x52    /* /< Perform MS raw
											* test: need to pass
											* stop_on_fail */
#define CMD_MSINITDATATEST          0x53    /* /< Perform MS Init
											* data test: need to
											* pass stop_on_fail */
#define CMD_SSRAWTEST               0x54    /* /< Perform SS raw
											* test: need to pass
											* stop_on_fail */
#define CMD_SSINITDATATEST          0x55    /* /< Perform SS Init
											* data test: need to
											* pass stop_on_fail */
#define CMD_MAINTEST                0x56    /* /< Perform a full
											* Mass production test:
											* need to pass
											* stop_on_fail saveInit
											* mpflag */
#define CMD_FREELIMIT               0x57    /* /< Free (if any)
											* limit file which was
											* loaded during any
											* test procedure */

/* Diagnostic */
#define CMD_DIAGNOSTIC              0x60    /* /< Perform a series
											* of commands &&
											* collect severals data
											* to detect any
											* malfunction */



/** @defgroup scriptless Scriptless Protocol
  * @ingroup proc_file_code
  * Scriptless Protocol allows ST Software (such as FingerTip Studio etc) to
  * communicate with the IC from an user space.
  * This mode gives access to common bus operations (write, read etc) and
  * support additional functionalities. \n
  * The protocol is based on exchange of binary messages included between a
  * start and an end byte
  */

#define MESSAGE_START_BYTE      0x7B    /* start byte of each message
										* transferred in Scriptless Mode */
#define MESSAGE_END_BYTE        0x7D    /* end byte of each message
										* transferred in Scriptless Mode */
#define MESSAGE_MIN_HEADER_SIZE 8       /* minimun number of bytes of the
										* structure of a messages exchanged
										* with host(include start/end byte,
										* counter, actions, msg_size) */

/**
  * Possible actions that can be requested by an host
  */
typedef enum {
	ACTION_WRITE                = (u16) 0x0001,    /* Bus Write */
	ACTION_READ                 = (u16) 0x0002,    /* Bus Read */
	ACTION_WRITE_READ           = (u16) 0x0003,    /* Bus Write followed by a Read */
	ACTION_GET_VERSION          = (u16) 0x0004,    /* Get
													* Version of
													* the protocol
													* (equal to the
													* first 2 bye
													* of driver
													* version) */
	ACTION_WRITEU8UX            = (u16) 0x0011,    /* Bus Write
													* with support
													* to different
													* address size */
	ACTION_WRITEREADU8UX        = (u16) 0x0012,    /* Bus writeRead
													* with support
													* to different
													* address size */
	ACTION_WRITETHENWRITEREAD   = (u16) 0x0013,    /* Bus write
													* followed by a
													* writeRead */
	ACTION_WRITEU8XTHENWRITEREADU8UX    = (u16) 0x0014,    /* Bus write
													* followed by a
													* writeRead
													* with support
													* to different
													* address size */
	ACTION_WRITEU8UXTHENWRITEU8UX       = (u16) 0x0015,    /* Bus write
															* followed by a
															* write with
															* support to
															* different
															* address size */
	ACTION_GET_FW               = (u16) 0x1000,    /* Get Fw
													* file content
													* used by the
													* driver */
	ACTION_GET_LIMIT            = (u16) 0x1001     /* /< Get Limit
													* File content
													* used by the
													* driver */
} Actions;

/**
  * Struct used to contain info of the message received by the host in
  * Scriptless mode
  */
typedef struct {
	u16         msg_size;   /* /< total size of the message in bytes */
	u16         counter;    /* /< counter ID to identify a message */
	Actions
	action;     /* /< type of operation requested by the host @see Actions */
	u8
	dummy;      /* /< (optional)in case of any kind of read operations, specify if the first byte is dummy */
} Message;

struct st80yProcData {
	int limit;	/* /< store the amount of data to print into the shell */
	int chunk;	/* /< store the chuk of data that should be printed in this iteration */
	int printed; /* /< store the amount of data already printed in the shell */
	struct proc_dir_entry
		*fts_dir;	/* /< reference to the directory fts under /proc */
	u8 *driver_test_buff;			/* /< pointer to an array of bytes used to store the result of the function executed */
	char buf_chunk[CHUNK_PROC];	   /* /< buffer used to store the message info received */
	Message mess;			/* /< store the information of the Scriptless message received */
	u8 bin_output;		/* /< Select the output type of the scriptless
								 * protocol(binary = 1  or hex string = 0) */
};

static struct st80yProcData *gSt80yProcData = NULL;

/**
  * Convert an array of bytes to an u64, src has MSB first (big endian).
  * @param src array of bytes
  * @param dest pointer to the destination u64.
  * @param size size of src (can be <= 8)
  * @return OK if success or ERROR_OP_NOT_ALLOW if size exceed 8
  */
static int u8ToU64_be(u8 *src, u64 *dest, int size)
{
	int i = 0;

	/* u64 temp =0; */
	if (size > sizeof(u64))
		return -1;

	else {
		*dest = 0;

		for (i = 0; i < size; i++)
			*dest |= (u64)(src[i]) << ((size - 1 - i) * 8);

		return 0;
	}
}

/**
  * Convert an array of 2 bytes to a u16, src has MSB first (big endian).
  * @param src pointer to the source byte array
  * @param dst pointer to the destination u16.
  * @return OK
  */
static int u8ToU16_be(u8 *src, u16 *dst)
{
	*dst = (u16)(((src[0] & 0x00FF) << 8) + (src[1] & 0x00FF));
	return OK;
}

/************************ SEQUENTIAL FILE UTILITIES **************************/
/**
  * This function is called at the beginning of the stream to a sequential file
  * or every time into the sequential were already written PAGE_SIZE bytes and
  * the stream need to restart
  * @param s pointer to the sequential file on which print the data
  * @param pos pointer to the offset where write the data
  * @return NULL if there is no data to print or the pointer to the beginning of
  * the data that need to be printed
  */
static void *fts_seq_start(struct seq_file *s, loff_t *pos)
{
	struct st80yProcData *St80yProcData = NULL;

	St80yProcData = gSt80yProcData;

	TPD_DEBUG("%s: Entering start(), pos = %lld limit = %d printed = %d\n",
		  __func__, *pos, St80yProcData->limit, St80yProcData->printed);

	if (St80yProcData->driver_test_buff == NULL && *pos == 0) {
		TPD_INFO("%s: No data to print!\n", __func__);
		St80yProcData->driver_test_buff = (u8 *)kzalloc(13 * sizeof(u8), GFP_KERNEL);

		snprintf(St80yProcData->driver_test_buff, 14, "{ %08X }\n", ERROR_OP_NOT_ALLOW);

		St80yProcData->limit = strlen(St80yProcData->driver_test_buff);
		/* TPD_DEBUG("%s: len = %d driver_test_buff = %s\n", __func__, limit, driver_test_buff); */

	} else {
		if (*pos != 0)
			*pos += St80yProcData->chunk - 1;

		if (*pos >= St80yProcData->limit)
			/* TPD_DEBUG("%s: Apparently, we're done.\n", __func__); */
			return NULL;
	}

	St80yProcData->chunk = CHUNK_PROC;

	if (St80yProcData->limit - *pos < CHUNK_PROC)
		St80yProcData->chunk = St80yProcData->limit - *pos;

	/* TPD_DEBUG("%s: In start(), updated pos = %ld limit = %d printed = %d chunk = %d\n",__func__, *pos, limit, printed, chunk); */
	memset(St80yProcData->buf_chunk, 0, CHUNK_PROC);
	memcpy(St80yProcData->buf_chunk, &St80yProcData->driver_test_buff[(int)*pos],
	       St80yProcData->chunk);

	return St80yProcData->buf_chunk;
}

/**
  * This function actually print a chunk amount of data in the sequential file
  * @param s pointer to the sequential file where to print the data
  * @param v pointer to the data to print
  * @return 0
  */
static int fts_seq_show(struct seq_file *s, void *v)
{
	struct st80yProcData *St80yProcData = NULL;

	St80yProcData = gSt80yProcData;

	/* TPD_DEBUG("%s: In show()\n", __func__); */
	seq_write(s, (u8 *)v, St80yProcData->chunk);
	St80yProcData->printed += St80yProcData->chunk;
	return 0;
}

/**
  * This function update the pointer and the counters to the next data to be
  * printed
  * @param s pointer to the sequential file where to print the data
  * @param v pointer to the data to print
  * @param pos pointer to the offset where write the next data
  * @return NULL if there is no data to print or the pointer to the beginning of
  * the next data that need to be printed
  */
static void *fts_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct st80yProcData *St80yProcData = NULL;

	St80yProcData = gSt80yProcData;

	/* int* val_ptr; */
	/* TPD_DEBUG("%s: In next(), v = %X, pos = %ld.\n", __func__, v, *pos); */
	(*pos) += St80yProcData->chunk;/* increase my position counter */
	St80yProcData->chunk = CHUNK_PROC;

	/* TPD_DEBUG("%s: In next(), updated pos = %ld limit = %d printed = %d\n", __func__, *pos, limit,printed); */
	if (*pos >= St80yProcData->limit)    /* are we done? */
		return NULL;

	else if (St80yProcData->limit - *pos < CHUNK_PROC)
		St80yProcData->chunk = St80yProcData->limit - *pos;

	memset(St80yProcData->buf_chunk, 0, CHUNK_PROC);
	memcpy(St80yProcData->buf_chunk, &St80yProcData->driver_test_buff[(int)*pos],
	       St80yProcData->chunk);
	return St80yProcData->buf_chunk;
}


/**
  * This function is called when there are no more data to print  the stream
  * need to be terminated or when PAGE_SIZE data were already written into the
  * sequential file
  * @param s pointer to the sequential file where to print the data
  * @param v pointer returned by fts_seq_next
  */
static void fts_seq_stop(struct seq_file *s, void *v)
{
	struct st80yProcData *St80yProcData = NULL;

	St80yProcData = gSt80yProcData;

	/* TPD_DEBUG("%s: Entering stop().\n", __func__); */

	if (v) {
		/* TPD_DEBUG("%s: v is %X.\n", __func__, v); */
	} else {
		/* TPD_DEBUG("%s: v is null.\n", __func__); */
		St80yProcData->limit = 0;
		St80yProcData->chunk = 0;
		St80yProcData->printed = 0;

		if (St80yProcData->driver_test_buff != NULL) {
			/* TPD_DEBUG("%s: Freeing and clearing driver_test_buff.\n", __func__); */
			kfree(St80yProcData->driver_test_buff);
			St80yProcData->driver_test_buff = NULL;

		} else {
			/* TPD_DEBUG("%s: driver_test_buff is already null.\n", __func__); */
		}
	}
}

/**
  * Struct where define and specify the functions which implements the flow for
  * writing on a sequential file
  */
static const struct seq_operations fts_seq_ops = {
	.start   = fts_seq_start,
	.next    = fts_seq_next,
	.stop    = fts_seq_stop,
	.show    = fts_seq_show
};

/**
  * This function open a sequential file
  * @param inode Inode in the file system that was called and triggered this
  * function
  * @param file file associated to the file node
  * @return error code, 0 if success
  */
static int fts_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &fts_seq_ops);
};


/**************************** DRIVER TEST ************************************/

/**
  * Receive the OP code and the inputs from shell when the file node is called,
  * parse it and then execute the corresponding function
  * echo cmd+parameters > /proc/fts/driver_test to execute the select command
  * cat /proc/fts/driver_test            to obtain the result into the
  * shell \n
  * the string returned in the shell is made up as follow: \n
  * { = start byte \n
  * the answer content and format strictly depend on the cmd executed. In
  * general can be: an HEX string or a byte array (e.g in case of 0xF- commands)
  * \n
  * } = end byte \n
  */
static ssize_t fts_driver_test_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *pos)
{
	int numberparam = 0;
	struct fts_ts_info *info = PDE_DATA(file_inode(file));
	char *p = NULL;
	char *pbuf = NULL;

	int res = -1, j, index = 0;
	int size = 6;
	int temp, byte_call = 0;
	u16 byteToRead = 0;
	u32 fileSize = 0;
	u8 *readdata = NULL;
	u8 *cmd = NULL;	/* worst case needs count bytes */
	u32 *funcToTest = NULL;
	u64 addr = 0;
	struct st80yProcData *St80yProcData = NULL;

	DataHeader dataHead;

	St80yProcData = gSt80yProcData;
	St80yProcData->mess.dummy = 0;
	St80yProcData->mess.action = 0;
	St80yProcData->mess.msg_size = 0;

	pbuf = (char *)kmalloc(count, GFP_KERNEL);
	if (!pbuf) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		return count;
	}

	cmd = (u8 *)kmalloc(count, GFP_KERNEL);
	if (!cmd) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		kfree(pbuf);
		return count;
	}

	funcToTest = (u32 *)kmalloc(((count + 1) / 3) * sizeof(u32), GFP_KERNEL);
	if (!funcToTest) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		kfree(pbuf);
		kfree(cmd);
		return count;
	}

	if (copy_from_user(pbuf, buf, count) != 0) {
		res = ERROR_ALLOC;
		goto END;
	}

	p = pbuf;
	if (count > MESSAGE_MIN_HEADER_SIZE - 1 && p[0] == MESSAGE_START_BYTE) {
		TPD_DEBUG("Enter in Byte Mode!\n");
		byte_call = 1;
		St80yProcData->mess.msg_size = (p[1] << 8) | p[2];
		St80yProcData->mess.counter = (p[3] << 8) | p[4];
		St80yProcData->mess.action = (p[5] << 8) | p[6];
		TPD_DEBUG("Message received: size = %d, counter_id = %d, action = %04X\n",
			  St80yProcData->mess.msg_size, St80yProcData->mess.counter,
			  St80yProcData->mess.action);
		size = MESSAGE_MIN_HEADER_SIZE + 2;    /* +2 error code */

		if (count < St80yProcData->mess.msg_size || p[count - 2] != MESSAGE_END_BYTE) {
			TPD_INFO("number of byte received or end byte wrong! msg_size = %d != %d, last_byte = %02X != %02X ... ERROR %08X\n",
				 St80yProcData->mess.msg_size, (int)count, p[count - 1], MESSAGE_END_BYTE,
				 ERROR_OP_NOT_ALLOW);
			res = ERROR_OP_NOT_ALLOW;
			goto END;
		} else {
			numberparam = St80yProcData->mess.msg_size - MESSAGE_MIN_HEADER_SIZE +
				      1;    /* +1 because put the internal op code */
			size = MESSAGE_MIN_HEADER_SIZE +
			       2;    /* +2 send also the first 2 lsb of the error code */

			switch (St80yProcData->mess.action) {
			case ACTION_READ:
				cmd[0] = funcToTest[0] = CMD_READ_BYTE;
				break;

			case ACTION_WRITE:
				cmd[0] = funcToTest[0] = CMD_WRITE_BYTE;
				break;

			case ACTION_WRITE_READ:
				cmd[0] = funcToTest[0] = CMD_WRITEREAD_BYTE;
				break;

			case ACTION_WRITETHENWRITEREAD:
				cmd[0] = funcToTest[0] = CMD_WRITETHENWRITEREAD_BYTE;
				break;

			case ACTION_WRITEU8UX:
				cmd[0] = funcToTest[0] = CMD_WRITEU8UX_BYTE;
				break;

			case ACTION_WRITEREADU8UX:
				cmd[0] = funcToTest[0] = CMD_WRITEREADU8UX_BYTE;
				break;

			case ACTION_WRITEU8UXTHENWRITEU8UX:
				cmd[0] = funcToTest[0] = CMD_WRITEU8UXTHENWRITEU8UX_BYTE;
				break;

			case ACTION_WRITEU8XTHENWRITEREADU8UX:
				cmd[0] = funcToTest[0] = CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE;
				break;

			default:
				TPD_INFO("Invalid Action = %d ... ERROR %08X\n", St80yProcData->mess.action,
					 ERROR_OP_NOT_ALLOW);
				res = ERROR_OP_NOT_ALLOW;
				goto END;
			}
			if (numberparam - 1 != 0) {
				memcpy(&cmd[1], &p[7], numberparam -
				       1);        /* -1 because i need to exclude the cmd[0] */
			}
		}

	} else {
		if (((count + 1) / 3) >= 1) {
			if (sscanf(p, "%02X ", &funcToTest[0]) == 1) {
				p += 3;
				cmd[0] = (u8)funcToTest[0];
				numberparam = 1;
			}
		} else {
			res = ERROR_OP_NOT_ALLOW;
			goto END;
		}

		TPD_INFO("functionToTest[0] = %02X cmd[0]= %02X\n", funcToTest[0], cmd[0]);

		switch (funcToTest[0]) {
		default:
			for (; numberparam < (count + 1) / 3; numberparam++) {
				if (sscanf(p, "%02X ", &funcToTest[numberparam]) == 1) {
					p += 3;
					cmd[numberparam] = (u8)funcToTest[numberparam];
					TPD_INFO("functionToTest[%d] = %02X cmd[%d]= %02X\n", numberparam,
						 funcToTest[numberparam], numberparam, cmd[numberparam]);
				}
			}
		}
	}

	TPD_INFO("Number of Parameters = %d\n", numberparam);

	/* elaborate input */
	if (numberparam >= 1) {
		switch (funcToTest[0]) {
		case CMD_WRITEREAD:
		case CMD_WRITEREAD_BYTE:
			if (numberparam >=
					5) {    /* need to pass: cmd[0]  cmd[1]  cmd[cmdLength-1] byteToRead1 byteToRead0 dummyByte */
				temp = numberparam - 4;

				if (cmd[numberparam - 1] == 0)
					St80yProcData->mess.dummy = 0;
				else
					St80yProcData->mess.dummy = 1;

				u8ToU16_be(&cmd[numberparam - 3], &byteToRead);
				TPD_DEBUG("bytesToRead = %d\n", byteToRead + St80yProcData->mess.dummy);

				readdata = (u8 *)kzalloc((byteToRead + St80yProcData->mess.dummy) * sizeof(u8),
							 GFP_KERNEL);
				res = info->bus_ops.fts_writeRead(info, &cmd[1], temp, readdata,
								  byteToRead + St80yProcData->mess.dummy);
				size += (byteToRead * sizeof(u8));
			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}
			break;

		case CMD_WRITE:
		case CMD_WRITE_BYTE:
			if (numberparam >= 2) {    /* need to pass: cmd[0]  cmd[1] cmd[cmdLength-1] */
				temp = numberparam - 1;
				res = info->bus_ops.fts_write(info, &cmd[1], temp);
			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_READ:
		case CMD_READ_BYTE:
			if (numberparam >=
					3) {    /* need to pass: byteToRead1 byteToRead0 (optional) dummyByte */
				if (numberparam == 3 || (numberparam == 4 && cmd[numberparam - 1] == 0))
					St80yProcData->mess.dummy = 0;
				else
					St80yProcData->mess.dummy = 1;

				u8ToU16_be(&cmd[1], &byteToRead);
				readdata = (u8 *)kzalloc((byteToRead + St80yProcData->mess.dummy) * sizeof(u8),
							 GFP_KERNEL);
				res = info->bus_ops.fts_read(info, readdata,
							     byteToRead + St80yProcData->mess.dummy);
				size += (byteToRead * sizeof(u8));
			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_WRITETHENWRITEREAD:
		case CMD_WRITETHENWRITEREAD_BYTE:

			/* need to pass: cmdSize1 cmdSize2 cmd1[0] cmd1[1] cmd1[cmdSize1-1] cmd2[0] cmd2[1]  cmd2[cmdSize2-1]  byteToRead1 byteToRead0 */
			if (numberparam >= 6) {
				u8ToU16_be(&cmd[numberparam - 2], &byteToRead);
				readdata = (u8 *)kzalloc(byteToRead * sizeof(u8), GFP_KERNEL);
				res = info->bus_ops.fts_writeThenWriteRead(info, &cmd[3], cmd[1],
						&cmd[3 + (int)cmd[1]], cmd[2], readdata, byteToRead);
				size += (byteToRead * sizeof(u8));
			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_WRITEU8UX:
		case CMD_WRITEU8UX_BYTE:

			/* need to pass: cmd addrSize addr[0]  addr[addrSize-1] data[0] data[1]  */
			if (numberparam >= 4) {
				if (cmd[2] <= sizeof(u64)) {
					u8ToU64_be(&cmd[3], &addr, cmd[2]);
					TPD_DEBUG("addr = %016llX %ld\n", addr, (long int)addr);
					res = info->bus_ops.fts_writeU8UX(info, cmd[1], cmd[2], addr, &cmd[3 + cmd[2]],
									  (numberparam - cmd[2] - 3));
				} else {
					TPD_INFO("Wrong address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_WRITEREADU8UX:
		case CMD_WRITEREADU8UX_BYTE:

			/* need to pass: cmd addrSize addr[0]  addr[addrSize-1] byteToRead1 byteToRead0 hasDummyByte */
			if (numberparam >= 6) {
				if (cmd[2] <= sizeof(u64)) {
					u8ToU64_be(&cmd[3], &addr, cmd[2]);
					u8ToU16_be(&cmd[numberparam - 3], &byteToRead);
					readdata = (u8 *)kzalloc(byteToRead * sizeof(u8), GFP_KERNEL);
					TPD_DEBUG("addr = %016llX byteToRead = %d\n", addr, byteToRead);
					res = info->bus_ops.fts_writeReadU8UX(info, cmd[1], cmd[2], addr, readdata,
									      byteToRead, cmd[numberparam - 1]);
					size += (byteToRead * sizeof(u8));
				} else {
					TPD_INFO("Wrong address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}
			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_WRITEU8UXTHENWRITEU8UX:
		case CMD_WRITEU8UXTHENWRITEU8UX_BYTE:

			/* need to pass: cmd1 addrSize1 cmd2 addrSize2 addr[0]  addr[addrSize1+addrSize2-1] data[0] data[1] */
			if (numberparam >= 6) {
				if ((cmd[2] + cmd[4]) <= sizeof(u64)) {
					u8ToU64_be(&cmd[5], &addr, cmd[2] + cmd[4]);

					TPD_DEBUG("addr = %016llX %lld\n", addr, addr);
					res = info->bus_ops.fts_writeU8UXthenWriteU8UX(info, cmd[1], cmd[2], cmd[3],
							cmd[4], addr, &cmd[5 + cmd[2] + cmd[4]], (numberparam - cmd[2] - cmd[4] - 5));
				} else {
					TPD_INFO("Wrong address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}

			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_WRITEU8UXTHENWRITEREADU8UX:
		case CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE:

			/* need to pass: cmd1 addrSize1 cmd2 addrSize2 addr[0]  addr[addrSize1+addrSize2-1]  byteToRead1 byteToRead0 hasDummybyte */
			if (numberparam >= 8) {
				if ((cmd[2] + cmd[4]) <= sizeof(u64)) {
					u8ToU64_be(&cmd[5], &addr, cmd[2] + cmd[4]);
					TPD_INFO("%s: cmd[5] = %02X, addr =  %016llX\n", __func__, cmd[5], addr);
					u8ToU16_be(&cmd[numberparam - 3], &byteToRead);
					readdata = (u8 *)kzalloc(byteToRead * sizeof(u8), GFP_KERNEL);
					res = info->bus_ops.fts_writeU8UXthenWriteReadU8UX(info, cmd[1], cmd[2], cmd[3],
							cmd[4], addr, readdata, byteToRead, cmd[numberparam - 1]);
					size += (byteToRead * sizeof(u8));

				} else {
					TPD_INFO("Wrong total address size!\n");
					res = ERROR_OP_NOT_ALLOW;
				}

			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_CHANGE_OUTPUT_MODE:

			/* need to pass: bin_output */
			if (numberparam >= 2) {
				St80yProcData->bin_output = cmd[1];
				TPD_DEBUG("Setting Scriptless output mode: %d\n", St80yProcData->bin_output);
				res = OK;

			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_FWWRITE:
			if (numberparam >= 3) {    /* need to pass: cmd[0]  cmd[1] cmd[cmdLength-1] */
				if (numberparam >= 2) {
					temp = numberparam - 1;
					res = info->bus_ops.fts_writeFwCmd(info, &cmd[1], temp);

				} else {
					TPD_INFO("Wrong parameters!\n");
					res = ERROR_OP_NOT_ALLOW;
				}

			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		case CMD_INTERRUPT:

			/* need to pass: enable */
			if (numberparam >= 2) {
				if (cmd[1] == 1)
					res = info->bus_ops.fts_enableInterrupt(info);

				else
					res = info->bus_ops.fts_disableInterrupt(info);

			} else {
				TPD_INFO("Wrong number of parameters!\n");
				res = ERROR_OP_NOT_ALLOW;
			}

			break;

		default:
			TPD_INFO("COMMAND ID NOT VALID!!!\n");
			res = ERROR_OP_NOT_ALLOW;
			break;
		}

	} else {
		TPD_INFO("NO COMMAND SPECIFIED!!! do: 'echo [cmd_code] [args] > stm_fts_cmd' before looking for result!\n");
		res = ERROR_OP_NOT_ALLOW;
	}

END:    /* here start the reporting phase, assembling the data to send in the file node */

	if (St80yProcData->driver_test_buff != NULL) {
		TPD_INFO("Consecutive echo on the file node, free the buffer with the previous result\n");
		kfree(St80yProcData->driver_test_buff);
	}

	if (byte_call == 0) {
		size *= 2;
		size += 2;    /* add \n and \0 (terminator char) */

	} else {
		if (St80yProcData->bin_output != 1) {
			size *= 2; /* need to code each byte as HEX string */
			size -= 1;    /* start byte is just one, the extra byte of end byte taken by \n */

		} else {
			size += 1;        /* add \n */
		}
	}

	TPD_DEBUG("Size = %d\n", size);
	St80yProcData->driver_test_buff = (u8 *)kzalloc(size, GFP_KERNEL);
	TPD_DEBUG("Finish to allocate memory!\n");

	if (St80yProcData->driver_test_buff == NULL) {
		TPD_DEBUG("Unable to allocate driver_test_buff! ERROR %08X\n",
			  ERROR_ALLOC);
		goto ERROR;
	}

	if (byte_call == 0) {
		index = 0;
		snprintf(&St80yProcData->driver_test_buff[index], 3, "{ ");
		index += 2;
		snprintf(&St80yProcData->driver_test_buff[index], 9, "%08X", res);

		index += 8;

		if (res >= OK) {
			/*all the other cases are already fine printing only the res.*/
			switch (funcToTest[0]) {
			case CMD_READ:
			case CMD_WRITEREAD:
			case CMD_WRITETHENWRITEREAD:
			case CMD_WRITEREADU8UX:
			case CMD_WRITEU8UXTHENWRITEREADU8UX:
			case CMD_READCONFIG:
			case CMD_POLLFOREVENT:

				/* TPD_DEBUG("Data = "); */
				if (St80yProcData->mess.dummy == 1)
					j = 1;

				else
					j = 0;

				for (; j < byteToRead + St80yProcData->mess.dummy; j++) {
					/* TPD_DEBUG( "%02X ", readdata[j]); */
					snprintf(&St80yProcData->driver_test_buff[index], 3, "%02X", readdata[j]);
					/* this approach is much more faster */
					index += 2;
				}

				/* TPD_DEBUG( "\n"); */
				break;

			case CMD_GETFWFILE:
			case CMD_GETLIMITSFILE:
				TPD_DEBUG("Start To parse!\n");

				for (j = 0; j < fileSize; j++) {
					/* TPD_DEBUG( "%02X ", readdata[j]); */
					snprintf(&St80yProcData->driver_test_buff[index], 3, "%02X", readdata[j]);
					index += 2;
				}

				TPD_DEBUG("Finish to parse!\n");
				break;

			case CMD_GETFORCELEN:
			case CMD_GETSENSELEN:
				snprintf(&St80yProcData->driver_test_buff[index], 3, "%02X", (u8)temp);
				index += 2;

				break;

			case CMD_READCOMPDATAHEAD:
				snprintf(&St80yProcData->driver_test_buff[index], 3, "%02X", dataHead.type);
				index += 2;
				break;

			default:
				break;
			}
		}

		snprintf(&St80yProcData->driver_test_buff[index], 4, " }\n");
		gSt80yProcData->limit = size - 1;/* avoid to print \0 in the shell */
		St80yProcData->printed = 0;

	} else {
		/* start byte */
		St80yProcData->driver_test_buff[index++] = MESSAGE_START_BYTE;

		if (St80yProcData->bin_output == 1) {
			/* msg_size */
			St80yProcData->driver_test_buff[index++] = (size & 0xFF00) >> 8;
			St80yProcData->driver_test_buff[index++] = (size & 0x00FF);
			/* counter id */
			St80yProcData->driver_test_buff[index++] = (St80yProcData->mess.counter &
					0xFF00) >> 8;
			St80yProcData->driver_test_buff[index++] = (St80yProcData->mess.counter &
					0x00FF);
			/* action */
			St80yProcData->driver_test_buff[index++] = (St80yProcData->mess.action & 0xFF00)
					>> 8;
			St80yProcData->driver_test_buff[index++] = (St80yProcData->mess.action &
					0x00FF);
			/* error */
			St80yProcData->driver_test_buff[index++] = (res & 0xFF00) >> 8;
			St80yProcData->driver_test_buff[index++] = (res & 0x00FF);

		} else {
			if (funcToTest[0] == CMD_GETLIMITSFILE_BYTE
					|| funcToTest[0] == CMD_GETFWFILE_BYTE)
				snprintf(&St80yProcData->driver_test_buff[index], 5, "%02X%02X",
					 (((fileSize + 3) / 4) & 0xFF00) >> 8, ((fileSize + 3) / 4) & 0x00FF);

			else
				snprintf(&St80yProcData->driver_test_buff[index], 5, "%02X%02X",
					 (size & 0xFF00) >> 8, size & 0xFF);

			index += 4;
			index += snprintf(&St80yProcData->driver_test_buff[index], 5, "%04X",
					  (u16)St80yProcData->mess.counter);
			index += snprintf(&St80yProcData->driver_test_buff[index], 5, "%04X",
					  (u16)St80yProcData->mess.action);
			index += snprintf(&St80yProcData->driver_test_buff[index], 5, "%02X%02X",
					  (res & 0xFF00) >> 8, res & 0xFF);
		}

		switch (funcToTest[0]) {
		case CMD_READ_BYTE:
		case CMD_WRITEREAD_BYTE:
		case CMD_WRITETHENWRITEREAD_BYTE:
		case CMD_WRITEREADU8UX_BYTE:
		case CMD_WRITEU8UXTHENWRITEREADU8UX_BYTE:
			if (St80yProcData->bin_output == 1) {
				if (St80yProcData->mess.dummy == 1)
					memcpy(&St80yProcData->driver_test_buff[index], &readdata[1], byteToRead);

				else
					memcpy(&St80yProcData->driver_test_buff[index], readdata, byteToRead);

				index += byteToRead;

			} else {
				j = St80yProcData->mess.dummy;

				for (; j < byteToRead + St80yProcData->mess.dummy; j++)
					index += snprintf(&St80yProcData->driver_test_buff[index], 3, "%02X",
							  (u8)readdata[j]);
			}

			break;

		case CMD_GETLIMITSFILE_BYTE:
		case CMD_GETFWFILE_BYTE:
			if (St80yProcData->bin_output == 1) {
				/* override the msg_size with dimension in words */
				St80yProcData->driver_test_buff[1] = (((fileSize + 3) / 4) & 0xFF00) >> 8;
				St80yProcData->driver_test_buff[2] = (((fileSize + 3) / 4) & 0x00FF);

				if (readdata != NULL)
					memcpy(&St80yProcData->driver_test_buff[index], readdata, fileSize);

				else
					TPD_DEBUG("readdata = NULL... returning junk data!");

				index += addr;  /* in this case the byte to read
                                * are stored in addr because it
                                * is a u64 end byte need to be
                                * inserted at the end of the
                                * padded memory */

			} else {
				for (j = 0; j < fileSize; j++)
					index += snprintf(&St80yProcData->driver_test_buff[index], 3, "%02X",
							  (u8)readdata[j]);

				for (; j < addr; j++) {
					index += snprintf(&St80yProcData->driver_test_buff[index], 3, "%02X",
							  0);        /* pad memory with 0x00 */
				}
			}

			break;

		default:
			break;
		}

		St80yProcData->driver_test_buff[index++] = MESSAGE_END_BYTE;
		St80yProcData->driver_test_buff[index] = '\n';
		gSt80yProcData->limit = size;
		St80yProcData->printed = 0;
	}

ERROR:
	numberparam = 0;
	if (readdata != NULL)
		kfree(readdata);

	kfree(pbuf);
	kfree(cmd);
	kfree(funcToTest);
	return count;
}

/**
  * file_operations struct which define the functions for the canonical
  * operation on a device file node (open. read, write etc.)
  */
static const struct file_operations fts_driver_test_ops = {
	.open        = fts_open,
	.read        = seq_read,
	.write       = fts_driver_test_write,
	.llseek      = seq_lseek,
	.release     = seq_release
};

/*****************************************************************************/

/**
  * This function is called in the probe to initialize and create the directory
  * proc/fts and the driver test file node DRIVER_TEST_FILE_NODE into the /proc
  * file system
  * @return OK if success or an error code which specify the type of error
  */
int fts_proc_init(struct touchpanel_data *ts)
{
	struct proc_dir_entry *entry;
	struct fts_ts_info *info = ts->chip_data;

	int retval = 0;

	gSt80yProcData  =  kzalloc(sizeof(struct st80yProcData), GFP_KERNEL);

	if (!gSt80yProcData) {
		TPD_INFO("no more memory\n");
		return -ENOMEM;
	}

	gSt80yProcData->fts_dir = proc_mkdir_data("fts", 0777, ts->prEntry_tp, ts);

	if (gSt80yProcData->fts_dir == NULL) {    /* directory creation failed */
		retval = -ENOMEM;
		goto out;
	}

	entry = proc_create_data(DRIVER_TEST_FILE_NODE, 0777, gSt80yProcData->fts_dir,
				 &fts_driver_test_ops, info);

	if (entry)
		TPD_INFO("%s: proc entry CREATED!\n", __func__);

	else {
		TPD_INFO("%s: error creating proc entry!\n", __func__);
		retval = -ENOMEM;
		goto badfile;
	}



	return OK;
badfile:
	remove_proc_entry("fts", ts->prEntry_tp);
out:
	return retval;
}

/**
  * Delete and Clean from the file system, all the references to the driver test
  * file node
  * @return OK
  */
void fts_proc_remove(struct touchpanel_data *ts)
{
	remove_proc_entry(DRIVER_TEST_FILE_NODE, gSt80yProcData->fts_dir);
	remove_proc_entry("fts", ts->prEntry_tp);
}

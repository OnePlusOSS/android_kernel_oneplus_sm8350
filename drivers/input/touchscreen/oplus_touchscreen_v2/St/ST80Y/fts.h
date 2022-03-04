// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef _LINUX_FTS_I2C_H_
#define _LINUX_FTS_I2C_H_

#include <linux/device.h>
#include "ftsSoftware.h"
#include "ftsHardware.h"

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/time.h>

#include "../st_common.h"
#include "../../touchpanel_common.h"

#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "st-st80y"
#else
#define TPD_DEVICE "st-st80y"
#endif

/****************** CONFIGURATION SECTION ******************/
/** @defgroup conf_section     Driver Configuration Section
  * Settings of the driver code in order to suit the HW set up and the
  * application behavior
  */
/* **** CODE CONFIGURATION **** */
#define FTS_TS_DRV_NAME        "st80y"    /*  driver name */

/* If both COMPUTE_INIT_METHOD and PRE_SAVED_METHOD are not defined, driver will be automatically configured as GOLDEN_VALUE_METHOD */
#define COMPUTE_INIT_METHOD     /* Allow to compute init data on phone during production */
#ifndef COMPUTE_INIT_METHOD
#define PRE_SAVED_METHOD        /* Pre-Saved Method used during production */
#endif


/* **** FEATURES USED IN THE IC **** */
#define GESTURE_MODE            /* enable the support of the gestures */
#ifdef GESTURE_MODE
#define USE_GESTURE_MASK        /* the gestures to select are referred using a gesture bitmask instead of their gesture IDs */
#endif

/***** END **** */


/* **** PANEL SPECIFICATION **** */
#define PRESSURE_MIN    0       /* min value of pressure reported */
#define PRESSURE_MAX    127     /* Max value of pressure reported */

#define DISTANCE_MIN    0       /* min distance between the tool and the display */
#define DISTANCE_MAX    127     /* Max distance between the tool and the display */
/* **** END **** */


#define GESTURE_MASK_SIZE 4

/* **** Auto Test **** */
#define WAIT_FOR_FRESH_FRAMES    200    /* /< Time in ms to wait after start to
                                	* sensing before reading a frame */
#define WAIT_AFTER_SENSEOFF        50    /*Time in ms to wait after stop*/
										/* sensing and before reading a frame*/
										/* from memory */

#define NO_INIT                    0    /* /< No Initialization required during
                                	* the MP */

#define RETRY_INIT_BOOT            3    /* /< number of retry of the
                                	* initialization process at boot */
/**************Flash********************/
/* Flash possible status */
#define FLASH_READY         0   /* /< value to indicate that the flash
                                * is ready */
#define FLASH_BUSY          1   /* /< value to indicate that the flash
                                * is busy */
#define FLASH_UNKNOWN      -1   /* /< value to indicate an unknown
	                        	* status of the flash */

#define FLASH_STATUS_BYTES  1   /* /< number of bytes to check for read
                                * the flash status */

/* Flash timing parameters */
#define FLASH_RETRY_COUNT       200     /* /< number of attemps to read the
                                        * flash status */
#define FLASH_WAIT_BEFORE_RETRY 50      /* /< time to wait in ms between status readings */

#define FLASH_CHUNK             (64 * 1024)    /* /< Max number of bytes that
												* the DMA can burn on the flash
												* in one shot in FTI */
#define DMA_CHUNK               32      /* /< Max number of bytes that can be
                                        * written in I2C to the DMA */

/**
  * Define which kind of erase page by page should be performed
  */
typedef enum {
	ERASE_ALL               = 0,    /* /< erase all the pages */
	SKIP_PANEL_INIT         = 1,    /* /< skip erase Panel Init Pages */
	SKIP_PANEL_CX_INIT      = 2     /* /< skip erase Panel Init and CX Pages */
} ErasePage;

/* Num bytes of release info in sys info*/
/*  (first bytes are external release) */
#define EXTERNAL_RELEASE_INFO_SIZE  8
#define RELEASE_INFO_SIZE           (EXTERNAL_RELEASE_INFO_SIZE)

/**
  * Struct which contains information and data of the FW that should be burnt
  *into the IC
  */
typedef struct {
	u8  *data;      /* /< pointer to an array of bytes which represent the FW data */
	u16 fw_ver;     /* /< FW version of the FW file */
	u16 config_id;  /* /< Config ID of the FW file */
	u16 cx_ver;     /* /< Cx version of the FW file */
	u8 flash_org_info[4]; /*  Flash Organization Information*/
	u8	externalRelease[8];    /* /< External Release  Info of the FW file */
	int data_size;    /* /< dimension of data (the actual data to be burnt) */
	u32 sec0_size;    /* /< dimension of section 0 (FW) in .ftb file */
	u32 sec1_size;    /* /< dimension of section 1 (Config) in .ftb file */
	u32 sec2_size;    /* /< dimension of section 2 (Cx) in .ftb file */
	u32 sec3_size;    /* /< dimension of section 3 (TBD) in .ftb file */
} Firmware;

/********FTS_COMPENSATION_H*****/
#define RETRY_COMP_DATA_READ 2                          /* /< max number of attempts to read initialization data */

/* Bytes dimension of Compensation Data Format */
#define COMP_DATA_HEADER    DATA_HEADER                 /* /< size in bytes of initialization data header */
#define COMP_DATA_GLOBAL    (16 - COMP_DATA_HEADER)     /* /< size in bytes of initialization data general info */
#define HEADER_SIGNATURE    0xA5                        /* /< signature used as starting byte of data loaded in memory */

/**
  * Struct which contains the general info about Frames and Initialization Data
  */
typedef struct {
	int force_node;    /* /< Number of Force Channels in the
                        * frame/Initialization data */
	int sense_node;    /* /< Number of Sense Channels in the
                        * frame/Initialization data */
	int type;          /* /< Type of frame/Initialization data */
} DataHeader;

/**
  * Struct which contains the MS Initialization data
  */
typedef struct {
	DataHeader  header;         /* /< Header */
	i8          cx1;            /* /< Cx1 value (can be negative)) */
	i8
	*node_data;     /* /< Pointer to an array of bytes which contains the
                                * CX2 data(can be negative) */
	int         node_data_size; /* /< size of the data */
} MutualSenseData;


/**
  * Struct which contains the SS Initialization data
  */
typedef struct {
	DataHeader  header;     /* /< Header */
	u8          f_ix1;      /* /< IX1 Force */
	u8          s_ix1;      /* /< IX1 Sense */
	i8          f_cx1;      /* /< CX1 Force (can be negative) */
	i8          s_cx1;      /* /< CX1 Sense (can be negative) */
	u8          f_max_n;    /* /< Force MaxN */
	u8          s_max_n;    /* /< Sense MaxN */

	u8          *ix2_fm;    /* /< pointer to an array of bytes which contains Force
							* Ix2 data node */
	u8          *ix2_sn;    /* /< pointer to an array of bytes which contains Sense
							* Ix2 data node */
	i8          *cx2_fm;    /* /< pointer to an array of bytes which contains Force
							* Cx2 data node(can be negative) */
	i8          *cx2_sn;    /* /< pointer to an array of bytes which contains Sense
							* Cx2 data node(can be negative)) */
} SelfSenseData;

/**
  * Struct which contains the TOT MS Initialization data
  */
typedef struct {
	DataHeader  header;         /* /< Header */
	short       *node_data;     /* /< pointer to an array of ushort which
                                * contains TOT MS Initialization data */
	int         node_data_size; /* /< size of data */
} TotMutualSenseData;

/**
  * Struct which contains the TOT SS Initialization data
  */
typedef struct {
	DataHeader  header;    /* /< Header */

	u16         *ix_fm;    /* /< pointer to an array of ushort which contains TOT
							* SS IX Force data */
	u16         *ix_sn;    /* /< pointer to an array of ushort which contains TOT
							* SS IX Sense data */
	short       *cx_fm;    /* /< pointer to an array of ushort which contains TOT
							* SS CX Force data can be negative */
	short       *cx_sn;    /* /< pointer to an array of ushort which contains TOT
							* SS CX Sense data can be negative */
} TotSelfSenseData;

/**************************FTS_FRAME_H*********************/
/* Number of data bytes for each node */
#define BYTES_PER_NODE              2                       /* /< number of data bytes for each node */
#define RETRY_FRAME_DATA_READ       2                       /* /< max number of attempts to read a frame */
#define SYNCFRAME_DATA_HEADER       (DATA_HEADER + 12)      /* /< number of bytes of Sync Frame Header */

/**
  * Possible types of MS frames
  */
typedef enum {
	MS_RAW          = 0,    /* /< Mutual Sense Raw Frame */
	MS_FILTER       = 1,    /* /< Mutual Sense Filtered Frame */
	MS_STRENGTH     = 2,    /* /< Mutual Sense Strength Frame (Baseline-Raw) */
	MS_BASELINE     = 3,    /* /< Mutual Sense Baseline Frame */
	MS_KEY_RAW      = 4,    /* /< Mutual Sense Key Raw Frame */
	MS_KEY_FILTER   = 5,    /* /< Mutual Sense Key Filter Frame */
	MS_KEY_STRENGTH = 6,    /* /< Mutual Sense Key Strength Frame (Baseline-Raw) */
	MS_KEY_BASELINE = 7,    /* /< Mutual Sense Key Baseline Frame */
	FRC_RAW         = 8,    /* /< Force Raw Frame */
	FRC_FILTER      = 9,    /* /< Force Filtered Frame */
	FRC_STRENGTH    = 10,   /* /< Force Strength Frame (Baseline-Raw) */
	FRC_BASELINE    = 11    /* /< Force Baseline Frame */
} MSFrameType;

/**
  * Possible types of SS frames
  */
typedef enum {
	SS_RAW              = 0,    /* /< Self Sense Raw Frame */
	SS_FILTER           = 1,    /* /< Self Sense Filtered Frame */
	SS_STRENGTH         = 2,    /* /< Self Sense Strength Frame (Baseline-Raw) */
	SS_BASELINE         = 3,    /* /< Self Sense Baseline Frame */
	SS_HVR_RAW          = 4,    /* /< Self Sense Hover Raw Frame */
	SS_HVR_FILTER       = 5,    /* /< Self Sense Hover Filter Frame */
	SS_HVR_STRENGTH     = 6,    /* /< Self Sense Hover Strength Frame (Baseline-Raw) */
	SS_HVR_BASELINE     = 7,    /* /< Self Sense Hover Baseline Frame */
	SS_PRX_RAW          = 8,    /* /< Self Sense Proximity Raw Frame */
	SS_PRX_FILTER       = 9,    /* /< Self Sense Proximity Filtered Frame */
	SS_PRX_STRENGTH     = 10,   /* /< Self Sense Proximity Strength Frame (Baseline-Raw) */
	SS_PRX_BASELINE     = 11,   /* /< Self Sense Proximity Baseline Frame */
	SS_DETECT_RAW       = 12,   /* /< Self Sense Detect Raw Frame */
	SS_DETECT_FILTER    = 13,   /* /< Self Sense Detect Filter Frame */
	SS_DETECT_STRENGTH  = 14,   /* /< Self Sense Detect Strength Frame */
	SS_DETECT_BASELINE  = 15    /* /< Self Sense Detect Baseline Frame */
} SSFrameType;

/**
  * Struct which contains the data of a MS Frame
  */
typedef struct {
	DataHeader
	header;         /* /< Header which contain basic info of the frame */
	short       *node_data;     /* /< Data of the frame */
	int         node_data_size; /* /< Dimension of the data of the frame */
} MutualSenseFrame;

/**
  * Struct which contains the data of a SS Frame
  */
typedef struct {
	DataHeader
	header;         /* /< Header which contain basic info of the frame */
	short       *force_data;    /* /< Force Channels Data */
	short       *sense_data;    /* /< Sense Channels Data */
} SelfSenseFrame;

/***/
/* TIMEOUT */
/** @defgroup timeouts     Timeouts
  * Definitions of all the Timeout used in several operations
  */
#define TIMEOUT_RESOLUTION                 2                            /* /< timeout resolution in ms (all timeout should be multiples of this unit) */
#define GENERAL_TIMEOUT                   (50 * TIMEOUT_RESOLUTION)     /* /< general timeout in ms */
#define RELEASE_INFO_TIMEOUT              (15 * TIMEOUT_RESOLUTION)     /* /< timeout to request release info in ms */

#define TIMEOUT_REQU_COMP_DATA            (100 * TIMEOUT_RESOLUTION)    /* /< timeout to request compensation data in ms */
#define TIMEOUT_REQU_DATA                 (200 * TIMEOUT_RESOLUTION)    /* /< timeout to request data in ms */
#define TIMEOUT_ITO_TEST_RESULT           (100 * TIMEOUT_RESOLUTION)    /* /< timeout to perform ito test in ms */
#define TIMEOUT_INITIALIZATION_TEST_RESULT    (5000 * TIMEOUT_RESOLUTION)     /* /< timeout to perform initialization test in ms */
#define TIEMOUT_ECHO                      (500 * TIMEOUT_RESOLUTION)   /* /< timeout of the echo command, should be the max of
															* all the possible commands(used in worst case) */

/**
  * Struct used to measure the time elapsed between a starting and ending point.
  */
typedef struct {
	struct timespec start;      /* /< store the starting time */
	struct timespec end;        /* /< store the finishing time */
} StopWatch;

/* HW DATA */
#define GPIO_NOT_DEFINED        -1         /* /< value assumed by reset_gpio when
                                        	* the reset pin of the IC is not connected */

#define ADDR_SIZE_HW_REG        BITS_32    /* /< value of AddrSize for Hw register
                                        	* in FTI @see AddrSize */

#define DATA_HEADER             4          /* /< size in byte of the header loaded
                                        	* with the data in the frambuffer */

/**
  * Type of CRC errors
  */
typedef enum {
	CRC_CODE        = 1,    /* /< CRC in the code section */
	CRC_CONFIG      = 2,    /* /< CRC in the config section */
	CRC_CX          = 3,    /* /< CRC in the cx section */
	CRC_PANEL       = 4     /* /< CRC in the panel section */
} CRC_Error;

/* CHIP INFO */
/** @defgroup system_info    System Info
  * System Info Data collect the most important informations about hw and fw
  */
/* Size in bytes of System Info data */
#define SYS_INFO_SIZE               216     /* Num bytes of die info */
#define DIE_INFO_SIZE               16      /* Num bytes of external release in config */

/* RETRY MECHANISM */
#define RETRY_MAX_REQU_DATA         2       /* /< Max number of attemps performed
											* when requesting data */
#define RETRY_SYSTEM_RESET          3       /* /< Max number of attemps performed
											* to reset the IC */

/**
  * Struct which contains fundamental informations about the chip and its
  *configuration
  */
typedef struct {
	u16 u16_apiVer_rev;     /* /< API revision version */
	u8 u8_apiVer_minor;     /* /< API minor version */
	u8 u8_apiVer_major;     /* /< API major version */
	u16 u16_chip0Ver;       /* /< Dev0 version */
	u16 u16_chip0Id;        /* /< Dev0 ID */
	u16 u16_chip1Ver;       /* /< Dev1 version */
	u16 u16_chip1Id;        /* /< Dev1 ID */
	u16 u16_fwVer;          /* /< Fw version */
	u16 u16_svnRev;         /* /< SVN Revision */
	u16 u16_cfgVer;         /* /< Config Version */
	u16 u16_cfgProjectId;   /* /< Config Project ID */
	u16 u16_cxVer;          /* /< Cx Version */
	u16 u16_cxProjectId;    /* /< Cx Project ID */
	u8 u8_cfgAfeVer;        /* /< AFE version in Config */
	u8 u8_cxAfeVer;         /* /< AFE version in CX */
	u8 u8_panelCfgAfeVer;   /* /< AFE version in PanelMem */
	u8 u8_protocol;         /* /< Touch Report Protocol */
	u8 u8_dieInfo[DIE_INFO_SIZE];               /* /< Die information */
	u8 u8_releaseInfo[RELEASE_INFO_SIZE];       /* /< Release information */
	u32 u32_fwCrc;      /* /< Crc of FW */
	u32 u32_cfgCrc;     /* /< Crc of config */
	u8 u8_mpFlag;       /* /< MP Flag */
	u8 u8_calibrationFlag;       /* /< calibrationFlag Flag */
	u8 u8_ssDetScanSet; /* /< Type of Detect Scan Selected */
	u32 u32_flash_org_info; 	/* /< u32_flash_org_info*/

	u16 u16_scrResX;    /* /< X resolution on main screen */
	u16 u16_scrResY;    /* /< Y resolution on main screen */
	u8 u8_scrTxLen;     /* /< Tx length */
	u8 u8_scrRxLen;     /* /< Rx length */
	u8 u8_keyLen;       /* /< Key Len */
	u8 u8_forceLen;     /* /< Force Len */

	u16 u16_dbgInfoAddr;        /* /< Offset of debug Info structure */

	u16 u16_msTchRawAddr;       /* /< Offset of MS touch raw frame */
	u16 u16_msTchFilterAddr;    /* /< Offset of MS touch filter frame */
	u16 u16_msTchStrenAddr;     /* /< Offset of MS touch strength frame */
	u16 u16_msTchBaselineAddr;  /* /< Offset of MS touch baseline frame */

	u16 u16_ssTchTxRawAddr;     /* /< Offset of SS touch force raw frame */
	u16 u16_ssTchTxFilterAddr;  /* /< Offset of SS touch force filter frame */
	u16 u16_ssTchTxStrenAddr;   /* /< Offset of SS touch force strength frame */
	u16 u16_ssTchTxBaselineAddr;/* /< Offset of SS touch force baseline frame */

	u16 u16_ssTchRxRawAddr;     /* /< Offset of SS touch sense raw frame */
	u16 u16_ssTchRxFilterAddr;  /* /< Offset of SS touch sense filter frame */
	u16 u16_ssTchRxStrenAddr;   /* /< Offset of SS touch sense strength frame */
	u16 u16_ssTchRxBaselineAddr;/* /< Offset of SS touch sense baseline frame */

	u16 u16_keyRawAddr;         /* /< Offset of key raw frame */
	u16 u16_keyFilterAddr;      /* /< Offset of key filter frame */
	u16 u16_keyStrenAddr;       /* /< Offset of key strength frame */
	u16 u16_keyBaselineAddr;    /* /< Offset of key baseline frame */

	u16 u16_frcRawAddr;         /* /< Offset of force touch raw frame */
	u16 u16_frcFilterAddr;      /* /< Offset of force touch filter frame */
	u16 u16_frcStrenAddr;       /* /< Offset of force touch strength frame */
	u16 u16_frcBaselineAddr;    /* /< Offset of force touch baseline frame */

	u16 u16_ssHvrTxRawAddr;     /* /< Offset of SS hover Force raw frame */
	u16 u16_ssHvrTxFilterAddr;  /* /< Offset of SS hover Force filter frame */
	u16 u16_ssHvrTxStrenAddr;   /* /< Offset of SS hover Force strength frame */
	u16 u16_ssHvrTxBaselineAddr;/* /< Offset of SS hover Force baseline frame */

	u16 u16_ssHvrRxRawAddr;     /* /< Offset of SS hover Sense raw frame */
	u16 u16_ssHvrRxFilterAddr;  /* /< Offset of SS hover Sense filter frame */
	u16 u16_ssHvrRxStrenAddr;   /* /< Offset of SS hover Sense strength frame */
	u16 u16_ssHvrRxBaselineAddr;/* /< Offset of SS hover Sense baseline frame */

	u16 u16_ssPrxTxRawAddr;         /* /< Offset of SS proximity force raw frame */
	u16 u16_ssPrxTxFilterAddr;      /* /< Offset of SS proximity force filter frame */
	u16 u16_ssPrxTxStrenAddr;       /* /< Offset of SS proximity force strength frame */
	u16 u16_ssPrxTxBaselineAddr;    /* /< Offset of SS proximity force baseline frame */

	u16 u16_ssPrxRxRawAddr;         /* /< Offset of SS proximity sense raw frame */
	u16 u16_ssPrxRxFilterAddr;      /* /< Offset of SS proximity sense filter frame */
	u16 u16_ssPrxRxStrenAddr;       /* /< Offset of SS proximity sense strength frame */
	u16 u16_ssPrxRxBaselineAddr;    /* /< Offset of SS proximity sense baseline frame */

	u16 u16_ssDetRawAddr;           /* /< Offset of SS detect raw frame */
	u16 u16_ssDetFilterAddr;        /* /< Offset of SS detect filter frame */
	u16 u16_ssDetStrenAddr;         /* /< Offset of SS detect strength frame */
	u16 u16_ssDetBaselineAddr;      /* /< Offset of SS detect baseline frame */
} SysInfo;

#define I2C_RETRY                   3    /* /< number of retry in case of i2c failure */
#define I2C_WAIT_BEFORE_RETRY       2    /* /< wait in ms before retry an i2c transaction */


/** @defgroup error_codes Error Codes
  * Error codes that can be reported by the driver functions.
  * An error code is made up by 4 bytes, each byte indicate a logic error
  * level.\n
  * From the LSB to the MSB, the logic level increase going from a low level
  * error (I2C,TIMEOUT) to an high level error (flashing procedure fail,
  * production test fail etc)
  */

/* FIRST LEVEL ERROR CODE */
/** @defgroup first_level    First Level Error Code
  * @ingroup error_codes
  * Errors related to low level operation which are not under control of driver,
  * such as: communication protocol (I2C/SPI), timeout, file operations ...
  */
#define OK                  ((int)0x00000000)       /* /< No ERROR */
#define ERROR_ALLOC         ((int)0x80000001)       /* /< allocation of
													* memory failed */
#define ERROR_BUS_R         ((int)0x80000002)       /* /< i2c/spi read
													* failed */
#define ERROR_BUS_W         ((int)0x80000003)       /* /< i2c/spi write
													* failed */
#define ERROR_BUS_WR        ((int)0x80000004)       /* /< i2c/spi write/read
													* failed */
#define ERROR_BUS_O         ((int)0x80000005)       /* /< error during
													* opening an i2c device */
#define ERROR_OP_NOT_ALLOW  ((int)0x80000006)       /* /< operation not
													* allowed */
#define ERROR_TIMEOUT       ((int)0x80000007)       /* /< timeout expired
													* exceed the max number
													* of retries the max
													* waiting time */
#define ERROR_FILE_NOT_FOUND        ((int)0x80000008)       /* /< the file that i
													* want to open is
													* found */
#define ERROR_FILE_PARSE            ((int)0x80000009)       /* /< error during
													* parsing the file */
#define ERROR_FILE_READ             ((int)0x8000000A)       /* /< error during
													* reading the file */
#define ERROR_LABEL_NOT_FOUND       ((int)0x8000000B)       /* /< label found */
#define ERROR_FW_NO_UPDATE          ((int)0x8000000C)       /* /< fw in the chip
													* newer than the one in
													* the memmh */
#define ERROR_FLASH_UNKNOWN         ((int)0x8000000D)       /* /< flash status busy
													* or unknown */

/* SECOND LEVEL ERROR CODE */
/** @defgroup second_level Second Level Error Code
  * @ingroup error_codes
  * Errors related to simple logic operations in the IC which require one
  * command or which are part of a more complex procedure
  */
#define ERROR_DISABLE_INTER         ((int)0x80000200)   /* /< unable to disable
                                                        * the interrupt */
#define ERROR_ENABLE_INTER          ((int)0x80000300)   /* /< unable to activate
                                                        * the interrup */
#define ERROR_READ_CONFIG           ((int)0x80000400)   /* /< failed to read
                                                        * config memory */
#define ERROR_GET_OFFSET            ((int)0x80000500)   /* /< unable to read an
                                                        * offset from memory */
#define ERROR_GET_FRAME_DATA        ((int)0x80000600)   /* /< unable to
                                                        *  retrieve the data of
                                                        *  a required frame */
#define ERROR_DIFF_DATA_TYPE        ((int)0x80000700)   /* /< FW answers with
                                                        *  an event that has a
                                                        * different address
                                                        * respect the request
                                                        * done */
#define ERROR_WRONG_DATA_SIGN       ((int)0x80000800)   /* /< the signature of
														* the host data is n ot
														* HEADER_SIGNATURE */
#define ERROR_SET_SCAN_MODE_FAIL    ((int)0x80000900)   /* /< setting the
                                                        * scanning mode failed
                                                        * (sense on/off etc...) */
#define ERROR_SET_FEATURE_FAIL      ((int)0x80000A00)   /* /< setting a
                                                        * specific feature
                                                        * failed */
#define ERROR_SYSTEM_RESET_FAIL     ((int)0x80000B00)   /* /< the comand
                                                        * SYSTEM RESET
                                                        * failed */
#define ERROR_FLASH_NOT_READY       ((int)0x80000C00)   /* /< flash
														* status n ot
														* ready within
														* a timeout */
#define ERROR_FW_VER_READ           ((int)0x80000D00)   /*  unable to read*/
                                                        /* fw_vers or the*/
                                                        /* config_id */
#define ERROR_GESTURE_ENABLE_FAIL   ((int)0x80000E00)   /* /< unable to
                                                        * enable/disable the
                                                        * gesture */
#define ERROR_GESTURE_START_ADD     ((int)0x80000F00)   /* /< unable to start
                                                        * add custom gesture */
#define ERROR_GESTURE_FINISH_ADD    ((int)0x80001000)   /* /< unable to finish
                                                        * to add custom gesture */
#define ERROR_GESTURE_DATA_ADD      ((int)0x80001100)   /* /< unable to add
                                                        * custom gesture data */
#define ERROR_GESTURE_REMOVE        ((int)0x80001200)   /* /< unable to remove
                                                        * custom gesture data */
#define ERROR_FEATURE_ENABLE_DISABLE   ((int)0x80001300)/* /< unable to
                                                        * enable/disable a
                                                        * feature mode in the IC */
#define ERROR_NOISE_PARAMETERS      ((int)0x80001400)   /* /< unable to
                                                        * set/read noise
                                                        * parameter in
                                                        * the IC */
#define ERROR_CH_LEN                ((int)0x80001500)   /* /< unable to read
                                                        * the force and/or
                                                        * sense length */
/* THIRD LEVEL ERROR CODE */
/** @defgroup third_level    Third Level Error Code
  * @ingroup error_codes
  * Errors related to logic operations in the IC which require more
  * commands/steps or which are part of a more complex procedure
  */
#define ERROR_REQU_COMP_DATA        ((int)0x80010000)   /* /< compensation data
                                                        * request failed */
#define ERROR_REQU_DATA             ((int)0x80020000)   /* /< data request failed */
#define ERROR_COMP_DATA_HEADER      ((int)0x80030000)   /* /< unable to retrieve
                                                        * compensation data header */
#define ERROR_COMP_DATA_GLOBAL      ((int)0x80040000)   /* /< unable to retrieve the
                                                        * global compensation data */
#define ERROR_COMP_DATA_NODE        ((int)0x80050000)   /* /< unable to retrieve
                                                        * the compensation data
                                                        * for each node */
#define ERROR_TEST_CHECK_FAIL       ((int)0x80060000)   /* check of*/
														/* production limits or*/
														/* of fw answers failed */
#define ERROR_MEMH_READ             ((int)0x80070000)   /* /< memh reading failed */
#define ERROR_FLASH_BURN_FAILED     ((int)0x80080000)   /* /< flash burn failed */
#define ERROR_MS_TUNING             ((int)0x80090000)   /* /< ms tuning failed */
#define ERROR_SS_TUNING             ((int)0x800A0000)   /* /< ss tuning failed */
#define ERROR_LP_TIMER_TUNING       ((int)0x800B0000)   /* /< lp timer calibration failed */
#define ERROR_SAVE_CX_TUNING        ((int)0x800C0000)   /* /< save cx data to flash failed */
#define ERROR_HANDLER_STOP_PROC     ((int)0x800D0000)   /* /< stop the poll of the FIFO if
                                                        * particular errors are found */
#define ERROR_CHECK_ECHO_FAIL       ((int)0x800E0000)   /* /< unable to retrieve echo event */
#define ERROR_GET_FRAME             ((int)0x800F0000)   /* /< unable to get frame */

/* FOURTH LEVEL ERROR CODE */
/** @defgroup fourth_level    Fourth Level Error Code
  * @ingroup error_codes
  * Errors related to the highest logic operations in the IC which have an
  * important impact on the driver flow or which require several commands and
  * steps to be executed
  */
#define ERROR_PROD_TEST_DATA            ((int)0x81000000)    /* /< production  data test failed */
#define ERROR_FLASH_PROCEDURE           ((int)0x82000000)    /* /< fw update procedure failed */
#define ERROR_PROD_TEST_ITO             ((int)0x83000000)    /* /< production ito test failed */
#define ERROR_PROD_TEST_INITIALIZATION  ((int)0x84000000)    /* /< production initialization test failed */
#define ERROR_GET_INIT_STATUS           ((int)0x85000000)    /* /< mismatch of MS or SS tuning_version */
/* end of error_commands section */

/**
  * Struct which store an ordered list of the errors events encountered during
  * the polling of a FIFO.
  * The max number of error events that can be stored is equal to FIFO_DEPTH
  */
typedef struct {
	u8  list[FIFO_DEPTH * FIFO_EVENT_SIZE];    /* /< byte array which contains
                                                * the series of error events
                                                * encountered from the last
                                                * reset of the list. */
	int count;                                 /* /< number of error events stored in the list */
	int last_index;                            /* /< index of the list where will be stored the next
												* error event. Subtract -1 to have the index of the
												* last error event */
} ErrorList;

struct st80ytestdata {
	int init_type;
	struct auto_testdata *st_testdata;
	int test_ok;
	int mutualraw;
	int mutualrawmap;
	int mutualcx2lp;
	int mutualcx2adjlp;
	int selfforcerawmap;
	int selfsenserawmap;
	int selfforceixtotal;
	int selfsenseixtotal;
	int itomutualminmaxraw;
	int stop_on_fail;
};

struct bus_ops {
	int (*fts_read)(void *chip_data, u8 *outBuf, int byteToRead);
	int (*fts_writeRead)(void *chip_data, u8 *cmd, int cmdLength, u8 *outBuf,
			     int byteToRead);
	int (*fts_write)(void *chip_data, u8 *cmd, int cmdLength);
	int (*fts_writeFwCmd)(void *chip_data, u8 *cmd, int cmdLenght);
	int (*fts_writeThenWriteRead)(void *chip_data, u8 *writeCmd1,
				      int writeCmdLength, u8 *readCmd1, int readCmdLength, u8 *outBuf,
				      int byteToRead);
	int (*fts_writeU8UX)(void *chip_data, u8 cmd, AddrSize addrSize, u64 address,
			     u8 *data, int dataSize);
	int (*fts_writeReadU8UX)(void *chip_data, u8 cmd, AddrSize addrSize,
				 u64 address, u8 *outBuf, int byteToRead, int hasDummyByte);
	int (*fts_writeU8UXthenWriteU8UX)(void *chip_data, u8 cmd1, AddrSize addrSize1,
					  u8 cmd2, AddrSize addrSize2, u64 address, u8 *data, int dataSize);
	int (*fts_writeU8UXthenWriteReadU8UX)(void *chip_data, u8 cmd1,
					      AddrSize addrSize1, u8 cmd2,
					      AddrSize addrSize2, u64 address, u8 *outBuf, int count, int hasDummyByte);
	int (*fts_disableInterrupt)(void *chip_data);
	int (*fts_enableInterrupt)(void *chip_data);
};

struct fts_ts_info {
	struct device
		*dev;                                   /* Pointer to the structure device */

	struct i2c_client
		*client;                                /* I2C client structure */

	struct hw_resource
		*hw_res;                                /* hw res parse from device trees */

	unsigned char
	data[FIFO_DEPTH][FIFO_EVENT_SIZE];      /* event data from FIFO */

	char
	*fw_name;                               /* FW path name */

	char
	*test_limit_name;                       /* test limit item name */

	tp_dev
	tp_type;                                /* tp type */

	u8
	key_mask;                               /* key mask of key event */
	int							proximity_status;
	bool cal_needed;
	int tp_index;
	int display_refresh_rate;
	bool game_mode;
	int *in_suspend;
	int smooth_level;
	int sensitive_level;
	int freq_point;
	unsigned int setmpflagcount;
	struct st80ytestdata st80ytestdata;
	bool auto_test_need_cal_support;
	char gesture_mask[4];
	struct bus_ops bus_ops;
	int  disable_irq_count;
	SysInfo systemInfo;
	ErrorList errors;
	struct monitor_data             *monitor_data;
	bool *is_usb_checked;
};

/* export declaration of functions in fts_proc.c */
extern int fts_proc_init(struct touchpanel_data *ts);
extern void fts_proc_remove(struct touchpanel_data *ts);
#endif  /*_LINUX_FTS_I2C_H_*/

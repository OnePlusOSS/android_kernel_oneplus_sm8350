/*
 *****************************************************************************
 * Copyright by ams AG                                                       *
 * All rights are reserved.                                                  *
 *                                                                           *
 * IMPORTANT - PLEASE READ CAREFULLY BEFORE COPYING, INSTALLING OR USING     *
 * THE SOFTWARE.                                                             *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       *
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         *
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS         *
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  *
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,     *
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT          *
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     *
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY     *
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT       *
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     *
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.      *
 *****************************************************************************
 */
/*
 *      PROJECT:   ToF CORE1 Main Application firmware
 *      $Revision: $
 *      LANGUAGE:  ANSI C
 *
 */

/*! \file tof_core1_main_app_enum.h
 *
 * \author Thomas Emberson
 *
 * \brief ToF application interface to the host, enumerated constants.
 */


#ifndef TOF_CORE1_MAIN_APP_ENUM
#define TOF_CORE1_MAIN_APP_ENUM


#define INT_RESULT_MASK     (0x01)
#define INT_DIAG_MASK       (0x02)
#define TOF_CORE1_NUMBER_OF_HISTOGRAMS      (5)
#define TOF_CORE1_NUMBER_OF_BINS            (256)

#define PACKED __attribute__((packed))

/* The DAX signal time given from the host is in units of 100 us, thus for use
 * in comparing the number to the us from the system time stamp, the number
 * must be converted */
#define DAX_TIME_TO_US(daxTime)     (daxTime * 100)
/* It take about 300-400 us to power up the charge pump and histogram RAM,
 * thus the host can delay up to about 500 us before starting to turn
 * charge pump and RAM */
#define DAX_POWER_UP_DELAY          (500)

/* Additional I2C address locations */
#define OL_FACTORY_CALIB_OFFSET     0x20

#define OL_COMMAND_START_MIN        OL_COMMAND_START_EXT_CAL
#define OL_COMMAND_START_MAX        OL_COMMAND_START

#define OL_COMMAND_ASYNC_MIN        OL_COMMAND_START_EXT_CAL
#define OL_COMMAND_ASYNC_MAX        OL_COMMAND_FACTORY_CALIB

typedef enum tof_core1_commands {
    OL_COMMAND_NULL                 = 0x00, /* Null, no command */
    OL_COMMAND_START_EXT_CAL        = 0x02, /* Same as OL_COMMAND_START_EXT with the addition of
                                             * loading of previous algorithm state and factory calibration
                                             * that has been written into the I2C RAM from address 0x20
                                             * There is an addition data byte to indicate what is in the
                                             * data */
    OL_COMMAND_START_EXT            = 0x03, /* Set flag to perform target measurement noise threshold,
                                             * future parameter, ranging time, repetition period, DAX
                                             * signal time, and algorithm settings */
    OL_COMMAND_START                = 0x04, /* Set flag to perform target measurement noise threshold,
                                             * future parameter, ranging time, repetition period, DAX
                                             * signal time */
    OL_COMMAND_FACTORY_CALIB        = 0x0A,  /* Start a factory calibration */
    OL_COMMAND_WR_CAL_STATE         = 0x0B, /* Store the factory calibration and previous algorithm
                                             * state into the device after it has been written
                                             * into the I2C RAM from address 0x20
                                             *
                                             * NOTE: Please use OL_COMMAND_START_EXT_CAL */
    OL_COMMAND_INVAL_CALIB          = 0x10, /* Invalidate calibration, calibration will be performed
                                             * when idle state is re-entered. */
    OL_COMMAND_ENTER_STANDBY        = 0x12, /* Enter standby state */
    OL_COMMAND_ENTER_BOOTLOADER     = 0x13, /* Start Bootloader again */
    OL_COMMAND_SET_DIAG_MASK        = 0x30, /* Set diagnostics state mask */
    OL_COMMAND_CONTINUE             = 0x32, /* Continue to the next natural state from a diagnostic or error
                                             * state (*check for dup) */
    OL_COMMAND_RD_AVAIL_SPADS       = 0x40, /* Read out available SPADs from factory calibration (fuses)
                                             * (registers 0x10-0x4B of register data set 1) */
    OL_COMMAND_RD_LAST_CONFIG       = 0x45, /* Read out last used configuration for background measurement
                                             * light measurement */
    OL_COMMAND_RD_FUSES             = 0x47, /* Read out fuses, uses data set 5 */
    OL_COMMAND_WR_AVAIL_SPADS       = 0x48, /* Overwrite available SPADs (uses contents of registers 0x10-0x4B
                                             * of register data set 1) */
    OL_COMMAND_RD_MEASURE_RES       = 0x50, /* Read out measurement collection results. */
    OL_COMMAND_RD_GEN_CONFIG        = 0x52, /* Read out configuration and status data including
                                             * current temperature. */
    OL_COMMAND_RD_RESULT            = 0x54, /* Read out last algorithm results. */
    OL_COMMAND_RD_RESULT2           = 0x55, /* Read out last algorithm results using the combined structure. */
    OL_COMMAND_RD_HISTO             = 0x80, /* ..0x93, Read 1 quarter of one histogram -
                                             * copy histogram bits[4:2], quarter bits[1:0] into 0x20..0x9f*/
    OL_COMMAND_SOFT_RESET           = 0xfd, /* Perform a processor soft reset of the application. Can be
                                             * performed anywhere with immediate results, as the soft reset
                                             * will be performed in the I2C interrupt handler*/
    OL_COMMAND_SYS_INIT             = 0xfe, /* Re-enter system initialization state, does NOT perform a soft
                                             * reset, will require leaving the current state processing to
                                             * take effect. Will set force_idle flag when this command is
                                             * parse in the interrupt handler to notify a cooperative function
                                             * to exit when checked..All configuration data will be reset,
                                             * calibration data will be maintained if valid.*/
    OL_COMMAND_STOP                 = 0xff, /* Stop whatever you are doing as soon as possible and reenter the
                                             * idle state (idle1). The current state will not be interrupted
                                             * and will require leaving the current state processing to take
                                             * effect. This command will reset the target_period to 0 to stop
                                             * continuous measurement. Will set force_idle flag when this
                                             * command is parse in the interrupt handler to notify a cooperative
                                             * function to exit when checked.*/
} tof_core1_commands_t;

typedef enum tof_core1_status {
    OL_STATUS_IDLE                    = 0x00, /* No error in idle state */
    OL_STATUS_DIAGNOSTIC              = 0x01, /* No error in a diagnostic state */
    OL_STATUS_START                   = 0x02, /* No error in starting/DAX delay */
    OL_STATUS_CALIBRATION             = 0x03, /* No error in calibration */
    OL_STATUS_LIGHT_COL               = 0x04, /* No error in light collection */
    OL_STATUS_ALGORITHM               = 0x05, /* No error in algorithm */
    OL_STATUS_STARTUP                 = 0x06, /* No error in startup initialization */
    OL_STATUS_VCSEL_PWR_FAIL          = 0x10, /* ERROR: VCSEL Power fail = wait for next command  */
    OL_STATUS_VCSEL_LED_A_FAIL        = 0x11, /* ERROR: VCSEL LED-A fail = wait for next command  */
    OL_STATUS_VCSEL_LED_K_FAIL        = 0x12, /* ERROR: VCSEL LED-K fail = wait for next command  */
    OL_STATUS_BDV_GEN_ERROR           = 0x13, /* ERROR: BDV mode unsupported = wait for next command  */
    OL_STATUS_UNUSED_CMD              = 0x14, /* ERROR: unused = wait for next command  */
    OL_STATUS_BDV_ERROR_LOW           = 0x15, /* ERROR: cannot find BDV (always too low) = wait for next command  */
    OL_STATUS_BDV_ERROR_HIGH          = 0x16, /* ERROR: cannot find BDV (always too high) = wait for next command  */
    OL_STATUS_DBV_COMPERATOR_ERR      = 0x17, /* ERROR: cannot find BDV (broken comperator) = wait for next command  */
    OL_STATUS_HIST_RAM                = 0x18, /* ERROR: Histogram RAM not enabled = wait for next command  */
    OL_STATUS_INVAL_DEVICE            = 0x19, /* ERROR: not BOLT = wait for next command  */
    OL_STATUS_INVAL_PARAM             = 0x1A, /* ERROR: Parameter wrong (e.g. BDV > 127, Iterations = 0, no spad selected) = wait for next command  */
    OL_STATUS_CALIB_ERROR             = 0x1B, /* ERROR: Calibration (not 2 peaks found) = wait for next command  */
    OL_STATUS_INVAL_CMD               = 0x1C, /* ERROR: unknown control code = wait for next command  */
    OL_STATUS_INVAL_STATE             = 0x1D, /* ERROR: invalid state */
    OL_STATUS_UNKNOWN                 = 0x1E, /* ERROR: unknown error */
    OL_STATUS_ERR_ALGORITHM           = 0x1F, /* ERROR: general algorithm error */
    OL_STATUS_ERR_DUP_ALLOC           = 0x20, /* ERROR: duplicate allocation error */
    OL_STATUS_ERR_NO_MEM              = 0x21, /* ERROR: ran out of work RAM */
    OL_STATUS_ERR_MEM_ID_NOT_FOUND    = 0x22, /* ERROR: ram ID not found in work RAM */
    OL_STATUS_INVAL_DATA              = 0x23, /* ERROR: invalid data in 0x20-0xEF */
    OL_STATUS_HAL_INTERRUPTED         = 0x24, /* ERROR: a HAL call was interrupted by an external trigger */
    OL_STATUS_MISSING_CALLBACK        = 0x25, /* ERROR: internal function detected missing call back function */
    OL_STATUS_ERR_FACT_CALIB          = 0x26, /* ERROR: could not perform factory calibration */
    OL_STATUS_ERR_MISSING_FACT_CAL    = 0x27, /* ERROR: missing factory calibration */
    OL_STATUS_ERR_INVALID_FACT_CAL    = 0x28, /* ERROR: invalid factory calibration data size */
    OL_STATUS_ERR_INVALID_ALG_STATE   = 0x29, /* ERROR: invalid algorithm state data size */
    OL_STATUS_ERR_INVALID_PROX_CONFIG = 0x2A, /* ERROR: invalid proximity algorithm configuration data */
    OL_STATUS_ERR_INVALID_DIST_CONFIG = 0x2B, /* ERROR: invalid distance algorithm configuration data */
} tof_core1_status_t;

#define OL_STATUS_ERROR_START       (OL_STATUS_VCSEL_PWR_FAIL)

typedef enum tof_core1_state {
    OL_STATE_STARTUP,
    OL_STATE_IDLE,
    OL_STATE_ERROR,
    OL_INTERNAL01,
    OL_INTERNAL02,
    OL_INTERNAL03,
    OL_INTERNAL04,
    OL_INTERNAL05,
    OL_INTERNAL06,
    OL_INTERNAL07,
    OL_INTERNAL08,
    OL_INTERNAL09,
    OL_INTERNAL10,
    OL_INTERNAL11,
    OL_INTERNAL12,
    OL_INTERNAL13,
    OL_INTERNAL14,
    OL_INTERNAL15,
    OL_INTERNAL16,
    OL_INTERNAL17,
    OL_INTERNAL18,
    OL_INTERNAL19,
    OL_INTERNAL20,
    OL_INTERNAL21,
    OL_INTERNAL22,
    OL_INTERNAL23,
    OL_INTERNAL24,
    OL_INTERNAL25,
    OL_INTERNAL26,
    OL_INTERNAL27,
    OL_INTERNAL28,
    OL_INTERNAL29,
} tof_core1_state_t;

#define OL_DATA_START               (OL_HISTOGRAM_START)
#define OL_DATA_END                 (0xE0 - 1)

/* FACTORY_CAL_SIZE defines the size of the factory calibration packet size,
 * it is to be downloaded to the target before the first target measurement
 * is started */
#define OL_FACTORY_CAL_SIZE         (14)

#define OL_RES2_LAST_STATE_DATA     (OL_RES2_STATE_DATA_10)
#define OL_RES2_STATE_DATA_SIZE     (11)

typedef struct tof_core2_result_add_target {
    uint16_t reliability;
    uint16_t distPeak;
} PACKED tof_core2_result_add_target_t;

typedef struct tof_core2_alg_state_data {
    uint8_t data[OL_RES2_STATE_DATA_SIZE];
} PACKED tof_core2_alg_state_data_t;

typedef struct tof_core2_factory_cal_data {
    union {
        struct {
            uint32_t        struct_rev:4;
            uint32_t        ct_intensity_cal:20;
            uint32_t        data0:8;
            uint8_t         data1[10];
        } PACKED;
        uint8_t data[OL_FACTORY_CAL_SIZE];
    };
} PACKED tof_core2_factory_cal_data_t;

#define LUT_LEN 51
#define LEFT_LUT_LEN 20
#define RIGHT_LUT_LEN 40
#define PRESENCE_THRESH_LEN 29
#define PROX_CONFIG_COMPRESSED_BYTES 85
#define DIST_CONFIG_COMPRESSED_BYTES 34

typedef struct tof_core2_prox_config_data {
    uint8_t data[PROX_CONFIG_COMPRESSED_BYTES];
} PACKED tof_core2_prox_config_data_t;

typedef struct tof_core2_dist_config_data {
    uint8_t data[DIST_CONFIG_COMPRESSED_BYTES];
} PACKED tof_core2_dist_config_data_t;

typedef struct tof_core2_algo_config_data {
    tof_core2_prox_config_data_t prox_config;
    tof_core2_dist_config_data_t dist_config;
} PACKED tof_core2_algo_config_data_t;

typedef enum {
    resultStatusNone = 0,       /* GPIO intterupt occurred during the proximity light
                                 * collection, no result possible */
    resultStatusProx = 1,       /* GPIO interrupt occurred during the distance light
                                 * measurement, Proximity result present */
    resultStatusProxDist = 2,   /* GPIO interrupt occurred during the distance
                                 * algorithm, result complete */
    resultStatusComplete = 3,
} tof_core2_result_status_t;

typedef union {
        struct {
            uint8_t reliability:6;
            uint8_t filler:1;
            uint8_t measureInterrupted:1;
        } PACKED;
        struct {
            uint8_t reliabilityE:6;
            tof_core2_result_status_t resultStatus:2;
        } PACKED;
        uint8_t data;
} PACKED tof_core2_result_info_t;

typedef struct tof_core2_result_data {
    uint8_t resultNum;
    tof_core2_result_info_t resultInfo;
    union {
        struct {
            uint8_t distPeak0;
            uint8_t distPeak1;
        } PACKED;
        uint16_t distPeak;
    };
    union {
        struct {
            uint8_t sysClock0;
            uint8_t sysClock1;
            uint8_t sysClock2;
            uint8_t sysClock3;
        } PACKED;
        struct {
            uint32_t sysClock;
        } PACKED;
    };
    tof_core2_alg_state_data_t stateData;
    union {
        struct {
            uint8_t referenceHits0;
            uint8_t referenceHits1;
            uint8_t referenceHits2;
            uint8_t referenceHits3;
        } PACKED;
        uint32_t referenceHits;
    };
    union {
        struct {
            uint8_t objectHits0;
            uint8_t objectHits1;
            uint8_t objectHits2;
            uint8_t objectHits3;
        } PACKED;
        uint32_t objectHits;
    };
    uint8_t numAddTargets;
    tof_core2_result_add_target_t addTargets[];
} PACKED tof_core2_result_data_t;

typedef struct tof_core2_result_status_regs {
    tof_core1_status_t status:8;
    tof_core1_commands_t registerContents:8;
    uint8_t tid;
} PACKED tof_core2_result_status_regs_t;

/* NOTE: Use this structure for reading the result2 data from the 8701
 *
 * To read the status, distance, clock delta, algorithm state information, and hit
 * count information, follow the pseudo code:
 *
 *          tof_core2_result_t result;
 *          readI2C(amsI2CAddr, &result, sizeof(tof_core2_result_status_regs_t) +
 *                                       tofoffsetof(tof_core2_result_data_t, addTargets));
 *
 * To read only the status, distance, clock delta, and algorithm information
 * use the following pseudo code:
 *
 *          tof_core2_result_t result;
 *          readI2C(amsI2CAddr, &result, sizeof(tof_core2_result_status_regs_t) +
 *                                       tofoffsetof(tof_core2_result_data_t, referenceHits));
 *
 * To read the status, distance, clock delta, algorithm state information, hit
 * count information, and addition targets, follow the pseudo code:
 *
 *          uint8_t data[128];      // Accommodate variable length field for addition targets
 *          tof_core2_result_t *result = (tof_core2_result_t *) data;
 *          readI2C(amsI2CAddr, data, 128);
 */
typedef struct tof_core2_result {
    tof_core2_result_status_regs_t status;
    tof_core2_result_data_t data;
} PACKED tof_core2_result_t;

#define OL_STATE_FIRST  OL_STATE_STARTUP
#define OL_STATE_LAST   OL_STATE_DIAG10
#define OL_STATE_ERROR  OL_STATE_ERROR

#define OL_STATE_NUM_TOTAL_STATES   OL_STATE_NUM_ROM_STATES

typedef enum tof_core1_tdc_avail {
    tdcAvailNone = 0,
    tdcAvail0,
    tdcAvail1,
    tdcAvail2,
    tdcAvail3,
    tdcAvail4,
    tdcAvailSum,
    tdcAvailAll5,
} tof_core1_tdc_avail_t;

typedef enum tof_core1_diag_num {
    OL_DIAG_START                   = 0,
    OL_DIAG_HIST_ELECTRICAL_CAL     = 1,
    OL_DIAG_HIST_OPTICAL_CAL        = 2,
    OL_DIAG_HIST_PROXIMITY          = 4,
    OL_DIAG_HIST_FACTORY_CALIB      = 6,
    OL_DIAG_HIST_DISTANCE           = 7,
    OL_DIAG_GEN_ALG                 = 8,
    OL_DIAG_HIST_SPAD_MEASURE       = 9,
    OL_DIAG_HIST_ALG_PILEUP         = 16,
    OL_DIAG_HIST_ALG_PU_TDC_SUM     = 17,
    OL_DIAG_HIST_PROX_ALG_PILEUP    = 18,
    OL_DIAG_LAST                    = 31,
} tof_core1_diag_num_t;

#define OL_DIAG_NUM                 (OL_DIAG_LAST + 1)

#define MASK_START                  (1 << OL_DIAG_START)
#define MASK_HIST_ELECTRICAL_CAL    (1 << OL_DIAG_HIST_ELECTRICAL_CAL)
#define MASK_HIST_OPTICAL_CAL       (1 << OL_DIAG_HIST_OPTICAL_CAL)
#define MASK_HIST_PROXIMITY         (1 << OL_DIAG_HIST_PROXIMITY)
#define MASK_HIST_FACTORY_CALIB     (1 << OL_DIAG_HIST_FACTORY_CALIB)
#define MASK_HIST_DISTANCE          (1 << OL_DIAG_HIST_DISTANCE)
#define MASK_GEN_ALG                (1 << OL_DIAG_GEN_ALG)
#define MASK_HIST_ALG_PILEUP        (1 << OL_DIAG_HIST_ALG_PILEUP)
#define MASK_HIST_ALG_PU_TDC_SUM    (1 << OL_DIAG_HIST_ALG_PU_TDC_SUM)
#define MASK_HIST_SPAD_MEASURE      (1 << OL_DIAG_HIST_SPAD_MEASURE)

typedef union tof_core1_diag_info {
    struct {
        uint16_t isDiag:1;
        tof_core1_diag_num_t diagNum:5;
        uint16_t size:1;
        uint16_t reserved0:1;
        tof_core1_tdc_avail_t tdcAvail:4;
        uint16_t reserved1:4;
    } PACKED;
    struct {
        uint8_t data_0;
        uint8_t data_1;
    } PACKED;
    uint16_t data;
} PACKED tof_core1_diag_info_t;

/*******************************************************************************
 *******************************************************************************
 * structures that mirror the registers from tof_core1_mainapp_collection_reg.h
 */

typedef union tof_core1_spad_config {
    struct {
        struct {
            union {
                struct {
                    uint8_t spad0_0;                   /* OL_SPAD0_0 */
                    uint8_t spad0_1;                   /* OL_SPAD0_1 */
                    uint8_t spad0_2;                   /* OL_SPAD0_2 */
                    uint8_t spad0_3;                   /* OL_SPAD0_3 */
                } PACKED;
                uint32_t spad0;                        /* OL_SPAD0   */
            } PACKED;
            union {
                struct {
                    uint8_t spad1_0;                   /* OL_SPAD1_0 */
                    uint8_t spad1_1;                   /* OL_SPAD1_1 */
                    uint8_t spad1_2;                   /* OL_SPAD1_2 */
                    uint8_t spad1_3;                   /* OL_SPAD1_3 */
                } PACKED;
                uint32_t spad1;                        /* OL_SPAD1   */
            } PACKED;
            union {
                struct {
                    uint8_t spad2_0;                   /* OL_SPAD2_0 */
                    uint8_t spad2_1;                   /* OL_SPAD2_1 */
                    uint8_t spad2_2;                   /* OL_SPAD2_2 */
                    uint8_t spad2_3;                   /* OL_SPAD2_3 */
                } PACKED;
                uint32_t spad2;                        /* OL_SPAD2   */
            } PACKED;
            union {
                struct {
                    uint8_t spad3_0;                   /* OL_SPAD3_0 */
                    uint8_t spad3_1;                   /* OL_SPAD3_1 */
                    uint8_t spad3_2;                   /* OL_SPAD3_2 */
                    uint8_t spad3_3;                   /* OL_SPAD3_3 */
                } PACKED;
                uint32_t spad3;                        /* OL_SPAD3   */
            } PACKED;
            union {
                struct {
                    uint8_t spad4_0;                   /* OL_SPAD4_0 */
                    uint8_t spad4_1;                   /* OL_SPAD4_1 */
                    uint8_t spad4_2;                   /* OL_SPAD4_2 */
                    uint8_t spad4_3;                   /* OL_SPAD4_3 */
                } PACKED;
                uint32_t spad4;                        /* OL_SPAD4   */
            } PACKED;
        } PACKED;
    } PACKED;
    uint32_t spad[TOF_CORE1_NUMBER_OF_HISTOGRAMS];
} PACKED tof_core1_spad_config_t;

typedef struct tof_core1_spec_measure_params {
    union {
        struct {
            uint8_t intcycles_0;                       /* OL_INTCYCLES_0 */
            uint8_t intcycles_1;                       /* OL_INTCYCLES_1 */
            uint8_t intcycles_2;                       /* OL_INTCYCLES_2 */
            uint8_t intcycles_3;                       /* OL_INTCYCLES_3 */
        } PACKED;
        uint32_t intcycles;                            /* OL_INTCYCLES */
    } PACKED;
    uint8_t correlationMode;                           /* OL_CORRELATIONMODE  */
    uint8_t softCorrelationMode;                       /* OL_SOFTCORRMODE     */
    uint8_t softCorrThreshold;                         /* OL_SOFTCORRTHRESHOLD    */
    uint8_t tdcStartInvert;                            /* OL_TDCSTARTINVERT   */
    union {
        struct {
            uint8_t vcselCfgFiller:2;
            uint8_t invertEnableVpulse:1;
            uint8_t vcselPulseMode:1;
            uint8_t vcselClkPulseLen:2;
            uint8_t vcselSetChpUnreg:1;
            uint8_t prePulseWidth2x:1;
        } PACKED;
        uint8_t vcselCfg;                              /* OL_VCSEL_CFG     */
    } PACKED;
    union {
        struct {
            uint8_t vcselIPulse:4;
            uint8_t vcselIPrePulse:2;
            uint8_t vcselPulseWidth:2;
        } PACKED;
        uint8_t vcselCfg2;                             /* OL_VCSEL_CFG2     */
    } PACKED;
    union {
        struct {
            uint8_t vcselClkSel:3;
            uint8_t ssAmplitude:5;
        } PACKED;
        uint8_t vcselCfg3;                             /* OL_VCSEL_CFG3     */
    } PACKED;
    uint8_t tdcPowerSaving;                            /* OL_TDCPOWERSAVING     */
    uint8_t delay;                                     /* OL_DELAY        */
    union {
        struct {
            uint8_t accuHitsThreshold:4;
            uint8_t binThreshold:4;
        } PACKED;
        uint8_t thresholds;                            /* OL_THRESHOLDS   */
    } PACKED;
    uint8_t dualMode;                                  /* OL_DUALMODE    */
    uint8_t cal_trig;                                  /* OL_QUENCHER  */
    uint8_t inclTdc0;                                  /* OL_ACCUHITS_INCL0     */
    uint8_t asyncReset;                                /* OL_ASYNCRESET         */
    union {
        struct {
            uint8_t enabTdcPulseShape:1;
            uint8_t enabQuePulseShape:1;
        } PACKED;
        uint8_t enPulseShape;                          /* OL_ENPULSESHAPE    */
    } PACKED;
    uint8_t spadDeadTime;                              /* OL_DEADTIME         */
    uint8_t startDelay;                                /* OL_TDCSTARTDELAY     */
    uint8_t tdcStartSelect;                            /* OL_TDCSTARTSELECT     */
    uint8_t chargePumpStopsTdc;                        /* OL_CPSTOPSTDC           */
} PACKED tof_core1_spec_measure_params_t;

typedef struct tof_core1_collection_reg_t {
    tof_core1_spad_config_t spads;
    tof_core1_spec_measure_params_t measureParams;
} PACKED tof_core_collection_t;

typedef union {
    struct {
        uint8_t dataFactoryConfig:1;
        uint8_t dataAlgorithmState:1;
        uint8_t dataConfiguration:1;
        uint8_t dataReserved:5;
    };
    uint8_t data;
} tof_core_data_t;

/*******************************************************************************
 *******************************************************************************
 * structures that mirror the registers from tof_core1_mainapp_general_reg.h
 */

typedef enum gpio_setting
{
    GPIO_DISABLED           = 0,
    GPIO_ACTIVE_L_PAUSE     = 1,
    GPIO_ACTIVE_H_PAUSE     = 2,
    GPIO_DAX_OUTPUT         = 3,
    GPIO_HOLD_LOW           = 4,
    GPIO_HOLD_HIGH          = 5,
    GPIO_FUTURE_6           = 6,
    GPIO_FUTURE_7           = 7,
    GPIO_FUTURE_8           = 8,
    GPIO_FUTURE_9           = 9,
    GPIO_FUTURE_10          = 10,
    GPIO_FUTURE_11          = 11,
    GPIO_FUTURE_12          = 12,
    GPIO_FUTURE_13          = 13,
    GPIO_FUTURE_14          = 14,
    GPIO_FUTURE_15          = 15,
} tof_core_gpio_t;

typedef struct tof_core1_general_app_config {
    union {
        struct {
            uint8_t diagGateMask0;                     /* OL_DIAG_GATE_MASK_0         */
            uint8_t diagGateMask1;                     /* OL_DIAG_GATE_MASK_1         */
            uint8_t diagGateMask2;                     /* OL_DIAG_GATE_MASK_2         */
            uint8_t diagGateMask3;                     /* OL_DIAG_GATE_MASK_3         */
        } PACKED;
        uint32_t diagGateMask;
    } PACKED;
    uint8_t repetitionPeriod;                          /* OL_REPETITION_PERIOD        */
    uint8_t rangingTime;                               /* OL_RANGING_TIME             */
    uint8_t currTemp;                                  /* OL_CURR_TEMP                */
    uint8_t reserved0;
    uint8_t distAlgParam0;
    uint8_t distAlgParam1;
    union {
        struct {
            uint8_t algProximityEnabled:1;
            uint8_t algDistanceEnabled:1;
            uint8_t algReserved:2;
            uint8_t algEnhancedResult:1;
            uint8_t algCombinedCapture:1;
            uint8_t algLegacyResult:1;
            uint8_t algKeepReady:1;
        };
        uint8_t algSetting;
    };
    union {
        struct {
            tof_core_gpio_t gpio0:4;
            tof_core_gpio_t gpio1:4;
        };
        uint8_t gpio;
    };
} PACKED tof_core1_general_app_config_t;

typedef struct tof_core1_general_measure_config {
    union {
        struct {
            uint8_t edge1:4;
            uint8_t edge2:4;
        } PACKED;
        uint8_t calibration;                           /* OL_CALIBRATION */
    } PACKED;
    uint8_t mhz;                                       /* OL_MHZ                      */
    uint8_t binShift;                                  /* OL_BIN_SHIFT    */
    uint8_t bdvMode;                                   /* OL_BDV_MODE    */
    union {
        struct {
            uint8_t intNoiseFloor:4;
            uint8_t optNoiseFloor:4;
        } PACKED;
        uint8_t calib;                                 /* OL_CALIB */
    } PACKED;
    uint8_t nominalPeakDistance;                       /* OL_CALIB2   */
    uint8_t reserved3;
    uint8_t reserved4;
} PACKED tof_core1_general_measure_config_t;

typedef struct tof_core1_general_calib_results {
    uint8_t calTemp;                                   /* OL_CAL_TEMP                 */
    uint8_t breakDownVolt;                             /* OL_BDV_DETECT, resultant breakdown voltage */
    union {
        struct {
            uint16_t binSize0; /* OL_BIN_SIZE_0_0 OL_BIN_SIZE_0_1 */
            uint16_t binSize1; /* OL_BIN_SIZE_1_0 OL_BIN_SIZE_1_1 */
            uint16_t binSize2; /* OL_BIN_SIZE_2_0 OL_BIN_SIZE_2_1 */
            uint16_t binSize3; /* OL_BIN_SIZE_3_0 OL_BIN_SIZE_3_1 */
            uint16_t binSize4; /* OL_BIN_SIZE_4_0 OL_BIN_SIZE_4_1 */
        } PACKED;
        uint16_t binSize[TOF_CORE1_NUMBER_OF_HISTOGRAMS];
        uint8_t binSizeBytes[TOF_CORE1_NUMBER_OF_HISTOGRAMS * (sizeof(uint16_t)/sizeof(uint8_t))];
    } PACKED;
    union {
        struct {
            uint8_t distanceOptical_0;                         /* OL_DISTANCE_OPTICAL_0  */
            uint8_t distanceOptical_1;                         /* OL_DISTANCE_OPTICAL_1  */
            uint8_t distanceOptical_2;                         /* OL_DISTANCE_OPTICAL_2  */
            uint8_t distanceOptical_3;                         /* OL_DISTANCE_OPTICAL_3  */
        } PACKED;
        uint32_t distanceOptical;
    } PACKED;
    union {
        struct {
            uint8_t proximityOptical_0;                         /* OL_PROXIMITY_OPTICAL_0  */
            uint8_t proximityOptical_1;                         /* OL_PROXIMITY_OPTICAL_1  */
            uint8_t proximityOptical_2;                         /* OL_PROXIMITY_OPTICAL_2  */
            uint8_t proximityOptical_3;                         /* OL_PROXIMITY_OPTICAL_3  */
        } PACKED;
        uint32_t proximityOptical;
    } PACKED;
} PACKED tof_core1_general_calib_results_t;

typedef struct tof_core1_general_config {
    tof_core1_general_app_config_t generalAppConfig;
    tof_core1_general_measure_config_t generalMeasureConfig;
    tof_core1_general_calib_results_t generalCalibResults;
} PACKED tof_core1_general_config_t;

/*******************************************************************************
 *******************************************************************************
 * structures that mirror the registers from tof_core1_mainapp_measure_info_reg.h
 */

typedef struct tof_core1_measure_info {
    union {
        struct {
            uint8_t accuHits0;                         /* OL_ACCUHITS0            */
            uint8_t accuHits1;                         /* OL_ACCUHITS1            */
            uint8_t accuHits2;                         /* OL_ACCUHITS2            */
            uint8_t accuHits3;                         /* OL_ACCUHITS3            */
        } PACKED;
        uint32_t accuHits;
    } PACKED;
    union {
        struct {
            uint8_t tdc0Hits0;                         /* OL_TDC0HITS0            */
            uint8_t tdc0Hits1;                         /* OL_TDC0HITS1            */
            uint8_t tdc0Hits2;                         /* OL_TDC0HITS2            */
            uint8_t tdc0Hits3;                         /* OL_TDC0HITS3            */
        } PACKED;
        uint32_t tdc0Hits;
    } PACKED;
    union {
        struct {
            uint8_t tdc1Hits0;                         /* OL_TDC1HITS0            */
            uint8_t tdc1Hits1;                         /* OL_TDC1HITS1            */
            uint8_t tdc1Hits2;                         /* OL_TDC1HITS2            */
            uint8_t tdc1Hits3;                         /* OL_TDC1HITS3            */
        } PACKED;
        uint32_t tdc1Hits;
    } PACKED;
    union {
        struct {
            uint8_t tdc2Hits0;                         /* OL_TDC2HITS0            */
            uint8_t tdc2Hits1;                         /* OL_TDC2HITS1            */
            uint8_t tdc2Hits2;                         /* OL_TDC2HITS2            */
            uint8_t tdc2Hits3;                         /* OL_TDC2HITS3            */
        } PACKED;
        uint32_t tdc2Hits;
    } PACKED;
    union {
        struct {
            uint8_t tdc3Hits0;                         /* OL_TDC3HITS0            */
            uint8_t tdc3Hits1;                         /* OL_TDC3HITS1            */
            uint8_t tdc3Hits2;                         /* OL_TDC3HITS2            */
            uint8_t tdc3Hits3;                         /* OL_TDC3HITS3            */
        } PACKED;
        uint32_t tdc3Hits;
    } PACKED;
    union {
        struct {
            uint8_t tdc4Hits0;                         /* OL_TDC4HITS0            */
            uint8_t tdc4Hits1;                         /* OL_TDC4HITS1            */
            uint8_t tdc4Hits2;                         /* OL_TDC4HITS2            */
            uint8_t tdc4Hits3;                         /* OL_TDC4HITS3            */
        } PACKED;
        uint32_t tdc4Hits;
    } PACKED;
    union {
        struct {
            uint8_t tdc0RawHitsl;                      /* OL_TDC0RAWHITSL         */
            uint8_t tdc0RawHitsh;                      /* OL_TDC0RAWHITSH         */
        } PACKED;
        uint16_t tdcRawHits0;
    } PACKED;
    union {
        struct {
            uint8_t tdc1RawHitsl;                      /* OL_TDC1RAWHITSL         */
            uint8_t tdc1RawHitsh;                      /* OL_TDC1RAWHITSH         */
        } PACKED;
        uint16_t tdcRawHits1;
    } PACKED;
    union {
        struct {
            uint8_t tdc2RawHitsl;                      /* OL_TDC2RAWHITSL         */
            uint8_t tdc2RawHitsh;                      /* OL_TDC2RAWHITSH         */
        } PACKED;
        uint16_t tdcRawHits2;
    } PACKED;
    union {
        struct {
            uint8_t tdc3RawHitsl;                      /* OL_TDC3RAWHITSL         */
            uint8_t tdc3RawHitsh;                      /* OL_TDC3RAWHITSH         */
        } PACKED;
        uint16_t tdcRawHits3;
    } PACKED;
    union {
        struct {
            uint8_t tdc4RawHitsl;                      /* OL_TDC4RAWHITSL         */
            uint8_t tdc4RawHitsh;                      /* OL_TDC4RAWHITSH         */
        } PACKED;
        uint16_t tdcRawHits4;
    } PACKED;
    union {
        struct {
            uint8_t tdc0Saturationl;                   /* OL_TDC0SATURATIONL      */
            uint8_t tdc0Saturationh;                   /* OL_TDC0SATURATIONH      */
        } PACKED;
        uint16_t tdcSaturationCounter0;
    } PACKED;
    union {
        struct {
            uint8_t tdc1Saturationl;                   /* OL_TDC1SATURATIONL      */
            uint8_t tdc1Saturationh;                   /* OL_TDC1SATURATIONH      */
        } PACKED;
        uint16_t tdcSaturationCounter1;
    } PACKED;
    union {
        struct {
            uint8_t tdc2Saturationl;                   /* OL_TDC2SATURATIONL      */
            uint8_t tdc2Saturationh;                   /* OL_TDC2SATURATIONH      */
        } PACKED;
        uint16_t tdcSaturationCounter2;
    } PACKED;
    union {
        struct {
            uint8_t  tdc3Saturationl;                  /* OL_TDC3SATURATIONL      */
            uint8_t  tdc3Saturationh;                  /* OL_TDC3SATURATIONH      */
        } PACKED;
        uint16_t tdcSaturationCounter3;
    } PACKED;
    union {
        struct {
            uint8_t  tdc4Saturationl;                  /* OL_TDC4SATURATIONL      */
            uint8_t  tdc4Saturationh;                  /* OL_TDC4SATURATIONH      */
        } PACKED;
        uint16_t tdcSaturationCounter4;
    } PACKED;
    union {
        struct {
            uint8_t  tdcifCounter0;                   /* OL_TDCIF_COUNTER_0      */
            uint8_t  tdcifCounter1;                   /* OL_TDCIF_COUNTER_1      */
            uint8_t  tdcifCounter2;                   /* OL_TDCIF_COUNTER_2      */
            uint8_t  tdcifCounter3;                   /* OL_TDCIF_COUNTER_3      */
        } PACKED;
        uint32_t tdcifCounter;
    } PACKED;

    union {
        struct {
            uint8_t  phaseErrorsL;                  /* OL_PHASEERRL          */
            uint8_t  phaseErrorsH;                  /* OL_PHASEERRH          */
        } PACKED;
        uint16_t phaseErrors;
    } PACKED;

    union {
        struct {
            uint8_t  lateHitsL;                     /* OL_LATEHITSL          */
            uint8_t  lateHitsH;                     /* OL_LATEHITSH          */
        } PACKED;
        uint16_t lateHits;
    } PACKED;

    union {
        struct {
            uint8_t tdcBusy:1;                      /* This bit indicates that a measurement is ongoing. This bit can be read at any time. */
            uint8_t thresholdExceeded:1;            /* TDC threshold has been exceeded in previous measurement cycle. If this bit is asserted, then the measurement has stopped not because tdc_count has been reached but because the threshold for "automatic exposure control" was surpassed. Value is only valid after a measurement. */
            uint8_t chargePumpOverload:1;           /* The cp_overload bit was flagged during the previous measurement. If enabled in cp_stops_tdc, then this is (can be) the reason for a premature end of the measurement. Note: the live value of cp_overload can be seen in the I2C register map. */
        } PACKED;
        uint8_t tdcifStatus;                        /* OL_TDCIF_STATUS */
    } PACKED;

    uint8_t collectionCycles;                       /* Number of times the TDC measurement was initiated to
                                                     * attempt to complete all of the requested iterations */

    union {
        struct {
            uint8_t  totalIterations0;                   /* OL_TDCIF_COUNTER_0      */
            uint8_t  totalIterations1;                   /* OL_TDCIF_COUNTER_1      */
            uint8_t  totalIterations2;                   /* OL_TDCIF_COUNTER_2      */
            uint8_t  totalIterations3;                   /* OL_TDCIF_COUNTER_3      */
        } PACKED;
        uint32_t totalIterations;
    } PACKED;
} PACKED tof_core1_measure_info_t;

/*******************************************************************************
 *******************************************************************************
 * structures that mirror the registers from tof_core1_mainapp_fuses_reg.h
 */

#define NUM_FUSES           (32)

typedef struct tof_core1_fuses {
    uint8_t fuse[NUM_FUSES];
} PACKED tof_core1_fuses_t;

/*******************************************************************************
 * \brief Defines the registers in the fuses page for the SPAD masks
 */

typedef struct tof_core1_fuses_page {
    tof_core1_fuses_t fuses;
} PACKED tof_core1_fuses_page_t;

typedef enum ram_width
{
    RAM_WIDTH_16,
    RAM_WIDTH_32,
} ram_width_t;

typedef enum ram_size
{
    RAM_SIZE_128,
    RAM_SIZE_256,
} ram_size_t;

typedef enum ram_contents
{
    /* Values from 0 - 7 can be passed to the host for memory transfer, choose wisely */
    RAM_CONT_TDC_SUM = 1,       /* Sum of the current TDC channels */
    RAM_CONT_ALL_TDC_SUM = 2,   /* Sum of the current TDC channels */

    RAM_CONT_WORK1 = 8,         /* Work space 1 */
    RAM_CONT_WORK2 = 9,         /* Work space 2 */
    RAM_CONT_SUM = 10,          /* Time corrected sum of all 4 TDCs */
    RAM_CONT_CHAN_A = 11,       /* Current TDC channel A */
    RAM_CONT_CHAN_B = 12,       /* Current TDC channel B */
    RAM_CONT_1A = 13,           /* TDC 1 channel A */
    RAM_CONT_1B = 14,           /* TDC 1 channel B */
    RAM_CONT_2A = 15,           /* TDC 2 channel A */
    RAM_CONT_2B = 16,           /* TDC 2 channel B */
    RAM_CONT_3A = 17,           /* TDC 3 channel A */
    RAM_CONT_3B = 18,           /* TDC 3 channel B */
    RAM_CONT_4A = 19,           /* TDC 4 channel A */
    RAM_CONT_4B = 20,           /* TDC 4 channel B */

    /* NOTE: the following 5 are specifically the 5 TDC RAM segements */
    RAM_CONT_TDC_RAM_0 = 21,     /* TDC 0 RAM histogram */
    RAM_CONT_TDC_RAM_1 = 22,     /* TDC 1 RAM histogram */
    RAM_CONT_TDC_RAM_2 = 23,     /* TDC 2 RAM histogram */
    RAM_CONT_TDC_RAM_3 = 24,     /* TDC 3 RAM histogram */
    RAM_CONT_TDC_RAM_4 = 25,     /* TDC 4 RAM histogram */

    /* NOTE: the following 5 enumerated constants will be automatically chosen
     * from the histogram RAM or main RAM depending on exactly how the histogram
     * was collected */
    RAM_CONT_TDC_SUM_0 = 26,     /* Main RAM TDC sum 0 histogram */
    RAM_CONT_TDC_SUM_1 = 27,     /* Main RAM TDC sum 1 histogram */
    RAM_CONT_TDC_SUM_2 = 28,     /* Main RAM TDC sum 2 histogram */
    RAM_CONT_TDC_SUM_3 = 29,     /* Main RAM TDC sum 3 histogram */
    RAM_CONT_TDC_SUM_4 = 30,     /* Main RAM TDC sum 4 histogram */
    RAM_CONT_FREE = 255,        /* This memory is not being used */
} ram_contents_t;

typedef enum tof_core1_histogram_source {
    tof_core1_histo_src_tdc_ram = 0,
    tof_core1_histo_src_ram_tdc_sum = RAM_CONT_TDC_SUM,
    tof_core1_histo_src_ram_all_tdc_sum = RAM_CONT_ALL_TDC_SUM,
} tof_core1_histogram_source_t;

/* Maps directly to the OL_STATUS_FLAGS register. Replicated in a structure
 * for compiler efficiency */
typedef union _appStatus
{
    struct {
        uint16_t validElectCalib:1;         /* Voltage breakdown and electrical calibration valid */
        uint16_t performMeasure:1;          /* start target measurement */
        uint16_t performFactoryCalib:1;     /* Perform the factory calibration measurement sequence */
        uint16_t calibError:1;              /* Is there is an error in calibration this bit will be set. */
        uint16_t isCommandPending:1;        /* Indicates a I2C interrupt was received and a command is waiting */
        uint16_t hostContinue:1;            /* Indicates a continue from diagnostic state has been received */
        uint16_t measureInterrupt:1;        /* Indicates the current measurement was delayed due
                                             * to external trigger */
        uint16_t performingMeasurement:1;   /* Indicates a measurement is current in progress */
        uint16_t reserved:5;
        uint16_t validOptCalib:1;           /* Voltage breakdown and electrical calibration valid */
        uint16_t pausedGpio0:1;             /* Measurements are currently paused on GPIO0 */
        uint16_t pausedGpio1:1;             /* Measurements are currently paused on GPIO1 */
    } PACKED;
    struct {
        uint8_t data_0;
        uint8_t data_1;
//        uint8_t data_2;         // TODO should be gone
    } PACKED;
    uint16_t data;
} appStateFlags_t;

typedef uint32_t (*rangingTimeRemaining_t)(void);

/* The following constants are used for external calibration and machine/algorithm state sizing */
#define MAX_EXTERNAL_CALIB_DATA     (128)
#define MAX_TOTAL_ALG_DATA_SIZE     (128)
#define MAX_ALG_DATA_SIZE           (64)

/*****************************************************************************
 *   Temporary structure and enum to be used for development
 */

typedef enum tof_core1_diag_src {
    OL_DIAG_SRC_CAL_ELECTRICAL = 0,
    OL_DIAG_SRC_CAL_OPTICAL,
    OL_DIAG_SRC_PROXIMITY,
    OL_DIAG_SRC_FACTORY_CALIB,
    OL_DIAG_SRC_DISTANCE,
    OL_DIAG_SRC_ALG_TAP_DATA,
} tof_core1_diag_src_t;

typedef union manuData {
    struct {
        uint16_t bin:8;                 /* Bin number 0-255 */
        uint16_t tdc:3;                 /* TDC # 0-4 */
        tof_core1_diag_src_t src:3;     /* 0 - electrical calib
                                         * 1 - optical calib
                                         * 2 - proximity
                                         * 3 - minor saturation
                                         * 5 - factory calibration
                                         * 4 - target
                                         * 6 - tap data
                                         */
        uint16_t ctr:2;                 /* Incremented in publish results */
    } PACKED;
    uint16_t data;
} PACKED manuData_t;

// TODO WE SHOULD BE ABLE TO REMOVE THIS
typedef union tof_core1_alg_state_data
{
    uint8_t data[MAX_ALG_DATA_SIZE];
    struct {
        uint8_t length;
        uint8_t algorithmData[MAX_ALG_DATA_SIZE - 1];
    };
} tof_core1_alg_state_data_t;

#endif // #ifndef TOF_CORE1_MAIN_APP_ENUM

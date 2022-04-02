/*
*****************************************************************************
* Copyright by ams AG                                                       *
* All rights are reserved.                                                  *
*                                                                           *
* IMPORTANT - PLEASE READ CAREFULLY BEFORE COPYING, INSTALLING OR USING     *
* THE SOFTWARE.                                                             *
*                                                                           *
* THIS SOFTWARE IS PROVIDED FOR USE ONLY IN CONJUNCTION WITH AMS PRODUCTS.  *
* USE OF THE SOFTWARE IN CONJUNCTION WITH NON-AMS-PRODUCTS IS EXPLICITLY    *
* EXCLUDED.                                                                 *
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

/***** tof_mainapp_driver_interface.h *****/

#ifndef __TOF_MAINAPP_DRIVER_INTERFACE_H
#define __TOF_MAINAPP_DRIVER_INTERFACE_H

#include <linux/types.h>
#include "tof_core1_mainapp_control_reg.h"
#include "tof_core1_mainapp_general_reg.h"
#include "tof_core1_mainapp_alg_res_reg.h"
#include "tof_core1_mainapp_alg_res2_reg.h"
#include "tof_core1_enum.h"

#define TOF8701_APP0_VER_V1               1
#define TOF8701_APP0_VER_V2               2
#define TOF8701_APP0_DELAY_DEFAULT         (0x0)
#define TOF8701_APP0_PERIOD_DEFAULT        (0x0)
#define TOF8701_APP0_RANGE_TIME_DEFAULT    (0x0)
#define TOF8701_APP0_PARAM_DEFAULT         (0xFF)
#define TOF8701_APP0_NOISE_THRSHLD_DEFAULT (0xFF)
#define TOF8701_APP0_ALG_DEFAULT           (0x0)
#define TOF8701_APP0_GPIO_DEFAULT          (0x0)
#define TOF8701_APP0_CMD_NUM_PARAMS       10
#define TOF8701_APP0_MAX_CMD_SIZE         (TOF8701_APP0_CMD_NUM_PARAMS + 1)
#define TOF8701_APP0_CMD_IDX              (TOF8701_APP0_MAX_CMD_SIZE - 1)
#define TOF8701_APP0_FORCE_COMMANDS       0xF0
#define TOF8701_APP0_DATA_START           0x20
#define TOF8701_APP0_DATA_END             0xDF
#define TOF8701_APP0_MAX_DATA_SIZE        (TOF8701_APP0_DATA_END - \
                                           TOF8701_APP0_DATA_START + 1)
#define TOF8701_APP0_OFFSET(x)            ((x) - 0x20)
#define TOF8701_APP0_IS_MEAS_CMD(x)       (((x) >= OL_COMMAND_ASYNC_MIN) && \
                                           ((x) <= OL_COMMAND_ASYNC_MAX))


#define TOF8701_APP0_MAX_HIST_BYTE_SIZE   512
#define TOF8701_APP0_HIST_CHUNK_SIZE      128
#define TOF8701_APP0_NUM_TDC              5
#define TOF8701_APP0_HIST_BIN_SIZE        (sizeof(char)*2)
#define TOF8701_APP0_MAX_NUM_HIST_BINS    ((TOF8701_APP0_MAX_HIST_BYTE_SIZE) /\
                                          (TOF8701_APP0_HIST_BIN_SIZE))

#define DRV_FRAME_ID_TO_HIST_TYPE(x)      (((x) >> 3) & 0x1F)
#define DRV_FRAME_ID_TO_HIST_TDC(x)       ((x) & 0x7)
#define BYTES_TO_16B(lsb, msb)            ((unsigned int)(((((unsigned char)msb) << 8) & 0xFF00) | ((unsigned char)lsb & 0xFF)))

#define APP0_ALG_CACHE_INFO_UPDATE_BIT      (7)
#define APP0_ALG_CACHE_INFO_UPDATE_FIELD    (1 << APP0_ALG_CACHE_INFO_UPDATE_BIT)
#define APP0_ALG_CACHE_INFO_UPDATE(reg)     (((reg) >> APP0_ALG_CACHE_INFO_UPDATE_BIT) & 0x1)
#define APP0_ALG_CACHE_INFO_SIZE(reg)       ((reg) & 0x7F)
#define APP0_DIAG_0_INFO_TO_DIAG_STATE(x)   (((x) >> 1) & 0x1F)
#define APP0_DIAG_STATE_TO_HIST_ID(x)       ((x) << 3)
#define APP0_DIAG_1_INFO_TO_TDC_MASK(x)     ((x) & 0x1F)
#define APP0_DIAG_SIZE_TO_HIST_SIZE(x)      ((((x)*2) + 2) * TOF8701_APP0_HIST_CHUNK_SIZE)
#define APP0_HIST_SIZE_TO_NUM_CHUNKS(x)     ((x)/TOF8701_APP0_HIST_CHUNK_SIZE)

#define TOF8701_MAX_STARTUP_RETRY           5
#define TOF8701_MAX_WAIT_RETRY              3
#define TOF8701_WAIT_UDELAY                 300
#define TOF8701_I2C_WAIT_USEC               1500

#define TOF8701_INFO_RECORD_SIZE            8

#define TOF8701_STAT_CPU_SLEEP(x)           (!((x) & 0x1))
#define TOF8701_STAT_CPU_BUSY(x)            ((x) == 0x1)
#define TOF8701_STAT_CPU_READY(x)           ((x) == 0x41)

#define TOF8701_I2C_CMD_SIZE                1
#define TOF8701_I2C_DATA_LEN_SIZE           1
#define TOF8701_I2C_CHKSUM_SIZE             1
#define TOF8701_I2C_NUM_DATA                128
#define TOF8701_I2C_MAX_DATA_SIZE           (TOF8701_I2C_NUM_DATA*sizeof(char))

/* macro to get size of a member of struct */
#define MEMBER_SIZE(type, member)           sizeof(((type *)0)->member)

/* Interrupt flag defines*/
#define TOF8701_INT1                        0x1
#define TOF8701_INT2                        0x1 << 1
#define TOF8701_INT3                        0x1 << 2
#define TOF8701_INT4                        0x1 << 3
#define TOF8701_INT_MASK                    TOF8701_INT1 | \
                                            TOF8701_INT2 | \
                                            TOF8701_INT3 | \
                                            TOF8701_INT4
#define LSB(x)                              ((x) & 0xFF)
#define SHORT_MSB(x)                        (((x) >> 8) & 0xFF)
#define IS_TOF_V2(major_ver)                ((major_ver) == 2)

enum tof8701_regs {
  TOF8701_APP_ID        = 0x00,
  TOF8701_VERS          = 0x01,
  TOF8701_REQ_APP_ID    = 0x02,
  TOF8701_STAT          = 0xE0,
  TOF8701_INT_STAT      = 0xE1,
  TOF8701_INT_EN        = 0xE2,
  TOF8701_ID            = 0xE3,
  TOF8701_REV_ID        = 0xE4,
};

enum tof8701_standby_ops {
  STANDBY = 0x00,
  WAKEUP  = 0x01,
};

enum tof8701_app_id {
  TOF8701_APP_ID_BOOTLOADER = 0x80,
  TOF8701_APP_ID_APP1       = 0xC1,
  TOF8701_APP_ID_APP0       = 0xC0,
};

/* V1 interface defined here since it is deprecated */
typedef enum tof_core1_diag_num_v1 {
    OL_DIAG_START_V1                   = 0,
    OL_DIAG_HIST_ELECTRICAL_CAL_V1     = 1,
    OL_DIAG_HIST_OPTICAL_CAL_V1        = 2,
    OL_DIAG_CAL_COMPLETE_V1            = 3,
    OL_DIAG_HIST_MAJOR_SAT_V1          = 4,
    OL_DIAG_HIST_MINOR_SAT_V1          = 5,
    OL_DIAG_HIST_BACKGROUND_V1         = 6,
    OL_DIAG_HIST_TARGET_V1             = 7,
    OL_DIAG_GEN_ALG_V1                 = 8,
    OL_DIAG_HIST_ALG_PILEUP_V1         = 16,
    OL_DIAG_HIST_ALG_PU_TDC_SUM_V1     = 17,
} tof_core1_diag_num_t_v1;
#define MASK_START_V1                  (1 << OL_DIAG_START_V1)
#define MASK_HIST_ELECTRICAL_CAL_V1    (1 << OL_DIAG_HIST_ELECTRICAL_CAL_V1)
#define MASK_HIST_OPTICAL_CAL_V1       (1 << OL_DIAG_HIST_OPTICAL_CAL_V1)
#define MASK_CAL_COMPLETE_V1           (1 << OL_DIAG_CAL_COMPLETE_V1)
#define MASK_HIST_MAJOR_SAT_V1         (1 << OL_DIAG_HIST_MAJOR_SAT_V1)
#define MASK_HIST_MINOR_SAT_V1         (1 << OL_DIAG_HIST_MINOR_SAT_V1)
#define MASK_HIST_BACKGROUND_V1        (1 << OL_DIAG_HIST_BACKGROUND_V1)
#define MASK_HIST_TARGET_V1            (1 << OL_DIAG_HIST_TARGET_V1)
#define MASK_GEN_ALG_V1                (1 << OL_DIAG_GEN_ALG_V1)
#define MASK_HIST_ALG_PILEUP_V1        (1 << OL_DIAG_HIST_ALG_PILEUP_V1)
#define MASK_HIST_ALG_PU_TDC_SUM_V1    (1 << OL_DIAG_HIST_ALG_PU_TDC_SUM_V1)

enum tof8701_app0_drv_frame_id {
  ID_RESULTS_V2                   = 0x0E,
  ID_RESULTS                      = 0x0F,

  //OL_DIAG_HIST_ELECTRICAL_CAL
  ID_CALIB_ELEC_HIST_ZONE_0_V1    = (OL_DIAG_HIST_ELECTRICAL_CAL_V1 << 3) + 0,
  ID_CALIB_ELEC_HIST_ZONE_1_V1    = (OL_DIAG_HIST_ELECTRICAL_CAL_V1 << 3) + 1,
  ID_CALIB_ELEC_HIST_ZONE_2_V1    = (OL_DIAG_HIST_ELECTRICAL_CAL_V1 << 3) + 2,
  ID_CALIB_ELEC_HIST_ZONE_3_V1    = (OL_DIAG_HIST_ELECTRICAL_CAL_V1 << 3) + 3,
  ID_CALIB_ELEC_HIST_ZONE_4_V1    = (OL_DIAG_HIST_ELECTRICAL_CAL_V1 << 3) + 4,
  ID_CALIB_ELEC_HIST_ZONE_0       = (OL_DIAG_HIST_ELECTRICAL_CAL << 3) + 0,
  ID_CALIB_ELEC_HIST_ZONE_1       = (OL_DIAG_HIST_ELECTRICAL_CAL << 3) + 1,
  ID_CALIB_ELEC_HIST_ZONE_2       = (OL_DIAG_HIST_ELECTRICAL_CAL << 3) + 2,
  ID_CALIB_ELEC_HIST_ZONE_3       = (OL_DIAG_HIST_ELECTRICAL_CAL << 3) + 3,
  ID_CALIB_ELEC_HIST_ZONE_4       = (OL_DIAG_HIST_ELECTRICAL_CAL << 3) + 4,


  //OL_DIAG_HIST_OPTICAL_CAL
  ID_CALIB_OPT_HIST_ZONE_0_V1     = (OL_DIAG_HIST_OPTICAL_CAL_V1 << 3) + 0,
  ID_CALIB_OPT_HIST_ZONE_1_V1     = (OL_DIAG_HIST_OPTICAL_CAL_V1 << 3) + 1,
  ID_CALIB_OPT_HIST_ZONE_2_V1     = (OL_DIAG_HIST_OPTICAL_CAL_V1 << 3) + 2,
  ID_CALIB_OPT_HIST_ZONE_3_V1     = (OL_DIAG_HIST_OPTICAL_CAL_V1 << 3) + 3,
  ID_CALIB_OPT_HIST_ZONE_4_V1     = (OL_DIAG_HIST_OPTICAL_CAL_V1 << 3) + 4,
  ID_CALIB_OPT_HIST_ZONE_0        = (OL_DIAG_HIST_OPTICAL_CAL << 3) + 0,
  ID_CALIB_OPT_HIST_ZONE_1        = (OL_DIAG_HIST_OPTICAL_CAL << 3) + 1,
  ID_CALIB_OPT_HIST_ZONE_2        = (OL_DIAG_HIST_OPTICAL_CAL << 3) + 2,
  ID_CALIB_OPT_HIST_ZONE_3        = (OL_DIAG_HIST_OPTICAL_CAL << 3) + 3,
  ID_CALIB_OPT_HIST_ZONE_4        = (OL_DIAG_HIST_OPTICAL_CAL << 3) + 4,

  //OL_DIAG_HIST_MAJOR_SAT / PROXIMITY (V2)
  ID_MAJ_SAT_HIST_ZONE_0_V1       = (OL_DIAG_HIST_MAJOR_SAT_V1 << 3) + 0,
  ID_MAJ_SAT_HIST_ZONE_1_V1       = (OL_DIAG_HIST_MAJOR_SAT_V1 << 3) + 1,
  ID_MAJ_SAT_HIST_ZONE_2_V1       = (OL_DIAG_HIST_MAJOR_SAT_V1 << 3) + 2,
  ID_MAJ_SAT_HIST_ZONE_3_V1       = (OL_DIAG_HIST_MAJOR_SAT_V1 << 3) + 3,
  ID_MAJ_SAT_HIST_ZONE_4_V1       = (OL_DIAG_HIST_MAJOR_SAT_V1 << 3) + 4,
  ID_PROXIMITY_HIST_ZONE_0        = (OL_DIAG_HIST_PROXIMITY << 3) + 0,
  ID_PROXIMITY_HIST_ZONE_1        = (OL_DIAG_HIST_PROXIMITY << 3) + 1,
  ID_PROXIMITY_HIST_ZONE_2        = (OL_DIAG_HIST_PROXIMITY << 3) + 2,
  ID_PROXIMITY_HIST_ZONE_3        = (OL_DIAG_HIST_PROXIMITY << 3) + 3,
  ID_PROXIMITY_HIST_ZONE_4        = (OL_DIAG_HIST_PROXIMITY << 3) + 4,

  //OL_DIAG_HIST_MINOR_SAT
  ID_MIN_SAT_HIST_ZONE_0_V1       = (OL_DIAG_HIST_MINOR_SAT_V1 << 3) + 0,
  ID_MIN_SAT_HIST_ZONE_1_V1       = (OL_DIAG_HIST_MINOR_SAT_V1 << 3) + 1,
  ID_MIN_SAT_HIST_ZONE_2_V1       = (OL_DIAG_HIST_MINOR_SAT_V1 << 3) + 2,
  ID_MIN_SAT_HIST_ZONE_3_V1       = (OL_DIAG_HIST_MINOR_SAT_V1 << 3) + 3,
  ID_MIN_SAT_HIST_ZONE_4_V1       = (OL_DIAG_HIST_MINOR_SAT_V1 << 3) + 4,

  //OL_DIAG_HIST_BACKGROUND
  ID_BACKGROUND_HIST_ZONE_0_V1    = (OL_DIAG_HIST_BACKGROUND_V1 << 3) + 0,
  ID_BACKGROUND_HIST_ZONE_1_V1    = (OL_DIAG_HIST_BACKGROUND_V1 << 3) + 1,
  ID_BACKGROUND_HIST_ZONE_2_V1    = (OL_DIAG_HIST_BACKGROUND_V1 << 3) + 2,
  ID_BACKGROUND_HIST_ZONE_3_V1    = (OL_DIAG_HIST_BACKGROUND_V1 << 3) + 3,
  ID_BACKGROUND_HIST_ZONE_4_V1    = (OL_DIAG_HIST_BACKGROUND_V1 << 3) + 4,

  //OL_DIAG_HIST_TARGET
  ID_TARGET_HIST_ZONE_0_V1        = (OL_DIAG_HIST_TARGET_V1 << 3) + 0,
  ID_TARGET_HIST_ZONE_1_V1        = (OL_DIAG_HIST_TARGET_V1 << 3) + 1,
  ID_TARGET_HIST_ZONE_2_V1        = (OL_DIAG_HIST_TARGET_V1 << 3) + 2,
  ID_TARGET_HIST_ZONE_3_V1        = (OL_DIAG_HIST_TARGET_V1 << 3) + 3,
  ID_TARGET_HIST_ZONE_4_V1        = (OL_DIAG_HIST_TARGET_V1 << 3) + 4,
  ID_DISTANCE_HIST_ZONE_0         = (OL_DIAG_HIST_DISTANCE << 3) + 0,
  ID_DISTANCE_HIST_ZONE_1         = (OL_DIAG_HIST_DISTANCE << 3) + 1,
  ID_DISTANCE_HIST_ZONE_2         = (OL_DIAG_HIST_DISTANCE << 3) + 2,
  ID_DISTANCE_HIST_ZONE_3         = (OL_DIAG_HIST_DISTANCE << 3) + 3,
  ID_DISTANCE_HIST_ZONE_4         = (OL_DIAG_HIST_DISTANCE << 3) + 4,

  //OL_DIAG_HIST_ALG_PILEUP
  ID_ALG_PILEUP_HIST_ZONE_0_V1    = (OL_DIAG_HIST_ALG_PILEUP_V1 << 3) + 0,
  ID_ALG_PILEUP_HIST_ZONE_1_V1    = (OL_DIAG_HIST_ALG_PILEUP_V1 << 3) + 1,
  ID_ALG_PILEUP_HIST_ZONE_2_V1    = (OL_DIAG_HIST_ALG_PILEUP_V1 << 3) + 2,
  ID_ALG_PILEUP_HIST_ZONE_3_V1    = (OL_DIAG_HIST_ALG_PILEUP_V1 << 3) + 3,
  ID_ALG_PILEUP_HIST_ZONE_4_V1    = (OL_DIAG_HIST_ALG_PILEUP_V1 << 3) + 4,
  ID_ALG_PILEUP_HIST_ZONE_0       = (OL_DIAG_HIST_ALG_PILEUP << 3) + 0,
  ID_ALG_PILEUP_HIST_ZONE_1       = (OL_DIAG_HIST_ALG_PILEUP << 3) + 1,
  ID_ALG_PILEUP_HIST_ZONE_2       = (OL_DIAG_HIST_ALG_PILEUP << 3) + 2,
  ID_ALG_PILEUP_HIST_ZONE_3       = (OL_DIAG_HIST_ALG_PILEUP << 3) + 3,
  ID_ALG_PILEUP_HIST_ZONE_4       = (OL_DIAG_HIST_ALG_PILEUP << 3) + 4,

  //OL_DIAG_HIST_ALG_PU_TDC_SUM
  ID_ALG_PU_SUM_HIST_V1           = (OL_DIAG_HIST_ALG_PU_TDC_SUM_V1 << 3) + 0,
  ID_ALG_PU_SUM_HIST              = (OL_DIAG_HIST_ALG_PU_TDC_SUM << 3) + 0,

  ID_ERROR                        = 0xFF,
};

//Special error code meanings in ID_ERROR frames
enum tof8701_app0_driver_error_codes {
  DRV_OK              = 0x00,
  ERR_COMM            = 0xFD,
  ERR_RETRIES         = 0xFE,
  ERR_BUF_OVERFLOW    = 0xFF,
};

enum tof8701_app0_device_error_codes {
  DEV_OK              = 0x00,
  ERR_DIAG_INFO       = 0xFD,
  ERR_TID             = 0xFE,
  ERR_MEASURE_TIMEOUT = 0xFF,
};

enum tof8701_app0_drv_frame_idx {
  DRV_FRAME_ID       = 0 ,
  DRV_FRAME_SIZE_LSB = 1 ,
  DRV_FRAME_SIZE_MSB = 2 ,
  DRV_FRAME_DATA     = 3 ,
};

#define APP0_GENERAL_CONFIG_RSP_SIZE sizeof(tof_core1_general_config_t)
union tof8701_app0_general_config_frame {
  tof_core1_general_config_t gen_cfg;
  char buf[APP0_GENERAL_CONFIG_RSP_SIZE];
}PACKED ;
union tof8701_app0_dataset {
  union tof8701_app0_general_config_frame   general_config_frame;
  char buf[TOF8701_APP0_MAX_DATA_SIZE];
}PACKED ;

/* Output buffer is (HEADER_SIZE)+(DATA_SET_SIZE) to account for framing
 * before sending up the data out of the driver
 *
 *   //Driver Output frame (to-userspace, all fields are 1-Byte):
 *   {  Frame_ID,
 *      Frame_size,
 *      //ToF device output frame (to-Driver over I2C):
 *      {  Frame_Data 0,
 *         Frame_Data 1,
 *         Frame_Data 2,
 *         .
 *         .
 *         .
 *         Frame_Data (size - 1)
 *      }
 *   }
 */
struct tof8701_app0_drv_frame_header {
  char id;
  char size_lsb;
  char size_msb;
}PACKED ;
#define APP0_DRV_FRAME_HEADER_SIZE sizeof(struct tof8701_app0_drv_frame_header)
#define APP0_DRV_FRAME_SIZE(frame_buf) ((frame_buf[DRV_FRAME_SIZE_LSB]       | \
                                       (frame_buf[DRV_FRAME_SIZE_MSB] << 8)) + \
                                       APP0_DRV_FRAME_HEADER_SIZE)

struct tof8701_app0_algo_target_result {
  char result_info;
  char dis_front_lsb; //distance in mm
  char dis_front_msb;
  char dis_peak_lsb;
  char dis_peak_msb;
  char dis_back_lsb;
  char dis_back_msb;
}PACKED ;
#define APP0_ALGO_RESULT_TARGET_SIZE       sizeof(struct tof8701_app0_algo_target_result)
#define APP0_ALGO_RESULT_HEADER_SIZE       2
#define APP0_ALGO_RESULTS_V2_RSP_SIZE      (sizeof(tof_core2_result_status_regs_t) + \
                                            offsetof(tof_core2_result_data_t, addTargets))
#define APP0_ALGO_RESULTS_RSP_SIZE(num)    ((APP0_ALGO_RESULT_TARGET_SIZE * (num)) + \
                                           APP0_ALGO_RESULT_HEADER_SIZE)
#define APP0_ALGO_RESULTS_MAX_TARGETS      (TOF8701_APP0_MAX_DATA_SIZE / \
                                            APP0_ALGO_RESULT_TARGET_SIZE)
#define APP0_ALGO_RESULT_TAR_QUADRANT(x)   (((x) >> 6) & 0x3)
#define APP0_ALGO_RESULT_TAR_CONFIDENCE(x) ((x) & 0x3F)
struct tof8701_app0_algo_results {
  char result_num;
  char num_targets;
  struct tof8701_app0_algo_target_result results[APP0_ALGO_RESULTS_MAX_TARGETS];
}PACKED ;
#define APP0_ALGO_RESULTS_MAX_RSP_SIZE sizeof(struct tof8701_app0_algo_results)
typedef struct tof8701_app0_algo_results_frame_t {
  struct tof8701_app0_drv_frame_header header;
  union {
    struct tof8701_app0_algo_results     results;
    tof_core2_result_t                   results_v2;
  }PACKED ;
}PACKED tof8701_app0_algo_results_frame_t;
#define APP0_ALGO_RESULTS_MAX_FRAME_SIZE sizeof(tof8701_app0_algo_results_frame_t)
union tof8701_app0_algo_results_frame {
  tof8701_app0_algo_results_frame_t results_frame;
  char buf[APP0_ALGO_RESULTS_MAX_FRAME_SIZE];
}PACKED ;

struct tof8701_app0_error {
  struct tof8701_app0_drv_frame_header header;
  char driver_err;
  char device_err;
}PACKED ;
union tof8701_app0_error_frame {
  struct tof8701_app0_error error;
  char buf[sizeof(struct tof8701_app0_error)];
}PACKED ;
#define TOF8701_APP0_ERROR_DATA_SIZE 2
#define TOF8701_APP0_ERROR_FRAME_SIZE 4

struct tof8701_app0_histogram {
  struct tof8701_app0_drv_frame_header header;
  char buf[TOF8701_APP0_MAX_HIST_BYTE_SIZE];
}PACKED ;
union tof8701_app0_histogram_frame {
  struct tof8701_app0_histogram hist;
  char buf[sizeof(struct tof8701_app0_histogram)];
}PACKED ;

union tof8701_app0_drv_anon_frame {
  union tof8701_app0_histogram_frame hist_frame;
  union tof8701_app0_error_frame err_frame;
  union tof8701_app0_algo_results_frame results_frame;
}PACKED ;
#define TOF8701_APP0_DRV_FRAME_MAX_SIZE sizeof(union tof8701_app0_drv_anon_frame)

/*****************************************************************************/
/***** APP0 commands *****/
/*****************************************************************************/
struct tof8701_app0_control_reg_frame {
  /* We read the first couple data Bytes for cases like result data whose second
   * byte is the number of targets */
  char buf[OL_RESULT_INFO_OFFSET];
}PACKED ;

#endif /* __TOF_MAINAPP_DRIVER_INTERFACE_H */

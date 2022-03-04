#ifndef _TOF_CORE1_MAINAPP_CONTROL_REG_H
#define _TOF_CORE1_MAINAPP_CONTROL_REG_H


// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!     THIS FILE HAS BEEN GENERATED AUTOMATICALLY       !!
// !!                     DO NOT EDIT                      !!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!



#define OL_APPID_OFFSET 0x00
#define OL_APPID (I2CBUS_BASE_ADDR+OL_APPID_OFFSET)
#define OL_APPID__appid__WIDTH 8
#define OL_APPID__appid__SHIFT 0
#define OL_APPID__appid__RESET 0

#define OL_APPREV_MAJOR_OFFSET 0x01
#define OL_APPREV_MAJOR (I2CBUS_BASE_ADDR+OL_APPREV_MAJOR_OFFSET)
#define OL_APPREV_MAJOR__apprevMajor__WIDTH 8
#define OL_APPREV_MAJOR__apprevMajor__SHIFT 0
#define OL_APPREV_MAJOR__apprevMajor__RESET 0

#define OL_APPREQID_OFFSET 0x02
#define OL_APPREQID (I2CBUS_BASE_ADDR+OL_APPREQID_OFFSET)
#define OL_APPREQID__appid__WIDTH 8
#define OL_APPREQID__appid__SHIFT 0
#define OL_APPREQID__appid__RESET 0

#define OL_STATE_FLAGS_0_OFFSET 0x04
#define OL_STATE_FLAGS_0 (I2CBUS_BASE_ADDR+OL_STATE_FLAGS_0_OFFSET)
// 0   
#define OL_STATE_FLAGS_0__validElectCalib__WIDTH 1
#define OL_STATE_FLAGS_0__validElectCalib__SHIFT 0
// 0   
#define OL_STATE_FLAGS_0__performMeasure__WIDTH 1
#define OL_STATE_FLAGS_0__performMeasure__SHIFT 1
// 0   
#define OL_STATE_FLAGS_0__performFactoryCalib__WIDTH 1
#define OL_STATE_FLAGS_0__performFactoryCalib__SHIFT 2
// 0   
#define OL_STATE_FLAGS_0__calibError__WIDTH 1
#define OL_STATE_FLAGS_0__calibError__SHIFT 3
// 0   
#define OL_STATE_FLAGS_0__isCommandPending__WIDTH 1
#define OL_STATE_FLAGS_0__isCommandPending__SHIFT 4
// 0   
#define OL_STATE_FLAGS_0__hostContinue__WIDTH 1
#define OL_STATE_FLAGS_0__hostContinue__SHIFT 5
// 0   
#define OL_STATE_FLAGS_0__measureInterrupt__WIDTH 1
#define OL_STATE_FLAGS_0__measureInterrupt__SHIFT 6
// 0   
#define OL_STATE_FLAGS_0__performingMeasurement__WIDTH 1
#define OL_STATE_FLAGS_0__performingMeasurement__SHIFT 7

#define OL_STATE_FLAGS_1_OFFSET 0x05
#define OL_STATE_FLAGS_1 (I2CBUS_BASE_ADDR+OL_STATE_FLAGS_1_OFFSET)
// 0   
#define OL_STATE_FLAGS_1__reserved__WIDTH 5
#define OL_STATE_FLAGS_1__reserved__SHIFT 0
// 0   
#define OL_STATE_FLAGS_1__valid100psOptCalib__WIDTH 1
#define OL_STATE_FLAGS_1__valid100psOptCalib__SHIFT 5
// 0   
#define OL_STATE_FLAGS_1__pausedGpio0__WIDTH 1
#define OL_STATE_FLAGS_1__pausedGpio0__SHIFT 6
// 0   
#define OL_STATE_FLAGS_1__pausedGpio1__WIDTH 1
#define OL_STATE_FLAGS_1__pausedGpio1__SHIFT 7

#define OL_CMD_DATA9_OFFSET 0x06
#define OL_CMD_DATA9 (I2CBUS_BASE_ADDR+OL_CMD_DATA9_OFFSET)
#define OL_CMD_DATA9__cmd_data9__WIDTH 8
#define OL_CMD_DATA9__cmd_data9__SHIFT 0
#define OL_CMD_DATA9__cmd_data9__RESET 0

#define OL_CMD_DATA8_OFFSET 0x07
#define OL_CMD_DATA8 (I2CBUS_BASE_ADDR+OL_CMD_DATA8_OFFSET)
#define OL_CMD_DATA8__cmd_data8__WIDTH 8
#define OL_CMD_DATA8__cmd_data8__SHIFT 0
#define OL_CMD_DATA8__cmd_data8__RESET 0

#define OL_CMD_DATA7_OFFSET 0x08
#define OL_CMD_DATA7 (I2CBUS_BASE_ADDR+OL_CMD_DATA7_OFFSET)
#define OL_CMD_DATA7__cmd_data7__WIDTH 8
#define OL_CMD_DATA7__cmd_data7__SHIFT 0
#define OL_CMD_DATA7__cmd_data7__RESET 0

#define OL_CMD_DATA6_OFFSET 0x09
#define OL_CMD_DATA6 (I2CBUS_BASE_ADDR+OL_CMD_DATA6_OFFSET)
#define OL_CMD_DATA6__cmd_data6__WIDTH 8
#define OL_CMD_DATA6__cmd_data6__SHIFT 0
#define OL_CMD_DATA6__cmd_data6__RESET 0

#define OL_CMD_DATA5_OFFSET 0x0A
#define OL_CMD_DATA5 (I2CBUS_BASE_ADDR+OL_CMD_DATA5_OFFSET)
#define OL_CMD_DATA5__cmd_data5__WIDTH 8
#define OL_CMD_DATA5__cmd_data5__SHIFT 0
#define OL_CMD_DATA5__cmd_data5__RESET 0

#define OL_CMD_DATA4_OFFSET 0x0B
#define OL_CMD_DATA4 (I2CBUS_BASE_ADDR+OL_CMD_DATA4_OFFSET)
#define OL_CMD_DATA4__cmd_data4__WIDTH 8
#define OL_CMD_DATA4__cmd_data4__SHIFT 0
#define OL_CMD_DATA4__cmd_data4__RESET 0

#define OL_CMD_DATA3_OFFSET 0x0C
#define OL_CMD_DATA3 (I2CBUS_BASE_ADDR+OL_CMD_DATA3_OFFSET)
#define OL_CMD_DATA3__cmd_data3__WIDTH 8
#define OL_CMD_DATA3__cmd_data3__SHIFT 0
#define OL_CMD_DATA3__cmd_data3__RESET 0

#define OL_CMD_DATA2_OFFSET 0x0D
#define OL_CMD_DATA2 (I2CBUS_BASE_ADDR+OL_CMD_DATA2_OFFSET)
#define OL_CMD_DATA2__cmd_data2__WIDTH 8
#define OL_CMD_DATA2__cmd_data2__SHIFT 0
#define OL_CMD_DATA2__cmd_data2__RESET 0

#define OL_CMD_DATA1_OFFSET 0x0E
#define OL_CMD_DATA1 (I2CBUS_BASE_ADDR+OL_CMD_DATA1_OFFSET)
#define OL_CMD_DATA1__cmd_data1__WIDTH 8
#define OL_CMD_DATA1__cmd_data1__SHIFT 0
#define OL_CMD_DATA1__cmd_data1__RESET 0

#define OL_CMD_DATA0_OFFSET 0x0F
#define OL_CMD_DATA0 (I2CBUS_BASE_ADDR+OL_CMD_DATA0_OFFSET)
#define OL_CMD_DATA0__cmd_data0__WIDTH 8
#define OL_CMD_DATA0__cmd_data0__SHIFT 0
#define OL_CMD_DATA0__cmd_data0__RESET 0

#define OL_COMMAND_OFFSET 0x10
#define OL_COMMAND (I2CBUS_BASE_ADDR+OL_COMMAND_OFFSET)
#define OL_COMMAND__command__WIDTH 8
#define OL_COMMAND__command__SHIFT 0
#define OL_COMMAND__command__RESET 0

#define OL_PREVIOUS_OFFSET 0x11
#define OL_PREVIOUS (I2CBUS_BASE_ADDR+OL_PREVIOUS_OFFSET)
// 0  	
#define OL_PREVIOUS__previousCommand__WIDTH 8
#define OL_PREVIOUS__previousCommand__SHIFT 0

#define OL_APPREV_MINOR_OFFSET 0x12
#define OL_APPREV_MINOR (I2CBUS_BASE_ADDR+OL_APPREV_MINOR_OFFSET)
// 0  	
#define OL_APPREV_MINOR__appRevMinor__WIDTH 8
#define OL_APPREV_MINOR__appRevMinor__SHIFT 0

#define OL_APPREV_PATCH_OFFSET 0x13
#define OL_APPREV_PATCH (I2CBUS_BASE_ADDR+OL_APPREV_PATCH_OFFSET)
// 0  	
#define OL_APPREV_PATCH__appRevPatch__WIDTH 8
#define OL_APPREV_PATCH__appRevPatch__SHIFT 0

#define OL_APPREV_BUILD_0_OFFSET 0x14
#define OL_APPREV_BUILD_0 (I2CBUS_BASE_ADDR+OL_APPREV_BUILD_0_OFFSET)
// 0  	
#define OL_APPREV_BUILD_0__appRevBuild_7_0__WIDTH 8
#define OL_APPREV_BUILD_0__appRevBuild_7_0__SHIFT 0

#define OL_APPREV_BUILD_1_OFFSET 0x15
#define OL_APPREV_BUILD_1 (I2CBUS_BASE_ADDR+OL_APPREV_BUILD_1_OFFSET)
// 0  	
#define OL_APPREV_BUILD_1__appRevBuild_15_8__WIDTH 8
#define OL_APPREV_BUILD_1__appRevBuild_15_8__SHIFT 0

#define OL_ALG_STATE_DATA_OFFSET 0x19
#define OL_ALG_STATE_DATA (I2CBUS_BASE_ADDR+OL_ALG_STATE_DATA_OFFSET)
// 0   
#define OL_ALG_STATE_DATA__algStateDataSize__WIDTH 7
#define OL_ALG_STATE_DATA__algStateDataSize__SHIFT 0
// 0   
#define OL_ALG_STATE_DATA__algStateDataUpdate__WIDTH 1
#define OL_ALG_STATE_DATA__algStateDataUpdate__SHIFT 7

#define OL_DIAG_INFO_0_OFFSET 0x1A
#define OL_DIAG_INFO_0 (I2CBUS_BASE_ADDR+OL_DIAG_INFO_0_OFFSET)
// 0  	
#define OL_DIAG_INFO_0__is_diag__WIDTH 1
#define OL_DIAG_INFO_0__is_diag__SHIFT 0
// 0  	
#define OL_DIAG_INFO_0__diag_num__WIDTH 5
#define OL_DIAG_INFO_0__diag_num__SHIFT 1
// 0  	
#define OL_DIAG_INFO_0__size__WIDTH 1
#define OL_DIAG_INFO_0__size__SHIFT 6
// 0  	
#define OL_DIAG_INFO_0__reserved0__WIDTH 1
#define OL_DIAG_INFO_0__reserved0__SHIFT 7

#define OL_DIAG_INFO_1_OFFSET 0x1B
#define OL_DIAG_INFO_1 (I2CBUS_BASE_ADDR+OL_DIAG_INFO_1_OFFSET)
// 0  	
#define OL_DIAG_INFO_1__tdc0avail__WIDTH 1
#define OL_DIAG_INFO_1__tdc0avail__SHIFT 0
// 0  	
#define OL_DIAG_INFO_1__tdc1avail__WIDTH 1
#define OL_DIAG_INFO_1__tdc1avail__SHIFT 1
// 0  	
#define OL_DIAG_INFO_1__tdc2avail__WIDTH 1
#define OL_DIAG_INFO_1__tdc2avail__SHIFT 2
// 0  	
#define OL_DIAG_INFO_1__tdc3avail__WIDTH 1
#define OL_DIAG_INFO_1__tdc3avail__SHIFT 3
// 0  	
#define OL_DIAG_INFO_1__tdc4avail__WIDTH 1
#define OL_DIAG_INFO_1__tdc4avail__SHIFT 4
// 0  	
#define OL_DIAG_INFO_1__reserved1__WIDTH 3
#define OL_DIAG_INFO_1__reserved1__SHIFT 5

#define OL_STATE_OFFSET 0x1C
#define OL_STATE (I2CBUS_BASE_ADDR+OL_STATE_OFFSET)
// 0  	
#define OL_STATE__state__WIDTH 8
#define OL_STATE__state__SHIFT 0

#define OL_STATUS_OFFSET 0x1D
#define OL_STATUS (I2CBUS_BASE_ADDR+OL_STATUS_OFFSET)
// 0   
#define OL_STATUS__status__WIDTH 6
#define OL_STATUS__status__SHIFT 0
// 0   
#define OL_STATUS__validFactCalib__WIDTH 1
#define OL_STATUS__validFactCalib__SHIFT 6
// 0   
#define OL_STATUS__validConfig__WIDTH 1
#define OL_STATUS__validConfig__SHIFT 7

#define OL_REGISTER_CONTENTS_OFFSET 0x1E
#define OL_REGISTER_CONTENTS (I2CBUS_BASE_ADDR+OL_REGISTER_CONTENTS_OFFSET)
// 0  	
#define OL_REGISTER_CONTENTS__register_contents__WIDTH 8
#define OL_REGISTER_CONTENTS__register_contents__SHIFT 0

#define OL_TID_OFFSET 0x1F
#define OL_TID (I2CBUS_BASE_ADDR+OL_TID_OFFSET)
// 0   
#define OL_TID__tid__WIDTH 8
#define OL_TID__tid__SHIFT 0

#define OL_HISTOGRAM_START_OFFSET 0x20
#define OL_HISTOGRAM_START (I2CBUS_BASE_ADDR+OL_HISTOGRAM_START_OFFSET)
#define OL_HISTOGRAM_START__hist_start__WIDTH 8
#define OL_HISTOGRAM_START__hist_start__SHIFT 0
#define OL_HISTOGRAM_START__hist_start__RESET 0

#define OL_HISTOGRAM_END_OFFSET 0x9F
#define OL_HISTOGRAM_END (I2CBUS_BASE_ADDR+OL_HISTOGRAM_END_OFFSET)
#define OL_HISTOGRAM_END__hist_end__WIDTH 8
#define OL_HISTOGRAM_END__hist_end__SHIFT 0
#define OL_HISTOGRAM_END__hist_end__RESET 0

#endif

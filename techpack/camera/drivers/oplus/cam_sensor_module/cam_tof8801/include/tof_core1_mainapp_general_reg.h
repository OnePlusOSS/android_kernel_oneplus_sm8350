#ifndef _TOF_CORE1_MAINAPP_GENERAL_REG_H
#define _TOF_CORE1_MAINAPP_GENERAL_REG_H


// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!     THIS FILE HAS BEEN GENERATED AUTOMATICALLY       !!
// !!                     DO NOT EDIT                      !!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!



#define OL_DIAG_GATE_MASK_0_OFFSET 0x20
#define OL_DIAG_GATE_MASK_0 (I2CBUS_BASE_ADDR+OL_DIAG_GATE_MASK_0_OFFSET)
#define OL_DIAG_GATE_MASK_0__diag_gate_mask_7_0__WIDTH 8
#define OL_DIAG_GATE_MASK_0__diag_gate_mask_7_0__SHIFT 0
#define OL_DIAG_GATE_MASK_0__diag_gate_mask_7_0__RESET 0

#define OL_DIAG_GATE_MASK_1_OFFSET 0x21
#define OL_DIAG_GATE_MASK_1 (I2CBUS_BASE_ADDR+OL_DIAG_GATE_MASK_1_OFFSET)
#define OL_DIAG_GATE_MASK_1__diag_gate_mask_15_8__WIDTH 8
#define OL_DIAG_GATE_MASK_1__diag_gate_mask_15_8__SHIFT 0
#define OL_DIAG_GATE_MASK_1__diag_gate_mask_15_8__RESET 0

#define OL_DIAG_GATE_MASK_2_OFFSET 0x22
#define OL_DIAG_GATE_MASK_2 (I2CBUS_BASE_ADDR+OL_DIAG_GATE_MASK_2_OFFSET)
#define OL_DIAG_GATE_MASK_2__diag_gate_mask_23_16__WIDTH 8
#define OL_DIAG_GATE_MASK_2__diag_gate_mask_23_16__SHIFT 0
#define OL_DIAG_GATE_MASK_2__diag_gate_mask_23_16__RESET 0

#define OL_DIAG_GATE_MASK_3_OFFSET 0x23
#define OL_DIAG_GATE_MASK_3 (I2CBUS_BASE_ADDR+OL_DIAG_GATE_MASK_3_OFFSET)
#define OL_DIAG_GATE_MASK_3__diag_gate_mask_32_24__WIDTH 8
#define OL_DIAG_GATE_MASK_3__diag_gate_mask_32_24__SHIFT 0
#define OL_DIAG_GATE_MASK_3__diag_gate_mask_32_24__RESET 0

#define OL_REPETITION_PERIOD_OFFSET 0x24
#define OL_REPETITION_PERIOD (I2CBUS_BASE_ADDR+OL_REPETITION_PERIOD_OFFSET)
#define OL_REPETITION_PERIOD__repetition_period__WIDTH 8
#define OL_REPETITION_PERIOD__repetition_period__SHIFT 0
#define OL_REPETITION_PERIOD__repetition_period__RESET 0

#define OL_RANGING_TIME_OFFSET 0x25
#define OL_RANGING_TIME (I2CBUS_BASE_ADDR+OL_RANGING_TIME_OFFSET)
#define OL_RANGING_TIME__rangingTime__WIDTH 8
#define OL_RANGING_TIME__rangingTime__SHIFT 0
#define OL_RANGING_TIME__rangingTime__RESET 0

#define OL_CURR_TEMP_OFFSET 0x26
#define OL_CURR_TEMP (I2CBUS_BASE_ADDR+OL_CURR_TEMP_OFFSET)
// 0   
#define OL_CURR_TEMP__currTemp__WIDTH 8
#define OL_CURR_TEMP__currTemp__SHIFT 0

#define OL_TESTING_OFFSET 0x27
#define OL_TESTING (I2CBUS_BASE_ADDR+OL_TESTING_OFFSET)
#define OL_TESTING__testingManuResult__WIDTH 1
#define OL_TESTING__testingManuResult__SHIFT 0
#define OL_TESTING__testingManuResult__RESET 0
#define OL_TESTING__testingManuHisto__WIDTH 1
#define OL_TESTING__testingManuHisto__SHIFT 1
#define OL_TESTING__testingManuHisto__RESET 0
#define OL_TESTING__testingSkipAlg__WIDTH 1
#define OL_TESTING__testingSkipAlg__SHIFT 2
#define OL_TESTING__testingSkipAlg__RESET 0
#define OL_TESTING__testingConCalib__WIDTH 1
#define OL_TESTING__testingConCalib__SHIFT 3
#define OL_TESTING__testingConCalib__RESET 0
#define OL_TESTING__testingDualResults__WIDTH 1
#define OL_TESTING__testingDualResults__SHIFT 4
#define OL_TESTING__testingDualResults__RESET 0

#define OL_CALIBRATION_OFFSET 0x28
#define OL_CALIBRATION (I2CBUS_BASE_ADDR+OL_CALIBRATION_OFFSET)
#define OL_CALIBRATION__edge1__WIDTH 4
#define OL_CALIBRATION__edge1__SHIFT 0
#define OL_CALIBRATION__edge1__RESET 2
#define OL_CALIBRATION__edge2__WIDTH 4
#define OL_CALIBRATION__edge2__SHIFT 4
#define OL_CALIBRATION__edge2__RESET 4

#define OL_DIST_ALG_PARAM_0_OFFSET 0x29
#define OL_DIST_ALG_PARAM_0 (I2CBUS_BASE_ADDR+OL_DIST_ALG_PARAM_0_OFFSET)
#define OL_DIST_ALG_PARAM_0__distAlgParam0__WIDTH 8
#define OL_DIST_ALG_PARAM_0__distAlgParam0__SHIFT 0
#define OL_DIST_ALG_PARAM_0__distAlgParam0__RESET 0
#define OL_DIST_ALG_PARAM_0__distAlgParam1__WIDTH 8
#define OL_DIST_ALG_PARAM_0__distAlgParam1__SHIFT 0
#define OL_DIST_ALG_PARAM_0__distAlgParam1__RESET 0

#define OL_ALG_SETTING_OFFSET 0x2B
#define OL_ALG_SETTING (I2CBUS_BASE_ADDR+OL_ALG_SETTING_OFFSET)
// 1    
#define OL_ALG_SETTING__algProximityEnabled__WIDTH 1
#define OL_ALG_SETTING__algProximityEnabled__SHIFT 0
// 1    
#define OL_ALG_SETTING__algDistanceEnabled__WIDTH 1
#define OL_ALG_SETTING__algDistanceEnabled__SHIFT 1
// 0    
#define OL_ALG_SETTING__algReserved__WIDTH 3
#define OL_ALG_SETTING__algReserved__SHIFT 2
// 0    
#define OL_ALG_SETTING__algCombinedCapture__WIDTH 1
#define OL_ALG_SETTING__algCombinedCapture__SHIFT 5
// 0    
#define OL_ALG_SETTING__algLegacyResult__WIDTH 1
#define OL_ALG_SETTING__algLegacyResult__SHIFT 6
// 0    
#define OL_ALG_SETTING__algKeepReady__WIDTH 1
#define OL_ALG_SETTING__algKeepReady__SHIFT 7

#define OL_GPIO_OFFSET 0x2C
#define OL_GPIO (I2CBUS_BASE_ADDR+OL_GPIO_OFFSET)
// 0      
#define OL_GPIO__gpio0__WIDTH 4
#define OL_GPIO__gpio0__SHIFT 0
// 0      
#define OL_GPIO__gpio1__WIDTH 4
#define OL_GPIO__gpio1__SHIFT 4

#define OL_MHZ_OFFSET 0x2D
#define OL_MHZ (I2CBUS_BASE_ADDR+OL_MHZ_OFFSET)
#define OL_MHZ__mhz__WIDTH 2
#define OL_MHZ__mhz__SHIFT 0
#define OL_MHZ__mhz__RESET 0

#define OL_BIN_SHIFT_OFFSET 0x2E
#define OL_BIN_SHIFT (I2CBUS_BASE_ADDR+OL_BIN_SHIFT_OFFSET)
#define OL_BIN_SHIFT__bin_shift__WIDTH 2
#define OL_BIN_SHIFT__bin_shift__SHIFT 0
#define OL_BIN_SHIFT__bin_shift__RESET 1

#define OL_BDV_MODE_OFFSET 0x2F
#define OL_BDV_MODE (I2CBUS_BASE_ADDR+OL_BDV_MODE_OFFSET)
#define OL_BDV_MODE__bdv_mode__WIDTH 8
#define OL_BDV_MODE__bdv_mode__SHIFT 0
#define OL_BDV_MODE__bdv_mode__RESET 0

#define OL_CALIB_OFFSET 0x30
#define OL_CALIB (I2CBUS_BASE_ADDR+OL_CALIB_OFFSET)
#define OL_CALIB__int_noise_floor__WIDTH 4
#define OL_CALIB__int_noise_floor__SHIFT 0
#define OL_CALIB__int_noise_floor__RESET 8
#define OL_CALIB__opt_noise_floor__WIDTH 4
#define OL_CALIB__opt_noise_floor__SHIFT 4
#define OL_CALIB__opt_noise_floor__RESET 12

#define OL_CALIB2_OFFSET 0x31
#define OL_CALIB2 (I2CBUS_BASE_ADDR+OL_CALIB2_OFFSET)
#define OL_CALIB2__nominal_peak_distance__WIDTH 8
#define OL_CALIB2__nominal_peak_distance__SHIFT 0
#define OL_CALIB2__nominal_peak_distance__RESET 64

#define OL_GC_RESERVE3_OFFSET 0x32
#define OL_GC_RESERVE3 (I2CBUS_BASE_ADDR+OL_GC_RESERVE3_OFFSET)
#define OL_GC_RESERVE3__gc_reserve3__WIDTH 8
#define OL_GC_RESERVE3__gc_reserve3__SHIFT 0
#define OL_GC_RESERVE3__gc_reserve3__RESET 0

#define OL_GC_RESERVE4_OFFSET 0x33
#define OL_GC_RESERVE4 (I2CBUS_BASE_ADDR+OL_GC_RESERVE4_OFFSET)
#define OL_GC_RESERVE4__gc_reserve4__WIDTH 8
#define OL_GC_RESERVE4__gc_reserve4__SHIFT 0
#define OL_GC_RESERVE4__gc_reserve4__RESET 0

#define OL_CAL_TEMP_OFFSET 0x34
#define OL_CAL_TEMP (I2CBUS_BASE_ADDR+OL_CAL_TEMP_OFFSET)
#define OL_CAL_TEMP__cal_temp__WIDTH 8
#define OL_CAL_TEMP__cal_temp__SHIFT 0
#define OL_CAL_TEMP__cal_temp__RESET 0

#define OL_BDV_DETECT_OFFSET 0x35
#define OL_BDV_DETECT (I2CBUS_BASE_ADDR+OL_BDV_DETECT_OFFSET)
// 0  
#define OL_BDV_DETECT__break_down_volt__WIDTH 7
#define OL_BDV_DETECT__break_down_volt__SHIFT 0

#define OL_BIN_SIZE_0_0_OFFSET 0x36
#define OL_BIN_SIZE_0_0 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_0_0_OFFSET)
// 0  
#define OL_BIN_SIZE_0_0__binSize0_7_0__WIDTH 8
#define OL_BIN_SIZE_0_0__binSize0_7_0__SHIFT 0

#define OL_BIN_SIZE_0_1_OFFSET 0x37
#define OL_BIN_SIZE_0_1 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_0_1_OFFSET)
// 0  
#define OL_BIN_SIZE_0_1__binSize0_15_8__WIDTH 8
#define OL_BIN_SIZE_0_1__binSize0_15_8__SHIFT 0

#define OL_BIN_SIZE_1_0_OFFSET 0x38
#define OL_BIN_SIZE_1_0 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_1_0_OFFSET)
// 0  
#define OL_BIN_SIZE_1_0__binSize1_7_0__WIDTH 8
#define OL_BIN_SIZE_1_0__binSize1_7_0__SHIFT 0

#define OL_BIN_SIZE_1_1_OFFSET 0x39
#define OL_BIN_SIZE_1_1 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_1_1_OFFSET)
// 0  
#define OL_BIN_SIZE_1_1__binSize1_15_8__WIDTH 8
#define OL_BIN_SIZE_1_1__binSize1_15_8__SHIFT 0

#define OL_BIN_SIZE_2_0_OFFSET 0x3A
#define OL_BIN_SIZE_2_0 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_2_0_OFFSET)
// 0  
#define OL_BIN_SIZE_2_0__binSize2_7_0__WIDTH 8
#define OL_BIN_SIZE_2_0__binSize2_7_0__SHIFT 0

#define OL_BIN_SIZE_2_1_OFFSET 0x3B
#define OL_BIN_SIZE_2_1 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_2_1_OFFSET)
// 0  
#define OL_BIN_SIZE_2_1__binSize2_15_8__WIDTH 8
#define OL_BIN_SIZE_2_1__binSize2_15_8__SHIFT 0

#define OL_BIN_SIZE_3_0_OFFSET 0x3C
#define OL_BIN_SIZE_3_0 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_3_0_OFFSET)
// 0  
#define OL_BIN_SIZE_3_0__binSize3_7_0__WIDTH 8
#define OL_BIN_SIZE_3_0__binSize3_7_0__SHIFT 0

#define OL_BIN_SIZE_3_1_OFFSET 0x3D
#define OL_BIN_SIZE_3_1 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_3_1_OFFSET)
// 0  
#define OL_BIN_SIZE_3_1__binSize3_15_8__WIDTH 8
#define OL_BIN_SIZE_3_1__binSize3_15_8__SHIFT 0

#define OL_BIN_SIZE_4_0_OFFSET 0x3E
#define OL_BIN_SIZE_4_0 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_4_0_OFFSET)
// 0  
#define OL_BIN_SIZE_4_0__binSize4_7_0__WIDTH 8
#define OL_BIN_SIZE_4_0__binSize4_7_0__SHIFT 0

#define OL_BIN_SIZE_4_1_OFFSET 0x3F
#define OL_BIN_SIZE_4_1 (I2CBUS_BASE_ADDR+OL_BIN_SIZE_4_1_OFFSET)
// 0  
#define OL_BIN_SIZE_4_1__binSize4_15_8__WIDTH 8
#define OL_BIN_SIZE_4_1__binSize4_15_8__SHIFT 0

#define OL_OPTICAL_CAL_0_OFFSET 0x40
#define OL_OPTICAL_CAL_0 (I2CBUS_BASE_ADDR+OL_OPTICAL_CAL_0_OFFSET)
// 0 
#define OL_OPTICAL_CAL_0__optical_7_0__WIDTH 8
#define OL_OPTICAL_CAL_0__optical_7_0__SHIFT 0
// 0 
#define OL_OPTICAL_CAL_0__optical_15_8__WIDTH 8
#define OL_OPTICAL_CAL_0__optical_15_8__SHIFT 0
// 0 
#define OL_OPTICAL_CAL_0__optical_23_16__WIDTH 8
#define OL_OPTICAL_CAL_0__optical_23_16__SHIFT 0
// 0 
#define OL_OPTICAL_CAL_0__optical_31_24__WIDTH 8
#define OL_OPTICAL_CAL_0__optical_31_24__SHIFT 0

#endif

#ifndef _TOF_CORE1_MAINAPP_ALG_RES_REG_H
#define _TOF_CORE1_MAINAPP_ALG_RES_REG_H


// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!     THIS FILE HAS BEEN GENERATED AUTOMATICALLY       !!
// !!                     DO NOT EDIT                      !!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!



#define OL_RESULT_NUMBER_OFFSET 0x20
#define OL_RESULT_NUMBER (I2CBUS_BASE_ADDR+OL_RESULT_NUMBER_OFFSET)
// 0   
#define OL_RESULT_NUMBER__result_num__WIDTH 8
#define OL_RESULT_NUMBER__result_num__SHIFT 0

#define OL_TARGETS_OFFSET 0x21
#define OL_TARGETS (I2CBUS_BASE_ADDR+OL_TARGETS_OFFSET)
// 0   
#define OL_TARGETS__targets__WIDTH 7
#define OL_TARGETS__targets__SHIFT 0
// 0   
#define OL_TARGETS__measInterrupted__WIDTH 1
#define OL_TARGETS__measInterrupted__SHIFT 7

#define OL_RESULT_INFO_OFFSET 0x22
#define OL_RESULT_INFO (I2CBUS_BASE_ADDR+OL_RESULT_INFO_OFFSET)
// 0   
#define OL_RESULT_INFO__quadrant__WIDTH 2
#define OL_RESULT_INFO__quadrant__SHIFT 6
// 0   
#define OL_RESULT_INFO__reliability__WIDTH 6
#define OL_RESULT_INFO__reliability__SHIFT 0

#define OL_DISTANCE_FRONT_0_OFFSET 0x23
#define OL_DISTANCE_FRONT_0 (I2CBUS_BASE_ADDR+OL_DISTANCE_FRONT_0_OFFSET)
// 0   
#define OL_DISTANCE_FRONT_0__distance_front_7_0__WIDTH 8
#define OL_DISTANCE_FRONT_0__distance_front_7_0__SHIFT 0

#define OL_DISTANCE_FRONT_1_OFFSET 0x24
#define OL_DISTANCE_FRONT_1 (I2CBUS_BASE_ADDR+OL_DISTANCE_FRONT_1_OFFSET)
// 0   
#define OL_DISTANCE_FRONT_1__distance_front_15_8__WIDTH 8
#define OL_DISTANCE_FRONT_1__distance_front_15_8__SHIFT 0

#define OL_DISTANCE_PEAK_0_OFFSET 0x25
#define OL_DISTANCE_PEAK_0 (I2CBUS_BASE_ADDR+OL_DISTANCE_PEAK_0_OFFSET)
// 0   
#define OL_DISTANCE_PEAK_0__distance_peak_7_0__WIDTH 8
#define OL_DISTANCE_PEAK_0__distance_peak_7_0__SHIFT 0

#define OL_DISTANCE_PEAK_1_OFFSET 0x26
#define OL_DISTANCE_PEAK_1 (I2CBUS_BASE_ADDR+OL_DISTANCE_PEAK_1_OFFSET)
// 0   
#define OL_DISTANCE_PEAK_1__distance_peak_15_8__WIDTH 8
#define OL_DISTANCE_PEAK_1__distance_peak_15_8__SHIFT 0

#define OL_DISTANCE_BACK_0_OFFSET 0x27
#define OL_DISTANCE_BACK_0 (I2CBUS_BASE_ADDR+OL_DISTANCE_BACK_0_OFFSET)
// 0   
#define OL_DISTANCE_BACK_0__distance_back_7_0__WIDTH 8
#define OL_DISTANCE_BACK_0__distance_back_7_0__SHIFT 0

#define OL_DISTANCE_BACK_1_OFFSET 0x28
#define OL_DISTANCE_BACK_1 (I2CBUS_BASE_ADDR+OL_DISTANCE_BACK_1_OFFSET)
// 0   
#define OL_DISTANCE_BACK_1__distance_back_15_8__WIDTH 8
#define OL_DISTANCE_BACK_1__distance_back_15_8__SHIFT 0

#endif

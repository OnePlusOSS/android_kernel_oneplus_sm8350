/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */
#ifndef _OPLUS_CAM_EEPROM_CORE_H_
#define _OPLUS_CAM_EEPROM_CORE_H_

#include "cam_eeprom_dev.h"
	int EEPROM_RamWrite32A(struct cam_eeprom_ctrl_t *e_ctrl,uint32_t addr, uint32_t data);
	int EEPROM_RamRead32A(struct cam_eeprom_ctrl_t *e_ctrl,uint32_t addr, uint32_t* data);
	void EEPROM_IORead32A(struct cam_eeprom_ctrl_t *e_ctrl, uint32_t IOadrs, uint32_t *IOdata );
	void EEPROM_IOWrite32A(struct cam_eeprom_ctrl_t *e_ctrl, uint32_t IOadrs, uint32_t IOdata );
	uint8_t EEPROM_FlashMultiRead(struct cam_eeprom_ctrl_t *e_ctrl,
		uint8_t SelMat, uint32_t UlAddress, uint32_t *PulData , uint8_t UcLength );
	int EEPROM_RamReadByte(struct cam_eeprom_ctrl_t *e_ctrl,
		uint32_t addr, uint32_t* data);
	uint8_t EEPROM_Lc898128Write(struct cam_eeprom_ctrl_t *e_ctrl,
		struct cam_write_eeprom_t *cam_write_eeprom);
	uint8_t EEPROM_Sem1215sWrite(struct cam_eeprom_ctrl_t *e_ctrl,
		struct cam_write_eeprom_t *cam_write_eeprom);
	int32_t EEPROM_CommonWrite(struct cam_eeprom_ctrl_t *e_ctrl,
		struct cam_write_eeprom_t *cam_write_eeprom);
	int32_t cam_eeprom_driver_cmd_oem(struct cam_eeprom_ctrl_t *e_ctrl, void *arg);
	int oplus_cam_eeprom_read_memory(struct cam_eeprom_ctrl_t *e_ctrl,
		struct cam_eeprom_memory_map_t *emap, int j, uint8_t *memptr);
#endif
/* _OPLUS_CAM_EEPROM_CORE_H_ */

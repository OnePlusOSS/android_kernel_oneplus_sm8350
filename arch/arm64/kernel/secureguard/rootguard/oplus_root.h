/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************
* Copyright (c)  2008- 2030  OPLUS Mobile communication Corp.ltd All rights reserved.
* File       : oplus_root.h
* Description: For rootguard syscall num
* Version   : 1.0
* Date        : 2019-12-19
* Author    :
* TAG         :
****************************************************************/
#ifdef CONFIG_OPLUS_SECURE_GUARD
#ifndef __ARCH_ARM64_OPLUS_ROOT_H_
#define __ARCH_ARM64_OPLUS_ROOT_H_

#define __NR_SETREUID32 	203
#define __NR_SETREGID32 	204
#define __NR_SETRESUID32 	208
#define __NR_SETRESGID32 	210
#define __NR_SETUID32 		213
#define __NR_SETGID32 		214

#define __NR_SETREGID 		143
#define __NR_SETGID 		144
#define __NR_SETREUID 		145
#define __NR_SETUID 		146
#define __NR_SETRESUID 		147
#define __NR_SETRESGID 		149

#endif
#endif /* CONFIG_OPLUS_SECURE_GUARD */

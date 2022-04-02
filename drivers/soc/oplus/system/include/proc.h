/************************************************************
 * Copyright 2017 OPLUS Mobile Comm Corp., Ltd.
 * All rights reserved.
 *
 * File       : proc.h
 * Description : disable selinux denied log in MP version
 * Version   : 1
 * Date        : 2016/01/06
 * Author    :
 * TAG         :
*
 ** ------------------------------- Revision History: ---------------------------------------
 **        <author>      <data>           <desc>
 ************************************************************/
#ifndef _SELINUX_PROC_H_
#define _SELINUX_PROC_H_

int is_avc_audit_enable(void);
int init_denied_proc(void);

#endif /* _SELINUX_PROC_H_ */

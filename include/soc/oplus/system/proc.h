/************************************************************
 **Copyright 2017 OPLUS Mobile Comm Corp., Ltd.
 **All rights reserved.
 **
 **File: oplus_sync_time.h
 **Description     : disable selinux denied log in MP version
 **
 ** Version: 1
 **Date : 
 **
 **
 ** Date created: 2016/01/06
 ** Author: sijiaquan@ANDROID.SELINUX
 ** ------------------------------- Revision History: ---------------------------------------
 **        <author>      <data>           <desc>
 **        sijiaquan    2017/12/12    create this file
 ************************************************************/
#ifndef _SELINUX_PROC_H_
#define _SELINUX_PROC_H_

int is_avc_audit_enable(void);
int init_denied_proc(void);

#endif /* _SELINUX_PROC_H_ */

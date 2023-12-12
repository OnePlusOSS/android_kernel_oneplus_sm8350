#ifndef __ACM_FS_H__
#define __ACM_FS_H__

int monitor_acm2(struct dentry *dentry, struct dentry *dentry2, int op);
#ifdef CONFIG_OPLUS_FEATURE_ACM3
/* must be consistent with fuse_opcode in fuse.h */
enum acm_f2fs_opcode {
	ACM_F2FS_UNLINK = 10,
	ACM_F2FS_RENAME = 12,
	ACM_F2FS_CREATE = 35,
};

int monitor_acm3(struct dentry *dentry, struct dentry *dentry2, int op);
#endif

#endif /* __ACM_FS_H__ */

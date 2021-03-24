#ifndef __OP_CHG_MODULE_H__
#define __OP_CHG_MODULE_H__

#include <linux/types.h>

#ifdef MODULE

#define OPLUS_CHG_MODEL_MAGIC0 0x20300000
#define OPLUS_CHG_MODEL_MAGIC1 0x20300001

struct oplus_chg_module {
	const char *name;
	size_t magic0;
	size_t magic1;
	int (*chg_module_init) (void);
	void (*chg_module_exit) (void);
};

#define oplus_chg_module_register(__name)			\
__attribute__((section(".oplus_chg_module.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic0 = OPLUS_CHG_MODEL_MAGIC0,			\
	.magic1 = OPLUS_CHG_MODEL_MAGIC1,			\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#define oplus_chg_module_register_null(__name)			\
__attribute__((section(".oplus_chg_module.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic0 = OPLUS_CHG_MODEL_MAGIC0,			\
	.magic1 = OPLUS_CHG_MODEL_MAGIC1,			\
	.chg_module_init = NULL,				\
	.chg_module_exit = NULL,				\
}

#else /* MODULE */

#define oplus_chg_module_register(__name)	\
	module_init(__name##_init);		\
	module_exit(__name##_exit)

#endif /* MODULE */

#endif /* __OP_CHG_MODULE_H__ */

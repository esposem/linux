/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STATSFS_INTERNAL_H_
#define _STATSFS_INTERNAL_H_

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/rwsem.h>
#include <linux/statsfs.h>

/* values, grouped by base */
struct statsfs_value_source {
	void *base_addr;
	bool files_created;
	struct statsfs_value *values;
	struct list_head list_element;
};

int statsfs_val_get_mode(struct statsfs_value *val);

#endif /* _STATSFS_INTERNAL_H_ */

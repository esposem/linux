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

struct statsfs_data_inode {
	struct statsfs_source *src;
	struct statsfs_value *val;
};

extern const struct file_operations statsfs_ops;

struct dentry *statsfs_create_file(struct statsfs_value *val,
				   struct statsfs_source *src);

struct dentry *statsfs_create_dir(const char *name, struct dentry *parent);

void statsfs_remove(struct dentry *dentry);
#define statsfs_remove_recursive statsfs_remove

int statsfs_val_get_mode(struct statsfs_value *val);

#endif /* _STATSFS_INTERNAL_H_ */

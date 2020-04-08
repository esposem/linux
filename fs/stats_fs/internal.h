/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STATS_FS_INTERNAL_H_
#define _STATS_FS_INTERNAL_H_

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/rwsem.h>
#include <linux/stats_fs.h>

/* values, grouped by base */
struct stats_fs_value_source {
	void *base_addr;
	bool files_created;
	uint32_t common_flags;
	struct stats_fs_value *values;
	struct list_head list_element;
};

#endif /* _STATS_FS_INTERNAL_H_ */

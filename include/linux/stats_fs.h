/* SPDX-License-Identifier: GPL-2.0
 *
 *  stats_fs.h - a tiny little statistics file system
 *
 *  Copyright (C) 2020 Emanuele Giuseppe Esposito
 *  Copyright (C) 2020 Redhat.
 *
 */

#ifndef _STATS_FS_H_
#define _STATS_FS_H_

#include <linux/list.h>

enum stat_aggr {
	STATS_FS_NONE = 0,
	STATS_FS_SUM,
	STATS_FS_MIN,
	STATS_FS_MAX,
	STATS_FS_COUNT_ZERO,
	STATS_FS_AVG,
};

/* flags used by source and internal source_value structure */
enum source_value_common_flags {
	STATS_FS_HIDDEN = 0x1,
};

enum stat_fs_value_flag {
	STATS_FS_FLOATING_VALUE = 0x1,
};

struct stats_fs_value;

struct stats_fs_type {
	uint64_t (*get)(struct stats_fs_value *, void *);
	void (*clear)(struct stats_fs_value *, void *);
	bool sign;
};

struct stats_fs_value {
	/* Name of the stat */
	char *name;

	/* Offset from base address to field containing the value */
	int offset;

	/* Type of the stat BOOL,U64,... */
	struct stats_fs_type *type;

	/* Aggregate type: MIN, MAX, SUM,... */
	enum stat_aggr aggr_kind;

	uint32_t value_flag;

	/* optional show function */
	char *(*show)(uint64_t);
};

struct stats_fs_source {
	struct kref refcount;

	char *name;

	uint32_t common_flags;

	/* list of source stats_fs_value_source*/
	struct list_head values_head;

	/* list of struct stats_fs_source for subordinate sources */
	struct list_head subordinates_head;

	struct list_head list_element;

	struct rw_semaphore rwsem;

	struct dentry *source_dentry;
};

#define STATS_FS_DEFINE_GET(name, type)	         			       \
	static inline uint64_t stats_fs_get_##name(struct stats_fs_value *val, \
						   void *base)                 \
	{                                                                      \
		return *((type *)(base + (uintptr_t)val->offset));   	       \
	}

#define STATS_FS_DEFINE_SET(name, type)                                        \
	static inline void stats_fs_set_##name(struct stats_fs_value *val,     \
						   void *base)                 \
	{                                                                      \
		*((type *)(base + (uintptr_t)val->offset)) = 0;                \
	}

#define STATS_FS_DEFINE_F_US(len)					       \
	STATS_FS_DEFINE_GET(u##len, u##len)				       \
	STATS_FS_DEFINE_GET(s##len, s##len)				       \
	STATS_FS_DEFINE_SET(len, u##len)

#define STATS_FS_DEFINE_F(len)						       \
	STATS_FS_DEFINE_GET(len, len)					       \
	STATS_FS_DEFINE_SET(len, len)

STATS_FS_DEFINE_F_US(8)
STATS_FS_DEFINE_F_US(16)
STATS_FS_DEFINE_F_US(32)
STATS_FS_DEFINE_F_US(64)
STATS_FS_DEFINE_F(bool)

extern struct stats_fs_type stats_fs_type_u8;
extern struct stats_fs_type stats_fs_type_s8;
extern struct stats_fs_type stats_fs_type_u16;
extern struct stats_fs_type stats_fs_type_s16;
extern struct stats_fs_type stats_fs_type_u32;
extern struct stats_fs_type stats_fs_type_s32;
extern struct stats_fs_type stats_fs_type_u64;
extern struct stats_fs_type stats_fs_type_s64;
extern struct stats_fs_type stats_fs_type_bool;

#if defined(CONFIG_STATS_FS)

/**
 * stats_fs_source_create - create a stats_fs_source
 * @flags: an integer containing all source flags (STATS_FS_HIDDEN, ...)
 * @fmt: source name format
 *
 * Creates a stats_fs_source with the given name. This
 * does not mean it will be backed by the filesystem yet, it will only
 * be visible to the user once one of its parents (or itself) are
 * registered in stats_fs.
 *
 * Returns a pointer to a stats_fs_source if it succeeds.
 * This or one of the parents' pointer must be passed to the stats_fs_put()
 * function when the file is to be removed.  If an error occurs,
 * ERR_PTR(-ERROR) will be returned.
 */
struct stats_fs_source *stats_fs_source_create(uint32_t flags, const char *fmt,
					       ...);

/**
 * stats_fs_source_register - register a source in the stats_fs filesystem
 * @source: a pointer to the source that will be registered
 *
 * Add the given folder as direct child of /sys/kernel/stats.
 * It also starts to recursively search its own child and create all folders
 * and files if they weren't already. All subsequent add_subordinate calls
 * on the same source that is used in this function will create corresponding
 * files and directories.
 */
void stats_fs_source_register(struct stats_fs_source *source);

/**
 * stats_fs_source_add_values - adds values to the given source
 * @source: a pointer to the source that will receive the values
 * @val: a pointer to the NULL terminated stats_fs_value array to add
 * @base_ptr: a pointer to the base pointer used by these values
 * @flags: an integer containing common value flags (STATS_FS_HIDDEN, ...)
 *
 * In addition to adding values to the source, also create the
 * files in the filesystem if the source already is backed up by a directory.
 *
 * Returns 0 it succeeds. If the value are already in the
 * source and have the same base_ptr, -EEXIST is returned.
 */
int stats_fs_source_add_values(struct stats_fs_source *source,
			       struct stats_fs_value *val, void *base_ptr,
			       uint32_t flags);

/**
 * stats_fs_source_add_subordinate - adds a child to the given source
 * @parent: a pointer to the parent source
 * @child: a pointer to child source to add
 *
 * Recursively create all files in the stats_fs filesystem
 * only if the parent has already a dentry (created with
 * stats_fs_source_register).
 * This avoids the case where this function is called before register.
 */
void stats_fs_source_add_subordinate(struct stats_fs_source *parent,
				     struct stats_fs_source *child);

/**
 * stats_fs_source_remove_subordinate - removes a child from the given source
 * @parent: a pointer to the parent source
 * @child: a pointer to child source to remove
 *
 * Look if there is such child in the parent. If so,
 * it will remove all its files and call stats_fs_put on the child.
 */
void stats_fs_source_remove_subordinate(struct stats_fs_source *parent,
					struct stats_fs_source *child);

/**
 * stats_fs_source_get_value - search a value in the source (and
 * subordinates)
 * @source: a pointer to the source that will be searched
 * @val: a pointer to the stats_fs_value to search
 * @ret: a pointer to the uint64_t that will hold the found value
 *
 * Look up in the source if a value with same value pointer
 * exists.
 * If not, it will return -ENOENT. If it exists and it's a simple value
 * (not an aggregate), the value that it points to will be returned.
 * If it exists and it's an aggregate (aggr_type != STATS_FS_NONE), all
 * subordinates will be recursively searched and every simple value match
 * will be used to aggregate the final result. For example if it's a sum,
 * all suboordinates having the same value will be sum together.
 *
 * This function will return 0 it succeeds.
 */
int stats_fs_source_get_value(struct stats_fs_source *source,
			      struct stats_fs_value *val, uint64_t *ret);

/**
 * stats_fs_source_get_value_by_name - search a value in the source (and
 * subordinates)
 * @source: a pointer to the source that will be searched
 * @name: a pointer to the string representing the value to search
 *        (for example "exits")
 * @ret: a pointer to the uint64_t that will hold the found value
 *
 * Same as stats_fs_source_get_value, but initially the name is used
 * to search in the given source if there is a value with a matching
 * name. If so, stats_fs_source_get_value will be called with the found
 * value, otherwise -ENOENT will be returned.
 */
int stats_fs_source_get_value_by_name(struct stats_fs_source *source,
				      char *name, uint64_t *ret);

/**
 * stats_fs_source_clear - search and clears a value in the source (and
 * subordinates)
 * @source: a pointer to the source that will be searched
 * @val: a pointer to the stats_fs_value to search
 *
 * Look up in the source if a value with same value pointer
 * exists.
 * If not, it will return -ENOENT. If it exists and it's a simple value
 * (not an aggregate), the value that it points to will be set to 0.
 * If it exists and it's an aggregate (aggr_type != STATS_FS_NONE), all
 * subordinates will be recursively searched and every simple value match
 * will be set to 0.
 *
 * This function will return 0 it succeeds.
 */
int stats_fs_source_clear(struct stats_fs_source *source,
			  struct stats_fs_value *val);

/**
 * stats_fs_source_revoke - disconnect the source from its backing data
 * @source: a pointer to the source that will be revoked
 *
 * Ensure that stats_fs will not access the data that were passed to
 * stats_fs_source_add_value for this source.
 *
 * Because open files increase the reference count for a stats_fs_source,
 * the source can end up living longer than the data that provides the
 * values for the source.  Calling stats_fs_source_revoke just before the
 * backing data is freed avoids accesses to freed data structures.  The
 * sources will return 0.
 */
void stats_fs_source_revoke(struct stats_fs_source *source);

/**
 * stats_fs_source_get - increases refcount of source
 * @source: a pointer to the source whose refcount will be increased
 */
void stats_fs_source_get(struct stats_fs_source *source);

/**
 * stats_fs_source_put - decreases refcount of source and deletes if needed
 * @source: a pointer to the source whose refcount will be decreased
 *
 * If refcount arrives to zero, take care of deleting
 * and free the source resources and files, by firstly recursively calling
 * stats_fs_source_remove_subordinate to the child and then deleting
 * its own files and allocations.
 */
void stats_fs_source_put(struct stats_fs_source *source);

/**
 * stats_fs_initialized - returns true if stats_fs fs has been registered
 */
bool stats_fs_initialized(void);

#else

#include <linux/err.h>

/*
 * We do not return NULL from these functions if CONFIG_STATS_FS is not enabled
 * so users have a chance to detect if there was a real error or not.  We don't
 * want to duplicate the design decision mistakes of procfs and devfs again.
 */

static inline struct stats_fs_source *stats_fs_source_create(uint32_t flags,
							     const char *fmt,
							     ...)
{
	return ERR_PTR(-ENODEV);
}

static inline void stats_fs_source_register(struct stats_fs_source *source)
{ }

static inline int stats_fs_source_add_values(struct stats_fs_source *source,
			       		     struct stats_fs_value *val,
					     void *base_ptr, uint32_t flags);
{
	return -ENODEV;
}

static inline void
stats_fs_source_add_subordinate(struct stats_fs_source *parent,
				struct stats_fs_source *child)
{ }

static inline void
stats_fs_source_remove_subordinate(struct stats_fs_source *parent,
				   struct stats_fs_source *child)
{ }

static inline int stats_fs_source_get_value(struct stats_fs_source *source,
					    struct stats_fs_value *val,
					    uint64_t *ret)
{
	return -ENODEV;
}

static inline int
stats_fs_source_get_value_by_name(struct stats_fs_source *source, char *name,
				  uint64_t *ret)
{
	return -ENODEV;
}

static inline int stats_fs_source_clear(struct stats_fs_source *source,
					struct stats_fs_value *val)
{
	return -ENODEV;
}

static inline void stats_fs_source_revoke(struct stats_fs_source *source)
{ }

static inline void stats_fs_source_get(struct stats_fs_source *source)
{ }

static inline void stats_fs_source_put(struct stats_fs_source *source)
{ }

static inline bool stats_fs_initialized(void)
{ }

#endif

#endif

/* SPDX-License-Identifier: GPL-2.0
 *
 *  statsfs.h - a tiny little statistics file system
 *
 *  Copyright (C) 2020 Emanuele Giuseppe Esposito
 *  Copyright (C) 2020 Redhat.
 *
 */

#ifndef _STATSFS_H_
#define _STATSFS_H_

#include <linux/list.h>

/* Used to distinguish signed types */
#define STATSFS_SIGN 0x8000

struct statsfs_source;

enum stat_type {
	STATSFS_U8 = 0,
	STATSFS_U16 = 1,
	STATSFS_U32 = 2,
	STATSFS_U64 = 3,
	STATSFS_BOOL = 4,
	STATSFS_S8 = STATSFS_U8 | STATSFS_SIGN,
	STATSFS_S16 = STATSFS_U16 | STATSFS_SIGN,
	STATSFS_S32 = STATSFS_U32 | STATSFS_SIGN,
	STATSFS_S64 = STATSFS_U64 | STATSFS_SIGN,
};

enum stat_aggr {
	STATSFS_NONE = 0,
	STATSFS_SUM,
	STATSFS_MIN,
	STATSFS_MAX,
	STATSFS_COUNT_ZERO,
	STATSFS_AVG,
};

struct statsfs_value {
	/* Name of the stat */
	char *name;

	/* Offset from base address to field containing the value */
	int offset;

	/* Type of the stat BOOL,U64,... */
	enum stat_type type;

	/* Aggregate type: MIN, MAX, SUM,... */
	enum stat_aggr aggr_kind;

	/* File mode */
	uint16_t mode;
};

struct statsfs_source {
	struct kref refcount;

	char *name;

	/* list of source statsfs_value_source*/
	struct list_head values_head;

	/* list of struct statsfs_source for subordinate sources */
	struct list_head subordinates_head;

	struct list_head list_element;

	struct rw_semaphore rwsem;

	struct dentry *source_dentry;
};

/**
 * statsfs_source_create - create a statsfs_source
 * Creates a statsfs_source with the given name. This
 * does not mean it will be backed by the filesystem yet, it will only
 * be visible to the user once one of its parents (or itself) are
 * registered in statsfs.
 *
 * Returns a pointer to a statsfs_source if it succeeds.
 * This or one of the parents' pointer must be passed to the statsfs_put()
 * function when the file is to be removed.  If an error occurs,
 * ERR_PTR(-ERROR) will be returned.
 */
struct statsfs_source *statsfs_source_create(const char *fmt, ...);

/**
 * statsfs_source_add_values - adds values to the given source
 * @source: a pointer to the source that will receive the values
 * @val: a pointer to the NULL terminated statsfs_value array to add
 * @base_ptr: a pointer to the base pointer used by these values
 *
 * In addition to adding values to the source, also create the
 * files in the filesystem if the source already is backed up by a directory.
 *
 * Returns 0 it succeeds. If the value are already in the
 * source and have the same base_ptr, -EEXIST is returned.
 */
int statsfs_source_add_values(struct statsfs_source *source,
			      struct statsfs_value *val, void *base_ptr);

/**
 * statsfs_source_add_subordinate - adds a child to the given source
 * @parent: a pointer to the parent source
 * @child: a pointer to child source to add
 *
 * Recursively create all files in the statsfs filesystem
 * only if the parent has already a dentry (created with
 * statsfs_source_register).
 * This avoids the case where this function is called before register.
 */
void statsfs_source_add_subordinate(struct statsfs_source *parent,
				    struct statsfs_source *child);

/**
 * statsfs_source_remove_subordinate - removes a child from the given source
 * @parent: a pointer to the parent source
 * @child: a pointer to child source to remove
 *
 * Look if there is such child in the parent. If so,
 * it will remove all its files and call statsfs_put on the child.
 */
void statsfs_source_remove_subordinate(struct statsfs_source *parent,
				       struct statsfs_source *child);

/**
 * statsfs_source_get_value - search a value in the source (and
 * subordinates)
 * @source: a pointer to the source that will be searched
 * @val: a pointer to the statsfs_value to search
 * @ret: a pointer to the uint64_t that will hold the found value
 *
 * Look up in the source if a value with same value pointer
 * exists.
 * If not, it will return -ENOENT. If it exists and it's a simple value
 * (not an aggregate), the value that it points to will be returned.
 * If it exists and it's an aggregate (aggr_type != STATSFS_NONE), all
 * subordinates will be recursively searched and every simple value match
 * will be used to aggregate the final result. For example if it's a sum,
 * all suboordinates having the same value will be sum together.
 *
 * This function will return 0 it succeeds.
 */
int statsfs_source_get_value(struct statsfs_source *source,
			     struct statsfs_value *val, uint64_t *ret);

/**
 * statsfs_source_get_value_by_name - search a value in the source (and
 * subordinates)
 * @source: a pointer to the source that will be searched
 * @name: a pointer to the string representing the value to search
 *        (for example "exits")
 * @ret: a pointer to the uint64_t that will hold the found value
 *
 * Same as statsfs_source_get_value, but initially the name is used
 * to search in the given source if there is a value with a matching
 * name. If so, statsfs_source_get_value will be called with the found
 * value, otherwise -ENOENT will be returned.
 */
int statsfs_source_get_value_by_name(struct statsfs_source *source, char *name,
				     uint64_t *ret);

/**
 * statsfs_source_clear - search and clears a value in the source (and
 * subordinates)
 * @source: a pointer to the source that will be searched
 * @val: a pointer to the statsfs_value to search
 *
 * Look up in the source if a value with same value pointer
 * exists.
 * If not, it will return -ENOENT. If it exists and it's a simple value
 * (not an aggregate), the value that it points to will be set to 0.
 * If it exists and it's an aggregate (aggr_type != STATSFS_NONE), all
 * subordinates will be recursively searched and every simple value match
 * will be set to 0.
 *
 * This function will return 0 it succeeds.
 */
int statsfs_source_clear(struct statsfs_source *source,
			 struct statsfs_value *val);

/**
 * statsfs_source_revoke - disconnect the source from its backing data
 * @source: a pointer to the source that will be revoked
 *
 * Ensure that statsfs will not access the data that were passed to
 * statsfs_source_add_value for this source.
 *
 * Because open files increase the reference count for a statsfs_source,
 * the source can end up living longer than the data that provides the
 * values for the source.  Calling statsfs_source_revoke just before the
 * backing data is freed avoids accesses to freed data structures.  The
 * sources will return 0.
 */
void statsfs_source_revoke(struct statsfs_source *source);

/**
 * statsfs_source_get - increases refcount of source
 * @source: a pointer to the source whose refcount will be increased
 */
void statsfs_source_get(struct statsfs_source *source);

/**
 * statsfs_source_put - decreases refcount of source and deletes if needed
 * @source: a pointer to the source whose refcount will be decreased
 *
 * If refcount arrives to zero, take care of deleting
 * and free the source resources and files, by firstly recursively calling
 * statsfs_source_remove_subordinate to the child and then deleting
 * its own files and allocations.
 */
void statsfs_source_put(struct statsfs_source *source);

/**
 * statsfs_initialized - returns true if statsfs fs has been registered
 */
bool statsfs_initialized(void);

#endif

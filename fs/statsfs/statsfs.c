// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/limits.h>
#include <linux/statsfs.h>

#include "internal.h"

struct statsfs_aggregate_value {
	uint64_t sum, min, max;
	uint32_t count, count_zero;
};

static int is_val_signed(struct statsfs_value *val)
{
	return val->type & STATSFS_SIGN;
}

int statsfs_val_get_mode(struct statsfs_value *val)
{
	return val->mode ? val->mode : 0644;
}

static struct statsfs_value *find_value(struct statsfs_value_source *src,
					struct statsfs_value *val)
{
	struct statsfs_value *entry;

	for (entry = src->values; entry->name; entry++) {
		if (entry == val) {
			WARN_ON(strcmp(entry->name, val->name) != 0);
			return entry;
		}
	}
	return NULL;
}

static struct statsfs_value *
search_value_in_source(struct statsfs_source *src, struct statsfs_value *arg,
		       struct statsfs_value_source **val_src)
{
	struct statsfs_value *entry;
	struct statsfs_value_source *src_entry;

	list_for_each_entry(src_entry, &src->values_head, list_element) {
		entry = find_value(src_entry, arg);
		if (entry) {
			*val_src = src_entry;
			return entry;
		}
	}

	return NULL;
}

/* Called with rwsem held for writing */
static struct statsfs_value_source *create_value_source(void *base)
{
	struct statsfs_value_source *val_src;

	val_src = kzalloc(sizeof(struct statsfs_value_source), GFP_KERNEL);
	if (!val_src)
		return ERR_PTR(-ENOMEM);

	val_src->base_addr = base;
	val_src->list_element =
		(struct list_head)LIST_HEAD_INIT(val_src->list_element);

	return val_src;
}

int statsfs_source_add_values(struct statsfs_source *source,
			      struct statsfs_value *stat, void *ptr)
{
	struct statsfs_value_source *val_src;
	struct statsfs_value_source *entry;

	down_write(&source->rwsem);

	list_for_each_entry(entry, &source->values_head, list_element) {
		if (entry->base_addr == ptr && entry->values == stat) {
			up_write(&source->rwsem);
			return -EEXIST;
		}
	}

	val_src = create_value_source(ptr);
	val_src->values = (struct statsfs_value *)stat;

	/* add the val_src to the source list */
	list_add(&val_src->list_element, &source->values_head);

	up_write(&source->rwsem);

	return 0;
}
EXPORT_SYMBOL_GPL(statsfs_source_add_values);

void statsfs_source_add_subordinate(struct statsfs_source *source,
				    struct statsfs_source *sub)
{
	down_write(&source->rwsem);

	statsfs_source_get(sub);
	list_add(&sub->list_element, &source->subordinates_head);

	up_write(&source->rwsem);
}
EXPORT_SYMBOL_GPL(statsfs_source_add_subordinate);

/* Called with rwsem held for writing */
static void
statsfs_source_remove_subordinate_locked(struct statsfs_source *source,
					 struct statsfs_source *sub)
{
	struct list_head *it, *safe;
	struct statsfs_source *src_entry;

	list_for_each_safe(it, safe, &source->subordinates_head) {
		src_entry = list_entry(it, struct statsfs_source, list_element);
		if (src_entry == sub) {
			WARN_ON(strcmp(src_entry->name, sub->name) != 0);
			list_del_init(&src_entry->list_element);
			statsfs_source_put(src_entry);
			return;
		}
	}
}

void statsfs_source_remove_subordinate(struct statsfs_source *source,
				       struct statsfs_source *sub)
{
	down_write(&source->rwsem);
	statsfs_source_remove_subordinate_locked(source, sub);
	up_write(&source->rwsem);
}
EXPORT_SYMBOL_GPL(statsfs_source_remove_subordinate);

/* Called with rwsem held for reading */
static uint64_t get_simple_value(struct statsfs_value_source *src,
				 struct statsfs_value *val)
{
	uint64_t value_found;
	void *address;

	address = src->base_addr + val->offset;

	switch (val->type) {
	case STATSFS_U8:
		value_found = *((uint8_t *)address);
		break;
	case STATSFS_U8 | STATSFS_SIGN:
		value_found = *((int8_t *)address);
		break;
	case STATSFS_U16:
		value_found = *((uint16_t *)address);
		break;
	case STATSFS_U16 | STATSFS_SIGN:
		value_found = *((int16_t *)address);
		break;
	case STATSFS_U32:
		value_found = *((uint32_t *)address);
		break;
	case STATSFS_U32 | STATSFS_SIGN:
		value_found = *((int32_t *)address);
		break;
	case STATSFS_U64:
		value_found = *((uint64_t *)address);
		break;
	case STATSFS_U64 | STATSFS_SIGN:
		value_found = *((int64_t *)address);
		break;
	case STATSFS_BOOL:
		value_found = *((uint8_t *)address);
		break;
	default:
		value_found = 0;
		break;
	}

	return value_found;
}

/* Called with rwsem held for reading */
static void clear_simple_value(struct statsfs_value_source *src,
			       struct statsfs_value *val)
{
	void *address;

	address = src->base_addr + val->offset;

	switch (val->type) {
	case STATSFS_U8:
		*((uint8_t *)address) = 0;
		break;
	case STATSFS_U8 | STATSFS_SIGN:
		*((int8_t *)address) = 0;
		break;
	case STATSFS_U16:
		*((uint16_t *)address) = 0;
		break;
	case STATSFS_U16 | STATSFS_SIGN:
		*((int16_t *)address) = 0;
		break;
	case STATSFS_U32:
		*((uint32_t *)address) = 0;
		break;
	case STATSFS_U32 | STATSFS_SIGN:
		*((int32_t *)address) = 0;
		break;
	case STATSFS_U64:
		*((uint64_t *)address) = 0;
		break;
	case STATSFS_U64 | STATSFS_SIGN:
		*((int64_t *)address) = 0;
		break;
	case STATSFS_BOOL:
		*((uint8_t *)address) = 0;
		break;
	default:
		break;
	}
}

/* Called with rwsem held for reading */
static void search_all_simple_values(struct statsfs_source *src,
				     struct statsfs_value_source *ref_src_entry,
				     struct statsfs_value *val,
				     struct statsfs_aggregate_value *agg)
{
	struct statsfs_value_source *src_entry;
	uint64_t value_found;

	list_for_each_entry(src_entry, &src->values_head, list_element) {
		/* skip aggregates */
		if (src_entry->base_addr == NULL)
			continue;

		/* useless to search here */
		if (src_entry->values != ref_src_entry->values)
			continue;

		/* must be here */
		value_found = get_simple_value(src_entry, val);
		agg->sum += value_found;
		agg->count++;
		agg->count_zero += (value_found == 0);

		if (is_val_signed(val)) {
			agg->max = (((int64_t)value_found) >=
				    ((int64_t)agg->max)) ?
					   value_found :
					   agg->max;
			agg->min = (((int64_t)value_found) <=
				    ((int64_t)agg->min)) ?
					   value_found :
					   agg->min;
		} else {
			agg->max = (value_found >= agg->max) ? value_found :
							       agg->max;
			agg->min = (value_found <= agg->min) ? value_found :
							       agg->min;
		}
	}
}

/* Called with rwsem held for reading */
static void do_recursive_aggregation(struct statsfs_source *root,
				     struct statsfs_value_source *ref_src_entry,
				     struct statsfs_value *val,
				     struct statsfs_aggregate_value *agg)
{
	struct statsfs_source *subordinate;

	/* search all simple values in this folder */
	search_all_simple_values(root, ref_src_entry, val, agg);

	/* recursively search in all subfolders */
	list_for_each_entry(subordinate, &root->subordinates_head,
			     list_element) {
		down_read(&subordinate->rwsem);
		do_recursive_aggregation(subordinate, ref_src_entry, val, agg);
		up_read(&subordinate->rwsem);
	}
}

/* Called with rwsem held for reading */
static void init_aggregate_value(struct statsfs_aggregate_value *agg,
				 struct statsfs_value *val)
{
	agg->count = agg->count_zero = agg->sum = 0;
	if (is_val_signed(val)) {
		agg->max = S64_MIN;
		agg->min = S64_MAX;
	} else {
		agg->max = 0;
		agg->min = U64_MAX;
	}
}

/* Called with rwsem held for reading */
static void store_final_value(struct statsfs_aggregate_value *agg,
			    struct statsfs_value *val, uint64_t *ret)
{
	int operation;

	operation = val->aggr_kind | is_val_signed(val);

	switch (operation) {
	case STATSFS_AVG:
		*ret = agg->count ? agg->sum / agg->count : 0;
		break;
	case STATSFS_AVG | STATSFS_SIGN:
		*ret = agg->count ? ((int64_t)agg->sum) / agg->count : 0;
		break;
	case STATSFS_SUM:
	case STATSFS_SUM | STATSFS_SIGN:
		*ret = agg->sum;
		break;
	case STATSFS_MIN:
	case STATSFS_MIN | STATSFS_SIGN:
		*ret = agg->min;
		break;
	case STATSFS_MAX:
	case STATSFS_MAX | STATSFS_SIGN:
		*ret = agg->max;
		break;
	case STATSFS_COUNT_ZERO:
	case STATSFS_COUNT_ZERO | STATSFS_SIGN:
		*ret = agg->count_zero;
		break;
	default:
		break;
	}
}

/* Called with rwsem held for reading */
static int statsfs_source_get_value_locked(struct statsfs_source *source,
					   struct statsfs_value *arg,
					   uint64_t *ret)
{
	struct statsfs_value_source *src_entry;
	struct statsfs_value *found;
	struct statsfs_aggregate_value aggr;

	*ret = 0;

	if (!arg)
		return -ENOENT;

	/* look in simple values */
	found = search_value_in_source(source, arg, &src_entry);

	if (!found) {
		printk(KERN_ERR "Statsfs: Value in source \"%s\" not found!\n",
		       source->name);
		return -ENOENT;
	}

	if (src_entry->base_addr != NULL) {
		*ret = get_simple_value(src_entry, found);
		return 0;
	}

	/* look in aggregates */
	init_aggregate_value(&aggr, found);
	do_recursive_aggregation(source, src_entry, found, &aggr);
	store_final_value(&aggr, found, ret);

	return 0;
}

int statsfs_source_get_value(struct statsfs_source *source,
			     struct statsfs_value *arg, uint64_t *ret)
{
	int retval;

	down_read(&source->rwsem);
	retval = statsfs_source_get_value_locked(source, arg, ret);
	up_read(&source->rwsem);

	return retval;
}
EXPORT_SYMBOL_GPL(statsfs_source_get_value);

/* Called with rwsem held for reading */
static void set_all_simple_values(struct statsfs_source *src,
				  struct statsfs_value_source *ref_src_entry,
				  struct statsfs_value *val)
{
	struct statsfs_value_source *src_entry;

	list_for_each_entry(src_entry, &src->values_head, list_element) {
		/* skip aggregates */
		if (src_entry->base_addr == NULL)
			continue;

		/* wrong to search here */
		if (src_entry->values != ref_src_entry->values)
			continue;

		if (src_entry->base_addr &&
			src_entry->values == ref_src_entry->values)
			clear_simple_value(src_entry, val);
	}
}

/* Called with rwsem held for reading */
static void do_recursive_clean(struct statsfs_source *root,
			       struct statsfs_value_source *ref_src_entry,
			       struct statsfs_value *val)
{
	struct statsfs_source *subordinate;

	/* search all simple values in this folder */
	set_all_simple_values(root, ref_src_entry, val);

	/* recursively search in all subfolders */
	list_for_each_entry(subordinate, &root->subordinates_head,
			     list_element) {
		down_read(&subordinate->rwsem);
		do_recursive_clean(subordinate, ref_src_entry, val);
		up_read(&subordinate->rwsem);
	}
}

/* Called with rwsem held for reading */
static int statsfs_source_clear_locked(struct statsfs_source *source,
				       struct statsfs_value *val)
{
	struct statsfs_value_source *src_entry;
	struct statsfs_value *found;

	if (!val)
		return -ENOENT;

	/* look in simple values */
	found = search_value_in_source(source, val, &src_entry);

	if (!found) {
		printk(KERN_ERR "Statsfs: Value in source \"%s\" not found!\n",
		       source->name);
		return -ENOENT;
	}

	if (src_entry->base_addr != NULL) {
		clear_simple_value(src_entry, found);
		return 0;
	}

	/* look in aggregates */
	do_recursive_clean(source, src_entry, found);

	return 0;
}

int statsfs_source_clear(struct statsfs_source *source,
			 struct statsfs_value *val)
{
	int retval;

	down_read(&source->rwsem);
	retval = statsfs_source_clear_locked(source, val);
	up_read(&source->rwsem);

	return retval;
}

/* Called with rwsem held for reading */
static struct statsfs_value *
find_value_by_name(struct statsfs_value_source *src, char *val)
{
	struct statsfs_value *entry;

	for (entry = src->values; entry->name; entry++)
		if (!strcmp(entry->name, val))
			return entry;

	return NULL;
}

/* Called with rwsem held for reading */
static struct statsfs_value *
search_in_source_by_name(struct statsfs_source *src, char *name)
{
	struct statsfs_value *entry;
	struct statsfs_value_source *src_entry;

	list_for_each_entry(src_entry, &src->values_head, list_element) {
		entry = find_value_by_name(src_entry, name);
		if (entry)
			return entry;
	}

	return NULL;
}

int statsfs_source_get_value_by_name(struct statsfs_source *source, char *name,
				     uint64_t *ret)
{
	struct statsfs_value *val;
	int retval;

	down_read(&source->rwsem);
	val = search_in_source_by_name(source, name);

	if (!val) {
		*ret = 0;
		up_read(&source->rwsem);
		return -ENOENT;
	}

	retval = statsfs_source_get_value_locked(source, val, ret);
	up_read(&source->rwsem);

	return retval;
}
EXPORT_SYMBOL_GPL(statsfs_source_get_value_by_name);

void statsfs_source_get(struct statsfs_source *source)
{
	kref_get(&source->refcount);
}
EXPORT_SYMBOL_GPL(statsfs_source_get);

void statsfs_source_revoke(struct statsfs_source *source)
{
	struct list_head *it, *safe;
	struct statsfs_value_source *val_src_entry;

	down_write(&source->rwsem);

	list_for_each_safe(it, safe, &source->values_head) {
		val_src_entry = list_entry(it, struct statsfs_value_source,
					   list_element);
		val_src_entry->base_addr = NULL;
	}

	up_write(&source->rwsem);
}
EXPORT_SYMBOL_GPL(statsfs_source_revoke);

/* Called with rwsem held for writing
 *
 * The refcount is 0 and the lock was taken before refcount
 * went from 1 to 0
 */
static void statsfs_source_destroy(struct kref *kref_source)
{
	struct statsfs_value_source *val_src_entry;
	struct list_head *it, *safe;
	struct statsfs_source *child, *source;

	source = container_of(kref_source, struct statsfs_source, refcount);

	/* iterate through the values and delete them */
	list_for_each_safe(it, safe, &source->values_head) {
		val_src_entry = list_entry(it, struct statsfs_value_source,
					   list_element);
		kfree(val_src_entry);
	}

	/* iterate through the subordinates and delete them */
	list_for_each_safe(it, safe, &source->subordinates_head) {
		child = list_entry(it, struct statsfs_source, list_element);
		statsfs_source_remove_subordinate_locked(source, child);
	}


	up_write(&source->rwsem);
	kfree(source->name);
	kfree(source);
}

void statsfs_source_put(struct statsfs_source *source)
{
	kref_put_rwsem(&source->refcount, statsfs_source_destroy,
		       &source->rwsem);
}
EXPORT_SYMBOL_GPL(statsfs_source_put);

struct statsfs_source *statsfs_source_create(const char *fmt, ...)
{
	va_list ap;
	char buf[100];
	struct statsfs_source *ret;
	int char_needed;

	va_start(ap, fmt);
	char_needed = vsnprintf(buf, 100, fmt, ap);
	va_end(ap);

	ret = kzalloc(sizeof(struct statsfs_source), GFP_KERNEL);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	ret->name = kstrdup(buf, GFP_KERNEL);
	if (!ret->name) {
		kfree(ret);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&ret->refcount);
	init_rwsem(&ret->rwsem);

	INIT_LIST_HEAD(&ret->values_head);
	INIT_LIST_HEAD(&ret->subordinates_head);
	INIT_LIST_HEAD(&ret->list_element);

	return ret;
}
EXPORT_SYMBOL_GPL(statsfs_source_create);

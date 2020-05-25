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
#include <linux/stats_fs.h>

const struct stats_fs_type stats_fs_type_stub;
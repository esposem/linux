========
Stats_FS
========

Stats_fs is a synthetic ram-based virtual filesystem that takes care of
gathering and displaying statistics for the Linux kernel subsystems.

The motivation for stats_fs comes from the fact that there is no common
way for Linux kernel subsystems to expose statistics to userspace shared
throughout the Linux kernel; subsystems have to take care of gathering and
displaying statistics by themselves, for example in the form of files in
debugfs.

Allowing each subsystem of the kernel to do so has two disadvantages.
First, it will introduce redundant code. Second, debugfs is anyway not the
right place for statistics (for example it is affected by lockdown).

Stats_fs offers a generic and stable API, allowing any kind of
directory/file organization and supporting multiple kind of aggregations
(not only sum, but also average, max, min and count_zero) and data types
(all unsigned and signed types plus boolean). The implementation takes
care of gathering and displaying information at run time; users only need
to specify the values to be included in each source.

Its main function is to display each statistics as a file in the desired
folder hierarchy defined through the API. Stats_fs files can be read, and
possibly cleared if their file mode allows it.

Stats_fs is typically mounted with a command like::

    mount -t stats_fs stats_fs /sys/kernel/stats_fs

(Or an equivalent /etc/fstab line).

Stats_fs has two main components: the public API defined by
include/linux/stats_fs.h, and the virtual file system in
/sys/kernel/stats_fs.

The API has two main elements, values and sources. Kernel
subsystems will create a source, add child
sources/values/aggregates and register it to the root source (that on the
virtual fs would be /sys/kernel/stats_fs).

The stats_fs API is defined in ``<linux/stats_fs.h>``.

    Sources
        Sources are created via ``stats_fs_source_create()``, and each source becomes
        a directory in the file system. Sources form a parent-child relationship;
        root sources are added to the file system via ``stats_fs_source_register()``.
        Therefore each Linux subsystem will add its own entry to the root,
        filesystem similar to what it is done in debugfs.
        Every other source is added to or removed from a parent through the
        ``stats_fs_source_add_subordinate()`` and ``stats_fs_source_remote_subordinate()``
        APIs. Once a source is created and added to the tree (via
        add_subordinate), it will be used to compute aggregate values in the
        parent source.

    Values
        Values represent quantites that are gathered by the stats_fs user.
        Examples of values include the number of vm exits of a given kind, the
        amount of memory used by some data structure, the length of the longest
        hash table chain, or anything like that. Values are defined with the
        stats_fs_source_add_values function. Each value is defined by a ``struct
        stats_fs_value``; the same ``stats_fs_value`` can be added to many different
        sources. A value can be considered "simple" if it fetches data from a
        user-provided location, or "aggregate" if it groups all values in the
        subordinate sources that include the same ``stats_fs_value``.

Because stats_fs is a different mountpoint than debugfs, it is not affected
by security lockdown).

Using Stats_fs
================

Define a value::

        struct statistics{
                uint64_t exit;
                ...
        };

        struct kvm {
                ...
                struct statistics stat;
        };

        struct stats_fs_value kvm_stats[] = {
                { "exit_vm", offsetof(struct kvm, stat.exit), STATS_FS_U64, STATS_FS_SUM },
                { NULL }
        };

The same ``struct stats_fs_value`` is used for both simple and aggregate
values, though the type and offset are only used for simple values.
Aggregates merge all values that use the same ``struct stats_fs_value``.

Create the parent source::

        struct stats_fs_source parent_source = stats_fs_source_create("parent");

Register it (files and folders
will only be visible after this function is called)::

        stats_fs_source_register(parent_source);

Create and add a child::

        struct stats_fs_source child_source = stats_fs_source_create("child");

        stats_fs_source_add_subordinate(parent_source, child_source);

Add values to parent and child (also here order doesn't matter)::

        struct kvm *base_ptr = kmalloc(..., sizeof(struct kvm));
        ...
        stats_fs_source_add_values(child_source, kvm_stats, base_ptr);
        stats_fs_source_add_values(parent_source, kvm_stats, NULL);

child_source will be a simple value, since it has a non-NULL base pointer,
while parent_source will be an aggregate.

Of course the same struct stats_fs_value array can be also passed with a
different base pointer, to represent the same value but in another instance
of the kvm struct

Search:

Fetch a value from the child source, returning the value
pointed by `(uint64_t *) base_ptr + kvm_stats[0].offset`::

        uint64_t ret_child, ret_parent;

        stats_fs_source_get_value(child_source, &kvm_stats[0], &ret_child);

Fetch an aggregate value, searching all subsources of parent_source for
the specified ``struct statsfs_value``::

        stats_fs_source_get_value(parent_source, &kvm_stats[0], &ret_parent);

        assert(ret_child == ret_parent); // check expected result

To make it more interesting, add another child::

        struct stats_fs_source child_source2 = stats_fs_source_create("child2");

        stats_fs_source_add_subordinate(parent_source, child_source2);
        // now  the structure is parent -> child1
        //                              -> child2

        struct kvm *other_base_ptr = kmalloc(..., sizeof(struct kvm));
        ...
        stats_fs_source_add_values(child_source2, kvm_stats, other_base_ptr);

Note that other_base_ptr points to another instance of kvm, so the struct
stats_fs_value is the same but the address at which they point is not.

Now get the aggregate value::

        uint64_t ret_child, ret_child2, ret_parent;

        stats_fs_source_get_value(child_source, &kvm_stats[0], &ret_child);
        stats_fs_source_get_value(parent_source, &kvm_stats[0], &ret_parent);
        stats_fs_source_get_value(child_source2, &kvm_stats[0], &ret_child2);

        assert((ret_child + ret_child2) == ret_parent);

Cleanup::

        stats_fs_source_remove_subordinate(parent_source, child_source);
        stats_fs_source_revoke(child_source);
        stats_fs_source_put(child_source);

        stats_fs_source_remove_subordinate(parent_source, child_source2);
        stats_fs_source_revoke(child_source2);
        stats_fs_source_put(child_source2);

        stats_fs_source_put(parent_source);
        kfree(other_base_ptr);
        kfree(base_ptr);

Calling stats_fs_source_revoke is very important, because it will ensure
that stats_fs will not access the data that were passed to
stats_fs_source_add_value for this source.

Because open files increase the reference count for a stats_fs_source, the
source can end up living longer than the data that provides the values for
the source.  Calling stats_fs_source_revoke just before the backing data
is freed avoids accesses to freed data structures. The sources will return
0.

This is not needed for the parent_source, since it just contains
aggregates that would be 0 anyways if no matching child value exist.

API Documentation
=================

.. kernel-doc:: include/linux/stats_fs.h
   :export: fs/stats_fs/*.c
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

static const struct modversion_info ____versions[]
__attribute__((section("__versions"))) = {
	{ 0x1f779334, "struct_module" },
	{ 0xdf83c692, "kmem_cache_destroy" },
	{ 0xa940c30f, "update_atime" },
	{ 0xc94c299b, "generic_file_llseek" },
	{ 0x9f322acd, "blockdev_direct_IO" },
	{ 0x89b301d4, "param_get_int" },
	{ 0x74cc238d, "current_kernel_time" },
	{ 0x8a34b34f, "malloc_sizes" },
	{ 0xed3e3e97, "remove_wait_queue" },
	{ 0x6e300932, "generic_file_open" },
	{ 0xe1608a58, "generic_delete_inode" },
	{ 0x7c54ec0a, "generic_file_aio_read" },
	{ 0x3c2a04e1, "dput" },
	{ 0x959a1f10, "nobh_commit_write" },
	{ 0x938646f8, "generic_file_writev" },
	{ 0x98097bda, "generic_read_dir" },
	{ 0x98bd6f46, "param_set_int" },
	{ 0x47f556e6, "generic_file_aio_write" },
	{ 0x8b18496f, "__copy_to_user_ll" },
	{ 0xaf0f04d3, "default_wake_function" },
	{ 0x720f2879, "mpage_readpages" },
	{ 0xc280a525, "__copy_from_user_ll" },
	{ 0x8aa5033d, "get_sb_single" },
	{ 0xf17c46f6, "kill_litter_super" },
	{ 0x96924853, "mpage_readpage" },
	{ 0x1b7d4074, "printk" },
	{ 0x39fda912, "d_rehash" },
	{ 0x617929b0, "d_alloc_root" },
	{ 0x1075bf0, "panic" },
	{ 0x9130e060, "mpage_writepages" },
	{ 0x891f2686, "kmem_cache_free" },
	{ 0x43b0c9c3, "preempt_schedule" },
	{ 0xc803e131, "shrink_dcache_sb" },
	{ 0x31f4fb1b, "inode_init_once" },
	{ 0x75810956, "kmem_cache_alloc" },
	{ 0x3480a283, "generic_file_mmap" },
	{ 0x72f092f5, "generic_file_sendfile" },
	{ 0xb0c16221, "block_write_full_page" },
	{ 0x17d59d01, "schedule_timeout" },
	{ 0xea346857, "unlock_new_inode" },
	{ 0x75e59c2, "register_chrdev" },
	{ 0x61c9a08e, "d_prune_aliases" },
	{ 0xd1c0b4e6, "kmem_cache_create" },
	{ 0x261a77c5, "register_filesystem" },
	{ 0xbfeef0a1, "__wake_up" },
	{ 0x6d20e7e1, "dcache_dir_open" },
	{ 0x8e9ba09f, "add_wait_queue" },
	{ 0xaad067ae, "iput" },
	{ 0xf3dc2c57, "dcache_dir_close" },
	{ 0x37a0cba, "kfree" },
	{ 0xb7512c2, "nobh_prepare_write" },
	{ 0x83054034, "d_splice_alias" },
	{ 0xc192d491, "unregister_chrdev" },
	{ 0x61a1a522, "block_sync_page" },
	{ 0x9e9d0696, "unregister_filesystem" },
	{ 0x682f5a38, "new_inode" },
	{ 0xd8a8d268, "generic_file_readv" },
	{ 0xa75896b8, "d_instantiate" },
	{ 0x7912f38f, "generic_block_bmap" },
	{ 0x2a07d03, "iget_locked" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=";


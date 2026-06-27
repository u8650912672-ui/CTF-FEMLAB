#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/panic.h>
#include <linux/panic_notifier.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/kbd_kern.h>

#define DEVICE_NAME "heap_stress"
#define MAX_ALLOCS 128
#define CMD_BUF 256

struct alloc_entry {
    void *ptr;
    size_t size;
    bool freed;
    int id;
};

static struct alloc_entry allocs[MAX_ALLOCS];
static int alloc_count;
static int last_alloc_id;
static struct notifier_block panic_nb;

static void black_screen(void)
{
    struct console *con;
    const char msg[] = "\033[40m\033[2J\033[1;1HKERNEL HEAP DETECTED - SYSTEM HALTED\n";

    console_lock();
    for_each_console(con) {
        if (con->write)
            con->write(con, msg, strlen(msg));
    }
    console_unlock();
}

static int panic_callback(struct notifier_block *nb, unsigned long code, void *unused)
{
    black_screen();
    return NOTIFY_DONE;
}

static void trigger_overflow(void)
{
    void *p;

    if (!last_alloc_id || alloc_count <= 0) {
        p = kmalloc(64, GFP_KERNEL);
        if (!p) return;
        memset(p, 0x41, 128);
        kfree(p);
        return;
    }

    for (int i = 0; i < alloc_count; i++) {
        if (!allocs[i].freed && allocs[i].ptr) {
            memset(allocs[i].ptr, 0x42, allocs[i].size + 64);
            pr_info("heap_stress: overflow on alloc #%d (%zu bytes + 64)\n", allocs[i].id, allocs[i].size);
            return;
        }
    }

    p = kmalloc(64, GFP_KERNEL);
    if (!p) return;
    memset(p, 0x43, 128);
    kfree(p);
}

static void trigger_uaf(void)
{
    for (int i = 0; i < alloc_count; i++) {
        if (allocs[i].ptr && !allocs[i].freed) {
            void *saved = allocs[i].ptr;
            kfree(allocs[i].ptr);
            allocs[i].freed = true;
            memset(saved, 0x44, 8);
            pr_info("heap_stress: use-after-free on alloc #%d\n", allocs[i].id);
            return;
        }
    }
}

static void trigger_double_free(void)
{
    for (int i = 0; i < alloc_count; i++) {
        if (allocs[i].ptr) {
            kfree(allocs[i].ptr);
            kfree(allocs[i].ptr);
            allocs[i].freed = true;
            pr_info("heap_stress: double-free on alloc #%d\n", allocs[i].id);
            return;
        }
    }
}

static int do_alloc(size_t size)
{
    if (alloc_count >= MAX_ALLOCS)
        return -ENOMEM;

    size = max(size, (size_t)1);
    if (size > 1024 * 1024)
        size = 1024 * 1024;

    allocs[alloc_count].ptr = kmalloc(size, GFP_KERNEL);
    if (!allocs[alloc_count].ptr)
        return -ENOMEM;

    allocs[alloc_count].size = size;
    allocs[alloc_count].freed = false;
    allocs[alloc_count].id = ++last_alloc_id;
    memset(allocs[alloc_count].ptr, 0xAA, size);
    pr_info("heap_stress: alloc #%d = %zu bytes at %px\n",
            allocs[alloc_count].id, size, allocs[alloc_count].ptr);
    alloc_count++;
    return allocs[alloc_count - 1].id;
}

static int do_free(int id)
{
    for (int i = 0; i < alloc_count; i++) {
        if (allocs[i].id == id && !allocs[i].freed) {
            kfree(allocs[i].ptr);
            allocs[i].freed = true;
            pr_info("heap_stress: freed alloc #%d\n", id);
            return 0;
        }
    }
    return -EINVAL;
}

static ssize_t device_write(struct file *filp, const char __user *buf,
                            size_t len, loff_t *off)
{
    char cmd[CMD_BUF];
    size_t param;

    if (len >= CMD_BUF)
        len = CMD_BUF - 1;

    if (copy_from_user(cmd, buf, len))
        return -EFAULT;
    cmd[len] = '\0';

    if (cmd[len-1] == '\n')
        cmd[len-1] = '\0';

    if (!strncmp(cmd, "alloc", 5)) {
        if (sscanf(cmd + 5, " %zu", &param) != 1)
            param = 64;
        do_alloc(param);
    } else if (!strcmp(cmd, "free")) {
        do_free(last_alloc_id);
    } else if (!strcmp(cmd, "overflow")) {
        trigger_overflow();
    } else if (!strcmp(cmd, "use-after-free") || !strcmp(cmd, "uaf")) {
        trigger_uaf();
    } else if (!strcmp(cmd, "double-free") || !strcmp(cmd, "doublefree")) {
        trigger_double_free();
    } else if (!strcmp(cmd, "panic")) {
        panic("heap_stress: manual panic triggered\n");
    } else if (!strcmp(cmd, "status")) {
        pr_info("heap_stress: %d allocations, last_id=%d\n", alloc_count, last_alloc_id);
        for (int i = 0; i < alloc_count; i++) {
            pr_info("  #%d: id=%d ptr=%px size=%zu freed=%d\n",
                    i, allocs[i].id, allocs[i].ptr, allocs[i].size, allocs[i].freed);
        }
    } else {
        pr_info("heap_stress: unknown command: %s\n", cmd);
    }

    return len;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = device_write,
};

static struct miscdevice mdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &fops,
    .mode = 0666,
};

static int __init init(void)
{
    int ret;

    ret = misc_register(&mdev);
    if (ret) {
        pr_err("heap_stress: failed to register device\n");
        return ret;
    }

    panic_nb.notifier_call = panic_callback;
    panic_nb.priority = INT_MAX;
    atomic_notifier_chain_register(&panic_notifier_list, &panic_nb);

    pr_info("heap_stress: loaded. Commands: alloc N, free, overflow, uaf, doublefree, status, panic\n");
    return 0;
}

static void __exit cleanup(void)
{
    atomic_notifier_chain_unregister(&panic_notifier_list, &panic_nb);

    for (int i = 0; i < alloc_count; i++) {
        if (allocs[i].ptr && !allocs[i].freed)
            kfree(allocs[i].ptr);
    }

    misc_deregister(&mdev);
    pr_info("heap_stress: unloaded\n");
}

module_init(init);
module_exit(cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("femboy dev");
MODULE_DESCRIPTION("Heap stress test device for KASAN detection");

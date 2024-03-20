#include <linux/module.h>
#include <linux/kernel.h> 
#include <linux/fs.h>       // For the character driver support
#include <linux/cdev.h>     // For cdev utilities
#include <linux/device.h>   // For device_create, class_create
#include <linux/slab.h>     // For kmalloc
#include <linux/mm.h>       // For remap_pfn_range

static int dev_major = 0; // 0 for dynamic allocation
static struct class *shmem_class = NULL;
static struct cdev shmem_cdev;
static struct page *shared_page = NULL;

static int shmem_mmap(struct file *filp, struct vm_area_struct *vma);

static const struct file_operations shmem_fops = {
    .owner = THIS_MODULE,
    .mmap = shmem_mmap,
};

static int __init shmem_init(void) {
    dev_t dev_id;
    alloc_chrdev_region(&dev_id, 0, 1, "shmem");
    dev_major = MAJOR(dev_id);

    cdev_init(&shmem_cdev, &shmem_fops);
    shmem_cdev.owner = THIS_MODULE;
    cdev_add(&shmem_cdev, dev_id, 1);

    shmem_class = class_create(THIS_MODULE, "shmem");
    device_create(shmem_class, NULL, dev_id, NULL, "shmem");

    // Allocate the shared page here
    shared_page = alloc_page(GFP_KERNEL);
    if (!shared_page) {
        device_destroy(shmem_class, dev_id);
        class_destroy(shmem_class);
        cdev_del(&shmem_cdev);
        unregister_chrdev_region(dev_id, 1);
        return -ENOMEM;
    }

    return 0;

}

static int shmem_mmap(struct file *filp, struct vm_area_struct *vma) {
    unsigned long pfn;
    // Ensure the memory area is of the expected size (e.g., PAGE_SIZE)
    if ((vma->vm_end - vma->vm_start) != PAGE_SIZE) {
        return -EINVAL; // Incorrect size
    }

    // Convert the shared page's address to a physical address, then to PFN
    pfn = page_to_pfn(shared_page);

    // Set up page protection for the mapping
    // This can be adjusted based on whether the memory should be writable, etc.
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    if (remap_pfn_range(vma, vma->vm_start,pfn, PAGE_SIZE, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}

static void __exit shmem_exit(void) {
    device_destroy(shmem_class, MKDEV(dev_major, 0));
    class_destroy(shmem_class);
    cdev_del(&shmem_cdev);
    unregister_chrdev_region(MKDEV(dev_major, 0), 1);

    if (shared_page)
        __free_page(shared_page);
}

module_init(shmem_init);
module_exit(shmem_exit);
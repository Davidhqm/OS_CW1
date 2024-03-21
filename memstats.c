#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/pagemap.h>
#include <linux/hugetlb.h>

static int proc_pid_memstats(struct seq_file *m, void *v);

struct pg_stat {
    unsigned long writable_pages;
    unsigned long read_only_pages;
    unsigned long shared_pages;
    unsigned long special_pages;
    unsigned long huge_pages;
    unsigned long pages_swapped;
};

static void process_pte(struct seq_file *m, pte_t *pte, unsigned long address, struct pg_stat *stat) {
    struct page *page = null;
    swp_entry_t entry;

    pte_page(*pte);
    if (pte_none(*pte) || !pte_present(*pte))
        return;

    // Counting writable pages
    if (pte_write(*pte))
        stat->writable_pages++;

    // Counting read-only pages
    if (!pte_write(*pte))
        stat->read_only_pages++;

    // Counting special pages
    if (pte_flags(*pte) & _PAGE_KERNEL) 
        stat->special_pages++;

    // Counting shared pages
    page = pte_page(*pte);
    if (page && page_ref_count(page) > 1) 
        stat->shared_pages++;

    // Counting huge pages
    if (pte_huge(*pte)) 
        stat->huge_pages++;
    
    // Counting swapped pages
    if (!pte_present(*pte) && !pte_none(*pte)) {
        entry = pte_to_swp_entry(*pte);
        if (swp_type(entry) != 0) {
            stat->pages_swapped++;
        }
    }
}

static void walk_pmd(struct seq_file *m, pmd_t *pmd, unsigned long address, struct pg_stat *stat) {
    pte_t *pte;
    unsigned long end;

    if (pmd_none(*pmd) || pmd_bad(*pmd))
        return;

    pte = pte_offset_map(pmd, address);
    end = address + PTRS_PER_PTE * PAGE_SIZE;
    for (; address < end; address += PAGE_SIZE, pte++) {
        process_pte(m, pte, address, stat);
    }
    pte_unmap(pte - 1);
}

static void walk_pud(struct seq_file *m, pud_t *pud, unsigned long address, struct pg_stat *stat) {
    pmd_t *pmd;
    unsigned long end;

    if (pud_none(*pud) || pud_bad(*pud))
        return;

    pmd = pmd_offset(pud, address);
    end = address + PTRS_PER_PMD * PAGE_SIZE;
    for (; address < end; address += PTRS_PER_PMD * PAGE_SIZE, pmd++) {
        walk_pmd(m, pmd, address, stat);
    }
}

static void walk_pgd(struct seq_file *m, pgd_t *pgd, struct pg_stat *stat) {
    pud_t *pud;
    unsigned long address = 0;
    unsigned long end;

    end = PGDIR_SIZE * PTRS_PER_PGD;
    for (; address < end; address += PGDIR_SIZE, pgd++) {
        if (pgd_none(*pgd) || pgd_bad(*pgd))
            continue;
        
        pud = pud_offset(pgd, address);
        walk_pud(m, pud, address, stat);
    }
}

static void walk_page_range(struct seq_file *m, struct mm_struct *mm, struct pg_stat *stat) {
    walk_pgd(m, mm->pgd, stat);
}

static int proc_pid_memstats(struct seq_file *m, struct pid_namespace *ns, struct pid *pid, struct task_struct *task) {
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    unsigned long vma_count = 0, biggest_vma_size = 0;
    unsigned long readable = 0, writable = 0, executable = 0;
    unsigned long shared = 0, private_vmas = 0, locked = 0, exec_image = 0;
    unsigned long file_backed = 0, anonymous = 0;
    unsigned long total_physical_pages = 0; 

    if (!mm)
        return -EACCES;

    down_read(&mm->mmap_sem);
    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        unsigned long vma_size = vma->vm_end - vma->vm_start;
        vma_count++;
        biggest_vma_size = max(biggest_vma_size, vma_size);

        if (vma->vm_flags & VM_READ) readable++;
        if (vma->vm_flags & VM_WRITE) writable++;
        if (vma->vm_flags & VM_EXEC) executable++;
        if (vma->vm_flags & VM_MAYSHARE) shared++;
        else private_vmas++;
        if (vma->vm_flags & VM_LOCKED) locked++;
        if (vma->vm_flags & VM_EXECUTABLE) exec_image++;
        if (vma->vm_file) file_backed++;
        else anonymous++;
    }
    up_read(&mm->mmap_sem);

    // Initialize your stats structure
    struct pg_stat stat = {0}; 

    // Ensure task->mm is not NULL
    if (task->mm) {
        down_read(&task->mm->mmap_sem);
        walk_page_range(m, task->mm, &stat);
        up_read(&task->mm->mmap_sem); 
    }

    total_physical_pages = stat.writable_pages + stat.read_only_pages + stat.shared_pages +
                            stat.special_pages + stat.huge_pages +stat.pages_swapped;

    // Presenting Virtual Memory Area Stats
    seq_puts(m, "Virtual Memory Area Stats:\n");
    seq_printf(m, "\tTotal VMAs: %lu\n", vma_count);
    seq_printf(m, "\tBiggest VMA Size: %lu\n", biggest_vma_size);
    seq_printf(m, "\tReadable VMAs: %lu\n", readable);
    seq_printf(m, "\tWritable VMAs: %lu\n", writable);
    seq_printf(m, "\tExecutable VMAs: %lu\n", executable);
    seq_printf(m, "\tShared VMAs: %lu\n", shared);
    seq_printf(m, "\tPrivate VMAs: %lu\n", private_vmas);
    seq_printf(m, "\tLocked VMAs: %lu\n", locked);
    seq_printf(m, "\tExecutable Image VMAs: %lu\n", exec_image);
    seq_printf(m, "\tFile Backed VMAs: %lu\n", file_backed);
    seq_printf(m, "\tAnonymous VMAs: %lu\n", anonymous);

    // Presenting Physical Pages Stats
    seq_puts(m, "Physical Pages Stats:\n");
    seq_printf(m, "\tTotal Physical Pages: %lu\n", total_physical_pages);
    seq_printf(m, "\tNumber of Pages Swapped Out: %lu\n", stat.pages_swapped);
    seq_printf(m, "\tRead-Only Pages: %lu\n", stat.read_only_pages);
    seq_puts(m, "\tWritable Pages: %lu\n", stat.writable_pages);
    seq_puts(m, "\tNumber of Shared Pages: %lu\n", stat.shared_pages);
    seq_puts(m, "\tNumber of Special Pages: %lu\n", stat.special_pages);
    seq_puts(m, "\tNumber of Huge Pages: %lu\n", stat.huge_pages);

    return 0;
}


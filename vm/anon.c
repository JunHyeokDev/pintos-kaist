/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_bitmap;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1); /* Pintos uses disks this way: 1:1 - swap */ 
	size_t swap_size = disk_size(swap_disk); /* swap_disk를 바이트 단위로 계산함*/
	size_t swap_slots = swap_size / (PGSIZE / DISK_SECTOR_SIZE); /* bitmap을 페이지 단위로 관리하기 위해 */
    swap_bitmap = bitmap_create(swap_slots);
	bitmap_set_all(swap_bitmap,false);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_index = BITMAP_ERROR;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	size_t swap_index = anon_page->swap_index;
	if (bitmap_test(swap_bitmap,swap_index)) {
    	for (size_t i = 0; i < (PGSIZE / DISK_SECTOR_SIZE); i++) {
        	disk_read(swap_disk, swap_index * (PGSIZE / DISK_SECTOR_SIZE) + i, 
        	kva + (i * DISK_SECTOR_SIZE));
    	}
		bitmap_set(swap_bitmap,swap_index,false);
		return true;
	}
	return false;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
    size_t swap_index = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
	if (swap_index != BITMAP_ERROR) {
        for (size_t i = 0; i < (PGSIZE / DISK_SECTOR_SIZE); i++) {
            disk_write(swap_disk, swap_index * (PGSIZE / DISK_SECTOR_SIZE) + i, 
            page->va + (i * DISK_SECTOR_SIZE));            
        }
		anon_page->swap_index = swap_index;
		pml4_clear_page(thread_current()->pml4,page->va);
		return true;
	}
	return false;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// if (bitmap_test(swap_bitmap, anon_page->swap_index)) {
	// 	bitmap_set(swap_bitmap, anon_page->swap_index, false);
	// }

	// bitmap_reset(swap_bitmap,anon_page->swap_index);
	// 대체 왜 reset을 해주면 에러가 나는걸까..?
}

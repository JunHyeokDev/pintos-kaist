/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "include/userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	/*Initializes a file-backed page. 
	The function first sets up the handlers for file-backed pages in page->operations.
	You may want to update some information on the page struct, 
	such as the file that is backing the memory.*/
	struct file_page *file_page = &page->file;

	struct lazy_load_data *data = (struct lazy_load_data*)page->uninit.aux;
	file_page->file = data->file;
	file_page->ofs = data->ofs;
	file_page->read_bytes = data->read_bytes;
	file_page->zero_bytes = data->zero_bytes;
	// free(data);
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;

	file_seek(file_page->file, file_page->ofs);

	if (file_read(file_page->file, kva, file_page->read_bytes) != (int) file_page->read_bytes ) {
		return false;
	}

	memset(kva+file_page->read_bytes, 0, file_page->zero_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *cur = thread_current();
	if(pml4_is_dirty(cur->pml4, page->va)) {
		file_seek(file_page->file, file_page->ofs);
		/* Writes SIZE bytes from BUFFER into FILE   kva -> file (진짜 디스크에 있는 파일)   */
		/* 따라서, 물리 메모리에 있는 정보를 파일에 쓰는 작업을 수행함.*/
		if (file_write(file_page->file, page->frame->kva, file_page->read_bytes) != (int) file_page->read_bytes) {
			return false;
		}
		pml4_set_dirty(cur->pml4, page->va, 0);
	}
	pml4_clear_page(cur->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	
	struct thread *curr = thread_current();

    if (pml4_is_dirty(curr->pml4, page->va)) {
        // 페이지가 수정되었으므로 파일에 다시 씁니다.
        file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
        // 페이지가 더 이상 수정되지 않았음을 표시합니다.
        pml4_set_dirty(curr->pml4, page->va, false);
    }
	// (uint64_t *pml4, void *upage)
	// clear만 
	pml4_clear_page(curr->pml4, page->va);
	/* 1. 지연된 메모리 해제 (Lazy Deallocation): */
	/* 2. 페이지를 물리 메모리에서 실제로 삭제하는 작업은 시간이 많이 걸릴 수 있습니다. "존재" 비트를 해제하는 것은 비교적 빠른 작업이므로, 성능 상의 이점을 가질 수 있습니다. */
	/* 3. 페이지가 실제로 메모리에서 제거되지 않고 "존재하지 않음"으로 표시되면, 이후에 해당 페이지에 접근할 때 페이지 폴트가 발생합니다. 페이지 폴트 핸들러는 페이지를 다시 로드하거나, 
	    필요한 경우 새로운 페이지를 할당할 수 있습니다.*/	
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// printf("addr: %p length: %d writable: %d file: %p offset: %d\n", addr, length, writable, file, offset);
	void *original_addr = addr;
	struct file *f = file_reopen(file); // 나중에 여러번 open 될 경우를 대비해서 다른 참조를 얻도록함.
    if (f == NULL) { 
		return NULL; }
    int page_cnt = (length + PGSIZE - 1) / PGSIZE;
	// malloc랩을 기억하는가? 아마 위와 비슷한 식을 봤을 것이다..

	// 파일은 작은데, 읽어야할 길이가 매우 크다면, 그냥 파일 길이만큼 읽으면 될 것이다.
    size_t read_bytes = length < file_length(f) ? length : file_length(f);  
    size_t zero_bytes = (PGSIZE - (read_bytes % PGSIZE));
	ASSERT (pg_ofs (addr) == 0); // It must fail if addr is not page-aligned 
	ASSERT (offset % PGSIZE == 0); // offset이 페이지 단위로 깔끔하게 나뉘어야 한다.
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0); // read & zero 길이가 페이지단위로 정렬 되어있는지 체크
	
	// 🐁 for (size_t i = 0; i < page_cnt; i++) { 이론상 page 수 만큼 for문을 돌면 될 것 같은데.... 실패한다..! 🐁 
	while (read_bytes >0 || zero_bytes > 0) {
		// Current code calculates the number of bytes to read from a file 
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE; 
		// the number of bytes to fill with zeros within the main loop
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_data *aux = (struct lazy_load_data*)malloc(sizeof(struct lazy_load_data));
        aux->file = f;
        aux->ofs = offset;
        aux->read_bytes = page_read_bytes;
        aux->zero_bytes = page_zero_bytes;
        // vm_alloc_page_with_initializer를 호출하여 대기 중인 객체를 생성합니다.
		// file_backed_initializer (struct page *page, enum vm_type type, void *kva)
		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
					writable, lazy_load_file, aux)){
            // free(aux);
            // file_close(f);
			return NULL;
		}

		// 해당 식을 while문 위에서 작성하면 틀린다.. 난 이해할 수 없어..
		// 🐁 Page의 갯수를 저장하는 코드, 그런데 해당 코드를 while문 밖에서 선언했을 때 mmap이 실패한다면 여러분들은 믿으시겠습니까??? 🐁
		struct page *page = spt_find_page(&thread_current()->spt, addr);
		page->page_cnt = page_cnt;

        /* Advance. */
        read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }
	/* If successful, this function returns the virtual address where the file is mapped. */
	return original_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// 보충페이지
	// 페이지 찾기
	// 순회하며 파괴, destroy();
	struct supplemental_page_table *spt = &(thread_current()->spt);
	struct page* page = spt_find_page(spt,addr);
	// 몇개를 지워야하지??
	// The entire file is mapped into consecutive virtual pages starting at addr
	// 따라서 이렇게 지워주면 되려나?
	int iter = page->page_cnt;
	for (int i=0; i< iter; i++) {
		if (page != NULL) {
			destroy(page);
		}
		addr+=PGSIZE;
		page = spt_find_page(spt,addr);
	}
}

bool
lazy_load_file (struct page *page, void *aux) {
	/* you have to find the file to read the segment from 
	and eventually read the segment into memory. */
		
	struct lazy_load_data *data = (struct lazy_load_data*) aux;
	struct file *file = data->file;
	off_t ofs = data->ofs;
	size_t read_bytes = data->read_bytes;
	size_t zero_bytes = data->zero_bytes;
	// file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) 
	file_seek(file,ofs);
	if(file_read(file,page->frame->kva, read_bytes) != (int)(read_bytes)) {
		palloc_free_page(page->frame->kva);
		return false;
	}
	memset(page->frame->kva + read_bytes, 0 , zero_bytes);
	return true;
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}
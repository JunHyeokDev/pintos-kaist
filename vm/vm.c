/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/lib/kernel/hash.h"
#include "include/threads/vaddr.h"
#include "userprog/process.h"
#include <string.h>
struct list frame_list;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	// printf("vm_alloc_page_with_initializer 진입 \n");
	struct supplemental_page_table *spt = &thread_current ()->spt;
	bool ret = false;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page* page = (struct page*) malloc(sizeof(struct page));

        /* Fetch the initializer according to the VM type */
		/* Initializing a function pointer */
        bool (*initializer)(struct page *, enum vm_type, void *);
		// vm_initializer 은 (struct page *, void *aux) 이다. 
		// anon_initializer (struct page *page, enum vm_type type, void *kva)
		// file_backed_initializer (struct page *page, enum vm_type type, void *kva) 
		// 는 뒤에 *kva 포인터가 하나 더 있다.
		//  (*initializer)(struct page *, enum vm_type, void *)
		// uninit_new의 마지막 매개변수 initializer의 타입은 위와 같다.
		if (VM_TYPE(type) == VM_ANON) {
			initializer = anon_initializer;
		}
		else if (VM_TYPE(type) == VM_FILE) {
			initializer = file_backed_initializer;
		} else {
			free(page); // 근데 위에서 ASSERT (VM_TYPE(type) != VM_UNINIT) 해주니 여기 올일은 없겠네
			return false;
		}
		/* TODO: Insert the page into the spt. */
		/* 여기서 넣어주는 init이 lazy_load_segment()임. process.c 에 있음. */
		uninit_new(page,upage,init,type,aux,initializer);
		page->writable = writable;
		ret = spt_insert_page(spt,page);
		return ret;
	}
err:
	printf("vm_alloc_page_with_initializer");
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {

	//  인자로 받은 vaddr에 해당하는 vm_entry를 검색 후 반환
	//  가상 메모리 주소에 해당하는 페이지 번호 추출 (pg_round_down())
	//  hash_find() 함수를 이용하여 vm_entry 검색 후 반환

	struct page *page = NULL;
    /* TODO: Fill this function. */
	page = (struct page *)malloc(sizeof(struct page));
    struct hash_elem *e;

    // va에 해당하는 hash_elem 찾기
    page->va = pg_round_down(va);
    e = hash_find(&spt->hash_brown, &page->hash_elem);
	// /* 우린 분명 page.va = va; 를 업데이트 하였다. 그런데 어떻게 해서 위의 hash_find 이 동작하는걸까? */
	// /* va를 건내주고, bucket 리스트에 있는 hash들을 순회하며 va가 같은 hash_elem을 가져온다. */
	// /* 사실 elem에 아무것도 설정되지 않아서 이상한 코드처럼 보이지만, 내부 find elem에서 va를 비교한 후 elem을 리턴한다.*/
	free(page);
    // 있으면 e에 해당하는 페이지 반환
    return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	// Insert struct page into the given supplemental page table. 
	// This function should checks that the virtual address does not exist in the given supplemental page table.
	// hash_insert -> 
	// succ = page_insert(&spt->hash_brown, page);
	// return succ;
	return hash_insert(&spt->hash_brown,&page->hash_elem) == NULL ? true : false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->hash_brown, &page->hash_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	// for문 돌면서 추방할녀석 선정하기
	struct list_elem *e = list_begin(&frame_list);
	struct thread *cur = thread_current();

	for (e; e!=list_end(&frame_list); e = list_next(e)) {
		victim = list_entry(e, struct frame, frame_elem);
		if (pml4_is_accessed(cur->pml4, victim->page->va)) {
			
		} else {
			return victim;
		}
	}

	return NULL;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));;
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER);

	if(frame->kva == NULL) {
		PANIC("todolater"); // 추방알고리즘 
	}
	/* 밑에 dealloc 함수가 있어서, free는 신경안써도 되는거같다. */
	frame->page = NULL; // ASSERT (frame->page == NULL); 가 있으므로, NULL로 초기화 해줍시다!
	list_push_back(&frame_list,&frame->frame_elem);
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	// Make sure you round down the addr to PGSIZE when handling the allocation.
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), true);
	// 이거랑 위랑 똑같음. 매크로써서 인터페이스(?) 처럼 구현한거같음.
	// vm_alloc_page_with_initializer(VM_ANON | VM_MARKER_0, stack_bottom, true, NULL, NULL) 
	// page fault는 rsp가 미할당된 stack 영역에 memory access를 시도하는 경우
	// (push/pop, indirect memory ref, call/ret) 등 발생합니다.
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
	if (page->writable == false) {
		return false;
	}
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	// printf("vm_try_handle_fault 오는거야? \n");

	/* read only 페이지에 대한 접근이 아닐 경우 (not_present 참조)*/
	/* 페이지 폴트가 일어난 주소에 대한 vm_entry 구조체 탐색 */
	/* vm_entry를 인자로 넘겨주며 handle_mm_fault() 호출 */
	/* 제대로 파일이 물리 메모리에 로드 되고 맵핑 됬는지 검사 */

	/*
	Step 1.
	1. 페이지폴트가 일어난 페이지를 찾는다. 
	2. 파일 시스템이던, 스왑 슬롯이던, 제로 페이지던 갖고온다.
	3. Copy-on-Write을 구현한다면 해당 페이지는 '프레임'에 존재하지만, '페이지 테이블' 에는 없다.
	4. 커널모드 접근, read-write 권한 오류 접근, 이상한 주소 접근 등 invalid 처리를 해야한다.
	
	Step 2.
	1. 페이지(가상)를 저장하기 위한 프레임(물리)을 얻는다.
	2. Copy-on-Write을 구현한다면 우리가 원하는 페이지는 이미 프레임에 있을것이다.

	Step 3.
	1. 데이터를 (file system, 스왑슬롯, 혹은 zero page) 갖고온다.
	2. Copy-on-Write을 구현한다면 우리가 원하는 페이지는 이미 프레임에 있을것이다.

	Step 4.
	1. page fault가 일어난 VA를 Physical Page를 가르키게 한다. threads/mmu.c 함수를 참조해라. */

	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	/* Stack 관련 fault 인지, 진짜 데이터가 없어서인지 구분해야함.*/
	if (addr == NULL || is_kernel_vaddr(addr)) {
		return false;
	}
	void *rsp = user ? f->rsp : thread_current()->user_rsp;

	/* not_present 가 true인 경우, 물리페이지가 없는 것이다. */
	if (not_present) {
		/* Stack Growth 처리 */
		/* 1. Fault Address가 현재 스택 포인터 위에 있어야 함 		     addr < USER_STACK */
		/* 2. Fault Address가 스택의 최대 크기 제한을 벗어나지 않아야 함    addr >= USER_STACK - 1MB && 1MB = 2^20 bytes	*/
		/* 3. PUSH 명령에 의해 발생한 경우를 처리  						 addr == rsp - 8 */
		if (rsp - 8 <= addr && addr <= USER_STACK &&  /* 1,3 번 케이스를 다룸 */
			addr >= USER_STACK - (1<< 20) ) { 		  /* 2번 케이스를 다룸 */
			/* On many GNU/Linux systems, the default limit is 8 MB. For this project,
		   	you should limit the stack size to be 1MB at maximum. */
			vm_stack_growth(addr);
		}

		page = spt_find_page(spt,addr);
		if (page == NULL) {
			return false;
		}
		if (write) {
			if (vm_handle_wp(page)) {
				return false;
			}
		}
		return vm_do_claim_page(page); 
 	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	// printf("vm_claim_page 잘 오나요? \n");
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL) {
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	// 해당 페이지와 물리 프레임을 연결.
	frame->page = page;
	page->frame = frame;
	
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *t = thread_current();
	pml4_set_page (t->pml4, page->va, frame->kva, page->writable);

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
//  hash_init(struct hash *, hash_hash_func *, hash_less_func *, void *aux)
	hash_init(&spt->hash_brown,vm_hash_func,vm_less_func,NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	/* hash.h  의 Iteration 파트를 참조하자. */
	/* A hash table iterator. */
	struct hash_iterator i;
	hash_first(&i,&src->hash_brown);
	/* Advances I to the next element in the hash table and returns
   	   it.  Returns a null pointer if no elements are left.  Elements
   	   are returned in arbitrary order.  */
	while(hash_next(&i))  {
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);

		// enum vm_type type = page_get_type(src_page);
		// 🐁 아래 식 대신, 위 함수 (page_get_type)를 썼다고 fork가 실패했다면 여러분들은 믿으시겠습니까??? 🐁
		enum vm_type type = src_page->operations->type;
		void *upage = src_page->va;
		bool writable = src_page->writable;
		vm_initializer *init = src_page->uninit.init;
		void *aux = src_page->uninit.aux;

		if (type == VM_UNINIT) {
			vm_alloc_page_with_initializer(VM_ANON,upage,writable,init,aux);
			continue;
		}

		if (type == VM_ANON) {
			if (!vm_alloc_page(type, upage, writable)) {
				return false;
			}
		}

		if (type == VM_FILE) {
			struct file_page *aux_args = (struct file_page *) malloc(sizeof(struct file_page));
            aux_args->file = src_page->file.file;
            aux_args->ofs = src_page->file.ofs;
            aux_args->read_bytes = src_page->file.read_bytes;
            aux_args->zero_bytes = src_page->file.zero_bytes;

            if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, aux_args))
                return false;
            continue;
		}

		/* 새로운 페이지 연결! */
		if (!vm_claim_page(upage)) {
			return false;
		}
		/* 복사! */
		struct page *dst_page = spt_find_page(dst,upage);
		memcpy(dst_page->frame->kva, src_page->frame->kva,PGSIZE);
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// hash_action_func -> 뒤에 함수 넘겨줘야함..
	// typedef void hash_action_func (struct hash_elem *e, void *aux);
	hash_clear(&spt->hash_brown, hash_destroy_func);
}

void
hash_destroy_func (struct hash_elem *e, void *aux) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	destroy(page); // anon_destroy 및 uninit_destroy가 호출된다.
	free(page);
}

/* 주어진 페이지 *p 에 대한 해시값을 리턴합니다. */
static
unsigned
vm_hash_func (const struct hash_elem *p_, void *aux UNUSED) {
	/* hash_entry()로 element에 대한 vm_entry 구조체 (ppt에서는 struct page를 vm_entry라고 부른다) */
	/* hash_bytes()를 이용해서 vm_entry의 멤버 vaddr에 대한 해시값을 구하고 반환 */
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

static
unsigned vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	/* PPT 설명 주석*/
	/* hash_entry()로 각각의 element에 대한 vm_entry 구조체를 얻은
	후 vaddr 비교 (b가 크다면 true, a가 크다면 false */
	const struct page *p_a = hash_entry(a, struct page, hash_elem);
	const struct page *p_b = hash_entry(b, struct page, hash_elem);
	return p_a->va < p_b->va;
}

bool
page_insert(struct hash *h, struct page *p) {
	return hash_insert(h,&p->hash_elem) == NULL ? true : false;
}


/* Finds, removes, and returns an element equal to E in hash
   table H.  Returns a null pointer if no equal element existed
   in the table.

   If the elements of the hash table are dynamically allocated,
   or own resources that are, then it is the caller's
   responsibility to deallocate them. */
bool
page_delete(struct hash *h, struct page *p) {
	return hash_delete(h,&p->hash_elem) == NULL ? false : true;
}
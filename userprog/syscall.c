#include "userprog/syscall.h"
#include <stdio.h>
#include "filesys/file.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include <devices/input.h>
#include "lib/kernel/stdio.h"
#include "threads/synch.h"
#include "threads/palloc.h"

// #define STDIN_FILENO 0
// #define STDOUT_FILENO 1

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void is_valid_addr(void *addr);
void halt (void); // 0 
void exit (int status);
bool create (const char *file, unsigned initial_size);
int open (const char *file);
bool remove(const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
int exec(const char *cmd_line);
int wait(int pid);
int fork(const char *thread_name, struct intr_frame *f);
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);

#define EOF -1


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	int syscall_n = f->R.rax; /* 시스템 콜 넘버 */
	// printf("시스콜 넘버가 뭐죠 ? %d \n",syscall_n);
#ifdef VM
	thread_current()->user_rsp = f->rsp;
#endif
	switch (syscall_n)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
	    f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_MMAP:
        f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
        break;
	case SYS_MUNMAP:
        munmap(f->R.rdi);
        break;
	default:
        exit(-1);
        break;
	}
}

void is_valid_addr(void *addr) {
	if (addr == NULL || !is_user_vaddr(addr)) {
		exit(-1);
	}
}

void halt (void) {
	power_off();
}

void exit (int status) {
	struct thread *curr = thread_current();
	curr->exit_code = status;
	printf ("%s: exit(%d)\n", curr->name,status);
	thread_exit();
}

int
wait(int pid) {
	return process_wait(pid);
}

int
exec(const char *cmd_line) {
	is_valid_addr(cmd_line);

	struct thread * cur = thread_current();
	
	char *fn_copy = palloc_get_page (0); // 이거 임포트 제대로 안돼서 에러남.
	if (fn_copy == NULL) { 
		exit(-1); 
	}
	strlcpy (fn_copy, cmd_line, PGSIZE);
	// sema_down(&(cur->fork_sema));

	if (process_exec(fn_copy) == -1) { 
		// printf("process_exec failed for: %s\n", cmd_line);
		exit(-1);
	}
	NOT_REACHED();
}

bool create (const char *file, unsigned initial_size) {
	// lock_acquire(&filesys_lock);
	is_valid_addr(file);
	bool ret = filesys_create(file,initial_size);
	// lock_release(&filesys_lock);
	return ret;
}

bool remove(const char *file) {
	is_valid_addr(file);
	return filesys_remove(file);
}

int open (const char *file) {
	is_valid_addr(file);
	
	struct file* tmp;
	int fd = -1;

	tmp = filesys_open(file);
	if (tmp == NULL) {
		return -1;
	}

	if(strcmp(thread_name(), file) == 0) {
		file_deny_write(tmp); // 현재 읽고 있는 파일은 write를 할 수 없도록 설정합니다.
	}

	fd = process_add_file(tmp);
	if (fd == -1) {
		file_close(tmp);  // file.c 
	}

	return fd;
}


void seek(int fd, unsigned position) {
	if (fd < 2 || fd > 128) {
		return;
	}
	struct file *tmp = process_get_file(fd);
	if (tmp) {
		file_seek(tmp, position);
	}
}

unsigned tell(int fd) {
	if (fd < 2 || fd > 128)
		return;
	struct file *tmp = process_get_file(fd);
	if (tmp)
		return file_tell(tmp);
	return NULL;
}

void 
close (int fd) {
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL)
		return;
	file_close(open_file);
	process_close_file(fd);
}

int filesize (int fd) {
	struct file *open_file = process_get_file(fd);
	if (open_file) 
		return file_length(open_file);
	return -1;
}
int
read (int fd, void *buffer, unsigned size) {		

	is_valid_addr(buffer);
	int bytes_read = 0;
	lock_acquire(&filesys_lock);
	// 표준 입력 처리: fd가 0인 경우 키보드에서 입력을 읽습니다.
    if (fd == 0) {
        unsigned i;
        for (i = 0; i < size; i++) {
			int c = input_getc();
			if (c == EOF) break;
            ((char*)buffer)[i] = (char)c;
			bytes_read++;
        }
	}
	else {
		struct file *file = process_get_file(fd);
    	bytes_read = file_read(file, buffer, size);
	}
	lock_release(&filesys_lock);
    return bytes_read;
}

int write (int fd, const void *buffer, unsigned size) {

	is_valid_addr(buffer);
	int writes = 0;
	lock_acquire(&filesys_lock);
	if (fd == 1) {
		putbuf(buffer,size);// void putbuf (const char *, size_t);
		lock_release(&filesys_lock);
        return size;
	}

	struct file *file  = process_get_file(fd);
	if (file == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}
	writes = file_write(file, buffer, size);
	lock_release(&filesys_lock);
	return writes;
}

int fork(const char *thread_name, struct intr_frame *f) {
	is_valid_addr(thread_name);
	return process_fork(thread_name, f);
}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
	/* Fail Condition */
	/* 1. when file opened as fd has a length of zero bytes.*/
    struct file *f = process_get_file(fd);
    if (f == NULL)
        return NULL;
	/* 2. Your mmap should also fail when length is zero */
	if (file_length(f) == 0 || (int)length <= 0)
        return NULL;
	
	/* 3. addr is not page-aligned or overlaps any existing set of mapped pages
		including the stack or pages mapped at executable load time */
    if (addr != pg_round_down(addr)) {
		return NULL;
	}
	/* 4. Therefore, if addr is 0, it must fail */ 
	if (addr == NULL) {
        return NULL;
	}

	/* 5.유저 가상 메모리 주소 범위 확인 */
    if (!is_user_vaddr(addr) || !is_user_vaddr(addr + length))
        return NULL;
		
	/* 6. 오프셋으로 정렬된 것인지 확인 */
	if (offset != pg_round_down(offset)) {
		return NULL;
	}
	if (offset % PGSIZE != 0) {
		return NULL;
	}
	/* 7. 페이지 유무 확인.  */ 
    if (spt_find_page(&thread_current()->spt, addr) != NULL)
        return NULL;
	// printf("addr: %p length: %d writable: %d fd: %d offset: %d\n", addr, length, writable, fd, offset);
    return do_mmap(addr, length, writable, f, offset);
}

void munmap(void *addr)
{
    do_munmap(addr);
}
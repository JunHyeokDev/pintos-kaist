#ifndef VM_ANON_H
#define VM_ANON_H
// #include "vm/vm.h"
#include "include/vm/vm.h"
#include "include/lib/stdbool.h"
#include <stdbool.h>
struct page;
enum vm_type;

struct anon_page {
    // struct page an_page;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif

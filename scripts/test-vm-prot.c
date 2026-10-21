#include <assert.h>
#include "../kernel/mm/prot.h"
int main(void) {
    assert(!nas_vm_prot_supported(0)); /* PROT_NONE needs VMA state */
    assert(nas_vm_prot_supported(NAS_VM_READ));
    assert(nas_vm_prot_supported(NAS_VM_READ|NAS_VM_WRITE));
    assert(nas_vm_prot_supported(NAS_VM_READ|NAS_VM_EXEC));
    assert(nas_vm_prot_supported(NAS_VM_WRITE)); /* implicit read on these ISAs */
    assert(nas_vm_prot_supported(NAS_VM_EXEC));
    assert(!nas_vm_prot_supported(NAS_VM_READ|NAS_VM_WRITE|NAS_VM_EXEC));
    assert(!nas_vm_prot_supported(NAS_VM_WRITE|NAS_VM_EXEC));
    assert(!nas_vm_prot_supported(8));
    return 0;
}

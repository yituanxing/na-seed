#include <assert.h>
#include <stdint.h>

/* Run this same host test in three Linux ABI decode configurations:
 * native x86-64, -DNAS_TEST_ARM64, -DNAS_TEST_RV64. No cross linker needed. */
#if defined(NAS_TEST_ARM64)
# undef __x86_64__
# define __aarch64__ 1
#elif defined(NAS_TEST_RV64)
# undef __x86_64__
# define __riscv 1
# define __riscv_xlen 64
#endif
#include "../kernel/arch/linux_syscall_abi.h"
#include "../kernel/task/capability.h"

static void test_uapi(void) {
#if defined(__x86_64__)
    assert(__NR_capget==125 && __NR_capset==126);
    assert(nas_linux_decode_syscall(__NR_capget)==NAS_SC_capget);
    assert(nas_linux_decode_syscall(__NR_capset)==NAS_SC_capset);
#else
    assert(nas_linux_decode_syscall(90)==NAS_SC_capget);
    assert(nas_linux_decode_syscall(91)==NAS_SC_capset);
#endif
    assert(nas_cap_version_words(NAS_CAP_VERSION_1)==1);
    assert(nas_cap_version_words(NAS_CAP_VERSION_2)==2);
    assert(nas_cap_version_words(NAS_CAP_VERSION_3)==2);
    assert(nas_cap_version_words(0)==0);
    assert(nas_cap_version_words(UINT32_MAX)==0);
    uint32_t version=0;
    assert(nas_cap_negotiate_version(&version,false,true)==0);
    assert(version==NAS_CAP_VERSION_3);
    version=0;
    assert((int64_t)nas_cap_negotiate_version(&version,false,false)==-EINVAL);
    assert(version==NAS_CAP_VERSION_3);
    version=0;
    assert((int64_t)nas_cap_negotiate_version(&version,true,true)==-EINVAL);
    assert(version==NAS_CAP_VERSION_3);
    version=NAS_CAP_VERSION_1;
    assert(nas_cap_negotiate_version(&version,false,false)==0);
    assert(version==NAS_CAP_VERSION_1);

    assert(nas_cap_validate_target(false,0,111)==0);
    assert(nas_cap_validate_target(false,222,111)==0);
    assert((int64_t)nas_cap_validate_target(false,-1,111)==-EINVAL);
    assert(nas_cap_validate_target(true,0,111)==0);
    assert(nas_cap_validate_target(true,111,111)==0);
    assert((int64_t)nas_cap_validate_target(true,222,111)==-EPERM);
    assert((int64_t)nas_cap_validate_target(true,-1,111)==-EPERM);
    assert(sizeof(struct nas_cap_user_header)==8);
    assert(sizeof(struct nas_cap_user_data)==12);
}

static void test_root_capabilities(void) {
    struct nas_credentials parent={0};
    struct nas_cap_user_data data[2]={{0},{0}};
    assert(nas_cap_get_keepcaps(&parent)==0);
    assert(nas_cap_set_keepcaps(&parent,1)==0);
    assert(nas_cap_get_keepcaps(&parent)==1);
    assert((int64_t)nas_cap_set_keepcaps(&parent,2)==-EINVAL);
    assert(nas_cap_get_keepcaps(&parent)==1);
    struct nas_credentials inherited=parent; /* actual fork copies creds */
    nas_cap_exec_reset(&inherited);
    assert(nas_cap_get_keepcaps(&inherited)==0);
    assert(nas_cap_get_keepcaps(&parent)==1);
    assert(nas_cap_set_keepcaps(&parent,0)==0);
    assert(nas_cap_get_keepcaps(&parent)==0);
    assert(nas_credentials_can_net_raw(&parent));
    nas_cap_get(&parent,data);
    assert(data[0].permitted==(uint32_t)NAS_CAP_NET_RAW);
    assert(data[0].effective==(uint32_t)NAS_CAP_NET_RAW);
    assert(data[0].inheritable==0);
    assert(data[1].permitted==0 && data[1].effective==0 && data[1].inheritable==0);

    /* Drop effective, then restore only because permitted remains held. */
    data[0].effective=0;
    assert(nas_cap_set(&parent,2,data)==0);
    assert(!nas_credentials_can_net_raw(&parent));
    struct nas_credentials fork_copy=parent;
    assert(!nas_credentials_can_net_raw(&fork_copy));
    data[0].effective=(uint32_t)NAS_CAP_NET_RAW;
    assert(nas_cap_set(&parent,2,data)==0);
    assert(nas_credentials_can_net_raw(&parent));
    assert(!nas_credentials_can_net_raw(&fork_copy));

    /* Capset may not raise unsupported privilege, even for NaSeed root. */
    data[0].permitted|=1U<<12; /* CAP_NET_ADMIN unsupported */
    assert((int64_t)nas_cap_set(&parent,2,data)==-EPERM);
    data[0].permitted=(uint32_t)NAS_CAP_NET_RAW;
    data[0].inheritable=1U<<13; /* no inheritable file capability model */
    assert((int64_t)nas_cap_set(&parent,2,data)==-EPERM);
    data[0].inheritable=0;
    data[1].permitted=1U; /* CAP_MAC_OVERRIDE, upper word */
    assert((int64_t)nas_cap_set(&parent,2,data)==-EPERM);
    data[1].permitted=0;
    data[0].permitted=0; /* effective must be subset of permitted */
    assert((int64_t)nas_cap_set(&parent,2,data)==-EPERM);
    assert(nas_credentials_can_net_raw(&parent)); /* rejected transaction is atomic */

    /* Valid permanent permitted drop then attempted restoration is denied. */
    data[0].effective=0;
    assert(nas_cap_set(&parent,1,data)==0);
    assert(!nas_credentials_can_net_raw(&parent));
    nas_cap_get(&parent,data);
    assert(data[0].effective==0 && data[0].permitted==0);
    data[0].effective=(uint32_t)NAS_CAP_NET_RAW;
    data[0].permitted=(uint32_t)NAS_CAP_NET_RAW;
    assert((int64_t)nas_cap_set(&parent,1,data)==-EPERM);
    assert(!nas_credentials_can_net_raw(&parent));
    assert((int64_t)nas_cap_set(&parent,0,data)==-EINVAL);
    assert((int64_t)nas_cap_set(&parent,3,data)==-EINVAL);
}

int main(void) {
    test_uapi();
    test_root_capabilities();
    return 0;
}

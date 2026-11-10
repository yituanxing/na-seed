#include "misc.h"
#include "chrdev.h"
#include "../../fd.h"

/* These pseudo devices have no bus transport. Their read/write FileOps
 * remain the authoritative implementations in the existing File layer;
 * only their /dev identity and discovery live here. */
static struct nas_chrdev_node misc_nodes[]={
    { .path="/dev/null", .info={
        .fd_kind=FD_NULL,.dev=2,.ino=3,.rdev=(1U<<8)|3U,.mode=0020666U }},
    { .path="/dev/zero", .info={
        .fd_kind=FD_ZERO,.dev=2,.ino=5,.rdev=(1U<<8)|5U,.mode=0020666U }},
    { .path="/dev/random", .info={
        .fd_kind=FD_RANDOM,.dev=2,.ino=8,.rdev=(1U<<8)|8U,.mode=0020666U }},
    { .path="/dev/urandom", .info={
        .fd_kind=FD_RANDOM,.dev=2,.ino=9,.rdev=(1U<<8)|9U,.mode=0020666U }}
};

int nas_misc_chrdev_init(void) {
    for(unsigned i=0;i<sizeof(misc_nodes)/sizeof(misc_nodes[0]);i++) {
        int rc=nas_chrdev_node_register(&misc_nodes[i]);
        if(rc) {
            while(i) {
                i--;
                (void)nas_chrdev_node_unregister(&misc_nodes[i]);
            }
            return rc;
        }
    }
    return 0;
}

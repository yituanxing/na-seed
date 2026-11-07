/* M172 host oracle for the production device-node registry.  This tests
 * ownership and publication independently of a QEMU boot or fake VFS. */
#include "../kernel/drivers/core/chrdev.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool hardware_present=true;
static bool present(unsigned unit) { return hardware_present && unit==7U; }

static struct nas_chrdev_node first={
    .path="/dev/test172",
    .info={.fd_kind=1,.unit=7,.dev=1,.ino=172,.rdev=172,
           .mode=0020660U,.size=0},
    .present=present
};
static struct nas_chrdev_node duplicate={
    .path="/dev/test172",
    .info={.fd_kind=1,.unit=8,.dev=1,.ino=173,.rdev=173,
           .mode=0020660U,.size=0}
};
static struct nas_chrdev_node second={
    .path="/dev/second172",
    .info={.fd_kind=1,.unit=9,.dev=1,.ino=174,.rdev=174,
           .mode=0020660U,.size=0}
};

/* evdev-style class providers publish eventN endpoints on demand rather
 * than creating one static registry object for each possible device. */
static int dynamic_describe(const char *name,struct nas_chrdev_info *out) {
    if(strcmp(name,"/dev/input/test172"))return -ENOENT;
    *out=(struct nas_chrdev_info){
        .fd_kind=2,.unit=4,.dev=1,.ino=191,.rdev=191,
        .mode=0020600U
    };
    return 0;
}
static int dynamic_open(unsigned description,unsigned unit) {
    return description==1 && unit==4 ? 0 : -EINVAL;
}
static const struct nas_chrdev_provider dynamic_provider={
    .name="M172 input class",.describe=dynamic_describe,.open=dynamic_open
};

static int find(const char *path,struct nas_chrdev_info *out,
                struct nas_chrdev_node **node) {
    const struct nas_chrdev_provider *provider=NULL;
    return nas_chrdev_describe(path,out,&provider,node);
}

int main(void) {
    struct nas_chrdev_info info={0};
    struct nas_chrdev_node *node=NULL;
    assert(find("/dev/test172",&info,&node)==-ENOENT);
    assert(nas_chrdev_node_register(NULL)==-EINVAL);
    assert(nas_chrdev_node_register(&first)==0);
    assert(nas_chrdev_node_register(&first)==-EBUSY);
    assert(nas_chrdev_node_register(&duplicate)==-EEXIST);
    assert(nas_chrdev_node_register(&second)==0);
    assert(nas_chrdev_provider_register(&dynamic_provider)==0);
    assert(nas_chrdev_provider_register(&dynamic_provider)==-EEXIST);

    assert(find("/dev/test172",&info,&node)==0);
    assert(node==&first && info.unit==7 && info.ino==172);
    assert(find("/dev/test172/child",&info,&node)==-ENOENT);
    assert(find("/dev/second172",&info,&node)==0);
    assert(node==&second && info.unit==9);

    {
        const struct nas_chrdev_provider *provider=NULL;
        assert(nas_chrdev_describe("/dev/input/test172",&info,&provider,&node)==0);
        assert(node==NULL && provider==&dynamic_provider && info.unit==4);
        assert(provider->open(1,info.unit)==0);
    }

    /* A pinned File-like consumer prevents a driver from deleting the
     * published endpoint until its final open description is released. */
    assert(nas_chrdev_node_pin(&first)==0);
    assert(nas_chrdev_node_pin(&first)==0);
    assert(nas_chrdev_node_unregister(&first)==-EBUSY);
    nas_chrdev_node_put(&first);
    assert(nas_chrdev_node_unregister(&first)==-EBUSY);
    nas_chrdev_node_put(&first);

    hardware_present=false;
    assert(find("/dev/test172",&info,&node)==-ENOENT);
    assert(nas_chrdev_node_pin(&first)==-ENODEV);
    assert(nas_chrdev_node_unregister(&first)==0);
    assert(find("/dev/test172",&info,&node)==-ENOENT);
    hardware_present=true;
    assert(nas_chrdev_node_register(&duplicate)==0);
    assert(find("/dev/test172",&info,&node)==0 && node==&duplicate);
    assert(nas_chrdev_node_unregister(&duplicate)==0);
    assert(nas_chrdev_node_unregister(&second)==0);
    assert(find("/dev/second172",&info,&node)==-ENOENT);

    puts("[M172 NODE PASS] static and dynamic publication, exact path, duplicate rejection, open pin, unplug, replacement");
    return 0;
}

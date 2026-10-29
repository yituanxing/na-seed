/* Compile the ACTUAL static exec Path resolver, inject lookup/pin failures,
 * verify that no borrowed inode escapes the lifetime of its owning Dentry. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/errno.h"
#include "../kernel/fs/vfs.h"
#include "../kernel/fs/lookup.h"

/* Inclusion intentionally exposes just the internal production helper to
 * this test; --gc-sections drops every unrelated exec/MM/ABI syscall body. */
#include "../kernel/task/exec.c"

static char fake_dentry;
static unsigned path_refs,inode_refs;
static int walk_error,node_error,pin_error,display_error;

int nas_fs_lookup_path_at(const struct nas_vfs_path *start,
                          const struct nas_vfs_path *root,const char *path,
                          bool follow,struct nas_vfs_path *out) {
    (void)start;(void)root;
    assert(follow && path && path[0]);
    *out=(struct nas_vfs_path){0};
    if(walk_error)return walk_error;
    out->dentry=(struct nas_vfs_dentry *)&fake_dentry;
    path_refs++;
    return 0;
}
int nas_vfs_path_node(const struct nas_vfs_path *path,
                      struct nas_fs_node *node) {
    assert(path->dentry && path_refs);
    if(node_error)return node_error;
    *node=(struct nas_fs_node){.ino=77};
    return 0;
}
int nas_vfs_path_string(const struct nas_vfs_path *path,char output[256]) {
    assert(path->dentry && path_refs);
    if(display_error)return display_error;
    strcpy(output,"/tmp/real-image");
    return 0;
}
int nas_vfs_node_pin(const struct nas_fs_node *node) {
    assert(node->ino==77 && path_refs>0);
    if(pin_error)return pin_error;
    inode_refs++;
    return 0;
}
int nas_vfs_node_unpin(const struct nas_fs_node *node) {
    assert(node->ino==77 && inode_refs>0);
    inode_refs--;
    return 0;
}
int nas_vfs_path_put(struct nas_vfs_path *path) {
    assert(path->dentry && path_refs);
    path_refs--;
    *path=(struct nas_vfs_path){0};
    return 0;
}
void nas_print(const char *msg) { fputs(msg,stderr); }
void nas_arch_halt(void) { assert(!"exec Path reference corrupted");__builtin_trap(); }

static void reset(void) {
    path_refs=inode_refs=0;
    walk_error=node_error=pin_error=display_error=0;
}
int main(void) {
    struct nas_fs_path_request request={0};
    strcpy(request.pathname,"image");
    struct nas_fs_node node={0};char display[256];
    reset();
    assert(exec_lookup_request(&request,&node,display)==0);
    assert(!strcmp(display,"/tmp/real-image"));
    assert(path_refs==0 && inode_refs==1);
    exec_drop_image_sources(&node,NULL);
    assert(!inode_refs);

    reset();walk_error=-ENOENT;
    assert(exec_lookup_request(&request,&node,display)==-ENOENT);
    assert(!path_refs && !inode_refs);
    reset();node_error=-ESTALE;
    assert(exec_lookup_request(&request,&node,display)==-ESTALE);
    assert(!path_refs && !inode_refs);
    reset();pin_error=-ENOMEM;
    assert(exec_lookup_request(&request,&node,display)==-ENOMEM);
    assert(!path_refs && !inode_refs);
    reset();display_error=-EIO;
    assert(exec_lookup_request(&request,&node,display)==-EIO);
    assert(!path_refs && !inode_refs);
    reset();display_error=-ENAMETOOLONG;
    assert(exec_lookup_request(&request,&node,display)==0);
    assert(!strcmp(display,"image") && !path_refs && inode_refs==1);
    exec_drop_image_sources(&node,NULL);
    assert(!inode_refs);
    puts("[EXEC PATH OWNER HOST PASS] Path-before-pin and every injected failure releases source");
    return 0;
}

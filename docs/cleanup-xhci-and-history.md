# Cleanup: H11–H15 QEMU xHCI platform common path

The five isolated profiles used five private copies of the same PCI tuple/fw_cfg gate, BAR0 lookup, 16 KiB read/write callbacks, IF restoration and pin-before-map sequence. Reuse the existing x86_qemu_hw_contract.h owner, and retain the class-specific binding, original command restoration, logging, release paths and exact existing virtual addresses. Successful mapping leaves IRQ exclusion with the caller; failures restore the saved IF and retain a pinned partial mapping. No new bus, resource registry, class driver, or physical-hardware claim is introduced. The profiles still own separate sessions and remain mutually restricted by their existing build configuration.

H11–H15 existing QEMU workflows now cover cleanup branch pushes and changes to the common headers. The tests and negative cases remain unchanged. All runtime acceptance is pending for the new commit.

## Historical divergence review

- fix/process-owner-and-unix-rights (b15b6fbc): existing current source already implements TGID wait ownership/TID ptrace, canonical OFD finalization for SCM_RIGHTS and pending accept, group session publication and sibling wait4 test. Do not import the old implementation.
- codex/m194-lwext4-sparse-hole (e27074fb): ext4_fread already zero-fills complete and trailing sparse holes. The historical patch coalesces holes, while the current implementation zero-fills full holes individually. Preserve current implementation; compare and recover missing coverage rather than wholesale merge.
- codex/m192-virtio-flush-diag (8b6b9979): the temporary M191-specific stage logger is not present in the current adapter; it is diagnostic code, not a missing I/O capability. Do not reintroduce milestone-specific instrumentation merely to merge an old branch.
- fix/init-leader-lifetime (3c966dc3): the old running_busybox/running_alpine/running_bash alias correction targets obsolete observer code; current final PID1 gate validates zombie/whole-group state, and the M44 source contains PID1 leader-first and group-exit tests. Further ancestry and behavior review required before branch retirement.

No historical refs have been deleted; remaining divergent Task/MM, VFS and CI branches still need deeper semantic comparison.

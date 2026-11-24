# H15 USB serial: class/backend ownership boundary (2026-11-24)

## Replaced direct dependency

Previously `kernel/fs/usb_serial_chrdev.c` included the H15 x86 platform
header and invoked controller-specific I/O, and the platform shutdown
called the Linux character-device unpublish function. Both dependencies
ran opposite to the intended device -> class -> FileOps direction.

Now the physical FTDI driver implements a borrowed
`nas_usb_serial_backend` interface. The **existing** chrdev registry,
OpenFile pinning, devfs publication and FileOps remain the only authoritative
user-visible path. No second registry or competing USB stack is introduced.

```
QEMU-qualified PCI/xHCI/FTDI (owns DMA/BAR/controller/RX)
  -> nas_usb_serial_backend (borrowed callbacks + publication owner)
  -> chrdev node / FileOps (Linux termios)
  -> shared OpenFile -> /dev/ttyUSB0
```

## Lifetime contract

- A ready driver provides all callbacks and grants one publication pin
  **before** the class registers the node; a failed registration rolls back
  the pin.
- The class drops the pin **only after** existing chrdev unregister succeeds.
  Open File descriptors keep the node pinned and return `-EBUSY` on unpublish.
- H15 shutdown independently returns `-EBUSY` while publication is pinned.
  Init must unpublish before quiescing xHCI and freeing DMA/BAR ownership.
- Hardware stop and resource release remain driver-owned; Linux ABI code has
  no platform header or direct H15 function dependency.
- The initial class adapter remains a single FT232BM ttyUSB0 with fixed
  real USB 115200/8N1 and QEMU-only qualification. This does not claim generic
  USB enumeration, physical host readiness, hotplug or SMP synchronization.

## Verification requirements

Host class fixture uses the actual USB class and chrdev implementations,
with a fake physical backend, to cover registration rollback/offline guard,
lifetime pin, FileOps RX/TX/ioctl and `-EBUSY` unpublish. Existing H15 xHCI
host model remains unchanged. The architecture guard now rejects restored
x86_H15 dependencies in the Linux USB class adapter and contains negative
mutation cases.

A **fresh exact-head** H15 system-QEMU run must still prove genuine FT232BM
host nonce/BusyBox traffic, missing-USB and missing-xHCI fail-closed tests,
and immutable signed Alpine media. Until it passes this commit remains draft.

## Next slice

Unify PCI config/BAR mapping/IRQ/DMA resources with one platform-owned
instance on the already-existing BusDevice/Driver model. Do not introduce
another registry or declare H11-H15 independent fixtures a multi-device
coexistence proof.

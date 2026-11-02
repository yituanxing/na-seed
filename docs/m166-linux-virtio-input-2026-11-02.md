# M166: Linux VirtIO Input on the NaSeed driver model

## Ownership and provenance

The layout is a targeted GPL-2.0-only port of the Linux input design, not a
second NaSeed-specific keyboard protocol:

- Linux `drivers/virtio/virtio_input.c`: VirtIO selector/config wire model,
  capability bitmaps, eventq/statusq, translated `virtio_input_event` records.
- Linux `drivers/input/input.c`: input-device registration, capability/state
  checks, and hardware-independent event dispatch.
- Linux `drivers/input/evdev.c`: independent readers of the Linux evdev ABI,
  packet boundaries and reader overflow `SYN_DROPPED` behavior.
- Reused and unchanged: the common NaSeed driver bus, DMA page ownership,
  modern PCI/MMIO VirtIO transport, and pinned upstream Unikraft split-ring
  implementation. Existing U-Boot VirtIO block/network drivers stay intact.

```
QEMU virtio-keyboard/tablet/mouse
    -> platform PCI or MMIO bus enumeration
    -> shared VirtIO transport + Unikraft DMA split queues
    -> kernel/drivers/input/virtio_input.c
    -> kernel/drivers/input/core.c
    -> kernel/drivers/input/evdev.c
    -> provider-registered /dev/input/eventN (FileOps)
    -> signed Alpine evtest + ordinary guest applications
    -> /dev/fb0 (independent framebuffer driver)
```

Only the bus scanner is ISA-specific. Input devices, packet generation,
reader lifetime, descriptors and user ABI use identical C on all three ISAs.
No hardware keycodes are converted to serial/TTY bytes by the driver.

## Lifecycle and contracts

1. Register evdev's character provider before hardware discovery.
2. PCI or MMIO enumerates VirtIO device ID 18; the input bus driver owns
   feature negotiation, selectors, both hardware queues and DMA buffers.
3. Linux-compatible capabilities determine device behavior. Every successful
   open creates an evdev reader for one *shared open-file description*, so
   `dup()`/`fork()` share the queue; separate opens do not steal events.
4. `EV_SYN/SYN_REPORT` completes a readable packet. A reader overrun emits
   `SYN_DROPPED` rather than silently returning a corrupted packet.
5. The common FileOps layer admits whole 24-byte native-LP64 `input_event`
   records across user VM pages and `readv()` segments. `EVIOCGVERSION`,
   `EVIOCGID`, `EVIOCGNAME`, `EVIOCGBIT`, `EVIOCGPROP`, `EVIOCGKEY` and
   `EVIOCGABS` are served through Linux ioctl framing.
6. Device unbind refuses to free live hardware while any reader owns it.
   Transport reset precedes DMA reclamation. Hotplug policy is **not**
   introduced here; existing deferred bus-remove infrastructure suffices.

Input is opt-in (`NAS_ENABLE_VIRTIO_INPUT=1` and
`NAS_QEMU_INPUT_OPTIN=1`), preserving historical serial-only QEMU profiles.
The existing transport currently uses bounded queue polling on scheduler
service turns, not an unimplemented interrupt callback. The output-side
`EV_LED/EV_SND` handlers are likewise deliberately not advertised until
their actual Linux statusq producer can be ported.

## Acceptance and limitations

- `sh scripts/check-m166-input-port.sh`: host tests of the actual input core,
  evdev, Unikraft/DMA queue port; clang syntax checks for three native targets
  where clang is installed.
- `.github/workflows/m166-batch.yml`: native QEMU **amd64/riscv64/arm64**,
  QMP real hardware key/absolute/button/relative events, signed Alpine evtest
  key-state reads, and five screenshot color oracles that inspect QEMU's
  physical framebuffer independently of the guest's serial messages.
- Four new original signed Alpine ELFs (evtest and the three libevdev tools)
  are staged and checked. The app executes evtest on both key transitions,
  and uses existing signed fbgrab/ImageMagick programs as a display oracle.

The input/evdev port is **not** a compositor, graphics accelerator, PS/2 or
USB HID stack, global seat policy, or end-user desktop input mapper. Those
are independent follow-on features once the present software demands them.
The real native QEMU workflow, not the host mocks, is the authority for
claiming full hardware acceptance.

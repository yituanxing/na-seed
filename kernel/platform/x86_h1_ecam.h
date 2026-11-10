#ifndef NASEED_X86_H1_ECAM_H
#define NASEED_X86_H1_ECAM_H

#include "firmware_acpi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The firmware's MMIO description is only a claim. The boot platform must
 * reject RAM, ACPI tables, framebuffers, kernel and bootloader allocations
 * before ECAM is mapped. This H1 gate has no BAR, DMA, IRQ or driver binding. */
typedef bool (*nas_x86_h1_mmio_allowed)(void *context, uint64_t physical,
                                         size_t bytes);
int nas_x86_h1_ecam_verify(const struct nas_acpi_discovery *acpi,
                            nas_x86_h1_mmio_allowed allowed, void *context);

#endif

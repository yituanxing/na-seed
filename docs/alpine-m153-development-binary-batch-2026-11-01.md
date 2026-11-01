# M153: 36 additional genuine signed Alpine 3.23 developer and ELF tools

M153 counts 36 **historically new, distinct SHA256** original ELF images.
The package index is official Alpine v3.23 `main+community`; APK install is
host-side, signature-checked, architecture-checked, and executed with
`--no-scripts`. Nothing is compiled inside NaSeed, and none of the new
programs is replaced with a script, BusyBox link, host shim, or rebuilt ELF.

## Software added

| Signed package | New ELF images | Genuine operation checked in guest |
| --- | ---: | --- |
| radare2 | 7 | ELF bytes, symbols, binary difference, string offsets, SHA256, x86 instruction assembly, numeric conversion |
| pahole / dwarves | 6 | Structure layout, function/global DWARF metadata, debug tag listings, changed type-layout report |
| moreutils | 7 | errno mapping, conditional stdin, Unicode, file locking, pipeline exit code, parallel commands, fan-out |
| nasm | 2 | Assemble two machine instructions; independently disassemble NOP/RET |
| yara | 2 | Match a real object and compile a binary rule then scan it |
| bloaty | 1 | Actual ELF section size analysis |
| cflow | 1 | Call graph from a nontrivial C source |
| difftastic | 1 | Actual comparison of two distinct text fixtures |
| mold | 1 | Link an actual native relocatable object to a shared ELF |
| ccache | 1 | Content-sensitive hash of two different object files |
| GCC | 4 | Macro preprocessing, archive creation, symbol inspection and archive indexing |
| LLVM clang21-extra-tools | 3 | Source formatting, C AST walk, static diagnostic of a real magic number |
| **Total** | **36** | **36 individually reported semantic checks** |

The upstream `coreutils` APK's dozens of command names are **all symlinks**
to one already-counted `bin/coreutils` ELF and yield zero new program images;
similarly, already admitted `ar`, `nm`, `objcopy`, `objdump`, `readelf`,
`eu-*`, `diff` and `find` are excluded rather than double counted.

## Gates and evidence

1. `scripts/stage-m153-prebuilt.sh PORT`: scans every prior milestone
   `scripts/m*-original-elfs.txt`, requires exactly 36 different paths,
   imports Alpine's signed x86_64/riscv64/aarch64 target APKs, checks native
   ELF magic/architecture and distinct actual SHA256 of resolved executables.
2. `scripts/host-configure-m153-batch.sh PORT ROOT`: uses host clang to make
   **target-ISA** object files with DWARF layout, functions, globals, and
   intentional changes; also copies real source, YARA rules, and assembler
   input to the guest rootfs. Guests never run a compiler for this fixture.
3. `scripts/probe-m153-batch.sh PORT`: executes the unchanged target-ISA ELF
   binaries under native QEMU. Exactly 36 named semantic `M153_*_PASS`
   markers and one `M153_BATCH_PASS` are mandatory; any failed command
   prints its captured actual stdout/stderr and causes the job to fail.
4. `.github/workflows/m153-batch.yml`: x86_64 by default; manual dispatch,
   `run-m153-multiarch`, and shared `run-m151-155-multiarch` trigger ARM64 and
   RV64 lanes. Uploads the signed APK log, 36 SHA hashes and raw QEMU serial.

Before authentic CI, host syntax and downloaded-APK static inspection are
**not** substitutes for guest acceptance. Report QEMU status separately.

Catalog integration is intentionally owned by the main agent; append only:

```
m153-batch	rootfs/alpine/packages-m153-batch.txt	scripts/m153-original-elfs.txt	scripts/m153-rootfs-copies.tsv	main+community
```

#include "firmware_acpi.h"

/* The H0 reader intentionally has no dependency on the kernel allocator,
 * VFS, PCI driver or libc. All little-endian ACPI wire fields are decoded
 * from bytes so firmware tables need not be naturally aligned. */
#define ACPI_RSDP_V1_BYTES 20U
#define ACPI_RSDP_V2_BYTES 36U
#define ACPI_RSDP_MAX_BYTES 4096U
#define ACPI_SDT_HEADER_BYTES 36U
#define ACPI_ROOT_MAX_BYTES 16384U
#define ACPI_TABLE_MAX_BYTES (1024U * 1024U)
#define ACPI_ROOT_ENTRY_LIMIT 256U
#define ACPI_MCFG_FIXED_BYTES 44U
#define ACPI_MCFG_ENTRY_BYTES 16U
#define ACPI_PCI_BUS_BYTES UINT64_C(1048576)

static bool signature_is(const uint8_t *bytes, const char *text,
                         size_t length) {
    for(size_t i=0;i<length;i++)if(bytes[i]!=(uint8_t)text[i])return false;
    return true;
}
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p+4)<<32);
}
static bool checksum_ok(const uint8_t *p, size_t bytes) {
    uint8_t sum=0;
    for(size_t i=0;i<bytes;i++)sum=(uint8_t)(sum+p[i]);
    return sum==0;
}
static void clear_discovery(struct nas_acpi_discovery *out) {
    uint8_t *bytes=(uint8_t *)(void *)out;
    for(size_t i=0;i<sizeof(*out);i++)bytes[i]=0;
}
static const uint8_t *map_table(nas_acpi_map_fn map, void *context,
                                 uint64_t physical, size_t bytes) {
    if(!map || !physical || !bytes || physical>UINT64_MAX-(uint64_t)bytes)
        return NULL;
    return (const uint8_t *)map(context,physical,bytes);
}
static const uint8_t *checked_sdt(nas_acpi_map_fn map, void *context,
                                   uint64_t physical, uint32_t limit,
                                   uint32_t *length) {
    const uint8_t *header=map_table(map,context,physical,ACPI_SDT_HEADER_BYTES);
    if(!header)return NULL;
    uint32_t bytes=le32(header+4U);
    if(bytes<ACPI_SDT_HEADER_BYTES || bytes>limit)return NULL;
    const uint8_t *table=map_table(map,context,physical,bytes);
    if(!table || !checksum_ok(table,bytes))return NULL;
    *length=bytes;
    return table;
}

static bool add_mcfg(struct nas_acpi_discovery *found,
                     const uint8_t *table, uint32_t length) {
    if(length<ACPI_MCFG_FIXED_BYTES ||
       (length-ACPI_MCFG_FIXED_BYTES)%ACPI_MCFG_ENTRY_BYTES)return false;
    unsigned count=(length-ACPI_MCFG_FIXED_BYTES)/ACPI_MCFG_ENTRY_BYTES;
    if(count>NAS_ACPI_MCFG_WINDOW_LIMIT-found->mcfg_count)return false;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *p=table+ACPI_MCFG_FIXED_BYTES+
                         i*ACPI_MCFG_ENTRY_BYTES;
        struct nas_acpi_mcfg_window candidate={
            .ecam_base=le64(p),.segment=le16(p+8U),
            .bus_start=p[10],.bus_end=p[11]
        };
        if(candidate.bus_start>candidate.bus_end ||
           !candidate.ecam_base ||
           (candidate.ecam_base&(ACPI_PCI_BUS_BYTES-1U)))return false;
        uint64_t covered=((uint64_t)candidate.bus_end-
                          candidate.bus_start+1U)*ACPI_PCI_BUS_BYTES;
        if(candidate.ecam_base>UINT64_MAX-covered)return false;
        for(unsigned j=0;j<found->mcfg_count;j++) {
            const struct nas_acpi_mcfg_window *prior=&found->mcfg[j];
            if(prior->segment==candidate.segment &&
               prior->bus_start<=candidate.bus_end &&
               candidate.bus_start<=prior->bus_end)return false;
        }
        found->mcfg[found->mcfg_count++]=candidate;
    }
    return true;
}

int nas_acpi_probe(const void *rsdp, size_t rsdp_available,
                   nas_acpi_map_fn map, void *context,
                   struct nas_acpi_discovery *out) {
    if(!out)return -1;
    clear_discovery(out);
    if(!rsdp || rsdp_available<ACPI_RSDP_V1_BYTES || !map)return -1;
    const uint8_t *r=(const uint8_t *)rsdp;
    if(!signature_is(r,"RSD PTR ",8U) ||
       !checksum_ok(r,ACPI_RSDP_V1_BYTES))return -1;
    bool xsdt=false;
    uint64_t root_physical=le32(r+16U);
    if(r[15]>=2U) {
        if(rsdp_available<ACPI_RSDP_V2_BYTES)return -1;
        uint32_t extended_length=le32(r+20U);
        if(extended_length<ACPI_RSDP_V2_BYTES ||
           extended_length>ACPI_RSDP_MAX_BYTES ||
           extended_length>rsdp_available ||
           !checksum_ok(r,extended_length))return -1;
        uint64_t reported_xsdt=le64(r+24U);
        if(reported_xsdt) {root_physical=reported_xsdt;xsdt=true;}
    }
    uint32_t root_bytes=0;
    const uint8_t *root=checked_sdt(map,context,root_physical,
                                     ACPI_ROOT_MAX_BYTES,&root_bytes);
    unsigned entry_bytes=xsdt?8U:4U;
    if(!root || !signature_is(root,xsdt?"XSDT":"RSDT",4U) ||
       (root_bytes-ACPI_SDT_HEADER_BYTES)%entry_bytes)return -1;
    unsigned entries=(root_bytes-ACPI_SDT_HEADER_BYTES)/entry_bytes;
    if(entries>ACPI_ROOT_ENTRY_LIMIT)return -1;

    struct nas_acpi_discovery found={0};
    found.used_xsdt=xsdt;
    found.table_count=entries;
    for(unsigned i=0;i<entries;i++) {
        const uint8_t *entry=root+ACPI_SDT_HEADER_BYTES+i*entry_bytes;
        uint64_t physical=xsdt?le64(entry):le32(entry);
        const uint8_t *header=map_table(map,context,physical,
                                         ACPI_SDT_HEADER_BYTES);
        if(!header)return -1;
        bool mcfg=signature_is(header,"MCFG",4U);
        bool madt=signature_is(header,"APIC",4U);
        if(!mcfg && !madt)continue;
        uint32_t length=0;
        const uint8_t *table=checked_sdt(map,context,physical,
                                          ACPI_TABLE_MAX_BYTES,&length);
        if(!table)return -1;
        if(mcfg && !add_mcfg(&found,table,length))return -1;
        if(madt) {
            /* MADT has an 8-byte fixed payload following the SDT header.
             * Its variable interrupt-controller records are intentionally
             * left to the later architecture-owned IRQ parser. */
            if(length<ACPI_SDT_HEADER_BYTES+8U)return -1;
            found.madt_present=true;
        }
    }
    *out=found;
    return 0;
}

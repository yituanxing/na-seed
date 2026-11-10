#include "../kernel/platform/firmware_acpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host-only byte fixtures: parser must not dereference unapproved addresses,
 * write physical resources or expose a partial discovery after any failure. */
enum {
    RSDP_SIZE=36, ROOT_SIZE=52, MCFG_SIZE=76, MADT_SIZE=44
};
#define XSDT_PA UINT64_C(0x100000)
#define RSDT_PA UINT64_C(0x110000)
#define MCFG_PA UINT64_C(0x200000)
#define MADT_PA UINT64_C(0x300000)

static uint8_t rsdp[RSDP_SIZE];
static uint8_t xsdt[ROOT_SIZE], rsdt[ROOT_SIZE];
static uint8_t mcfg[MCFG_SIZE], madt[MADT_SIZE];
static bool reject_mcfg;
static unsigned maps;

static void put16(uint8_t *p, uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
static void put32(uint8_t *p, uint32_t v) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));
}
static void checksum(uint8_t *p, size_t len, size_t at) {
    p[at]=0;
    uint8_t sum=0;
    for(size_t i=0;i<len;i++)sum=(uint8_t)(sum+p[i]);
    p[at]=(uint8_t)(0U-sum);
}
static void make_sdt(uint8_t *p, size_t capacity, const char *tag,
                     uint32_t length) {
    memset(p,0,capacity);
    memcpy(p,tag,4);
    put32(p+4,length);
    p[8]=1;
}
static void finish_sdt(uint8_t *p) {
    uint32_t len=(uint32_t)p[4] | ((uint32_t)p[5]<<8) |
                 ((uint32_t)p[6]<<16) | ((uint32_t)p[7]<<24);
    checksum(p,len,9);
}
static void fixture(void) {
    memset(rsdp,0,sizeof(rsdp));
    memcpy(rsdp,"RSD PTR ",8);
    rsdp[15]=2;
    put32(rsdp+16,(uint32_t)RSDT_PA);
    put32(rsdp+20,sizeof(rsdp));
    put64(rsdp+24,XSDT_PA);
    checksum(rsdp,20,8);
    checksum(rsdp,36,32);

    make_sdt(xsdt,sizeof(xsdt),"XSDT",ROOT_SIZE);
    put64(xsdt+36,MCFG_PA);
    put64(xsdt+44,MADT_PA);
    finish_sdt(xsdt);

    make_sdt(rsdt,sizeof(rsdt),"RSDT",44U);
    put32(rsdt+36,(uint32_t)MCFG_PA);
    put32(rsdt+40,(uint32_t)MADT_PA);
    finish_sdt(rsdt);

    make_sdt(mcfg,sizeof(mcfg),"MCFG",MCFG_SIZE);
    put64(mcfg+44,UINT64_C(0xe0000000));
    put16(mcfg+52,0);
    mcfg[54]=0;mcfg[55]=127;
    put64(mcfg+60,UINT64_C(0xf0000000));
    put16(mcfg+68,1);
    mcfg[70]=128;mcfg[71]=255;
    finish_sdt(mcfg);

    make_sdt(madt,sizeof(madt),"APIC",MADT_SIZE);
    finish_sdt(madt);
    reject_mcfg=false;maps=0;
}
static const void *map(void *context, uint64_t physical, size_t bytes) {
    (void)context;
    maps++;
    struct {uint64_t physical;const uint8_t *data;size_t length;} items[]={
        {XSDT_PA,xsdt,sizeof(xsdt)},
        {RSDT_PA,rsdt,44U},
        {MCFG_PA,mcfg,sizeof(mcfg)},
        {MADT_PA,madt,sizeof(madt)}
    };
    for(unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++) {
        if(reject_mcfg && items[i].physical==MCFG_PA)continue;
        if(physical>=items[i].physical &&
           physical-items[i].physical<=items[i].length &&
           bytes<=items[i].length-(size_t)(physical-items[i].physical))
            return items[i].data+(physical-items[i].physical);
    }
    return NULL;
}
static void check(bool condition, const char *what) {
    if(!condition){fprintf(stderr,"[H0 ACPI FAIL] %s\n",what);exit(1);}
}
static void probe_ok(const char *what) {
    struct nas_acpi_discovery result;
    check(nas_acpi_probe(rsdp,sizeof(rsdp),map,NULL,&result)==0,what);
    check(result.madt_present, "MADT present");
    check(result.used_xsdt==(rsdp[15]>=2U),"RSDT/XSDT root selection");
    check(result.table_count==2U, "two root entries");
    check(result.mcfg_count==2U, "two MCFG windows");
    check(result.mcfg[0].ecam_base==UINT64_C(0xe0000000) &&
          result.mcfg[0].bus_end==127U &&
          result.mcfg[1].segment==1U &&
          result.mcfg[1].bus_start==128U, "window mapping");
    check(maps>=6U,"resolver traversed physical tables");
}
static void probe_fail(const char *what) {
    struct nas_acpi_discovery result;
    memset(&result,0xaa,sizeof(result));
    check(nas_acpi_probe(rsdp,sizeof(rsdp),map,NULL,&result)!=0,what);
    const uint8_t *p=(const uint8_t *)&result;
    for(size_t i=0;i<sizeof(result);i++)
        check(p[i]==0U,"failure must not publish partial discovery");
}
int main(void) {
    fixture();probe_ok("valid XSDT/MCFG/MADT");

    fixture();
    rsdp[8]^=1U;probe_fail("bad RSDP checksum");
    fixture();
    rsdp[32]^=1U;probe_fail("bad extended RSDP checksum");
    fixture();
    xsdt[9]^=1U;probe_fail("bad XSDT checksum");
    fixture();
    mcfg[9]^=1U;probe_fail("bad MCFG checksum");
    fixture();
    put32(madt+4,36U);finish_sdt(madt);
    probe_fail("MADT without fixed interrupt-controller header");
    fixture();
    mcfg[54]=128;mcfg[55]=64;finish_sdt(mcfg);
    probe_fail("reversed bus range");
    fixture();
    mcfg[44]=1;finish_sdt(mcfg);
    probe_fail("unaligned ECAM base");
    fixture();
    put16(mcfg+68,0);mcfg[70]=64;mcfg[71]=255;finish_sdt(mcfg);
    probe_fail("overlapping segments");
    fixture();
    put32(xsdt+4,35);finish_sdt(xsdt);
    probe_fail("truncated XSDT");
    fixture();reject_mcfg=true;
    probe_fail("unmapped physical table");
    fixture();
    rsdp[15]=0;checksum(rsdp,20,8);
    probe_ok("ACPI 1.0 RSDT fallback");
    fixture();
    struct nas_acpi_discovery result;
    check(nas_acpi_probe(rsdp,20,map,NULL,&result)!=0,
          "truncated extended RSDP refused");
    puts("[H0 ACPI HOST PASS] RSDP/XSDT/RSDT/MCFG read-only validation");
    return 0;
}

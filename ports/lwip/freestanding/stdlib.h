#ifndef NASEED_LWIP_FREESTANDING_STDLIB_H
#define NASEED_LWIP_FREESTANDING_STDLIB_H
/* Lightweight numeric parsing for netif names and optional netdb services.
 * Neither malloc/free nor a second kernel heap is exposed to lwIP. */
int nas_lwip_atoi(const char *text);
#define atoi nas_lwip_atoi
#endif

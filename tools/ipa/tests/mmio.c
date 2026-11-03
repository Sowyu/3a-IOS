#include "IpaMmio.h"
uint32_t test_read(uintptr_t base,unsigned off){return ipa_mmio_read32(base,off);}
void test_write(uintptr_t base,unsigned off,uint32_t value){ipa_mmio_write32(base,off,value);}
void test_write64(uintptr_t base,unsigned off,uint64_t value){ipa_mmio_write64(base,off,value);}

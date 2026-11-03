#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "QcomRpmh.h"
static struct {bool bcm_probe;uintptr_t db;uint32_t rail_addr[2],rail_index[2],xo_addr;}m;
static uint8_t db[0x20000];
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t m_rd(uintptr_t base,unsigned at){(void)base;assert(at<=sizeof(db)-4);return le32(db+at);}
static void sweet_log_sync(void){}
static void w16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void w32(uint8_t *p,unsigned v){w16(p,v);w16(p+2,v>>16);}

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "BatteryRead.c"
#include "QcomGlink.h"
static struct {BatteryRead read;bool reads,battery_up,fatal,failed;unsigned state;uint64_t cycle_started,request_sent,cycle_due;QcomGlink gl;QcomGlinkChannel *battery;}m;
static uint64_t now=1000;
static unsigned sent,published,cleared;
static int send_result;
static uint8_t request[24];
static uint64_t m_now(void){return now;}
static void sweet_log_sync(void){}
static void battery_snapshot_clear(void){cleared++;}
static void battery_snapshot_publish(const BatteryRead *b,uint64_t t){assert(t==now && battery_read_valid(b));published++;}
int qcom_glink_send(QcomGlink *g,QcomGlinkChannel *ch,const void *p,uint32_t n){(void)g;(void)ch;assert(n==24 && battery_read_allowed(p,n));if(send_result)return send_result;memcpy(request,p,n);sent++;return 0;}

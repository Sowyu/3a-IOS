#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "ModemTftpProbe.h"
typedef struct { int unused; } QcomQrtr;
static struct {bool tftp_probe,failed;unsigned tftp_requests;} m;
static unsigned sends;static int fail_send;
static int qcom_qrtr_send_data(QcomQrtr *q,uint32_t port,uint32_t node,uint32_t from,const uint8_t *data,uint32_t len){
 (void)q;assert(port==0x4006 && node==3 && from==42);assert(len==15 && data[1]==5);sends++;return fail_send;
}

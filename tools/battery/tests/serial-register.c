/* 2300: register_serial_functions from the patch. Built twice: the new side must keep the list acyclic,
 * the old side must be caught making the self-loop that spun serial_keyboard_poll (ast197, ast203). */
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
struct pe_serial_functions { int id; struct pe_serial_functions *next; };
static struct pe_serial_functions *gPESF;
#include "serial-register.inc"
/* uart_getc's walk, bounded: the number of devices, or -1 if it never reaches NULL. */
static int walk(void){int n=0;for(struct pe_serial_functions *p=gPESF;p;p=p->next){if(++n>100)return -1;}return n;}
int main(void){
 static struct pe_serial_functions fbcon={1,NULL},uart={2,NULL};
 register_serial_functions(&fbcon);assert(walk()==1);
 register_serial_functions(&fbcon);   /* serial_init() again, as PE_init_kprintf does */
 assert(walk()==1 && "second registration of the same device makes no cycle");
 register_serial_functions(&uart);assert(walk()==2 && gPESF==&uart && uart.next==&fbcon);
 register_serial_functions(&fbcon);register_serial_functions(&uart);assert(walk()==2);
 puts("PASS: serial device registration is idempotent; the list stays acyclic");
}

/* See COPYING.txt for license details. */

/*
 * irmp.h  (HOST TEST SHIM — not the real IRMP)
 *
 * Only the IRMP protocol ID macros referenced by flipper_ir.c, with the exact
 * values from Infrared/irmpprotocols.h so the protocol name<->id round-trip
 * matches firmware behaviour. Never part of the firmware build.
 *
 * M1 Project — host test harness
 */

#ifndef IRMP_H_
#define IRMP_H_

#define IRMP_UNKNOWN_PROTOCOL     0
#define IRMP_SIRCS_PROTOCOL       1
#define IRMP_NEC_PROTOCOL         2
#define IRMP_KASEIKYO_PROTOCOL    5
#define IRMP_RC5_PROTOCOL         7
#define IRMP_DENON_PROTOCOL       8
#define IRMP_RC6_PROTOCOL         9
#define IRMP_SAMSUNG32_PROTOCOL   10
#define IRMP_APPLE_PROTOCOL       11
#define IRMP_NOKIA_PROTOCOL       16
#define IRMP_RCCAR_PROTOCOL       19
#define IRMP_JVC_PROTOCOL         20
#define IRMP_NEC16_PROTOCOL       27
#define IRMP_NEC42_PROTOCOL       28
#define IRMP_BOSE_PROTOCOL        31
#define IRMP_RCMM32_PROTOCOL      36
#define IRMP_LGAIR_PROTOCOL       40
#define IRMP_SAMSUNG48_PROTOCOL   41

#endif /* IRMP_H_ */

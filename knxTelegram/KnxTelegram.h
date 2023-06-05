/**
 * @file KnxTelegram.h
 * @author Mateusz Zolisz <mateusz.zolisz@gmail.com>
 * @brief 
 * @version 0.2
 * @date 2023-06-01
 * 
 * @copyright Copyright (c) 2023
 * 
 */

/**
 * KNX Telegram Description
 * 
 * Telegram Fields:
 *  -> Control Field (1 byte)
 *  -> Source Address (2 bytes)
 *  -> Target Address (2 bytes + 1 bit)
 *  -> Routing (3 bits)
 *  -> Length (4 bits)
 *  -> Data (Up to 16 bytes)
 *  -> Check (1 byte)
 *   
 */

#ifndef KNX_TELEGRAM_H
#define KNX_TELEGRAM_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/* 4 MSB bits has no evaluation */
#define KNX_CMD_VALUE_RESPONSE 0b00000001
#define KNX_CMD_VALUE_READ 0b00000000
#define KNX_CMD_VALUE_WRITE 0b00000010
#define KNX_CMD_MEMORY_WRITE 0b00001010

typedef struct {
  uint8_t area;
  uint8_t line;
  uint8_t device;
} KnxSourceAddress;

typedef struct {
  uint8_t area;
  uint8_t line;
  uint16_t device;
} KnxTargetPhysicalAddress;

typedef struct {
  uint8_t main;
  uint8_t middle;
  uint16_t sub;
} KnxTargetGroupAddress;

typedef struct {
  bool retransmission;
  char* priority;
} KnxControl;

/** === Control Field === */
uint8_t knxCreateControlField(bool retransmission, char* priority);
KnxControl knxDecodeControlField(uint8_t field);
void knxPrintControl(KnxControl control);

/** === Source address === */
KnxSourceAddress knxCreateSourceAddressStructFromString(char* address);
uint16_t knxSourceAddresFromStructToField(KnxSourceAddress address);
uint16_t knxCreateSourceAddressFieldFromString(char* address);
bool knxIsValidSourceAddress(KnxSourceAddress address);
KnxSourceAddress knxDecodeSourceAddressField(uint16_t field);
void knxPrintSourceAddress(KnxSourceAddress address);

/** === Target Physical (Individual) address  === */
KnxTargetPhysicalAddress knxDecodeTargetPhysicalAddressField(uint16_t field);
KnxTargetGroupAddress knxDecodeTargetGroupAddressField(uint16_t field);
uint16_t knxTargetPhysicalAddressStructToField(KnxTargetPhysicalAddress address);
KnxTargetPhysicalAddress knxCreateTargetPhysicalAddressStructFromString(char* address);
void knxPrintTargetPhysicalAddress(KnxTargetPhysicalAddress address);

/** === Target Group address  === */
uint16_t knxCreateTargetPhysicalAddressFieldFromString(char* address);
KnxTargetGroupAddress knxCreateTargetGroupAddressStructFromString(char* address);
uint16_t knxTargetGroupAddressStructToField(KnxTargetGroupAddress address);
uint16_t knxCreateTargetGroupAddressFieldFromString(char* address);
void knxPrintTargetGroupAddress(KnxTargetGroupAddress address);

/** === Target address type*/
void knxSetTargetAddressType(uint8_t *field, bool type);
bool knxGetTargetAddressType(uint8_t field);

/** === Routing counter === */
void knxSetRoutingCounter(uint8_t *field, uint8_t value);
uint8_t knxGetRoutingCounter(uint8_t field);

/** === Data Field === */
void knxSetDataLength(uint8_t *field, uint8_t length);
uint8_t knxGetDataLength(uint8_t field);

// DPT - Switch (1.001)
uint16_t knxCreateDataSwitchField(uint8_t cmd, bool state);

// DPT - Dimming
uint32_t knxCreateDataDimmingField(uint8_t cmd, uint8_t value);

/** There will be more DPTs soon ... */

/** === Checksum === */
uint8_t knxCalculateChecksum(uint8_t telegram[], uint8_t size);

#endif // KNX_TELEGRAM_H

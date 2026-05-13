#ifndef PDS_LITTLE_ENDIAN_H
#define PDS_LITTLE_ENDIAN_H

#include <stdint.h>

uint16_t read_uint16_from_little_endian_bytes(const uint8_t *bytes);
int16_t read_int16_from_little_endian_bytes(const uint8_t *bytes);
int32_t read_int32_from_little_endian_bytes(const uint8_t *bytes);

void write_uint8_to_payload(
    uint8_t *buffer,
    uint16_t *position,
    uint8_t value);
void write_uint16_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    uint16_t value);
void write_int16_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    int16_t value);
void write_uint32_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    uint32_t value);
void write_int32_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    int32_t value);

#endif /* PDS_LITTLE_ENDIAN_H */

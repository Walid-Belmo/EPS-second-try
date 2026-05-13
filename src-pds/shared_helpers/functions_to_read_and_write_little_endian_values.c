#include <stdint.h>

#include "assertion_handler.h"
#include "shared_helpers/functions_to_read_and_write_little_endian_values.h"

uint16_t read_uint16_from_little_endian_bytes(const uint8_t *bytes)
{
    SATELLITE_ASSERT(bytes != (void *)0);

    return (uint16_t)(((uint16_t)bytes[0])
        | ((uint16_t)bytes[1] << 8u));
}

int16_t read_int16_from_little_endian_bytes(const uint8_t *bytes)
{
    uint16_t unsigned_value = read_uint16_from_little_endian_bytes(bytes);

    return (int16_t)unsigned_value;
}

int32_t read_int32_from_little_endian_bytes(const uint8_t *bytes)
{
    SATELLITE_ASSERT(bytes != (void *)0);

    uint32_t unsigned_value =
        ((uint32_t)bytes[0])
        | ((uint32_t)bytes[1] << 8u)
        | ((uint32_t)bytes[2] << 16u)
        | ((uint32_t)bytes[3] << 24u);

    return (int32_t)unsigned_value;
}

void write_uint8_to_payload(
    uint8_t *buffer,
    uint16_t *position,
    uint8_t value)
{
    SATELLITE_ASSERT(buffer != (void *)0);
    SATELLITE_ASSERT(position != (void *)0);

    buffer[*position] = value;
    *position = (uint16_t)(*position + 1u);
}

void write_uint16_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    uint16_t value)
{
    write_uint8_to_payload(buffer, position, (uint8_t)(value & 0xFFu));
    write_uint8_to_payload(buffer, position, (uint8_t)((value >> 8u) & 0xFFu));
}

void write_int16_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    int16_t value)
{
    write_uint16_to_little_endian_payload(buffer, position, (uint16_t)value);
}

void write_uint32_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    uint32_t value)
{
    write_uint16_to_little_endian_payload(
        buffer,
        position,
        (uint16_t)(value & 0xFFFFu));
    write_uint16_to_little_endian_payload(
        buffer,
        position,
        (uint16_t)((value >> 16u) & 0xFFFFu));
}

void write_int32_to_little_endian_payload(
    uint8_t *buffer,
    uint16_t *position,
    int32_t value)
{
    write_uint32_to_little_endian_payload(buffer, position, (uint32_t)value);
}

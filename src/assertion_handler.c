// =============================================================================
// assertion_handler.c
// Desktop implementation of the assertion failure handler.
// Prints the file path and line number to stderr, then terminates the program.
//
// Category: PURE LOGIC (no hardware)
//
// On the real chip, this file is replaced by a version that logs to PuTTY
// (debug build) or notifies the OBC and resets (flight build).
// See conventions.md Rule C5 for the full specification.
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "assertion_handler.h"

void satellite_handle_assertion_failure(
    const char *file_path,
    int32_t line_number)
{
    (void)fprintf(stderr,
        "!!! ASSERTION FAILED — PROGRAM HALTED !!!\n"
        "File: %s\n"
        "Line: %d\n",
        file_path,
        (int)line_number);

    exit(1);
}

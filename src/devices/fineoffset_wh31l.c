/** @file
    Ambient Weather (Fine Offset) WH31L protocol.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol analysis by @MksRasp.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Ambient Weather (Fine Offset) WH31L protocol.
915 MHz FSK PCM Lightning-Strike sensor, based on AS3935 Franklin lightning sensor (FCC ID WA5WH57E).

Note that Ambient Weather is likely rebranded Fine Offset products.

56 us bit length with a preamble of 40 bit flips (0xaaaaaaaaaa) and a 0x2dd4 sync-word.
A transmission contains a single packet.

In the back of this device are 4 DIP switches
- sensitivity:  2 switches, 4 possible combinations
- short or long antenna 1 switch
- indoor or outdoor 1 switch

None of these DIP switches make any difference to the data.

Data layout:

    YY SI II II FF KK CC XX AA ?? ?

- Y: 8 bit fixed Type Code of 0x57
- S: 4 bit state indicator: maybe 0: start-up, 4: strike, 8: past-strikes
- I: 20 bit device ID
- F: 20 bit flags: (battery low seems to be the 2-bit on the first byte)
- K: 6 bit estimated distance to front of storm, 1 to 25 miles / 1 to 40 km, 63 is invalid/no strike
- C: 8 bit lightning strike count
- X: 8 bit CRC-8, poly 0x31, init 0x00
- A: 8 bit SUM-8

Example packets:

    {141} aa aa aa aa aa a2 dd 45 78 10 5c 80 58 10 1d f0 b8 10
    {140} aa aa aa aa aa a2 dd 45 78 10 5c 80 58 10 1d f0 b8 20
    {142} aa aa aa aa aa a2 dd 45 74 10 5c 80 5b f0 19 ac 44 08
    {143} aa aa aa aa aa a2 dd 45 74 10 5c 80 5b f0 19 ac 40 04

Some payloads:

    57 0 105c8 05 bf 00 dd c6
    57 8 105c8 05 81 01 df 0b
    57 4 105c8 05 bf 01 9a c4
    57 0 105c8 05 bf 00
    57 8 105c8 05 85 01
    57 8 20b90 0b 0a 02
    57 8 105c8 05 81 02

Raw flex decoder and BitBench format:

    rtl_433 -c 0 -R 0 -X "n=WH31L,m=FSK_PCM,s=56,l=56,r=1500,preamble=2dd4" -f 915M

    TYPE:8h STATE:4h ID:20h FLAGS:8b2b KM:6d COUNT:8d CRC:8h ADD:8h 16x

*/

#include "decoder.h"

static int fineoffset_wh31l_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0x2d, 0xd4}; // (partial) preamble and sync word

    int row = 0;
    // Search for preamble and sync-word
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, 24);
    // No preamble detected
    if (start_pos == bitbuffer->bits_per_row[row])
        return DECODE_ABORT_EARLY;
    if (decoder->verbose)
        fprintf(stderr, "%s: WH31L detected, buffer is %d bits length\n", __func__, bitbuffer->bits_per_row[row]);

    // Remove preamble and sync word, keep whole payload
    uint8_t b[9];
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + 24, b, 9 * 8);

    // Check type code
    if (b[0] != 0x57) {
        return DECODE_ABORT_EARLY;
    }

    // Validate checksums
    uint8_t c_crc = crc8(b, 8, 0x31, 0x00);
    if (c_crc) {
        if (decoder->verbose)
            fprintf(stderr, "%s: bad CRC\n", __func__);
        return DECODE_FAIL_MIC;
    }
    uint8_t c_sum = add_bytes(b, 8) - b[8];
    if (c_sum) {
        if (decoder->verbose)
            fprintf(stderr, "%s: bad SUM\n", __func__);
        return DECODE_FAIL_MIC;
    }

    int state      = (b[1] >> 4);
    int id         = ((b[1] & 0xf) << 16) | (b[2] << 8) | (b[3]);
    int flags      = (b[4] << 4) | (b[5] >> 4);
    int battery_ok = (b[4] & 0x02) == 0;
    int s_dist     = (b[5] & 0x3f);
    int s_count    = (b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "FineOffset-WH31L",
            "id" ,              "",                 DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_INT,    battery_ok,
            "state",            "State",            DATA_FORMAT, "%01x", DATA_INT,    state,
            "flags",            "Flags",            DATA_FORMAT, "%03x", DATA_INT,    flags,
            "storm_dist_km",    "Storm Dist",       DATA_COND, s_dist != 63, DATA_FORMAT, "%d km", DATA_INT,    s_dist,
            "strike_count",     "Strike Count",     DATA_INT,    s_count,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "state",
        "flags",
        "storm_dist_km",
        "strike_count",
        "mic",
        NULL,
};

r_device fineoffset_wh31l = {
        .name        = "Ambient Weather (Fine Offset) WH31L Lightning-Strike sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 56,
        .long_width  = 56,
        .reset_limit = 1000,
        .decode_fn   = &fineoffset_wh31l_decode,
        .fields      = output_fields,
};

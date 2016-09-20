/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Unicode.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#define EXPECT_EQ(a, b) if(a!=b) std::cout

void UTF8toUTF16ZeroLength() {
    ssize_t measured;

    const uint8_t str[] = { };

    measured = utf8_to_utf16_length(str, 0);
    EXPECT_EQ(0, measured)
            << "Zero length input should return zero length output.";
}

void UTF8toUTF16ASCIILength() {
    ssize_t measured;

    // U+0030 or ASCII '0'
    const uint8_t str[] = { 0x30 };

    measured = utf8_to_utf16_length(str, sizeof(str));
    EXPECT_EQ(1, measured)
            << "ASCII glyphs should have a length of 1 char_16_t";
}

void UTF8toUTF16Plane1Length() {
    ssize_t measured;

    // U+2323 SMILE
    const uint8_t str[] = { 0xE2, 0x8C, 0xA3 };

    measured = utf8_to_utf16_length(str, sizeof(str));
    EXPECT_EQ(1, measured)
            << "Plane 1 glyphs should have a length of 1 char_16_t";
}

void UTF8toUTF16SurrogateLength() {
    ssize_t measured;

    // U+10000
    const uint8_t str[] = { 0xF0, 0x90, 0x80, 0x80 };

    measured = utf8_to_utf16_length(str, sizeof(str));
    EXPECT_EQ(2, measured)
            << "Surrogate pairs should have a length of 2 char_16_t";
}

void UTF8toUTF16TruncatedUTF8() {
    ssize_t measured;

    // Truncated U+2323 SMILE
    // U+2323 SMILE
    const uint8_t str[] = { 0xE2, 0x8C };

    measured = utf8_to_utf16_length(str, sizeof(str));
    EXPECT_EQ(-1, measured)
            << "Truncated UTF-8 should return -1 to indicate invalid";
}

void trace_hex(char *f, int len)
{
    int i = 0;
    for(; i < len; i++)
    {
        printf("%02X", 0xff&f[i]);
    }
    printf("\n");
}

void UTF8toUTF16Normal() {
    const uint8_t str[] = {
        0x30, // U+0030, 1 UTF-16 character
        0xC4, 0x80, // U+0100, 1 UTF-16 character
        0xE2, 0x8C, 0xA3, // U+2323, 1 UTF-16 character
        0xF0, 0x90, 0x80, 0x80, // U+10000, 2 UTF-16 character
    };

    char_16_t output[1 + 1 + 1 + 2 + 1]; // Room for NULL

    utf8_to_utf16(str, sizeof(str), output);

    EXPECT_EQ(0x0030, output[0])
            << "should be U+0030";
    EXPECT_EQ(0x0100, output[1])
            << "should be U+0100";
    EXPECT_EQ(0x2323, output[2])
            << "should be U+2323";
    EXPECT_EQ(0xD800, output[3])
            << "should be first half of surrogate U+10000";
    EXPECT_EQ(0xDC00, output[4])
            << "should be second half of surrogate U+10000";
    EXPECT_EQ('\0', output[5])
            << "should be NULL terminated";
}

int main()
{
    UTF8toUTF16ZeroLength();
    UTF8toUTF16ASCIILength();
    UTF8toUTF16Plane1Length();
    UTF8toUTF16SurrogateLength();
    UTF8toUTF16TruncatedUTF8();
    UTF8toUTF16Normal();
    return 0;
}


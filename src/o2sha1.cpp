// o2sha1.cpp - compute web socket Sec-WebSocket-Accept code
//
// Roger B. Dannenberg
// Feb 2021

#include <cstdint>
#include <string.h>
#include <stdio.h>

const char BASE64CONVERT[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

static uint32_t hash[5];

static uint32_t left_rotate(int64_t n, int64_t b)
{
    return ((n << b) | (n >> (32 - b)));
}


void process_chunk(const char *chunk)
{
    // Process a chunk of data and return the new digest variables.
    // a chunk is a 64-byte string
    uint32_t w[80];
    memset(w, 0, sizeof(w));
    // Break chunk into sixteen 4-byte big-endian words w[i]
    for (int i = 0; i < 16; i++) {
        uint32_t s = 0;
        for (int j = 0; j < 4; j++) {
            s = (s << 8) + (chunk[(i * 4) + j] & 0xff);
            w[i] = s;
        }
    }
    // Extend the sixteen 4-byte words into eighty 4-byte words
    for (int i = 16; i < 80; i++) {
        w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    // Initialize hash value for this chunk
    uint32_t a = hash[0];
    uint32_t b = hash[1];
    uint32_t c = hash[2];
    uint32_t d = hash[3];
    uint32_t e = hash[4];
    uint32_t f;
    uint32_t k;
    for (int i = 0; i < 80; i++) {
        if (i <= 19) {
            // Use alternative 1 for f from FIPS PB 180-1
            f = d ^ (b & (c ^ d));
            k = 0x5A827999;
        } else if (i <= 39) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i <= 59) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {  // if (i <= 79) {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t_a = left_rotate(a, 5) + f + e + k + w[i];
        uint32_t t_b = a;
        uint32_t t_c = left_rotate(b, 30);
        uint32_t t_d = c;
        uint32_t t_e = d;
        a = t_a;
        b = t_b;
        c = t_c;
        d = t_d;
        e = t_e;
    }
    // Add this chunk's hash to result so far
    hash[0] = hash[0] + a;
    hash[1] = hash[1] + b;
    hash[2] = hash[2] + c;
    hash[3] = hash[3] + d;
    hash[4] = hash[4] + e;
}


void sha1_with_magic(char sha1[32], const char *key)
{
    hash[0] = 0x67452301;
    hash[1] = 0xEFCDAB89;
    hash[2] = 0x98BADCFE;
    hash[3] = 0x10325476;
    hash[4] = 0xC3D2E1F0;
    
    char message[128];
    const char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    sha1[0] = 0;
    int len = strlen(key) + sizeof(magic) - 1;
    if (len >= 64) {
        return; // fails because key is too large
    }
    strcpy(message, key);
    strcat(message, magic);

    // pre-processing
    message[len] = 128;
    for (int i = len + 1; i < 126; i++) message[i] = 0;
    int message_bit_length = len * 8;
    char t1 = message_bit_length >> 8;
    char t2 = message_bit_length & 0xFF;
    message[126] = t1;
    message[127] = t2;
    process_chunk(message);
    process_chunk(message + 64);

    char bytes_left_right[84];
    memset(bytes_left_right, 0, sizeof(bytes_left_right));
    for (int i = 0; i < 5; i++) {
        uint32_t h = hash[i];
        int shift = 30;
        for (int j = 0; j < 16; j++) {
            bytes_left_right[(i << 4) + j] = (h >> shift) & 3;
            shift -= 2;
        }
    }
    for (int i = 0, j = 0; i < 81; i += 3, j++) {
        int s = (bytes_left_right[i] << 4) + (bytes_left_right[i + 1] << 2) +
                bytes_left_right[i + 2];
        sha1[j] = BASE64CONVERT[s];
    }
    sha1[27] = '=';
    sha1[28] = 0;  // EOS
    printf("SHA1:\ninput string: %s\noutput_string: %s\n", key, sha1);
}

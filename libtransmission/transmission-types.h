/*
 * This file Copyright (C) Transmission authors and contributors
 *
 * It may be used under the 3-Clause BSD License, the GNU Public License v2,
 * or v3, or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <stdint.h> /* uintN_t */

typedef int8_t tr_priority;
typedef uint16_t tr_port;
typedef uint32_t tr_block_index_t;
typedef uint32_t tr_file_index_t;
typedef uint32_t tr_piece_index_t;
typedef uint32_t tr_tracker_tier_t;

struct tr_block_range_t
{
    tr_block_index_t first;
    tr_block_index_t last;
};

enum tr_preallocation_mode
{
    TR_PREALLOCATE_NONE = 0,
    TR_PREALLOCATE_SPARSE = 1,
    TR_PREALLOCATE_FULL = 2
};

enum tr_encryption_mode
{
    TR_CLEAR_PREFERRED,
    TR_ENCRYPTION_PREFERRED,
    TR_ENCRYPTION_REQUIRED
};

// https://www.bittorrent.org/beps/bep_0003.html
// A string of length 20 which this downloader uses as its id. Each
// downloader generates its own id at random at the start of a new
// download. This value will also almost certainly have to be escaped.
#define PEER_ID_LEN 20
typedef char tr_peer_id_t[PEER_ID_LEN];

#define TR_SHA1_DIGEST_LEN 20
typedef char tr_peer_digest_t[TR_SHA1_DIGEST_LEN];
typedef char tr_sha1_digest_string_t[TR_SHA1_DIGEST_LEN * 2 + 1];

/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <map>
#include <string>
#include <vector>

#include "transmission.h"

#include "quark.h"
#include "torrent-metainfo-public.h"
#include "tr-assert.h"

struct tr_torrent_metainfo
{
    bool parse(std::byte const* benc, size_t benc_len, tr_error** error = nullptr);

    std::string magnet() const;

    struct tracker_t
    {
        tr_quark announce_url;
        tr_quark scrape_url;
        tr_tracker_tier_t tier;

        tracker_t(tr_quark announce_in, tr_quark scrape_in, tr_tracker_tier_t tier_in)
            : announce_url{ announce_in }
            , scrape_url{ scrape_in }
            , tier{ tier_in }
        {
        }

        bool operator<(tracker_t const& that) const
        {
            return announce_url < that.announce_url;
        }
    };

    struct file_t
    {
        uint64_t size = 0; // size of the file, in bytes
        uint64_t offset = 0; // file begins at the torrent's nth byte
        tr_piece_index_t first_piece = 0; // we need pieces [first_piece...
        tr_piece_index_t final_piece = 0; // ...final_piece] to dl this file
        std::string path;
        bool is_renamed = false;

        file_t(std::string path_in, uint64_t size_in, bool is_renamed)
            : size{ size_in }
            , path{ std::move(path_in) }
            , is_renamed{ is_renamed }
        {
        }
    };

    std::string comment;
    std::string creator;
    std::string name;
    std::string source;

    std::multimap<tr_tracker_tier_t, tracker_t> trackers;
    std::vector<std::string> webseed_urls;
    std::vector<tr_sha1_digest_t> pieces;
    std::vector<file_t> files;
    std::vector<uint64_t> file_sizes;

    tr_sha1_digest_string_t info_hash_chars;
    tr_sha1_digest_t info_hash;

    std::string_view infoHashString() const
    {
        // trim one byte off the end because of zero termination
        return std::string_view{ std::data(info_hash_chars), std::size(info_hash_chars) - 1 };
    }

    uint64_t info_dict_size = 0;
    uint64_t info_dict_offset = 0;

    time_t time_created = 0;

    uint64_t total_size = 0;
    uint32_t piece_size = 0;
    tr_piece_index_t n_pieces = 0;

    bool is_private = true;
};

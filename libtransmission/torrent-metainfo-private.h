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

#include <optional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "transmission.h"

#include "torrent-metainfo.h"
#include "quark.h"

struct tr_torrent_metainfo
{
    bool parse(tr_variant* variant, tr_error** error = nullptr);

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
        tr_piece_index_t last_piece = 0; // ...last_piece] to dl this file
        std::string path;
        bool is_renamed = false;

        file_t(std::string const& path_in, uint64_t size_in, bool is_renamed)
            : size{ size_in }
            , path{ path_in }
            , is_renamed{ is_renamed }
        {
        }
    };

    struct info_dict_t // TODO: is this actually needed
    {
        uint64_t length;
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

    tr_sha1_digest_string_t info_hash_string;
    tr_sha1_digest_t info_hash;

    info_dict_t info_dict;

    time_t time_created;

    uint64_t size;
    uint32_t piece_size;
    tr_piece_index_t piece_count;

    bool is_private;
};

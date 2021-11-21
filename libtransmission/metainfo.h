/*
 * This file Copyright (C) 2005-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "quark.h"

using namespace std::literals;

struct tr_error;
struct tr_variant;

struct tr_magnet_metainfo
{
    bool parseMagnet(std::string_view magnet_link, tr_error** error = nullptr);

    std::string magnet() const;

    static bool convertAnnounceToScrape(std::string& setme, std::string_view announce_url);

    std::string_view infoHashString() const
    {
        // trim one byte off the end because of zero termination
        return std::string_view{ std::data(info_hash_chars), std::size(info_hash_chars) - 1 };
    }

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

    std::vector<std::string> webseed_urls;

    std::string name;

    std::multimap<tr_tracker_tier_t, tracker_t> trackers;

    tr_sha1_digest_string_t info_hash_chars;

    tr_sha1_digest_t info_hash;

    enum class FilenameFormat
    {
        NameAndParitalHash,
        FullHash
    };

    std::string makeFilename(std::string_view dirname, FilenameFormat format, std::string_view suffix) const;

protected:
    bool addTracker(tr_tracker_tier_t tier, std::string_view announce_url);
};

struct tr_torrent_metainfo : public tr_magnet_metainfo
{
    bool parseBenc(std::string_view benc, tr_error** error = nullptr);

    // Helper wrapper around parseBenc().
    //
    // If you're looping through several files, passing in a non-nullptr
    // `buffer` can reduce the number of memory allocations needed to
    // load multiple files.
    bool parseTorrentFile(std::string_view benc_filename, std::vector<char>* buffer = nullptr, tr_error** error = nullptr);

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
    std::string source;

    std::vector<tr_sha1_digest_t> pieces;
    std::vector<file_t> files;

    // Location of the bencoded info dict in the entire bencoded torrent data.
    // Used when loading pieces of it to sent to magnet peers.
    // See http://bittorrent.org/beps/bep_0009.html
    uint64_t info_dict_size = 0;
    uint64_t info_dict_offset = 0;

    // Location of the bencoded 'pieces' checksums in the entire bencoded
    // torrent data. Used when loading piece checksums on demand.
    uint64_t pieces_offset = 0;

    time_t time_created = 0;

    uint64_t total_size = 0;
    uint32_t piece_size = 0;
    tr_piece_index_t n_pieces = 0;

    bool is_private = true;
};

// FIXME(ckerr): move the rest of this file to a private header OR REMOVE
#if 0

void tr_metainfoRemoveSaved(tr_session const* session, tr_info const* info);

/** @brief Private function that's exposed here only for unit tests */
bool tr_metainfoAppendSanitizedPathComponent(std::string& out, std::string_view in, bool* is_adjusted);

// FIXME(ckerr): remove
struct tr_metainfo_parsed
{
    tr_info info = {};
    uint64_t info_dict_length = 0;
    std::vector<tr_sha1_digest_t> pieces;

    tr_metainfo_parsed() = default;

    tr_metainfo_parsed(tr_metainfo_parsed&& that) noexcept
    {
        std::swap(this->info, that.info);
        std::swap(this->pieces, that.pieces);
        std::swap(this->info_dict_length, that.info_dict_length);
    }

    tr_metainfo_parsed(tr_metainfo_parsed const&) = delete;

    tr_metainfo_parsed& operator=(tr_metainfo_parsed const&) = delete;

    ~tr_metainfo_parsed()
    {
        tr_metainfoFree(&info);
    }
};

// FIXME(ckerr): remove
std::optional<tr_metainfo_parsed> tr_metainfoParse(tr_session const* session, tr_variant const* variant, tr_error** error);

void tr_metainfoMigrateFile(
    tr_session const* session,
    tr_info const* info,
    tr_torrent_metainfo::FilenameFormat old_format,
    tr_torrent_metainfo::FilenameFormat new_format);
#endif

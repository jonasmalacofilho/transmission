/*
 * This file Copyright (C) Transmission authors and contributors
 *
 * It may be used under the 3-Clause BSD License, the GNU Public License v2,
 * or v3, or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "tr-macros.h"
#include "transmission.h"
#include "error.h"

struct tr_torrent_metainfo;

//// Lifecycle

struct tr_torrent_metainfo* tr_torrentMetainfoNewFromData(char const* data, size_t data_len, struct tr_error** error);

struct tr_torrent_metainfo* tr_torrentMetainfoNewFromFile(char const* filename, struct tr_error** error);

void tr_torrentMetainfoFree(struct tr_torrent_metainfo* tm);

//// Accessors

char* tr_torrentMetainfoMagnet(struct tr_torrent_metainfo const* tm);

/// Info

struct tr_torrent_metainfo_info
{
    char const* comment;
    char const* creator;
    char const* info_hash_string;
    char const* name;
    char const* source;

    time_t time_created;

    tr_sha1_digest_t info_hash;

    uint64_t total_size;
    tr_piece_index_t n_pieces;

    bool is_private;
};

struct tr_torrent_metainfo_info* tr_torrentMetainfoGet(
    struct tr_torrent_metainfo const* tm,
    struct tr_torrent_metainfo_info* setme);

/// Files

struct tr_torrent_metainfo_file_info
{
    // The path specified in the .torrent file, not the complete path to on the disk.
    // To get the complete path, prepend tr_torrentGetCurrentDir().
    char const* path;

    // size of the file, in bytes, when fully downloaded
    uint64_t size;
};

size_t tr_torrentMetainfoFileCount(struct tr_torrent_metainfo const* tm);

struct tr_torrent_metainfo_file_info* tr_torrentMetainfoFile(
    struct tr_torrent_metainfo const* tm,
    size_t nth,
    struct tr_torrent_metainfo_file_info* setme);

/// Trackers

struct tr_torrent_metainfo_tracker_info
{
    char const* announce_url;
    char const* scrape_url;
    tr_tracker_tier_t tier;
};

size_t tr_torrentMetainfoTrackerCount(struct tr_torrent_metainfo const* tm);

struct tr_torrent_metainfo_tracker_info* tr_torrentMetainfoTracker(
    struct tr_torrent_metainfo const* tm,
    size_t nth,
    struct tr_torrent_metainfo_tracker_info* setme);

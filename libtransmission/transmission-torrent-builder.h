/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "transmission.h"

struct tr_error;
struct tr_session;
struct tr_torrent_builder;
struct tr_torrent_metainfo;

tr_torrent_builder* tr_torrentBuilderNew(tr_session const* session, tr_torrent_metainfo* builder_takes_ownership);
tr_torrent* tr_torrentBuilderBuild(tr_torrent_builder* builder, tr_error** error);
void tr_torrentBuilderFree(tr_torrent_builder* builder);

// set what to do with a source .torrent file after the torrent is built.
void tr_torrentBuilderSetAddedFileIgnore(tr_torrent_builder* builder);
void tr_torrentBuilderSetAddedFileTrash(tr_torrent_builder* builder, void (*trash)(char const*));
void tr_torrentBuilderSetAddedRename(tr_torrent_builder* builder);

/**
 * Set how many peers this torrent can use simultaneously.
 *
 * - If not called. `tr_sessionGetPeerLimitPerTorrent()` is the default.
 *
 * - This function is used for new torrents. Pre-existing torrents being
 *   re-instantiated on session startup will already have a value for this
 *   in their .resume file.
 */
void tr_torrentBuilderSetPeerLimit(tr_torrent_builder* builder, uint16_t limit);

/**
 * Set the folder where the torrent will be downloaded.
 *
 * - If not called. `tr_sessionGetDownloadDir()` is the default.
 *
 * - This function is used for new torrents. Pre-existing torrents being
 *   re-instantiated on session startup will already have a value for this
 *   in their .resume file.
 */
void tr_torrentBuilderSetDownloadDir(tr_torrent_builder* builder, char const* directory);

/**
 * Set the folder where the torrent will be paused when added.
 *
 * - If not called. `tr_sessionGetPaused()` is the default.
 *
 * - This function is used for new torrents. Pre-existing torrents being
 *   re-instantiated on session startup will already have a value for this
 *   in their .resume file.
 */
void tr_torrentBuilderSetPaused(tr_torrent_builder* builder, bool paused);

/**
 * Forces the torrent to be paused when added, overriding any .resume setting.
 *
 * Useful when Transmission is started with `--paused` in the command line.
 */
void tr_torrentBuilderForcePaused(tr_torrent_builder* builder);

// don't use this.
void tr_torrentBuilderSetIncompleteDir(tr_torrent_builder* builder, char const* directory);

/** @brief Set the priorities for files in a torrent */
void tr_torrentBuilderSetFilePriorities(
    tr_torrent_builder* builder,
    tr_file_index_t const* files,
    tr_file_index_t file_count,
    tr_priority_t priority);

/** @brief Set the download flag for files in a torrent */
void tr_torrentBuilderSetFilesWanted(
    tr_torrent_builder* builder,
    tr_file_index_t const* file_indices,
    tr_file_index_t file_count,
    bool wanted);

/** @brief Set the torrent's bandwidth priority. */
void tr_torrentBuilderSetBandwidthPriority(tr_torrent_builder* builder, tr_priority_t priority);

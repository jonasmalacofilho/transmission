/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "error.h"
#include "error-types.h"
#include "file.h"
#include "session.h"
#include "torrent-metainfo.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

struct optional_args
{
    std::optional<bool> paused;
    std::optional<uint16_t> peer_limit;
    std::string download_dir;
};

/** Opaque class used when instantiating torrents.
 * @ingroup tr_ctor */
struct tr_ctor
{
    tr_session const* const session;
    bool saveInOurTorrentsDir = false;
    std::optional<bool> delete_source;

    tr_priority_t priority = TR_PRI_NORMAL;
    std::optional<tr_torrent_metainfo> tm;

    struct optional_args optional_args[2];

    std::string source_file;
    std::string incomplete_dir;

    std::vector<std::byte> contents;

    std::vector<tr_file_index_t> want;
    std::vector<tr_file_index_t> not_want;
    std::vector<tr_file_index_t> low;
    std::vector<tr_file_index_t> normal;
    std::vector<tr_file_index_t> high;

    explicit tr_ctor(tr_session const* session_in)
        : session{ session_in }
    {
    }
};

/***
****
***/

static void clearMetainfo(tr_ctor* ctor)
{
    ctor->tm.reset();
    ctor->source_file.clear();
}

char const* tr_ctorGetSourceFile(tr_ctor const* ctor)
{
    return ctor->source_file.c_str();
}

bool tr_ctorSetMetainfo(tr_ctor* ctor, void const* benc, size_t benc_len, tr_error** error)
{
    clearMetainfo(ctor);

    auto tm = tr_torrent_metainfo{};
    if (!tm.parseBenc(reinterpret_cast<std::byte const*>(benc), benc_len, error))
    {
        return false;
    }

    ctor->tm = std::move(tm);
    return true;
}

bool tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, char const* magnet_link, tr_error** error)
{
    if (magnet_link == nullptr)
    {
        tr_error_set_literal(error, TR_ERROR_EINVAL, "no magnet link specified");
        return false;
    }

    auto tm = tr_torrent_metainfo{};
    if (!tm.parseMagnet(magnet_link, error))
    {
        return false;
    }

    ctor->tm = std::move(tm);
    return true;
}

bool tr_ctorSetMetainfoFromFile(tr_ctor* ctor, char const* filename, tr_error** error)
{
    if (filename == nullptr)
    {
        tr_error_set_literal(error, TR_ERROR_EINVAL, "no file specified");
        return false;
    }

    auto const filename_sv = std::string_view{ filename };
    if (!tr_loadFile(ctor->contents, filename_sv, error))
    {
        return false;
    }

    if (!tr_ctorSetMetainfo(ctor, std::data(ctor->contents), std::size(ctor->contents), error))
    {
        return false;
    }

    ctor->source_file = filename_sv;

    // if no `name' field was set, then set it from the filename
    if (ctor->tm && std::empty(ctor->tm->name))
    {
        char* base = tr_sys_path_basename(filename, nullptr);
        ctor->tm->name = base;
        tr_free(base);
    }

    return true;
}

/***
****
***/

void tr_ctorSetFilePriorities(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t fileCount, tr_priority_t priority)
{
    switch (priority)
    {
    case TR_PRI_LOW:
        ctor->low.assign(files, files + fileCount);
        break;

    case TR_PRI_HIGH:
        ctor->high.assign(files, files + fileCount);
        break;

    default: // TR_PRI_NORMAL
        ctor->normal.assign(files, files + fileCount);
        break;
    }
}

void tr_ctorInitTorrentPriorities(tr_ctor const* ctor, tr_torrent* tor)
{
    for (auto file_index : ctor->low)
    {
        tr_torrentInitFilePriority(tor, file_index, TR_PRI_LOW);
    }

    for (auto file_index : ctor->normal)
    {
        tr_torrentInitFilePriority(tor, file_index, TR_PRI_NORMAL);
    }

    for (auto file_index : ctor->high)
    {
        tr_torrentInitFilePriority(tor, file_index, TR_PRI_HIGH);
    }
}

void tr_ctorSetFilesWanted(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t fileCount, bool wanted)
{
    auto& indices = wanted ? ctor->want : ctor->not_want;
    indices.assign(files, files + fileCount);
}

void tr_ctorInitTorrentWanted(tr_ctor const* ctor, tr_torrent* tor)
{
    tr_torrentInitFileDLs(tor, std::data(ctor->not_want), std::size(ctor->not_want), false);
    tr_torrentInitFileDLs(tor, std::data(ctor->want), std::size(ctor->want), true);
}

/***
****
***/

void tr_ctorSetDeleteSource(tr_ctor* ctor, bool delete_source)
{
    ctor->delete_source = delete_source;
}

bool tr_ctorGetDeleteSource(tr_ctor const* ctor, bool* setme)
{
    auto const& delete_source = ctor->delete_source;
    if (!delete_source)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = *delete_source;
    }

    return true;
}

/***
****
***/

void tr_ctorSetSave(tr_ctor* ctor, bool saveInOurTorrentsDir)
{
    ctor->saveInOurTorrentsDir = saveInOurTorrentsDir;
}

bool tr_ctorGetSave(tr_ctor const* ctor)
{
    return ctor != nullptr && ctor->saveInOurTorrentsDir;
}

void tr_ctorSetPaused(tr_ctor* ctor, tr_ctorMode mode, bool paused)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    ctor->optional_args[mode].paused = paused;
}

void tr_ctorSetPeerLimit(tr_ctor* ctor, tr_ctorMode mode, uint16_t peer_limit)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    ctor->optional_args[mode].peer_limit = peer_limit;
}

void tr_ctorSetDownloadDir(tr_ctor* ctor, tr_ctorMode mode, char const* directory)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    ctor->optional_args[mode].download_dir.assign(directory ? directory : "");
}

void tr_ctorSetIncompleteDir(tr_ctor* ctor, char const* directory)
{
    ctor->incomplete_dir.assign(directory ? directory : "");
}

bool tr_ctorGetPeerLimit(tr_ctor const* ctor, tr_ctorMode mode, uint16_t* setme)
{
    auto const& peer_limit = ctor->optional_args[mode].peer_limit;
    if (!peer_limit)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = *peer_limit;
    }

    return true;
}

bool tr_ctorGetPaused(tr_ctor const* ctor, tr_ctorMode mode, bool* setme)
{
    auto const& paused = ctor->optional_args[mode].paused;
    if (!paused)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = *paused;
    }

    return true;
}

bool tr_ctorGetDownloadDir(tr_ctor const* ctor, tr_ctorMode mode, char const** setme)
{
    auto const& str = ctor->optional_args[mode].download_dir;
    if (std::empty(str))
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = str.c_str();
    }

    return true;
}

bool tr_ctorGetIncompleteDir(tr_ctor const* ctor, char const** setme)
{
    auto const& str = ctor->incomplete_dir;
    if (std::empty(str))
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = str.c_str();
    }

    return true;
}

#if 0
bool tr_ctorGetMetainfo(tr_ctor const* ctor, tr_variant const** setme)
{
    if (!ctor->isSet_metainfo)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = &ctor->metainfo;
    }

    return true;
}
#endif

tr_session* tr_ctorGetSession(tr_ctor const* ctor)
{
    return const_cast<tr_session*>(ctor->session);
}

/***
****
***/

static bool isPriority(int i)
{
    return i == TR_PRI_LOW || i == TR_PRI_NORMAL || i == TR_PRI_HIGH;
}

void tr_ctorSetBandwidthPriority(tr_ctor* ctor, tr_priority_t priority)
{
    if (isPriority(priority))
    {
        ctor->priority = priority;
    }
}

tr_priority_t tr_ctorGetBandwidthPriority(tr_ctor const* ctor)
{
    return ctor->priority;
}

/***
****
***/

tr_ctor* tr_ctorNew(tr_session const* session)
{
    auto* const ctor = new tr_ctor{ session };

    if (session != nullptr)
    {
        tr_ctorSetDeleteSource(ctor, tr_sessionGetDeleteSource(session));
        tr_ctorSetPaused(ctor, TR_FALLBACK, tr_sessionGetPaused(session));
        tr_ctorSetPeerLimit(ctor, TR_FALLBACK, session->peerLimitPerTorrent);
        tr_ctorSetDownloadDir(ctor, TR_FALLBACK, tr_sessionGetDownloadDir(session));
    }

    tr_ctorSetSave(ctor, true);
    return ctor;
}

void tr_ctorFree(tr_ctor* ctor)
{
    clearMetainfo(ctor);
    delete ctor;
}

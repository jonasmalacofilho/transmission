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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "transmission.h"

#include "crypto-utils.h"
#include "error-types.h"
#include "error.h"
#include "file.h"
#include "metainfo.h"
#include "metainfo.h"
#include "platform.h"
#include "session.h"
#include "torrent-ctor.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

tr_ctor::tr_ctor(tr_session* session)
{
    setDeleteSource(session->deleteSourceTorrent);
    setDownloadDir(session->downloadDir());
    setPaused(tr_sessionGetPaused(session));
    setPeerLimit(session->peerLimitPerTorrent);
    setIncompleteDir(session->incompleteDir());
}

tr_torrent* tr_ctor::createTorrent(tr_session* session, tr_error** error)
{
    if (session == nullptr || !tm_)
    {
        tr_error_set_literal(error, EINVAL, "no session or no metainfo");
        return nullptr;
    }

    if (auto* dupe = session->torrent(tm_->info_hash); dupe != nullptr)
    {
        tr_error_set_literal(error, EEXIST, "duplicate torrent");
        return nullptr;
    }

    setPaused(TR_FALLBACK, tr_sessionGetPaused(session));
    setPeerLimit(TR_FALLBACK, session->peerLimitPerTorrent);
    setDownloadDir(TR_FALLBACK, tr_sessionGetDownloadDir(session));
    if (!delete_source_)
    {
        setDeleteSource(tr_sessionGetDeleteSource(session));
    }

    static int next_unique_id = 1;
    auto* const tor = new tr_torrent{};
    tor->metainfo = *tm_;
    tor->session = session;
    tor->session = session;
    tor->uniqueId = next_unique_id++;
    tor->queuePosition = tr_sessionCountTorrents(session);
    tor->dnd_pieces_ = tr_bitfield{ tor->info.pieceCount };
    tor->checked_pieces_ = tr_bitfield{ tor->info.pieceCount };
    tr_sha1(tor->obfuscatedHash, "req2", 4, tor->metainfo.info_hash, SHA_DIGEST_LENGTH, nullptr);

    auto download_dir = downloadDir(TR_FORCE);
    if (!download_dir)
    {
        download_dir = ctor->downloadDir(TR_FALLBACK);
    }
    if (download_dir)
    {
        tor->downloadDir = tr_strvDup(*download_dir);
    }

    if (tr_sessionIsIncompleteDirEnabled(session))
    {
        auto incomplete_dir = ctor->incompleteDir();
        if (!incomplete_dir)
        {
            incomplete_dir = tr_sessionGetIncompleteDir(session);
        }

        tor->incompleteDir = tr_strvDup(*incomplete_dir);
    }

    tor->bandwidth = new Bandwidth(session->bandwidth);

    tor->bandwidth->setPriority(ctor->bandwidthPriority());
    tor->error = TR_STAT_OK;
    tor->finishedSeedingByIdle = false;

    tr_peerMgrAddTorrent(session->peerMgr, tor);

    TR_ASSERT(tor->downloadedCur == 0);
    TR_ASSERT(tor->uploadedCur == 0);

    tr_torrentSetDateAdded(tor, tr_time()); /* this is a default value to be overwritten by the resume file */

    torrentInitFromInfo(tor);

    // tr_torrentLoadResume() calls a lot of tr_torrentSetFoo() methods
    // that set things as dirty, but... these settings being loaded are
    // the same ones that would be saved back again, so don't let them
    // affect the 'is dirty' flag.
    auto const was_dirty = tor->isDirty;
    bool didRenameResumeFileToHashOnlyName = false;
    auto const loaded = tr_torrentLoadResume(tor, ~(uint64_t)0, ctor, &didRenameResumeFileToHashOnlyName);
    tor->isDirty = was_dirty;

    if (didRenameResumeFileToHashOnlyName)
    {
        /* Rename torrent file as well */
        >> tr_metainfoMigrateFile(session, &tor->info, TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH, TR_METAINFO_BASENAME_HASH);
    }

    tor->completeness = tr_cpGetStatus(&tor->completion);
    setLocalErrorIfFilesDisappeared(tor);

    // TODO(ckerr): this paragraph is awkward because it's
    // trying to fit a new workflow into old code.
    // can tr_torrentInitFilePriority, setFileWanted be udpated to this flow?
    for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
    {
        tr_torrentInitFilePriority(tor, i, tor->info.files[i].priority);
        setFileWanted(tor, i, !tor->info.files[i].dnd);
    }
    tr_cpInvalidateDND(&tor->completion);

    refreshCurrentDir(tor);

    bool const doStart = tor->isRunning;
    tor->isRunning = false;
    if ((loaded & TR_FR_SPEEDLIMIT) == 0)
    {
        tr_torrentUseSpeedLimit(tor, TR_UP, false);
        tr_torrentSetSpeedLimit_Bps(tor, TR_UP, tr_sessionGetSpeedLimit_Bps(tor->session, TR_UP));
        tr_torrentUseSpeedLimit(tor, TR_DOWN, false);
        tr_torrentSetSpeedLimit_Bps(tor, TR_DOWN, tr_sessionGetSpeedLimit_Bps(tor->session, TR_DOWN));
        tr_torrentUseSessionLimits(tor, true);
    }

    if ((loaded & TR_FR_RATIOLIMIT) == 0)
    {
        tr_torrentSetRatioMode(tor, TR_RATIOLIMIT_GLOBAL);
        tr_torrentSetRatioLimit(tor, tr_sessionGetRatioLimit(tor->session));
    }

    if ((loaded & TR_FR_IDLELIMIT) == 0)
    {
        tr_torrentSetIdleMode(tor, TR_IDLELIMIT_GLOBAL);
        tr_torrentSetIdleLimit(tor, tr_sessionGetIdleLimit(tor->session));
    }

    tr_sessionAddTorrent(session, tor);

    /* if we don't have a local .torrent file already, assume the torrent is new */
    bool const isNewTorrent = !tr_sys_path_exists(tor->info.torrent, nullptr);

    /* maybe save our own copy of the metainfo */
    std::cerr << __FILE__ << ':' << __LINE__ << " source file [" << ctor->sourceFile() << ']' << std::endl;
    std::cerr << __FILE__ << ':' << __LINE__ << " torrent dir [" << session->torrentDir << ']' << std::endl;
    if (!tr_strvStartsWith(ctor->sourceFile(), session->torrentDir))
    {
        tr_error* error = nullptr;
        std::cerr << __FILE__ << ':' << __LINE__ << " saving our own copy" << std::endl;
        if (!tr_saveFile(ctor->contents(), tor->info.torrent, &error))
        {
            std::cerr << __FILE__ << ':' << __LINE__ << " " << error->message << ' ' << error->code << std::endl;
            tr_torrentSetLocalError(tor, "Unable to save torrent file: %s (%d)", error->message, error->code);
        }
        tr_error_clear(&error);
    }

    tor->tiers = tr_announcerAddTorrent(tor, onTrackerResponse, nullptr);

    if (isNewTorrent)
    {
        if (tr_torrentHasMetadata(tor))
        {
            callScriptIfEnabled(tor, TR_SCRIPT_ON_TORRENT_ADDED);
        }

        if (!tr_torrentHasMetadata(tor) && !doStart)
        {
            tor->prefetchMagnetMetadata = true;
            tr_torrentStartNow(tor);
        }
        else
        {
            tor->startAfterVerify = doStart;
            tr_torrentVerify(tor, nullptr, nullptr);
        }
    }
    else if (doStart)
    {
        tr_torrentStart(tor);
    }
}
}
}

void tr_ctor::clearMetainfo()
{
    tm_.reset();
    source_file_.clear();
}

///

bool tr_ctor::setMetainfo(std::string_view benc, tr_error** error)
{
    clearMetainfo();

    auto tm = tr_torrent_metainfo{};
    if (!tm.parseBenc(benc, error))
    {
        return false;
    }

    tm_ = std::move(tm);
    return true;
}

bool tr_ctor::setMetainfoFromMagnetLink(std::string_view magnet_link, tr_error** error)
{
    auto tm = tr_torrent_metainfo{};
    if (!tm.parseMagnet(magnet_link, error))
    {
        return false;
    }

    tm_ = std::move(tm);
    return true;
}

bool tr_ctor::setMetainfoFromFile(char const* filename, tr_error** error)
{
    if (!tr_loadFile(contents_, filename, error))
    {
        return false;
    }

    if (!setMetainfo(std::string_view{ std::data(contents_), std::size(contents_) }, error))
    {
        return false;
    }

    source_file_ = filename;

    // if no `name' field was set, then set it from the filename
    if (tm_ && std::empty(tm_->name))
    {
        char* base = tr_sys_path_basename(filename, nullptr);
        tm_->name = base;
        tr_free(base);
    }

    return true;
}

///

void tr_ctor::setFilePriorities(tr_file_index_t const* files, tr_file_index_t fileCount, tr_priority_t priority)
{
    auto const* walk = files;
    auto const* const end = walk + fileCount;
    for (; walk != end; ++walk)
    {
        priorities_[*walk] = priority;
    }
}

void tr_ctor::setFilesWanted(tr_file_index_t const* files, tr_file_index_t fileCount, bool wanted)
{
    auto const* walk = files;
    auto const* const end = walk + fileCount;
    for (; walk != end; ++walk)
    {
        if (wanted)
        {
            not_wanted_.erase(*walk);
        }
        else
        {
            not_wanted_.insert(*walk);
        }
    }
}

///

void tr_ctor::setDeleteSource(bool delete_source)
{
    delete_source_ = delete_source;
}

bool tr_ctor::getDeleteSource() const
{
    return delete_source_ && *delete_source_;
}

/***
****
***/

void tr_ctor::setPaused(tr_ctorMode mode, bool paused)
{
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    optional_args_[mode].paused = paused;
}

void tr_ctor::setPeerLimit(tr_ctorMode mode, uint16_t peer_limit)
{
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    optional_args_[mode].peer_limit = peer_limit;
}

void tr_ctor::setDownloadDir(tr_ctorMode mode, char const* directory)
{
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    optional_args_[mode].download_dir.assign(directory ? directory : "");
}

void tr_ctor::setIncompleteDir(std::string_view directory)
{
    incomplete_dir_.assign(directory);
}

std::optional<uint16_t> tr_ctor::peerLimit(tr_ctorMode mode) const
{
    return optional_args_[mode].peer_limit;
}

std::optional<bool> tr_ctor::paused(tr_ctorMode mode) const
{
    return optional_args_[mode].paused;
}

std::optional<std::string_view> tr_ctor::downloadDir(tr_ctorMode mode) const
{
    return optional_args_[mode].download_dir;
}

std::optional<std::string_view> tr_ctor::incompleteDir() const
{
    if (std::empty(incomplete_dir_))
    {
        return {};
    }

    return incomplete_dir_;
}

#if 0
bool tr_ctor::getInfo(tr_info& setme, tr_error** error) const
{
    if (!ctor->tm_)
    {
        tr_error_set_literal(error, ENODATA, "No metadata to get");
        return false;
    }

    auto const& src = *this->tm_;
    auto& tgt = setme;

    tgt.comment = tr_strvDup(src.comment);
    tgt.creator = tr_strvDup(src.creator);
    tgt.dateCreated = src.time_created;
    tgt.isFolder = std::size(src.files) != 1;
    tgt.isPrivate = src.is_private;
    tgt.name = tr_strvDup(src.name);
    tgt.originalName = tr_strvDup(src.name);
    tgt.pieceCount = src.n_pieces;
    tgt.pieceSize = src.piece_size;
    tgt.source = tr_strvDup(src.source);
    tgt.totalSize = src.total_size;

    std::copy_n(std::data(src.info_hash), std::size(src.info_hash), reinterpret_cast<std::byte*>(tgt.hash));

    auto const hashstr = src.infoHashString();
    std::copy(std::begin(hashstr), std::end(hashstr), tgt.hashString);

    tgt.webseedCount = std::size(src.webseed_urls);
    tgt.webseeds = tr_new(char*, tgt.webseedCount);
    std::transform(
        std::begin(src.webseed_urls),
        std::end(src.webseed_urls),
        tgt.webseeds,
        [](auto const& url) { return tr_strvDup(url); });

    int tracker_id = 0;
    tgt.trackerCount = std::size(src.trackers);
    tgt.trackers = tr_new(tr_tracker_info, tgt.trackerCount);
    std::transform(
        std::begin(src.trackers),
        std::end(src.trackers),
        tgt.trackers,
        [&tracker_id](auto const& it)
        {
            auto info = tr_tracker_info{};
            info.tier = it.first;
            info.announce = tr_strvDup(tr_quark_get_string_view(it.second.announce_url));
            info.scrape = tr_strvDup(tr_quark_get_string_view(it.second.scrape_url));
            info.id = ++tracker_id;
            return info;
        });

    tgt.fileCount = std::size(src.files);
    tgt.files = tr_new(tr_file, tgt.fileCount);
    for (size_t i = 0, n = tgt.fileCount; i < n; ++i)
    {
        auto& in = src.files[i];
        auto& out = tgt.files[i];
        out.mtime = 0;
        out.length = in.size;
        out.name = tr_strvDup(in.path);
        out.firstPiece = in.first_piece;
        out.lastPiece = in.final_piece;
        out.is_renamed = in.is_renamed;
        out.dnd = this->not_wanted_.count(i) != 0;
        auto const it = this->priorities_.find(i);
        out.priority = it == std::end(this->priorities_) ? TR_PRI_NORMAL : it->second;
    }


        enum class FilenameFormat
    {
        NameAndParitalHash,
        FullHash
    };

    std::string makeFilename(std::string_view dirname, FilenameFormat format, std::string_view suffix) const;

    tgt.torrent = session_ != nullptr ?
        tr_strvDup(tr_buildTorrentFilename(tr_getTorrentDir(session_), &tgt, TR_METAINFO_BASENAME_HASH, ".torrent"sv)) :
        nullptr;

    return true;
}
#endif

tr_session* tr_ctor::session() const
{
    return session_;
}

void tr_ctor::setBandwidthPriority(tr_priority_t priority)
{
    if (isPriority(priority))
    {
        priority_ = priority;
    }
}

tr_priority_t tr_ctor::bandwidthPriority() const
{
    return priority_;
}

std::string_view tr_ctor::contents() const
{
    return { std::data(contents_), std::size(contents_) };
}

std::string_view tr_ctor::sourceFile() const
{
    return source_file_;
}

bool tr_ctor::isPriority(int i)
{
    return i == TR_PRI_LOW || i == TR_PRI_NORMAL || i == TR_PRI_HIGH;
}

std::optional<tr_torrent_metainfo> metainfo() const
{
    return tm_;
}

/**
***  C Bindings
**/

tr_ctor* tr_ctorNew()
{
    return new tr_ctor();
}

void tr_ctorFree(tr_ctor* ctor)
{
    delete ctor;
}

void tr_ctorSetDeleteSource(tr_ctor* ctor, bool do_delete)
{
    ctor->setDeleteSource(do_delete);
}

bool tr_ctorSetMetainfo(tr_ctor* ctor, void const* benc, size_t benc_len, tr_error** error)
{
    auto benc_sv = std::string_view{ static_cast<char const*>(benc), benc_len };
    return ctor->setMetainfo(benc_sv, error);
}

bool tr_ctorSetMetainfoFromFile(tr_ctor* ctor, char const* filename, tr_error** error)
{
    return ctor->setMetainfoFromFile(filename, error);
}

bool tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, char const* magnet_link, tr_error** error)
{
    if (magnet_link == nullptr)
    {
        tr_error_set_literal(error, EINVAL, "null argument");
        return false;
    }

    return ctor->setMetainfoFromMagnetLink(magnet_link, error);
}

void tr_ctorSetPeerLimit(tr_ctor* ctor, tr_ctorMode mode, uint16_t limit)
{
    ctor->setPeerLimit(mode, limit);
}

void tr_ctorSetDownloadDir(tr_ctor* ctor, tr_ctorMode mode, char const* directory)
{
    ctor->setDownloadDir(mode, directory);
}

void tr_ctorSetIncompleteDir(tr_ctor* ctor, char const* directory)
{
    ctor->setIncompleteDir(directory);
}

void tr_ctorSetPaused(tr_ctor* ctor, tr_ctorMode mode, bool is_paused)
{
    ctor->setPaused(mode, is_paused);
}

void tr_ctorSetFilePriorities(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t file_count, tr_priority_t priority)
{
    ctor->setFilePriorities(files, file_count, priority);
}

void tr_ctorSetFilesWanted(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t file_count, bool wanted)
{
    ctor->setFilesWanted(files, file_count, wanted);
}

bool tr_ctorIsMetainfoValid(tr_ctor const* ctor)
{
    return !!ctor->metainfo();
}

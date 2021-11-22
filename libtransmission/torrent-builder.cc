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
#include "peer-mgr.h"
#include "platform.h"
#include "resume.h"
#include "session.h"
#include "torrent-builder.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

static int next_unique_id = 1;

tr_torrent_builder::tr_torrent_builder(tr_session* session, tr_torrent_metainfo*&& metainfo)
    : session_{ session }
{
    tor_ = new tr_torrent{};
    tor_->initBlockInfo(metainfo_->total_size, metainfo_->piece_size);
    tor_->addedDate = tr_time();
    tor_->anyDate = tr_time();
    tor_->bandwidth = new Bandwidth();
    tor_->bandwidth->setPriority(TR_PRI_NORMAL);
    tor_->checked_pieces_ = tr_bitfield{ metainfo_->n_pieces };
    tor_->dnd_pieces_ = tr_bitfield{ metainfo_->n_pieces };
    tor_->downloadDir = tr_strvDup(session->downloadDir());
    tor_->file_settings_.resize(std::size(tor_->metainfo.files));
    tor_->incompleteDir = session_->useIncompleteDir() ? tr_strvDup(session->incompleteDir()) : nullptr;
    tor_->isRunning = !tr_sessionGetPaused(session_);
    tor_->maxConnectedPeers = session->peerLimitPerTorrent;
    tor_->metainfo = *metainfo_;
    tor_->queuePosition = tr_sessionCountTorrents(session_);
    tor_->session = session_;
    tor_->uniqueId = next_unique_id++;
    tr_sha1(tor_->obfuscatedHash, "req2", 4, tor_->metainfo.info_hash, SHA_DIGEST_LENGTH, nullptr);
    tr_cpConstruct(&tor_->completion, tor_);
    tr_torrentSetIdleLimit(tor_, tr_sessionGetIdleLimit(tor_->session));
    tr_torrentSetIdleMode(tor_, TR_IDLELIMIT_GLOBAL);
    tr_torrentSetRatioLimit(tor_, tr_sessionGetRatioLimit(tor_->session));
    tr_torrentSetRatioMode(tor_, TR_RATIOLIMIT_GLOBAL);
    tr_torrentSetSpeedLimit_Bps(tor_, TR_DOWN, tr_sessionGetSpeedLimit_Bps(tor_->session, TR_DOWN));
    tr_torrentSetSpeedLimit_Bps(tor_, TR_UP, tr_sessionGetSpeedLimit_Bps(tor_->session, TR_UP));
    tr_torrentUseSessionLimits(tor_, true);
    tr_torrentUseSpeedLimit(tor_, TR_DOWN, false);
    tr_torrentUseSpeedLimit(tor_, TR_UP, false);

    delete metainfo;
}

void tr_torrent_builder::forcePaused()
{
    force_paused_ = true;
    tor_->isRunning = false;
}

void tr_torrent_builder::setBandwidthPriority(tr_priority_t priority)
{
    tor_->bandwidth->setPriority(priority);
}

void tr_torrent_builder::setDownloadDir(std::string_view directory)
{
    // TODO(ckerr): add tr_torrent::setDownloadDir(std::string_view)
    auto const sz_directory = std::string{ directory };
    tr_torrentSetDownloadDir(tor_, sz_directory.c_str());
}

void tr_torrent_builder::setFilePriorities(tr_file_index_t const* files, tr_file_index_t file_count, tr_priority_t priority)
{
    tr_torrentSetFilePriorities(tor_, files, file_count, priority);
}

void tr_torrent_builder::setFilesWanted(tr_file_index_t const* files, tr_file_index_t file_count, bool wanted)
{
    tr_torrentSetFileDLs(tor_, files, file_count, wanted);
}

void tr_torrent_builder::setPaused(bool paused)
{
    tor_->isRunning = !paused;
}

void tr_torrent_builder::setPeerLimit(uint16_t limit)
{
    tr_torrentSetPeerLimit(tor_, limit);
}

void tr_torrent_builder::manageSourceFile(tr_error** error) const
{
    if (std::empty(metainfo_->source_filename))
    {
        return;
    }

    auto tmpstr = std::string{};

    switch (added_file_action_)
    {
    case AddedFile::Trash:
        trash_func_(metainfo_->source_filename.c_str());
        break;

    case AddedFile::Rename:
        tmpstr = tr_strvJoin(metainfo_->source_filename, ".added"sv);
        tr_sys_path_rename(metainfo_->source_filename.c_str(), tmpstr.c_str(), error);
        break;

    case AddedFile::Ignore:
        // no-op
        break;
    }
}

tr_torrent* tr_torrent_builder::build(tr_error** error) const
{
    if (auto* dupe = session_->torrent(metainfo_->info_hash); dupe != nullptr)
    {
        tr_error_set_literal(error, EEXIST, "duplicate torrent");
        return nullptr;
    }

    manageSourceFile(error);

    tor_->bandwidth->setParent(tor_->session->bandwidth);
    tr_peerMgrAddTorrent(session_->peerMgr, tor_);

    // tr_torrentLoadResume() calls a lot of tr_torrentSetFoo() methods
    // that set things as dirty, but... these settings being loaded are
    // the same ones that would be saved back again, so don't let them
    // affect the 'is dirty' flag.
    auto const was_dirty = tor_->isDirty;
    bool resume_file_was_migrated = false;
    auto const loaded = tr_torrentLoadResume(tor_, ~(uint64_t)0, &resume_file_was_migrated);
    tor_->isDirty = was_dirty;

    if (force_paused_)
    {
        tor_->isRunning = false;
    }

#if 0
    // FIXME(ckerr)
    if (resume_file_was_migrated)
    {
        tr_metainfoMigrateFile(session, &tor->info, TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH, TR_METAINFO_BASENAME_HASH);
    }
#endif
    tor_->completeness = tr_cpGetStatus(&tor_->completion);
    // FIXME(ckerr)
    // setLocalErrorIfFilesDisappeared(tor);

    for (tr_file_index_t i = 0, n = std::size(tor->metainfo.files); i < n; ++i)
    {
        setFileWanted(tor, i, !tor->file_settings_[i].dnd);
        tr_torrentInitFilePriority(tor, i, tor->file_settings_[i].priority);
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

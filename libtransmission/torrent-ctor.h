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

#include "error-types.h"
#include "error.h"
#include "file.h"
#include "metainfo.h"
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

struct tr_ctor
{
public:
    explicit tr_ctor(tr_session* session);

    bool setMetainfo(std::string_view benc, tr_error** error = nullptr);
    bool setMetainfoFromFile(char const* filename, tr_error** error = nullptr);
    bool setMetainfoFromMagnetLink(std::string_view magnet_link, tr_error** error = nullptr);

    tr_torrent* createTorrent(tr_session* session, tr_error** error = nullptr);

    void setBandwidthPriority(tr_priority_t priority);
    void setDeleteSource(bool delete_source);
    void setDownloadDir(std::string_view directory);
    void setFilePriorities(tr_file_index_t const* files, tr_file_index_t fileCount, tr_priority_t priority);
    void setFilesWanted(tr_file_index_t const* files, tr_file_index_t fileCount, bool wanted);
    void setIncompleteDir(std::string_view directory);
    void setPaused(bool paused);
    void setPeerLimit(uint16_t peer_limit);

    bool getDeleteSource() const;
    bool paused() const;
    uint16_t peerLimit() const;
    std::string_view downloadDir() const;

    std::string_view incompleteDir() const;
    std::optional<tr_torrent_metainfo> metainfo() const;
    std::string_view contents() const;
    std::string_view sourceFile() const;
    tr_priority_t bandwidthPriority() const;
    tr_session* session() const;
    //bool info(tr_info& setme, tr_error** error = nullptr) const;

private:
    tr_session* const session_;

    static bool isPriority(int i);
    void clearMetainfo();

    bool paused_ = false;
    uint16_t peer_limit_ = 50;
    std::string download_dir_;

    bool delete_source_ = false;

    tr_priority_t priority_ = TR_PRI_NORMAL;
    std::optional<tr_torrent_metainfo> tm_;

    std::unordered_set<tr_file_index_t> not_wanted_;
    std::unordered_map<tr_file_index_t, tr_priority_t> priorities_;

    std::vector<char> contents_;

    std::string source_file_;
    std::string incomplete_dir_;
};

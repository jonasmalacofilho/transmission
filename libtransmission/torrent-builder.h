/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "transmission.h"

struct tr_torrent;
struct tr_error;

struct tr_torrent_builder
{
public:
    using trash_func_t = void (*)(char const*);

    tr_torrent_builder(tr_session* session, tr_torrent_metainfo*&& metainfo);
    tr_torrent* build(tr_error** error = nullptr) const;

    void setAddedFileIgnore();
    void setAddedFileTrash(trash_func_t func);
    void setAddedRename();

    void forcePaused();
    void setBandwidthPriority(tr_priority_t priority);
    void setDownloadDir(std::string_view directory);
    void setFilePriorities(tr_file_index_t const* files, tr_file_index_t file_count, tr_priority_t priority);
    void setFilesWanted(tr_file_index_t const* file_indices, tr_file_index_t file_count, bool wanted);
    void setPaused(bool paused);
    void setPeerLimit(uint16_t limit);

    // don't use this.
    void setIncompleteDir(std::string_view directory);

private:
private:
    static bool isPriority(int i);
    void clearMetainfo();
    void manageSourceFile(tr_error** error) const;

    tr_session* const session_;
    tr_torrent* tor_ = nullptr;

    enum class AddedFile
    {
        Ignore,
        Trash,
        Rename
    };
    trash_func_t trash_func_ = nullptr;
    AddedFile added_file_action_ = AddedFile::Ignore;

    bool paused_ = false;
    bool force_paused_ = false;
    uint16_t peer_limit_ = 50;
    std::string download_dir_;

    bool delete_source_ = false;

    tr_priority_t priority_ = TR_PRI_NORMAL;
    std::unique_ptr<tr_torrent_metainfo> metainfo_;

    std::unordered_set<tr_file_index_t> not_wanted_;
    std::unordered_map<tr_file_index_t, tr_priority_t> priorities_;

    std::vector<char> contents_;

    std::string source_file_;
    std::string incomplete_dir_;
};

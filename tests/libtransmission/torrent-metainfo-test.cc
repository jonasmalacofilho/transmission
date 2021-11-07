/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string>
#include <string_view>

#include "transmission.h"

#include "crypto-utils.h"
#include "error-types.h"
#include "torrent-metainfo.h"

#include <gtest/gtest.h>

using namespace std::literals;

auto constexpr AssetsPath = std::string_view{ LIBTRANSMISSION_TEST_ASSETS_DIR };

TEST(TorrentMetainfoTest, FailsAndSetsErrorIfBadFile)
{
    tr_error* error = nullptr;
    auto const filename = std::string{ AssetsPath } + std::string{ "/this-file-does-not-exist.torrent" };
    EXPECT_EQ(nullptr, tr_torrentMetainfoNewFromFile(filename.c_str(), &error));
    EXPECT_NE(nullptr, error);
    EXPECT_TRUE(TR_ERROR_IS_ENOENT(error->code));
    tr_error_clear(&error);
}

TEST(TorrentMetainfoTest, FailsAndErrorIfBadBencData)
{
    tr_error* error = nullptr;
    auto const filename = std::string{ AssetsPath } + std::string{ "/corrupt-benc.torrent" };
    EXPECT_EQ(nullptr, tr_torrentMetainfoNewFromFile(filename.c_str(), &error));
    EXPECT_NE(nullptr, error);
    EXPECT_EQ(TR_ERROR_EINVAL, error->code);
    tr_error_clear(&error);
}

TEST(TorrentMetainfoTest, FailsAndErrorIfNoInfoDict)
{
    tr_error* error = nullptr;
    auto const filename = std::string{ AssetsPath } + std::string{ "/no-info-dict.torrent" };
    EXPECT_EQ(nullptr, tr_torrentMetainfoNewFromFile(filename.c_str(), &error));
    EXPECT_NE(nullptr, error);
    EXPECT_EQ(TR_ERROR_EINVAL, error->code);
    tr_error_clear(&error);
}

TEST(TorrentMetainfoTest, FailsAndErrorIfNoNameInInfoDict)
{
}

TEST(TorrentMetainfoTest, PieceLength)
{
}

TEST(TorrentMetainfoTest, ParsesName)
{
    // prefers name.utf-8
}

TEST(TorrentMetainfoTest, ParsesPrivateFlag)
{
}

TEST(TorrentMetainfoTest, ParsesSourceFromInfoDict)
{
    tr_error* error = nullptr;
    auto info = tr_torrent_metainfo_info{};
    auto const filename = std::string{ AssetsPath } + std::string{ "/source-in-info.torrent" };
    auto* const tm = tr_torrentMetainfoNewFromFile(filename.c_str(), &error);

    EXPECT_EQ(nullptr, error);
    EXPECT_EQ(&info, tr_torrentMetainfoGet(tm, &info));
    EXPECT_EQ("txt"sv, std::string_view{ info.source });

    tr_torrentMetainfoFree(tm);
}

TEST(TorrentMetainfoTest, ParsesSourceFromTop)
{
    // same as ParsesSourceFromInfoDict, but the 'source' key is in the top-level dict

    tr_error* error = nullptr;
    auto info = tr_torrent_metainfo_info{};
    auto const filename = std::string{ AssetsPath } + std::string{ "/source-in-top.torrent" };
    auto* const tm = tr_torrentMetainfoNewFromFile(filename.c_str(), &error);

    EXPECT_EQ(nullptr, error);
    EXPECT_EQ(&info, tr_torrentMetainfoGet(tm, &info));
    EXPECT_EQ("txt"sv, std::string_view{ info.source });

    tr_torrentMetainfoFree(tm);
}

TEST(TorrentMetainfoTest, SingleFile)
{
    tr_error* error = nullptr;
    auto const filename = std::string{ AssetsPath } + std::string{ "/single-file.torrent" };
    auto* const tm = tr_torrentMetainfoNewFromFile(filename.c_str(), &error);
    EXPECT_EQ(nullptr, error);

    auto info = tr_torrent_metainfo_info{};
    EXPECT_EQ(&info, tr_torrentMetainfoGet(tm, &info));
    EXPECT_EQ(""sv, info.comment);
    EXPECT_EQ("Transmission/3.00 (bb6b5a062e)"sv, info.creator);
    EXPECT_EQ("8634b6345eceddb0c605af1ec6108ba3008127de"sv, info.info_hash_string);
    auto info_hash = tr_sha1_digest_t{};
    tr_hex_to_sha1(std::data(info_hash), info.info_hash_string);
    EXPECT_EQ(info_hash, info.info_hash);
    EXPECT_EQ(true, info.is_private);
    EXPECT_EQ("hello.txt"sv, info.name);
    EXPECT_EQ(1, info.n_pieces);
    EXPECT_EQ(1636238372, info.time_created);
    EXPECT_EQ(6, info.total_size);

    auto tracker_info = tr_torrent_metainfo_tracker_info{};
    EXPECT_EQ(1, tr_torrentMetainfoTrackerCount(tm));
    EXPECT_EQ(&tracker_info, tr_torrentMetainfoTracker(tm, 0, &tracker_info));
    EXPECT_EQ("http://example.org/announce"sv, tracker_info.announce_url);
    EXPECT_EQ("http://example.org/scrape"sv, tracker_info.scrape_url);
    EXPECT_EQ(0, tracker_info.tier);

    auto file_info = tr_torrent_metainfo_file_info{};
    EXPECT_EQ(1, tr_torrentMetainfoFileCount(tm));
    EXPECT_EQ(&file_info, tr_torrentMetainfoFile(tm, 0, &file_info));
    EXPECT_EQ("hello.txt"sv, std::string_view{ file_info.path });
    EXPECT_EQ(6, file_info.size);
    EXPECT_EQ(0, tracker_info.tier);

    tr_torrentMetainfoFree(tm);
}

TEST(TorrentMetainfoTest, CreationDateIsOptional)
{
    // this torrent is like 'single-file.torrent' but has no creation date

    tr_error* error = nullptr;
    auto const filename = std::string{ AssetsPath } + std::string{ "/no-creation-date.torrent" };
    auto* const tm = tr_torrentMetainfoNewFromFile(filename.c_str(), &error);
    EXPECT_EQ(nullptr, error);

    auto info = tr_torrent_metainfo_info{};
    EXPECT_EQ(&info, tr_torrentMetainfoGet(tm, &info));
    EXPECT_EQ("8634b6345eceddb0c605af1ec6108ba3008127de"sv, info.info_hash_string);
    EXPECT_EQ("hello.txt"sv, info.name);
    EXPECT_EQ(0, info.time_created);
    EXPECT_EQ(1, info.n_pieces);
    EXPECT_EQ(6, info.total_size);

    tr_torrentMetainfoFree(tm);
}

TEST(TorrentMetainfoTest, ChecksPieceCount)
{
    // this torrent is like 'single-file.torrent' but has too much piece data

    tr_error* error = nullptr;
    auto const filename = std::string{ AssetsPath } + std::string{ "/wrong-piece-count.torrent" };

    EXPECT_EQ(nullptr, tr_torrentMetainfoNewFromFile(filename.c_str(), &error));
    EXPECT_NE(nullptr, error);
    EXPECT_EQ(TR_ERROR_EINVAL, error->code);

    tr_error_clear(&error);
}

TEST(TorrentMetainfoTest, MultiFile)
{
    tr_error* error = nullptr;
    auto const filename = std::string{ AssetsPath } + std::string{ "/multifile.torrent" };
    auto* const tm = tr_torrentMetainfoNewFromFile(filename.c_str(), &error);
    EXPECT_EQ(nullptr, error);

    auto info = tr_torrent_metainfo_info{};
    EXPECT_EQ(&info, tr_torrentMetainfoGet(tm, &info));
    EXPECT_EQ("this is the comment"sv, info.comment);
    EXPECT_EQ("Transmission/3.00 (bb6b5a062e)"sv, info.creator);
    EXPECT_EQ("872bb1ee696856f3a9779c69284121d273c079c2"sv, info.info_hash_string);
    auto info_hash = tr_sha1_digest_t{};
    tr_hex_to_sha1(std::data(info_hash), info.info_hash_string);
    EXPECT_EQ(info_hash, info.info_hash);
    EXPECT_EQ(false, info.is_private);
    EXPECT_EQ("test"sv, std::string_view{ info.name });
    EXPECT_EQ(1, info.n_pieces);
    EXPECT_EQ(1636241186, info.time_created);
    EXPECT_EQ(12, info.total_size);

    auto tracker_info = tr_torrent_metainfo_tracker_info{};
    EXPECT_EQ(1, tr_torrentMetainfoTrackerCount(tm));
    EXPECT_EQ(&tracker_info, tr_torrentMetainfoTracker(tm, 0, &tracker_info));
    EXPECT_EQ("http://example.org/announce?id=foo"sv, std::string_view{ tracker_info.announce_url });
    EXPECT_EQ("http://example.org/scrape?id=foo"sv, std::string_view{ tracker_info.scrape_url });
    EXPECT_EQ(0, tracker_info.tier);
    auto* const magnet = tr_torrentMetainfoMagnet(tm);
    EXPECT_EQ(
        "magnet:?xt=urn:btih:872bb1ee696856f3a9779c69284121d273c079c2&dn=test&tr=http%3A%2F%2Fexample.org%2Fannounce%3Fid%3Dfoo"sv,
        std::string_view{ magnet });
    tr_free(magnet);

    auto file_info = tr_torrent_metainfo_file_info{};
    EXPECT_EQ(2, tr_torrentMetainfoFileCount(tm));
    EXPECT_EQ(&file_info, tr_torrentMetainfoFile(tm, 0, &file_info));
    EXPECT_EQ("test/hello.txt"sv, std::string_view{ file_info.path });
    EXPECT_EQ(&file_info, tr_torrentMetainfoFile(tm, 1, &file_info));
    EXPECT_EQ("test/world.txt"sv, std::string_view{ file_info.path });
    EXPECT_EQ(6, file_info.size);
    EXPECT_EQ(0, tracker_info.tier);

    tr_torrentMetainfoFree(tm);
}

TEST(TorrentMetainfoTest, BencOffsets)
{
    auto const filename = std::string{ AssetsPath } + std::string{ "/multifile.torrent" };
    auto benc = std::vector<std::byte>{};
    auto metainfo = tr_torrent_metainfo{};

    EXPECT_TRUE(tr_loadFile(benc, filename));
    EXPECT_TRUE(metainfo.parse(std::data(benc), std::size(benc)));
    EXPECT_EQ(152, metainfo.info_dict_size);
    EXPECT_EQ(176, metainfo.info_dict_offset);
    EXPECT_EQ(292, metainfo.pieces_offset);
}

TEST(TorrentMetainfoTest, Pieces)
{
}

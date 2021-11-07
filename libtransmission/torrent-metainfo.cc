/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <iterator>

#include <event2/util.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "error-types.h"
#include "error.h"
#include "platform.h"
#include "torrent-metainfo-private.h"
#include "torrent-metainfo.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"

using namespace std::literals;

/***
****
***/

namespace
{

bool appendSanitizedComponent(std::string& out, std::string_view in, bool* is_adjusted)
{
    auto const original_out_len = std::size(out);
    auto const original_in = in;
    *is_adjusted = false;

    // remove leading spaces
    auto constexpr leading_test = [](auto ch)
    {
        return isspace(ch);
    };
    auto const it = std::find_if_not(std::begin(in), std::end(in), leading_test);
    in.remove_prefix(std::distance(std::begin(in), it));

    // remove trailing spaces and '.'
    auto constexpr trailing_test = [](auto ch)
    {
        return isspace(ch) || ch == '.';
    };
    auto const rit = std::find_if_not(std::rbegin(in), std::rend(in), trailing_test);
    in.remove_suffix(std::distance(std::rbegin(in), rit));

    // munge banned characters
    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    auto constexpr ensure_legal_char = [](auto ch)
    {
        auto constexpr Banned = std::string_view{ "<>:\"/\\|?*" };
        auto const banned = Banned.find(ch) != Banned.npos || (unsigned char)ch < 0x20;
        return banned ? '_' : ch;
    };
    auto const old_out_len = std::size(out);
    std::transform(std::begin(in), std::end(in), std::back_inserter(out), ensure_legal_char);

    // munge banned filenames
    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    auto constexpr ReservedNames = std::array<std::string_view, 22>{
        "CON"sv,  "PRN"sv,  "AUX"sv,  "NUL"sv,  "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv,
        "COM8"sv, "COM9"sv, "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
    };
    for (auto const& name : ReservedNames)
    {
        size_t const name_len = std::size(name);
        if (evutil_ascii_strncasecmp(out.c_str() + old_out_len, std::data(name), name_len) != 0 ||
            (out[old_out_len + name_len] != '\0' && out[old_out_len + name_len] != '.'))
        {
            continue;
        }

        out.insert(std::begin(out) + old_out_len + name_len, '_');
        break;
    }

    *is_adjusted = original_in != std::string_view{ out.c_str() + original_out_len };
    return std::size(out) > original_out_len;
}

char* getfile(bool* is_adjusted, std::string_view root, tr_variant* path, std::string& buf)
{
    *is_adjusted = false;

    if (!tr_variantIsList(path))
    {
        return nullptr;
    }

    buf = root;
    for (int i = 0, n = tr_variantListSize(path); i < n; i++)
    {
        auto raw = std::string_view{};
        if (!tr_variantGetStrView(tr_variantListChild(path, i), &raw))
        {
            return nullptr;
        }

        auto is_component_adjusted = bool{};
        auto const pos = std::size(buf);
        if (!appendSanitizedComponent(buf, raw, &is_component_adjusted))
        {
            continue;
        }

        buf.insert(std::begin(buf) + pos, TR_PATH_DELIMITER);

        *is_adjusted |= is_component_adjusted;
    }

    if (std::size(buf) <= std::size(root))
    {
        return nullptr;
    }

    char* const ret = tr_utf8clean(buf);
    *is_adjusted |= buf != ret;
    return ret;
}

char const* parseFiles(tr_torrent_metainfo& setme, tr_variant* info_dict)
{
    setme.size = 0;
    setme.files.clear();

    auto is_root_adjusted = bool{ false };
    auto root_name = std::string{};
    if (!appendSanitizedComponent(root_name, setme.name, &is_root_adjusted))
    {
        return "invalid name";
    }

    // bittorrent 1.0 spec
    // http://bittorrent.org/beps/bep_0003.html
    //
    // "There is also a key length or a key files, but not both or neither.
    //
    // "If length is present then the download represents a single file,
    // otherwise it represents a set of files which go in a directory structure.
    // In the single file case, length maps to the length of the file in bytes.
    auto len = int64_t{};
    tr_variant* files_entry = nullptr;
    if (tr_variantDictFindInt(info_dict, TR_KEY_length, &len))
    {
        setme.size = len;
        setme.files.emplace_back(root_name, len, is_root_adjusted);
    }
    // "For the purposes of the other keys, the multi-file case is treated as
    // only having a single file by concatenating the files in the order they
    // appear in the files list. The files list is the value files maps to,
    // and is a list of dictionaries containing the following keys:
    // length - The length of the file, in bytes.
    // path - A list of UTF-8 encoded strings corresponding to subdirectory
    // names, the last of which is the actual file name (a zero length list
    // is an error case).
    // In the multifile case, the name key is the name of a directory.
    else if (tr_variantDictFindList(info_dict, TR_KEY_files, &files_entry))
    {

        auto buf = std::string{};
        auto const n_files = size_t{ tr_variantListSize(files_entry) };
        for (size_t i = 0; i < n_files; ++i)
        {
            auto* const file_entry = tr_variantListChild(files_entry, i);
            if (!tr_variantIsDict(file_entry))
            {
                return "'files' is not a dictionary";
            }

            tr_variant* path = nullptr;
            if (!tr_variantDictFindList(file_entry, TR_KEY_path_utf_8, &path) &&
                !tr_variantDictFindList(file_entry, TR_KEY_path, &path))
            {
                return "path";
            }

            bool is_file_adjusted = false;
            char* const file = getfile(&is_file_adjusted, root_name, path, buf);
            if (file == nullptr)
            {
                return "path";
            }

            if (!tr_variantDictFindInt(file_entry, TR_KEY_length, &len))
            {
                return "length";
            }

            setme.files.emplace_back(buf, len, is_root_adjusted || is_file_adjusted);
            setme.size += len;

            tr_free(file);
        }
    }
    else
    {
        // TODO: add support for 'file tree' BitTorrent 2 torrents / hybrid torrents.
        // Patches welcomed!
        // https://www.bittorrent.org/beps/bep_0052.html#info-dictionary
        return "'info' dict has neither 'files' nor 'length' key";
    }

    return nullptr;
}

bool tr_convertAnnounceToScrape(std::string& out, std::string_view in)
{
    /* To derive the scrape URL use the following steps:
     * Begin with the announce URL. Find the last '/' in it.
     * If the text immediately following that '/' isn't 'announce'
     * it will be taken as a sign that that tracker doesn't support
     * the scrape convention. If it does, substitute 'scrape' for
     * 'announce' to find the scrape page. */

    auto constexpr oldval = "/announce"sv;
    auto pos = in.rfind(oldval.front());
    if (pos != in.npos && in.find(oldval, pos) == pos)
    {
        auto const prefix = in.substr(0, pos);
        auto const suffix = in.substr(pos + std::size(oldval));
        tr_buildBuf(out, prefix, "/scrape"sv, suffix);
        return true;
    }

    // some torrents with UDP announce URLs don't have /announce
    if (in.find("udp:"sv) == 0)
    {
        out = in;
        return true;
    }

    return false;
}

// https://www.bittorrent.org/beps/bep_0012.html
void parseAnnounce(tr_torrent_metainfo& setme, tr_variant* meta)
{
    auto buf = std::string{};
    auto tier = tr_tracker_tier_t{ 0 };

    setme.trackers.clear();

    // announce-list
    // example: d['announce-list'] = [ [tracker1], [backup1], [backup2] ]
    tr_variant* tiers = nullptr;
    if (tr_variantDictFindList(meta, TR_KEY_announce_list, &tiers))
    {
        size_t const n_tiers = tr_variantListSize(tiers);
        for (size_t i = 0; i < n_tiers; ++i)
        {
            auto any_added_in_tier = bool{ false };
            tr_variant* const tier_v = tr_variantListChild(tiers, i);
            size_t const n_trackers_in_tier = tr_variantListSize(tier_v);
            for (size_t j = 0; j < n_trackers_in_tier; j++)
            {
                auto url = std::string_view{};
                if (tr_variantGetStrView(tr_variantListChild(tier_v, j), &url))
                {
                    url = tr_strvstrip(url);
                    if (tr_urlIsValidTracker(url))
                    {
                        auto const announce_url = tr_quark_new(url);
                        auto const scrape_url = tr_convertAnnounceToScrape(buf, url) ? tr_quark_new(buf) : TR_KEY_NONE;
                        setme.trackers.insert({ tier, { announce_url, scrape_url, tier } });
                        any_added_in_tier = true;
                    }
                }
            }

            if (any_added_in_tier)
            {
                ++tier;
            }
        }
    }

    // single 'announce' url
    auto url = std::string_view{};
    if (std::empty(setme.trackers) && tr_variantDictFindStrView(meta, TR_KEY_announce, &url))
    {
        url = tr_strvstrip(url);
        if (tr_urlIsValidTracker(url))
        {
            auto const announce_url = tr_quark_new(url);
            auto const scrape_url = tr_convertAnnounceToScrape(buf, url) ? tr_quark_new(buf) : TR_KEY_NONE;
            setme.trackers.insert({ tier, { announce_url, scrape_url, tier } });
        }
    }
}

/**
 * @brief Ensure that the URLs for multfile torrents end in a slash.
 *
 * See http://bittorrent.org/beps/bep_0019.html#metadata-extension
 * for background on how the trailing slash is used for "url-list"
 * fields.
 *
 * This function is to workaround some .torrent generators, such as
 * mktorrent and very old versions of utorrent, that don't add the
 * trailing slash for multifile torrents if omitted by the end user.
 */
std::string fixWebseedUrl(tr_torrent_metainfo const& tm, std::string_view url)
{
    url = tr_strvstrip(url);

    if (std::size(tm.files) > 1 && !std::empty(url) && url.back() != '/')
    {
        return std::string{ url } + '/';
    }

    return std::string{ url };
}

void parseWebseeds(tr_torrent_metainfo& setme, tr_variant* meta)
{
    setme.webseed_urls.clear();

    auto url = std::string_view{};
    tr_variant* urls = nullptr;
    if (tr_variantDictFindList(meta, TR_KEY_url_list, &urls))
    {
        size_t const n = tr_variantListSize(urls);
        setme.webseed_urls.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (tr_variantGetStrView(tr_variantListChild(urls, i), &url) && tr_urlIsValid(url))
            {
                setme.webseed_urls.push_back(fixWebseedUrl(setme, url));
            }
        }
    }
    else if (tr_variantDictFindStrView(meta, TR_KEY_url_list, &url) && tr_urlIsValid(url)) // handle single items in webseeds
    {
        setme.webseed_urls.push_back(fixWebseedUrl(setme, url));
    }
}

char const* parseImpl(tr_torrent_metainfo& setme, tr_variant* meta)
{
    int64_t i = 0;
    auto sv = std::string_view{};

    /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
     * from the Metainfo file. Note that the value will be a bencoded
     * dictionary, given the definition of the info key above. */
    tr_variant* info_dict = nullptr;
    if (tr_variantDictFindDict(meta, TR_KEY_info, &info_dict))
    {
        size_t blen = 0;
        char* bstr = tr_variantToStr(info_dict, TR_VARIANT_FMT_BENC, &blen);
        tr_sha1(reinterpret_cast<uint8_t*>(std::data(setme.info_hash)), bstr, (int)blen, nullptr);
        tr_sha1_to_hex(std::data(setme.info_hash_string), std::data(setme.info_hash));
        setme.info_dict.length = blen;
        tr_free(bstr);
    }
    else
    {
        return "missing 'info' dictionary";
    }

    // name
    if (tr_variantDictFindStrView(info_dict, TR_KEY_name_utf_8, &sv) || tr_variantDictFindStrView(info_dict, TR_KEY_name, &sv))
    {
        char* const tmp = tr_utf8clean(sv);
        setme.name = tmp ? tmp : "";
        tr_free(tmp);
    }
    else
    {
        return "'info' dictionary has neither 'name.utf-8' nor 'name'";
    }

    // comment (optional)
    if (tr_variantDictFindStrView(meta, TR_KEY_comment_utf_8, &sv) || tr_variantDictFindStrView(meta, TR_KEY_comment, &sv))
    {
        char* const tmp = tr_utf8clean(sv);
        setme.comment = tmp ? tmp : "";
        tr_free(tmp);
    }
    else
    {
        setme.comment.clear();
    }

    // created by (optional)
    if (tr_variantDictFindStrView(meta, TR_KEY_created_by_utf_8, &sv) ||
        tr_variantDictFindStrView(meta, TR_KEY_created_by, &sv))
    {
        char* const tmp = tr_utf8clean(sv);
        setme.creator = tmp ? tmp : "";
        tr_free(tmp);
    }
    else
    {
        setme.creator.clear();
    }

    // creation date (optional)
    if (tr_variantDictFindInt(meta, TR_KEY_creation_date, &i))
    {
        setme.time_created = i;
    }
    else
    {
        setme.time_created = 0;
    }

    // private (optional)
    if (tr_variantDictFindInt(info_dict, TR_KEY_private, &i) || tr_variantDictFindInt(meta, TR_KEY_private, &i))
    {
        setme.is_private = i != 0;
    }
    else
    {
        setme.is_private = false;
    }

    // source (optional)
    if (tr_variantDictFindStrView(info_dict, TR_KEY_source, &sv) || tr_variantDictFindStrView(meta, TR_KEY_source, &sv))
    {
        auto* const tmp = tr_utf8clean(sv);
        setme.source = tmp ? tmp : "";
        tr_free(tmp);
    }
    else
    {
        setme.source.clear();
    }

    // piece length
    if (tr_variantDictFindInt(info_dict, TR_KEY_piece_length, &i) && (i > 0))
    {
        setme.piece_size = i;
    }
    else
    {
        return "'info' dict 'piece length' is missing or has an invalid value";
    }

    // pieces
    if (tr_variantDictFindStrView(info_dict, TR_KEY_pieces, &sv) && (std::size(sv) % SHA_DIGEST_LENGTH == 0))
    {
        auto const n = std::size(sv) / sizeof(tr_sha1_digest_t);
        setme.piece_count = n;
        setme.pieces.resize(n);
        std::copy_n(std::data(sv), std::size(sv), reinterpret_cast<char*>(std::data(setme.pieces)));
    }
    else
    {
        return "'info' dict 'pieces' is missing or has an invalid value";
    }

    // files
    auto const* const errstr = parseFiles(setme, info_dict);
    if (errstr != nullptr)
    {
        return errstr;
    }

    if (std::empty(setme.files) || setme.size == 0)
    {
        return "no files found";
    }

    // do the size and piece size match up?
    auto const expected_piece_count = (setme.size + (setme.piece_size - 1)) / setme.piece_size;
    if (uint64_t(setme.piece_count) != expected_piece_count)
    {
        return "piece count and file sizes do not match";
    }

    parseAnnounce(setme, meta);
    parseWebseeds(setme, meta);

    return nullptr;
}

} // namespace

//// Public API

/// Lifecycle

tr_torrent_metainfo* tr_torrentMetainfoNewFromData(char const* data, size_t data_len, struct tr_error** error)
{
    auto top = tr_variant{};

    auto const benc_parse_err = tr_variantFromBenc(&top, data, data_len);
    if (benc_parse_err)
    {
        tr_error_set(error, TR_ERROR_EINVAL, "Error parsing bencoded data: %s", tr_strerror(benc_parse_err));
        return nullptr;
    }

    auto* tm = new tr_torrent_metainfo{};
    auto const* const errmsg = parseImpl(*tm, &top);
    tr_variantFree(&top);
    if (errmsg != nullptr)
    {
        tr_error_set(error, TR_ERROR_EINVAL, "Error parsing metainfo: %s", errmsg);
        delete tm;
        return nullptr;
    }

    return tm;
}

tr_torrent_metainfo* tr_torrentMetainfoNewFromFile(char const* filename, struct tr_error** error)
{
    auto raw_len = size_t{};
    tr_error* my_error = nullptr;
    auto* const raw = tr_loadFile(filename, &raw_len, &my_error);
    if (my_error)
    {
        tr_error_propagate(error, &my_error);
        return nullptr;
    }

    auto* const tm = tr_torrentMetainfoNewFromData(reinterpret_cast<char const*>(raw), raw_len, error);
    tr_free(raw);
    return tm;
}

void tr_torrentMetainfoFree(tr_torrent_metainfo* tm)
{
    delete tm;
}

////  Accessors

/// Info

tr_torrent_metainfo_info* tr_torrentMetainfoGet(tr_torrent_metainfo const* tm, tr_torrent_metainfo_info* setme)
{
    setme->comment = tm->comment.c_str();
    setme->creator = tm->creator.c_str();
    setme->info_hash = tm->info_hash;
    setme->info_hash_string = tm->info_hash_string;
    setme->is_private = tm->is_private;
    setme->name = tm->name.c_str();
    setme->piece_count = tm->piece_count;
    setme->size = tm->size;
    setme->source = tm->source.c_str();
    setme->time_created = tm->time_created;
    return setme;
}

/// Files

size_t tr_torrentMetainfoFileCount(tr_torrent_metainfo const* tm)
{
    return std::size(tm->files);
}

tr_torrent_metainfo_file_info* tr_torrentMetainfoFile(
    tr_torrent_metainfo const* tm,
    size_t n,
    tr_torrent_metainfo_file_info* setme)
{
    auto& file = tm->files[n];
    setme->path = file.path.c_str();
    setme->size = file.size;
    return setme;
}

/// Trackers

size_t tr_torrentMetainfoTrackerCount(tr_torrent_metainfo const* tm)
{
    return std::size(tm->trackers);
}

tr_torrent_metainfo_tracker_info* tr_torrentMetainfoTracker(
    tr_torrent_metainfo const* tm,
    size_t n,
    tr_torrent_metainfo_tracker_info* setme)
{
    auto it = std::begin(tm->trackers);
    std::advance(it, n);
    auto const& tracker = it->second;
    setme->announce_url = tr_quark_get_string(tracker.announce_url);
    setme->scrape_url = tr_quark_get_string(tracker.scrape_url);
    setme->tier = tracker.tier;
    return setme;
}

#include "plex_search.h"

#include <algorithm>
#include <cctype>

#include "phi/runtime/str.h"

namespace phicore::plex::ipc {

namespace str = phi::str;

namespace {

std::vector<std::string> wordsOf(const std::string &text)
{
    std::vector<std::string> out;
    std::string current;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return !needle.empty() && haystack.find(needle) != std::string::npos;
}

/// The share of the query's words found in the text, 0..1.
double wordShare(const std::string &query, const std::string &normalisedText)
{
    const std::vector<std::string> words = wordsOf(query);
    if (words.empty()) return 0.0;
    std::size_t found = 0;
    for (const std::string &word : words)
        if (contains(normalisedText, normalised(word))) ++found;
    return static_cast<double>(found) / static_cast<double>(words.size());
}

Track trackFromMetadata(const Json &item)
{
    Track track;
    track.ratingKey = jsonString(item, "ratingKey");
    track.title = jsonString(item, "title");
    track.artist = jsonString(item, "grandparentTitle");
    track.album = jsonString(item, "parentTitle");
    track.durationMs = item.contains("duration") && item["duration"].is_number()
        ? item["duration"].get<std::int64_t>()
        : 0;
    const Json media = jsonValue(item, "Media");
    if (media.is_array() && !media.empty() && media.front().is_object()) {
        track.container = jsonString(media.front(), "container");
        const Json parts = jsonValue(media.front(), "Part");
        if (parts.is_array() && !parts.empty() && parts.front().is_object()) {
            track.partKey = jsonString(parts.front(), "key");
            const std::string partContainer = jsonString(parts.front(), "container");
            if (!partContainer.empty()) track.container = partContainer;
        }
    }
    return track;
}

std::vector<Track> tracksFrom(const Json &metadata)
{
    std::vector<Track> out;
    if (!metadata.is_array()) return out;
    for (const Json &item : metadata) {
        if (!item.is_object() || jsonString(item, "type") != "track") continue;
        Track track = trackFromMetadata(item);
        if (!track.ratingKey.empty() && !track.title.empty()) out.push_back(std::move(track));
    }
    return out;
}

} // namespace

std::string normalised(const std::string &text)
{
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

int scoreTrack(const Track &track, const std::string &query)
{
    const std::string q = normalised(query);
    const std::string title = normalised(track.title);
    const std::string artist = normalised(track.artist);
    if (q.empty() || title.empty()) return 0;
    if (q == artist + title || q == title + artist || q == title) return 100;
    if (contains(q, title) && contains(q, artist)) return 95;
    if (contains(title, q)) return 80;
    if (contains(q, title)) return 70;
    const double share = wordShare(query, artist + " " + title);
    return share >= 0.5 ? static_cast<int>(share * 50.0) : 0;
}

int scoreTitle(const Track &track, const std::string &titleQuery)
{
    const std::string q = normalised(titleQuery);
    const std::string title = normalised(track.title);
    if (q.empty() || title.empty()) return 0;
    if (q == title) return 100;
    if (contains(title, q)) return 85;
    if (contains(q, title)) return 75;
    const double share = wordShare(titleQuery, title);
    return share >= 0.5 ? static_cast<int>(share * 50.0) : 0;
}

SearchHubs parseHubs(const std::string &body)
{
    SearchHubs out;
    const Json root = parseJson(body);
    const Json hubs = jsonValue(jsonValue(root, "MediaContainer"), "Hub");
    if (!hubs.is_array()) return out;
    for (const Json &hub : hubs) {
        if (!hub.is_object()) continue;
        const std::string type = jsonString(hub, "type");
        const Json metadata = jsonValue(hub, "Metadata");
        if (type == "track") {
            std::vector<Track> tracks = tracksFrom(metadata);
            out.tracks.insert(out.tracks.end(), tracks.begin(), tracks.end());
        } else if ((type == "artist" || type == "album") && metadata.is_array() && !metadata.empty()
                   && metadata.front().is_object()) {
            const Json &first = metadata.front();
            if (type == "artist" && out.artistKey.empty()) {
                out.artistKey = jsonString(first, "ratingKey");
                out.artistName = jsonString(first, "title");
            } else if (type == "album" && out.albumKey.empty()) {
                out.albumKey = jsonString(first, "ratingKey");
                out.albumName = jsonString(first, "title");
            }
        }
    }
    return out;
}

std::vector<Track> parseTracks(const std::string &body)
{
    const Json root = parseJson(body);
    return tracksFrom(jsonValue(jsonValue(root, "MediaContainer"), "Metadata"));
}

std::string parseDelegationToken(const std::string &body)
{
    const Json root = parseJson(body);
    std::string token = jsonString(jsonValue(root, "MediaContainer"), "token");
    if (token.empty()) token = jsonString(root, "token");
    return token;
}

std::string hubsSearchPath(const std::string &query, int limit)
{
    return "/hubs/search?query=" + urlEncode(query) + "&limit=" + std::to_string(limit)
        + "&includeCollections=0&includeExternalMedia=0";
}

std::string artistTracksPath(const std::string &artistKey)
{
    return "/library/metadata/" + urlEncode(artistKey) + "/allLeaves";
}

std::string albumTracksPath(const std::string &albumKey)
{
    return "/library/metadata/" + urlEncode(albumKey) + "/children";
}

std::string delegationTokenPath()
{
    return "/security/token?type=delegation&scope=all";
}

std::vector<Split> splits(const std::string &query, std::size_t maxTries)
{
    std::vector<Split> out;
    const std::vector<std::string> words = wordsOf(query);
    if (words.size() < 2) return out;
    for (std::size_t n = words.size() - 1; n >= 1 && out.size() < maxTries; --n) {
        Split split;
        for (std::size_t i = 0; i < words.size(); ++i) {
            std::string &part = i < n ? split.artist : split.title;
            part += (part.empty() ? "" : " ") + words[i];
        }
        out.push_back(std::move(split));
        if (n == 1) break;
    }
    return out;
}

Playable playableFor(const Track &track, const PlexSettings &settings, const std::string &delegationToken)
{
    Playable out;
    const std::string container = str::toLower(track.container);
    const std::string base = settings.serverBaseUrl();
    if (!track.partKey.empty() && (container == "mp3" || container == "flac" || container == "wav")) {
        out.url = base + track.partKey + "?X-Plex-Token=" + urlEncode(delegationToken);
        out.mimeType = container == "mp3" ? "audio/mpeg" : container == "flac" ? "audio/flac" : "audio/wav";
        return out;
    }
    // The universal transcoder, asked for MP3 the way Plex Web asks for it:
    // the product and platform are the ones the server was seen to answer
    // with chunked MP3 on the bench (2026-10-25); an unknown client gets a
    // profile the server chooses, which is not something to debug twice.
    out.url = base + "/music/:/transcode/universal/start.mp3?hasMDE=1&path="
        + urlEncode("/library/metadata/" + track.ratingKey)
        + "&mediaIndex=0&partIndex=0&protocol=http&directPlay=0&directStream=0&musicBitrate=192"
        + "&session=" + urlEncode(settings.controllerIdentifier + "-" + track.ratingKey)
        + "&X-Plex-Client-Identifier=" + urlEncode(settings.controllerIdentifier)
        + "&X-Plex-Product=Plex+Web&X-Plex-Platform=Chrome&X-Plex-Version=4.0"
        + "&X-Plex-Client-Profile-Extra="
        + urlEncode("add-transcode-target(type=musicProfile&context=streaming&protocol=http&container=mp3&audioCodec=mp3)")
        + "&X-Plex-Token=" + urlEncode(delegationToken);
    out.mimeType = "audio/mpeg";
    return out;
}

Json itemsJson(std::vector<Track> tracks, const PlexSettings &settings, const std::string &delegationToken,
               std::size_t limit)
{
    std::stable_sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b) { return a.score > b.score; });
    Json items = Json::array();
    for (const Track &track : tracks) {
        if (items.size() >= limit) break;
        if (track.score <= 0) continue;
        const Playable playable = playableFor(track, settings, delegationToken);
        items.push_back(Json{
            {"mediaId", playable.url},
            {"mediaType", "music"},
            {"mimeType", playable.mimeType},
            {"score", track.score},
            {"metadata", Json{
                {"title", track.title},
                {"artist", track.artist},
                {"album", track.album},
                {"durationMs", track.durationMs},
                {"ratingKey", track.ratingKey},
                {"source", "plex"},
            }},
        });
    }
    return Json{{"items", items}};
}

} // namespace phicore::plex::ipc

#include "plex_search.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

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

/// The words of a name, normalised, the empty ones (an "&") gone.
std::vector<std::string> normalisedWords(const std::string &text)
{
    std::vector<std::string> out;
    for (const std::string &word : wordsOf(text)) {
        std::string n = normalised(word);
        if (!n.empty()) out.push_back(std::move(n));
    }
    return out;
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return !needle.empty() && haystack.find(needle) != std::string::npos;
}

std::size_t editDistance(const std::string &a, const std::string &b)
{
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

/// The consonants of a word with the ones that sound alike merged and
/// doubles dropped: what is left of a name a transcriber heard wrong.
/// "robin" and "robbie" are "rbn" and "rb", "kette" and "katie" both "kd",
/// "dreisand" and "streisand" "drsnd" and "sdrsnd".
std::string skeleton(const std::string &word)
{
    std::string out;
    for (const char c : word) {
        char k = 0;
        switch (c) {
        case 'a': case 'e': case 'i': case 'o': case 'u': case 'y': case 'h': continue;
        case 'b': case 'p': k = 'b'; break;
        case 'd': case 't': k = 'd'; break;
        case 'g': case 'k': case 'c': case 'q': k = 'k'; break;
        case 'f': case 'v': case 'w': k = 'f'; break;
        case 's': case 'z': case 'x': k = 's'; break;
        default: k = c; break;
        }
        if (out.empty() || out.back() != k) out.push_back(k);
    }
    return out;
}

/// How alike two normalised words are: 1 equal, 0.9 a letter or two off,
/// 0.6 the same skeleton give or take one, 0 otherwise.
double wordLikeness(const std::string &said, const std::string &name)
{
    if (said == name) return 1.0;
    const std::size_t cap = said.size() >= 8 ? 2 : said.size() >= 4 ? 1 : 0;
    if (cap > 0 && editDistance(said, name) <= cap) return 0.9;
    // By sound only for words of about one length that begin alike: without
    // that, "paparaz" sounds like "Bruce" and "angels" like "Nicole".
    const std::size_t lengthGap = said.size() > name.size() ? said.size() - name.size() : name.size() - said.size();
    if (lengthGap > 2 || said.front() != name.front()) return 0.0;
    const std::string a = skeleton(said);
    const std::string b = skeleton(name);
    if (a.size() >= 2 && b.size() >= 2 && editDistance(a, b) <= 1) return 0.6;
    return 0.0;
}

Track trackFromMetadata(const Json &item)
{
    Track track;
    track.ratingKey = jsonString(item, "ratingKey");
    track.title = jsonString(item, "title");
    track.artist = jsonString(item, "grandparentTitle");
    track.album = jsonString(item, "parentTitle");
    const auto number = [&](const char *key) -> std::int64_t {
        return item.contains(key) && item[key].is_number() ? item[key].get<std::int64_t>() : 0;
    };
    track.durationMs = number("duration");
    track.ratingCount = number("ratingCount");
    track.index = static_cast<int>(number("index"));
    track.parentIndex = static_cast<int>(number("parentIndex"));
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
    return 0;
}

int scoreTitle(const std::string &titleText, const std::string &titleQuery)
{
    const std::string q = normalised(titleQuery);
    const std::string title = normalised(titleText);
    if (q.empty() || title.empty()) return 0;
    if (q == title) return 100;
    // A letter or two off over the whole title: "thankyoustarrs".
    const std::size_t longer = std::max(q.size(), title.size());
    const std::size_t distance = editDistance(q, title);
    if (distance * 5 <= longer) return static_cast<int>(100 - (distance * 100 + longer - 1) / longer);
    if (contains(title, q) && q.size() * 2 >= title.size()) return 85;
    if (contains(q, title) && title.size() * 2 >= q.size()) return 75;
    // By sound, word for word: every word of the title has the consonants of
    // the word said in its place ("viel" for "Feel", pi5 2026-10-25). Only
    // ever asked among one artist's tracks, so a collision is one of theirs.
    const std::vector<std::string> said = normalisedWords(titleQuery);
    const std::vector<std::string> words = normalisedWords(titleText);
    if (said.size() != words.size()) return 0;
    for (std::size_t i = 0; i < said.size(); ++i) {
        if (said[i].size() < 3 || words[i].size() < 3) return 0;
        const std::string a = skeleton(said[i]);
        if (a.empty() || a != skeleton(words[i])) return 0;
    }
    return 60;
}

namespace {

/// The name and what was said, both written together: a transcriber cuts
/// a name it does not know at the wrong places ("Kat im Lua" for Katie
/// Melua, pi5 2026-10-25), and written together the two are two letters
/// apart. Only for eight letters or more, and a quarter of them at most.
int joinedScore(const std::string &artistName, const std::string &artistQuery)
{
    const std::string said = normalised(artistQuery);
    const std::string name = normalised(artistName);
    if (said.size() < 8 || name.size() < 8) return 0;
    const std::size_t longer = std::max(said.size(), name.size());
    const std::size_t distance = editDistance(said, name);
    if (distance * 4 > longer) return 0;
    return static_cast<int>(100 - (distance * 100 + longer - 1) / longer);
}

int wordScore(const std::string &artistName, const std::string &artistQuery);

} // namespace

int scoreArtist(const std::string &artistName, const std::string &artistQuery)
{
    return std::max(wordScore(artistName, artistQuery), joinedScore(artistName, artistQuery));
}

namespace {

int wordScore(const std::string &artistName, const std::string &artistQuery)
{
    const std::vector<std::string> said = normalisedWords(artistQuery);
    const std::vector<std::string> name = normalisedWords(artistName);
    if (said.empty() || name.empty()) return 0;
    std::vector<bool> used(name.size(), false);
    double matched = 0.0;
    double total = 0.0;
    // Sound alone is not a name: "jazz" sounds like "Jaffa" and "paparaz"
    // like "Phaedrus" (pi5's library, 2026-10-25). A reading needs one word
    // of four letters or more that is spelled right or nearly.
    bool spelled = false;
    for (const std::string &word : said) {
        total += static_cast<double>(word.size());
        double best = 0.0;
        std::size_t bestAt = name.size();
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (used[i]) continue;
            const double likeness = wordLikeness(word, name[i]);
            if (likeness > best) {
                best = likeness;
                bestAt = i;
            }
        }
        if (bestAt < name.size()) {
            used[bestAt] = true;
            matched += best * static_cast<double>(word.size());
            if (best >= 0.9 && word.size() >= 4) spelled = true;
        }
    }
    if (!spelled) return 0;
    for (std::size_t i = 0; i < name.size(); ++i)
        if (!used[i]) total += static_cast<double>(name[i].size());
    return total > 0.0 ? static_cast<int>(100.0 * matched / total + 0.5) : 0;
}

} // namespace

std::vector<ArtistGuess> guessArtists(const std::vector<Artist> &artists, const std::string &query)
{
    const std::vector<std::string> words = wordsOf(query);
    std::vector<ArtistGuess> out;
    for (std::size_t n = words.size(); n >= 1; --n) {
        std::string artistPart;
        std::string title;
        for (std::size_t i = 0; i < words.size(); ++i) {
            std::string &part = i < n ? artistPart : title;
            part += (part.empty() ? "" : " ") + words[i];
        }
        // The best two artists for this front part, the runner-up to judge
        // a weak best by. A runner-up whose name only extends the best one
        // ("Barbra Streisand & Celine Dion") is the same artist, not a rival.
        ArtistGuess best;
        int second = 0;
        for (const Artist &artist : artists) {
            const int score = scoreArtist(artist.title, artistPart);
            if (score > best.score) {
                if (best.score > 0 && normalised(artist.title).rfind(normalised(best.artist.title), 0) != 0)
                    second = best.score;
                best = ArtistGuess{artist, score, title};
            } else if (score > second && normalised(artist.title).rfind(normalised(best.artist.title), 0) != 0) {
                second = score;
            }
        }
        // The artist alone is played at once, with no title to confirm it, so
        // it needs more: "pink floyd money" sounded enough like "Pink Feat.
        // James T. Moore" at 50 to play one of his songs.
        const int needed = title.empty() ? 60 : 50;
        const bool worth = best.score >= needed || (best.score >= 30 && best.score >= 2 * second);
        if (worth) out.push_back(std::move(best));
        if (n == 1) break;
    }
    std::stable_sort(out.begin(), out.end(), [](const ArtistGuess &a, const ArtistGuess &b) { return a.score > b.score; });
    return out;
}

std::vector<Artist> artistsToFetch(const std::vector<ArtistGuess> &guesses, std::size_t limit)
{
    std::vector<Artist> out;
    std::set<std::string> seen;
    for (const ArtistGuess &guess : guesses) {
        if (out.size() >= limit) break;
        if (seen.insert(guess.artist.ratingKey).second) out.push_back(guess.artist);
    }
    return out;
}

std::vector<Track> matchArtistTracks(std::vector<Track> tracks, const std::vector<ArtistGuess> &guesses,
                                     const std::string &artistKey, const std::function<std::size_t(std::size_t)> &pick)
{
    std::vector<Track> out;
    if (tracks.empty()) return out;
    for (const ArtistGuess &guess : guesses) {
        if (guess.artist.ratingKey != artistKey) continue;
        if (guess.title.empty()) {
            // The artist alone: one of the ten best-known tracks.
            std::vector<Track> known = tracks;
            std::stable_sort(known.begin(), known.end(),
                             [](const Track &a, const Track &b) { return a.ratingCount > b.ratingCount; });
            const std::size_t top = std::min<std::size_t>(known.size(), 10);
            Track chosen = known[pick ? pick(top) % top : 0];
            // Below a named title on purpose: "Robbie Williams Angels" read
            // as the artist alone must lose to the reading that found Angels.
            chosen.score = guess.score * 7 / 10;
            out.push_back(std::move(chosen));
            continue;
        }
        // A title named, or an album: the album's first track.
        for (const Track &track : tracks) {
            const int byTitle = scoreTitle(track.title, guess.title);
            if (byTitle >= 60) {
                Track scored = track;
                scored.score = (guess.score + byTitle) / 2;
                out.push_back(std::move(scored));
            }
        }
        std::map<std::string, Track> albumOpeners;
        for (const Track &track : tracks) {
            if (scoreTitle(track.album, guess.title) < 60) continue;
            auto it = albumOpeners.find(track.album);
            if (it == albumOpeners.end()
                || std::pair(track.parentIndex, track.index) < std::pair(it->second.parentIndex, it->second.index))
                albumOpeners[track.album] = track;
        }
        for (auto &[album, track] : albumOpeners) {
            Track scored = track;
            scored.score = (guess.score + scoreTitle(album, guess.title)) / 2 - 5;
            out.push_back(std::move(scored));
        }
    }
    return out;
}

SearchHubs parseHubs(const std::string &body)
{
    SearchHubs out;
    const Json root = parseJson(body);
    const Json hubs = jsonValue(jsonValue(root, "MediaContainer"), "Hub");
    if (!hubs.is_array()) return out;
    for (const Json &hub : hubs) {
        if (!hub.is_object() || jsonString(hub, "type") != "track") continue;
        std::vector<Track> tracks = tracksFrom(jsonValue(hub, "Metadata"));
        out.tracks.insert(out.tracks.end(), tracks.begin(), tracks.end());
    }
    return out;
}

std::vector<Track> parseTracks(const std::string &body)
{
    const Json root = parseJson(body);
    return tracksFrom(jsonValue(jsonValue(root, "MediaContainer"), "Metadata"));
}

std::vector<std::string> parseMusicSections(const std::string &body)
{
    std::vector<std::string> out;
    const Json dirs = jsonValue(jsonValue(parseJson(body), "MediaContainer"), "Directory");
    if (!dirs.is_array()) return out;
    for (const Json &dir : dirs)
        if (dir.is_object() && jsonString(dir, "type") == "artist" && !jsonString(dir, "key").empty())
            out.push_back(jsonString(dir, "key"));
    return out;
}

std::vector<Artist> parseArtists(const std::string &body)
{
    std::vector<Artist> out;
    const Json metadata = jsonValue(jsonValue(parseJson(body), "MediaContainer"), "Metadata");
    if (!metadata.is_array()) return out;
    for (const Json &item : metadata) {
        if (!item.is_object()) continue;
        Artist artist{jsonString(item, "ratingKey"), jsonString(item, "title")};
        if (!artist.ratingKey.empty() && !artist.title.empty()) out.push_back(std::move(artist));
    }
    return out;
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

std::string sectionsPath()
{
    return "/library/sections";
}

std::string artistsPath(const std::string &sectionKey)
{
    return "/library/sections/" + urlEncode(sectionKey) + "/all?type=8";
}

std::string artistTracksPath(const std::string &artistKey)
{
    return "/library/metadata/" + urlEncode(artistKey) + "/allLeaves";
}

std::string delegationTokenPath()
{
    return "/security/token?type=delegation&scope=all";
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
    std::set<std::string> seen;
    for (const Track &track : tracks) {
        if (items.size() >= limit) break;
        if (track.score <= 0 || !seen.insert(track.ratingKey).second) continue;
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

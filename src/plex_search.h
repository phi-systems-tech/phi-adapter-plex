#pragma once
// The `media.search` action: what the library has for "Katie Melua Thankyou
// Stars", answered as items a player can fetch.
//
// Plex's own search finds "Katie" for "katy" but not "Thankyou, Stars" for
// "thank you stars", and nothing at all for an artist and a title in one
// breath - the way a sentence says it. So the search is ours and Plex only
// supplies the candidates: the whole query first; then, with the words split
// into an artist's name in front and a title behind, the artist's tracks
// matched by a normalised title (letters and digits only, so "Thankyou" and
// "thank you" are one word). Every step is one HTTP request, planned here as
// data and carried out by the instance, so the plan and the matching are
// tested without a server.
//
// A match is played through a URL the player fetches itself: the part as it
// is for MP3, FLAC and WAV (the satellite decodes those), the universal
// transcoder's MP3 for anything else, both with a delegation token minted for
// this answer - the server's own token never leaves the adapter.
#include <cstdint>
#include <string>
#include <vector>

#include "plex_json.h"
#include "plex_settings.h"

namespace phicore::plex::ipc {

/// A track the library has.
struct Track {
    std::string ratingKey;
    std::string title;
    std::string artist;
    std::string album;
    std::int64_t durationMs = 0;
    /// The part's path on the server and its container ("mp3", "mp4"...).
    std::string partKey;
    std::string container;
    /// 0..100, how well it answers the query.
    int score = 0;
};

/// What one `/hubs/search` answer holds that the search can use.
struct SearchHubs {
    std::vector<Track> tracks;
    /// First artist and album hits, by ratingKey, for the split search.
    std::string artistKey;
    std::string artistName;
    std::string albumKey;
    std::string albumName;
};

/// Letters and digits only, lowercased: "Thankyou, Stars" and "thank you
/// stars" are one string. A dumb fold on purpose - a title with an umlaut
/// is typed with it on both sides.
std::string normalised(const std::string &text);

/// How well a track answers the whole query (artist and title in any order,
/// extra words tolerated), 0 when it does not.
int scoreTrack(const Track &track, const std::string &query);
/// How well a track's title answers the title part of a split query, when
/// the artist already matched.
int scoreTitle(const Track &track, const std::string &titleQuery);

/// `/hubs/search` JSON into tracks and the first artist and album.
SearchHubs parseHubs(const std::string &body);
/// `/library/metadata/<key>/allLeaves` or `.../children` JSON into tracks.
std::vector<Track> parseTracks(const std::string &body);
/// `/security/token` JSON into the delegation token; empty when none.
std::string parseDelegationToken(const std::string &body);

/// The server paths of the search, in the order they are asked.
std::string hubsSearchPath(const std::string &query, int limit);
std::string artistTracksPath(const std::string &artistKey);
std::string albumTracksPath(const std::string &albumKey);
std::string delegationTokenPath();

/// The ways a query splits into an artist in front and a title behind:
/// "katy melua thank you stars" is tried as "katy melua" + "thank you
/// stars", "katy" + "melua thank you stars". The longest artist first, at
/// most `maxTries`. One word cannot split.
struct Split {
    std::string artist;
    std::string title;
};
std::vector<Split> splits(const std::string &query, std::size_t maxTries);

/// The playable URL and MIME type of a track, with the token minted for it.
struct Playable {
    std::string url;
    std::string mimeType;
};
Playable playableFor(const Track &track, const PlexSettings &settings, const std::string &delegationToken);

/// The action's answer: the best `limit` tracks as the README's items, the
/// highest score first.
Json itemsJson(std::vector<Track> tracks, const PlexSettings &settings, const std::string &delegationToken,
               std::size_t limit);

} // namespace phicore::plex::ipc

#pragma once
// The `media.search` action: what the library has for "Katie Melua Thankyou
// Stars", answered as items a player can fetch.
//
// Plex's own search finds "Katie" for "katy" but not "Thankyou, Stars" for
// "thank you stars", nothing for an artist and a title in one breath - the
// way a sentence says it - and nothing at all for what a transcriber made of
// a name ("Paparaz Dreisand" for Barbra Streisand, "kette melua"). So Plex is
// asked once with the whole query, and then the search is ours: the library's
// artists (listed once and kept) are matched against every front part of the
// query word by word - a word equal, a letter or two off, or the same
// consonants once the ones that sound alike are merged - and the best artists'
// tracks are matched against the rest, by title or by album. A query that is
// only an artist plays one of the artist's best-known tracks. Every step is
// data here and one HTTP request in the instance, so the matching is tested
// without a server.
//
// A match is played through a URL the player fetches itself: the part as it
// is for MP3, FLAC and WAV (the satellite decodes those), the universal
// transcoder's MP3 for anything else, both with a delegation token minted for
// this answer - the server's own token never leaves the adapter.
#include <cstdint>
#include <functional>
#include <optional>
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
    /// Last.fm's listener count as Plex keeps it: how well known a track is.
    std::int64_t ratingCount = 0;
    int index = 0;       ///< position on its album
    int parentIndex = 0; ///< disc
    /// The part's path on the server and its container ("mp3", "mp4"...).
    std::string partKey;
    std::string container;
    /// 0..100, how well it answers the query.
    int score = 0;
};

struct Artist {
    std::string ratingKey;
    std::string title;
};

/// What one `/hubs/search` answer holds that the search can use.
struct SearchHubs {
    std::vector<Track> tracks;
};

/// Letters and digits only, lowercased: "Thankyou, Stars" and "thank you
/// stars" are one string. A dumb fold on purpose - a title with an umlaut
/// is typed with it on both sides.
std::string normalised(const std::string &text);

/// How well a track answers the whole query (artist and title in any order,
/// extra words tolerated), 0 when it does not.
int scoreTrack(const Track &track, const std::string &query);
/// How well a title answers what was said for it: equal, contained, or a
/// few letters off ("thank you starrs"), 0 when none.
int scoreTitle(const std::string &title, const std::string &titleQuery);
/// How well an artist's name answers what was said for it, word by word.
/// Words of the name nobody said count against it, so "Robbie Williams"
/// beats "Robbie Williams Feat. Jon Lovitz" for "robin williams".
int scoreArtist(const std::string &artistName, const std::string &artistQuery);

/// One reading of the query: its first `words` words name `artist`, the
/// rest is the title (empty: the artist alone).
struct ArtistGuess {
    Artist artist;
    int score = 0;
    std::string title;
};
/// Every front part of the query against every artist, the readings worth
/// following, best first. A reading is worth it at 50 (60 for the artist
/// alone, which nothing confirms), or at 30 when nothing
/// else comes close - a transcriber's "Paparaz Dreisand" still names one
/// artist far better than any other.
std::vector<ArtistGuess> guessArtists(const std::vector<Artist> &artists, const std::string &query);
/// The distinct artists of the guesses, best first, at most `limit`.
std::vector<Artist> artistsToFetch(const std::vector<ArtistGuess> &guesses, std::size_t limit);
/// The artist's tracks scored against every guess for that artist: a title
/// or an album named, or, for the artist alone, one of the best-known tracks -
/// `pick` chooses among the `pickAmong` best-known (a random index in the instance,
/// fixed in tests) so asking twice does not always play the same song.
std::vector<Track> matchArtistTracks(std::vector<Track> tracks, const std::vector<ArtistGuess> &guesses,
                                     const std::string &artistKey, const std::function<std::size_t(std::size_t)> &pick,
                                     std::size_t pickAmong = 10);

/// `/hubs/search` JSON into its tracks.
SearchHubs parseHubs(const std::string &body);
/// `/library/metadata/<key>/allLeaves` JSON into tracks.
std::vector<Track> parseTracks(const std::string &body);
/// `/library/sections` JSON into the keys of the music sections.
std::vector<std::string> parseMusicSections(const std::string &body);
/// `/library/sections/<key>/all?type=8` JSON into artists.
std::vector<Artist> parseArtists(const std::string &body);
/// `/security/token` JSON into the delegation token; empty when none.
std::string parseDelegationToken(const std::string &body);
/// The album a track is on: its key, its title and the library section.
struct AlbumOf {
    std::string key;
    std::string title;
    std::string sectionId;
};
/// `/library/metadata/<key>` JSON of a track: its album; none when the item
/// is not a track or its title is not `title` (an item of another server
/// that happens to share the key).
std::optional<AlbumOf> parseAlbumOf(const std::string &body, const std::string &title);
/// "Next song" (`media.step`): the track `step` places from `ratingKey` on
/// its album, in disc and track order and round at the ends; none when the
/// album holds no other track, or not this one.
std::optional<Track> albumNeighbour(std::vector<Track> albumTracks, const std::string &ratingKey, int step);

/// The server paths of the search, in the order they are asked.
std::string hubsSearchPath(const std::string &query, int limit);
std::string sectionsPath();
std::string artistsPath(const std::string &sectionKey);
std::string artistTracksPath(const std::string &artistKey);
std::string delegationTokenPath();
std::string metadataPath(const std::string &ratingKey);
std::string albumTracksPath(const std::string &albumKey);
/// Every track of a section on an album of this title: a sampler whose
/// tracks name their own artists is one album per artist on the server, a
/// track each ("Love Compilation", pi5 2026-10-26).
std::string albumTitleTracksPath(const std::string &sectionId, const std::string &albumTitle);

/// The playable URL and MIME type of a track, with the token minted for it.
struct Playable {
    std::string url;
    std::string mimeType;
};
Playable playableFor(const Track &track, const PlexSettings &settings, const std::string &delegationToken);

/// The action's answer: the best `limit` tracks as the README's items, the
/// highest score first, each track once.
Json itemsJson(std::vector<Track> tracks, const PlexSettings &settings, const std::string &delegationToken,
               std::size_t limit);

/// The library's artists whose names sound nearest to what was heard - the
/// letters in threes shared, a long name not winning by length - as bare
/// names ("Robbie Williams", not "Robbie Williams Feat. ..."), at most
/// `limit`. The answer carries them as `names`: a transcriber told them
/// reads the same sentence again far better (the voice bench, 2026-10-25:
/// 24 to 33 of 36 requests found).
std::vector<std::string> nearestArtistNames(const std::vector<Artist> &artists, const std::string &heard, std::size_t limit);
/// An artist's best-known titles (Last.fm's listeners), each once, at most
/// `limit` - with the names, what the transcriber is told when it reads a
/// request again: "9 Millen Beisekos" came back "Nine Million Bicycles" once
/// Katie Melua's titles were in the hint (pi5, 2026-10-26).
std::vector<std::string> bestKnownTitles(std::vector<Track> tracks, std::size_t limit);

} // namespace phicore::plex::ipc

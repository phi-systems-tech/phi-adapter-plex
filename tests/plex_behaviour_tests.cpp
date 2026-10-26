#include <phi/adapter/testing/check.h>

#include "plex_model.h"
#include "plex_protocol.h"
#include "plex_schema.h"
#include "plex_search.h"
#include "plex_server.h"
#include "plex_settings.h"

#include <algorithm>
#include <string>

using namespace phicore::plex::ipc;
namespace sdk = phicore::adapter::sdk;
namespace v1 = phicore::adapter::v1;

namespace {

PlexSettings settings()
{
    PlexSettings value;
    value.serverHost = "10.0.0.10";
    value.serverPort = 32400;
    value.token = "secret token";
    value.serverMachineIdentifier = "server-id";
    value.playerHost = "10.0.0.20";
    value.playerPort = 32500;
    value.playerClientIdentifier = "player-id";
    value.playerName = "Living room";
    value.controllerIdentifier = "phi-controller";
    return value;
}

sdk::MediaPlayerInvokeRequest request(v1::MediaPlayerAction action)
{
    sdk::MediaPlayerInvokeRequest value;
    value.setCmdId(7).setDeviceExternalId("player-id").setAction(action);
    return value;
}

bool hasHeader(const phicore::adapter::net::HttpClient::Call &call,
               const std::string &name, const std::string &value)
{
    return std::any_of(call.headers().begin(), call.headers().end(), [&](const auto &header) {
        return header.first == name && header.second == value;
    });
}

void testTransportCommands()
{
    const auto play = buildMediaCommand(settings(), request(v1::MediaPlayerAction::Play), 11);
    PHI_CHECK(play.ok);
    PHI_CHECK(play.call.url() ==
              "http://10.0.0.20:32500/player/playback/play?commandID=11&type=video");
    PHI_CHECK(hasHeader(play.call, "X-Plex-Token", "secret token"));
    PHI_CHECK(hasHeader(play.call, "X-Plex-Target-Client-Identifier", "player-id"));

    const auto next = buildMediaCommand(settings(), request(v1::MediaPlayerAction::Next), 12);
    PHI_CHECK(next.ok && next.call.url().find("/skipNext?") != std::string::npos);
    const auto previous = buildMediaCommand(settings(), request(v1::MediaPlayerAction::Previous), 13);
    PHI_CHECK(previous.ok && previous.call.url().find("/skipPrevious?") != std::string::npos);
}

void testSeekAndPlayMedia()
{
    auto seek = request(v1::MediaPlayerAction::SeekTo);
    seek.setPositionMs(12345);
    const auto builtSeek = buildMediaCommand(settings(), seek, 20);
    PHI_CHECK(builtSeek.ok && builtSeek.call.url().find("offset=12345") != std::string::npos);

    auto media = request(v1::MediaPlayerAction::PlayMedia);
    media.setMediaId("456").setMediaType("movie").setPositionMs(900);
    const auto built = buildMediaCommand(settings(), media, 21);
    PHI_CHECK(built.ok);
    PHI_CHECK(built.call.url().find("key=%2Flibrary%2Fmetadata%2F456") != std::string::npos);
    PHI_CHECK(built.call.url().find("machineIdentifier=server-id") != std::string::npos);
    PHI_CHECK(built.call.url().find("token=secret%20token") != std::string::npos);

    media.setMediaId("https://example.invalid/movie.mp4");
    PHI_CHECK(!buildMediaCommand(settings(), media, 22).ok);
    media.setMediaId("456").setEnqueue(v1::MediaPlayerEnqueue::Add);
    const auto enqueue = buildMediaCommand(settings(), media, 23);
    PHI_CHECK(!enqueue.ok && enqueue.status == v1::CmdStatus::NotSupported);
}

void testIdentityParsing()
{
    PHI_CHECK(machineIdentifierFromIdentity(
                  R"({"MediaContainer":{"machineIdentifier":"json-id"}})") == "json-id");
    PHI_CHECK(machineIdentifierFromIdentity(
                  R"(<MediaContainer machineIdentifier="xml-id" size="0"/>)") == "xml-id");
    PHI_CHECK(machineIdentifierFromIdentity("not plex").empty());
}

void testDeviceCapabilities()
{
    const v1::Device device = buildDevice(settings());
    const auto expected = v1::MediaPlayerFeature::Play
        | v1::MediaPlayerFeature::Pause
        | v1::MediaPlayerFeature::Stop
        | v1::MediaPlayerFeature::Next
        | v1::MediaPlayerFeature::Previous
        | v1::MediaPlayerFeature::SeekTo
        | v1::MediaPlayerFeature::PlayMedia;
    PHI_CHECK(device.externalId() == "player-id");
    PHI_CHECK(device.mediaPlayerFeatures() == expected);

    PlexSettings notReady = settings();
    notReady.serverMachineIdentifier.clear();
    const auto expectedNotReady = v1::MediaPlayerFeature::Play
        | v1::MediaPlayerFeature::Pause
        | v1::MediaPlayerFeature::Stop
        | v1::MediaPlayerFeature::Next
        | v1::MediaPlayerFeature::Previous
        | v1::MediaPlayerFeature::SeekTo;
    PHI_CHECK(buildDevice(notReady).mediaPlayerFeatures() == expectedNotReady);
}

void testPercentEncoding()
{
    PHI_CHECK(urlEncode("/library/metadata/1") == "%2Flibrary%2Fmetadata%2F1");
    PHI_CHECK(urlEncode("a b+c") == "a%20b%2Bc");
}

void testHttpStatusMapping()
{
    phicore::adapter::net::HttpClient::Result unauthorized;
    unauthorized.setStatus(401);
    PHI_CHECK(statusForHttp(unauthorized) == v1::CmdStatus::NotAuthorized);

    phicore::adapter::net::HttpClient::Result rejected;
    rejected.setStatus(404);
    PHI_CHECK(statusForHttp(rejected) == v1::CmdStatus::Failure);

    phicore::adapter::net::HttpClient::Result offline;
    PHI_CHECK(statusForHttp(offline) == v1::CmdStatus::TemporarilyOffline);
}

// What the server answered on the bench (2026-10-25), shortened to what the
// search reads.
constexpr const char *kHubsTrack = R"({"MediaContainer":{"Hub":[
  {"type":"artist","size":1,"Metadata":[{"ratingKey":"3277","type":"artist","title":"Katie Melua"}]},
  {"type":"track","size":1,"Metadata":[{"ratingKey":"3287","type":"track","title":"Thankyou, Stars",
    "grandparentTitle":"Katie Melua","parentTitle":"Piece by Piece","duration":219521,
    "Media":[{"container":"mp4","audioCodec":"aac","Part":[{"key":"/library/parts/2317/1451267003/file.m4a","container":"mp4"}]}]}]}]}})";
constexpr const char *kLeaves = R"({"MediaContainer":{"size":3,"Metadata":[
  {"ratingKey":"3287","type":"track","title":"Thankyou, Stars","grandparentTitle":"Katie Melua","parentTitle":"Piece by Piece","duration":219521,
   "ratingCount":105355,"index":5,"parentIndex":1,
   "Media":[{"container":"mp4","Part":[{"key":"/library/parts/2317/1/file.m4a","container":"mp4"}]}]},
  {"ratingKey":"3290","type":"track","title":"Nine Million Bicycles","grandparentTitle":"Katie Melua","parentTitle":"Piece by Piece","duration":195000,
   "ratingCount":900000,"index":2,"parentIndex":1,
   "Media":[{"container":"mp3","Part":[{"key":"/library/parts/2320/1/file.mp3","container":"mp3"}]}]},
  {"ratingKey":"3291","type":"track","title":"Shy Boy","grandparentTitle":"Katie Melua","parentTitle":"Piece by Piece","duration":195000,
   "ratingCount":1000,"index":1,"parentIndex":1,
   "Media":[{"container":"mp3","Part":[{"key":"/library/parts/2321/1/file.mp3","container":"mp3"}]}]}]}})";

// A slice of the household's 636 artists, the confusable ones kept.
std::vector<Artist> library()
{
    return {{"1", "Barbra Streisand"}, {"2", "Barbra Streisand & Celine Dion"}, {"3", "Katie Melua"},
            {"4", "Robbie Williams"}, {"5", "Robbie Williams & Nicole Kidman"},
            {"6", "Robbie Williams Feat. Jon Lovitz"}, {"7", "J. Williams Feat. Dane Rumble"},
            {"8", "Kate Bush"}, {"9", "Bruce Springsteen"}, {"10", "Paul Simon"}};
}

void testSearchScores()
{
    PHI_CHECK(normalised("Thankyou, Stars") == "thankyoustars");

    const std::vector<Track> leaves = parseTracks(kLeaves);
    PHI_CHECK(leaves.size() == 3);
    const Track &stars = leaves.front();
    PHI_CHECK(stars.ratingKey == "3287" && stars.artist == "Katie Melua" && stars.container == "mp4");
    PHI_CHECK(stars.durationMs == 219521 && stars.ratingCount == 105355 && stars.index == 5);

    // The whole query: artist and title in one breath, in either order.
    PHI_CHECK(scoreTrack(stars, "Katie Melua Thankyou Stars") == 100);
    PHI_CHECK(scoreTrack(stars, "thank you stars katie melua") == 100);
    PHI_CHECK(scoreTrack(stars, "katie melua thank you stars please") == 95);
    PHI_CHECK(scoreTrack(stars, "bicycles") == 0);
    Track aria;
    aria.title = "Kálmán: Die Csárdásfürstin - Ich Warte Auf Das Große Wunder";
    aria.artist = "Anneliese Rothenberger";
    PHI_CHECK(scoreTrack(aria, "warte") == 0);
    // A title a letter off, as a transcriber writes it.
    PHI_CHECK(scoreTitle("Thankyou, Stars", "thank you starrs") >= 90);
    PHI_CHECK(scoreTitle("Thankyou, Stars", "thank you stars") == 100);
    PHI_CHECK(scoreTitle("Nine Million Bicycles", "thank you stars") == 0);
    PHI_CHECK(scoreTitle("Angels", "angels") == 100);
    // The work inside a long classical title, a letter off.
    PHI_CHECK(scoreTitle("Aida: “Ritorna vincitor”", "Aida") == 90);
    PHI_CHECK(scoreTitle("RIGOLETTO - Tochter - Mein Vater", "Ricoletto") == 90);
    PHI_CHECK(scoreTitle("Un ballo in maschera: “Ecco l’orrido campo”", "maschera") == 80);
    PHI_CHECK(scoreTitle("Come Undone", "irgendwas") == 0);
    // By sound, word for word.
    PHI_CHECK(scoreTitle("Feel", "viel") == 60);
    PHI_CHECK(scoreTitle("Feel", "fear") == 0);
    PHI_CHECK(scoreTitle("Come Undone", "viel") == 0);

    // Names as they were heard on pi5 (2026-10-25).
    PHI_CHECK(scoreArtist("Robbie Williams", "robin williams") >= 85);
    PHI_CHECK(scoreArtist("Robbie Williams Feat. Jon Lovitz", "robin williams") < 50);
    PHI_CHECK(scoreArtist("Katie Melua", "kette melua") >= 75);
    PHI_CHECK(scoreArtist("Katie Melua", "kate melua") >= 85);
    PHI_CHECK(scoreArtist("Barbra Streisand", "paparaz dreisand") >= 30);
    PHI_CHECK(scoreArtist("Bruce Springsteen", "paparaz dreisand") == 0);
    // Cut at the wrong places: written together it is two letters off.
    PHI_CHECK(scoreArtist("Katie Melua", "Kat im Lua") >= 75);
    PHI_CHECK(scoreArtist("Katie Melua", "kat") == 0);
}

// What a second reading is told: the names nearest to what was heard, bare.
void testNearestNames()
{
    const std::vector<Artist> artists = library();
    const std::vector<std::string> kette = nearestArtistNames(artists, "kette melua", 3);
    PHI_CHECK(!kette.empty() && kette.front() == "Katie Melua");
    const std::vector<std::string> robbe = nearestArtistNames(artists, "Robbe William Steele", 5);
    PHI_CHECK(!robbe.empty() && robbe.front() == "Robbie Williams");
    // One name once, whatever it was sung with.
    PHI_CHECK(std::count(robbe.begin(), robbe.end(), "Robbie Williams") == 1);
    PHI_CHECK(std::find(robbe.begin(), robbe.end(), "Robbie Williams Feat. Jon Lovitz") == robbe.end());
    const std::vector<std::string> barbra = nearestArtistNames(artists, "Bavarian Drei Zahn", 5);
    PHI_CHECK(std::find(barbra.begin(), barbra.end(), "Barbra Streisand") != barbra.end());
    PHI_CHECK(nearestArtistNames(artists, "", 5).empty());

    const std::vector<std::string> titles = bestKnownTitles(parseTracks(R"({"MediaContainer":{"Metadata":[
          {"ratingKey":"1","type":"track","title":"Shy Boy","grandparentTitle":"Katie Melua","ratingCount":10},
          {"ratingKey":"2","type":"track","title":"Nine Million Bicycles","grandparentTitle":"Katie Melua","ratingCount":900},
          {"ratingKey":"3","type":"track","title":"Nine Million Bicycles","grandparentTitle":"Katie Melua","ratingCount":800}]}})"), 5);
    PHI_CHECK(titles.size() == 2 && titles.front() == "Nine Million Bicycles");
}

void testArtistReadings()
{
    const std::vector<Artist> artists = library();
    const auto first = [](const std::vector<Track> &tracks) {
        const auto best = std::max_element(tracks.begin(), tracks.end(),
                                           [](const Track &a, const Track &b) { return a.score < b.score; });
        return best == tracks.end() ? std::string() : best->title;
    };

    // "Robin Williams Angels": the artist with the title behind, the bare
    // "Robbie Williams" first among the Williamses.
    const std::vector<ArtistGuess> robbie = guessArtists(artists, "Robin Williams Angels");
    PHI_CHECK(!robbie.empty() && robbie.front().artist.title == "Robbie Williams" && robbie.front().title == "Angels");
    const std::vector<Track> angels = matchArtistTracks(
        parseTracks(R"({"MediaContainer":{"Metadata":[
          {"ratingKey":"50","type":"track","title":"Angels","grandparentTitle":"Robbie Williams","parentTitle":"Life thru a Lens","ratingCount":2000000},
          {"ratingKey":"51","type":"track","title":"Feel","grandparentTitle":"Robbie Williams","parentTitle":"Escapology","ratingCount":1500000}]}})"),
        robbie, "4", [](std::size_t) { return std::size_t{0}; });
    PHI_CHECK(first(angels) == "Angels");

    // "Paparaz Dreisand": "paparaz" belongs to no name, so it is not the
    // artist alone - and a title "Dreisand" is not found. Not understood
    // rather than a guess.
    for (const ArtistGuess &g : guessArtists(artists, "Paparaz Dreisand"))
        PHI_CHECK(!g.title.empty());
    // A title said and not found is not a random song by the artist: the
    // best-known come back only to be asked about, below any vote.
    for (const ArtistGuess &g : guessArtists(artists, "Robbe William Steele"))
        PHI_CHECK(!g.title.empty());
    const std::vector<ArtistGuess> steele = guessArtists(artists, "Robbe William Steele");
    const std::vector<Track> none = matchArtistTracks(
        parseTracks(R"({"MediaContainer":{"Metadata":[
          {"ratingKey":"51","type":"track","title":"Feel","grandparentTitle":"Robbie Williams","ratingCount":1500000}]}})"),
        steele, "4", [](std::size_t) { return std::size_t{0}; });
    PHI_CHECK(none.size() == 1 && none.front().title == "Feel" && none.front().score < 50);

    // The artist alone plays one of the best-known tracks: the pick is among
    // them, the best-known first.
    const std::vector<ArtistGuess> katie = guessArtists(artists, "kette melua");
    PHI_CHECK(!katie.empty() && katie.front().artist.title == "Katie Melua" && katie.front().title.empty());
    const std::vector<Track> picked = matchArtistTracks(parseTracks(kLeaves), katie, "3",
                                                        [](std::size_t) { return std::size_t{0}; });
    PHI_CHECK(first(picked) == "Nine Million Bicycles");
    const std::vector<Track> second = matchArtistTracks(parseTracks(kLeaves), katie, "3",
                                                        [](std::size_t) { return std::size_t{1}; });
    PHI_CHECK(first(second) == "Thankyou, Stars");

    // An album named: its first track.
    const std::vector<ArtistGuess> album = guessArtists(artists, "katie melua piece by piece");
    const std::vector<Track> opener = matchArtistTracks(parseTracks(kLeaves), album, "3",
                                                        [](std::size_t) { return std::size_t{0}; });
    PHI_CHECK(first(opener) == "Shy Boy");

    // Nothing in the library sounds like it.
    PHI_CHECK(guessArtists(artists, "the rolling stones").empty());
    PHI_CHECK(artistsToFetch(robbie, 2).size() == 1);
}

void testSearchParsing()
{
    const SearchHubs hubs = parseHubs(kHubsTrack);
    PHI_CHECK(hubs.tracks.size() == 1 && hubs.tracks.front().title == "Thankyou, Stars");
    PHI_CHECK(parseMusicSections(R"({"MediaContainer":{"Directory":[{"key":"1","type":"movie"},{"key":"2","type":"artist"}]}})")
              == std::vector<std::string>{"2"});
    const std::vector<Artist> artists = parseArtists(R"({"MediaContainer":{"Metadata":[{"ratingKey":"3277","title":"Katie Melua"}]}})");
    PHI_CHECK(artists.size() == 1 && artists.front().title == "Katie Melua");
    PHI_CHECK(hubsSearchPath("katy melua", 3) == "/hubs/search?query=katy%20melua&limit=3&includeCollections=0&includeExternalMedia=0");
    PHI_CHECK(artistsPath("2") == "/library/sections/2/all?type=8");
    PHI_CHECK(artistTracksPath("3277") == "/library/metadata/3277/allLeaves");
    PHI_CHECK(parseDelegationToken(R"({"MediaContainer":{"size":0,"token":"abc"}})") == "abc");
    PHI_CHECK(parseDelegationToken("{}").empty());
}

void testPlayableUrls()
{
    const PlexSettings s = settings();
    const std::vector<Track> leaves = parseTracks(kLeaves);
    // AAC in MP4 goes through the transcoder as MP3; the delegation token
    // rides along and the server's own token does not.
    const Playable transcoded = playableFor(leaves.front(), s, "DELEGATED");
    PHI_CHECK(transcoded.mimeType == "audio/mpeg");
    PHI_CHECK(transcoded.url.rfind("http://10.0.0.10:32400/music/:/transcode/universal/start.mp3?", 0) == 0);
    PHI_CHECK(transcoded.url.find("path=%2Flibrary%2Fmetadata%2F3287") != std::string::npos);
    PHI_CHECK(transcoded.url.find("X-Plex-Token=DELEGATED") != std::string::npos);
    PHI_CHECK(transcoded.url.find("secret") == std::string::npos);
    // MP3 is fetched as it is.
    const Playable direct = playableFor(leaves[1], s, "DELEGATED");
    PHI_CHECK(direct.mimeType == "audio/mpeg");
    PHI_CHECK(direct.url == "http://10.0.0.10:32400/library/parts/2320/1/file.mp3?X-Plex-Token=DELEGATED");

    std::vector<Track> scored = leaves;
    scored[0].score = 60;
    scored[1].score = 90;
    const Json items = itemsJson(scored, s, "DELEGATED", 5);
    PHI_CHECK(items["items"].size() == 2);
    PHI_CHECK(items["items"][0]["metadata"]["title"] == "Nine Million Bicycles");
    PHI_CHECK(items["items"][0]["score"] == 90);
    PHI_CHECK(items["items"][1]["mediaType"] == "music");
    PHI_CHECK(itemsJson({}, s, {}, 5)["items"].empty());
}

void testServerOnlyInstance()
{
    PlexSettings serverOnly = settings();
    serverOnly.playerHost.clear();
    serverOnly.playerClientIdentifier.clear();
    PHI_CHECK(serverOnly.hasServer() && !serverOnly.hasPlayer());
    // The action is announced, hidden from the UI, and the player fields are
    // not required any more.
    const v1::AdapterCapabilities caps = capabilities();
    bool announced = false;
    for (const auto &action : caps.instanceActions())
        if (action.id() == "media.search" && action.placement() == v1::AdapterActionPlacement::Hidden) announced = true;
    PHI_CHECK(announced);
    const v1::AdapterConfigSchema schema = configSchema();
    for (const auto &field : schema.instance().fields())
        if (field.key() == "playerHost" || field.key() == "playerClientIdentifier")
            PHI_CHECK(!v1::hasFlag(field.flags(), v1::AdapterConfigFieldFlag::Required));
}

void testServerDevice()
{
    const ServerInfo info = parseServerRoot(R"({"MediaContainer":{"friendlyName":"pi5vs","version":"1.41.6.9685-d301f511a","machineIdentifier":"e201"}})");
    PHI_CHECK(info.name == "pi5vs" && info.version == "1.41.6.9685-d301f511a" && info.machineIdentifier == "e201");
    const v1::Device device = buildServerDevice(info);
    PHI_CHECK(device.externalId() == "server" && device.name() == "Plex pi5vs" && device.firmware() == info.version);
    PHI_CHECK(device.deviceClass() == v1::DeviceClass::Gateway);
    bool sessions = false, title = false, position = false;
    for (const auto &c : serverChannels()) {
        sessions = sessions || c.kind() == v1::ChannelKind::MediaSessions;
        title = title || c.kind() == v1::ChannelKind::MediaTitle;
        position = position || (c.kind() == v1::ChannelKind::MediaPosition && v1::hasFlag(c.flags(), v1::ChannelFlag::NoHistory));
    }
    PHI_CHECK(sessions && title && position);

    // Two streams: the playing one is shown, the paused one counted.
    const SessionsSummary two = parseSessions(R"({"MediaContainer":{"size":2,"Metadata":[
      {"type":"movie","title":"A Star Is Born","duration":8160000,"viewOffset":60000,"Player":{"title":"Wohnzimmer TV","state":"paused"}},
      {"type":"track","title":"Angels","grandparentTitle":"Robbie Williams","duration":264000,"viewOffset":30500,"Player":{"title":"Plexamp","state":"playing"}}]}})");
    PHI_CHECK(two.count == 2 && two.title == "Angels" && two.artist == "Robbie Williams" && two.playedOn == "Plexamp");
    PHI_CHECK(two.playing && two.positionS == 30 && two.durationS == 264);
    const SessionsSummary none = parseSessions(R"({"MediaContainer":{"size":0}})");
    PHI_CHECK(none.count == 0 && none.title.empty() && !none.playing);
}

void testSearchSettings()
{
    const PlexSettings defaults = settingsFromAdapter(v1::Adapter{}, Json::object());
    PHI_CHECK(defaults.musicBitrate == 192 && defaults.artistCacheMinutes == 15 && defaults.artistPickCount == 10);
    PHI_CHECK(defaults.musicSections.empty());
    const PlexSettings set = settingsFromAdapter(v1::Adapter{}, Json{{"musicBitrate", 320}, {"artistPickCount", 3},
                                                                     {"musicSections", " 2, 5 ,"}, {"artistCacheMinutes", 0}});
    PHI_CHECK(set.musicBitrate == 320 && set.artistPickCount == 3 && set.artistCacheMinutes == 1);
    PHI_CHECK(set.musicSections == std::vector<std::string>({"2", "5"}));
    PlexSettings s = settings();
    s.musicBitrate = 128;
    const Playable p = playableFor(parseTracks(kLeaves).front(), s, "T");
    PHI_CHECK(p.url.find("musicBitrate=128") != std::string::npos);
    // The pick count bounds the pick: among one, the best-known always.
    const std::vector<ArtistGuess> katie = guessArtists(library(), "katie melua");
    const std::vector<Track> one = matchArtistTracks(parseTracks(kLeaves), katie, "3",
                                                     [](std::size_t n) { return n - 1; }, 1);
    PHI_CHECK(!one.empty() && one.front().title == "Nine Million Bicycles");
}

} // namespace

int main()
{
    testTransportCommands();
    testSeekAndPlayMedia();
    testIdentityParsing();
    testDeviceCapabilities();
    testPercentEncoding();
    testHttpStatusMapping();
    testSearchScores();
    testNearestNames();
    testArtistReadings();
    testSearchParsing();
    testPlayableUrls();
    testServerOnlyInstance();
    testServerDevice();
    testSearchSettings();
    return phi::testing::report("plex_behaviour_tests");
}

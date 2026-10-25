#include <phi/adapter/testing/check.h>

#include "plex_model.h"
#include "plex_protocol.h"
#include "plex_schema.h"
#include "plex_search.h"
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
// search reads: the artist and the albums for "katie melua", one track for
// "thankyou stars".
constexpr const char *kHubsArtist = R"({"MediaContainer":{"Hub":[
  {"type":"show","size":0},
  {"type":"artist","size":1,"Metadata":[{"ratingKey":"3277","type":"artist","title":"Katie Melua"}]},
  {"type":"album","size":2,"Metadata":[{"ratingKey":"3278","type":"album","title":"Piece by Piece","parentTitle":"Katie Melua"}]},
  {"type":"track","size":0}]}})";
constexpr const char *kHubsTrack = R"({"MediaContainer":{"Hub":[
  {"type":"track","size":1,"Metadata":[{"ratingKey":"3287","type":"track","title":"Thankyou, Stars",
    "grandparentTitle":"Katie Melua","parentTitle":"Piece by Piece","duration":219521,
    "Media":[{"container":"mp4","audioCodec":"aac","Part":[{"key":"/library/parts/2317/1451267003/file.m4a","container":"mp4"}]}]}]}]}})";
constexpr const char *kLeaves = R"({"MediaContainer":{"size":2,"Metadata":[
  {"ratingKey":"3287","type":"track","title":"Thankyou, Stars","grandparentTitle":"Katie Melua","parentTitle":"Piece by Piece","duration":219521,
   "Media":[{"container":"mp4","Part":[{"key":"/library/parts/2317/1/file.m4a","container":"mp4"}]}]},
  {"ratingKey":"3290","type":"track","title":"Nine Million Bicycles","grandparentTitle":"Katie Melua","parentTitle":"Piece by Piece","duration":195000,
   "Media":[{"container":"mp3","Part":[{"key":"/library/parts/2320/1/file.mp3","container":"mp3"}]}]}]}})";

void testSearchNormalisationAndScores()
{
    PHI_CHECK(normalised("Thankyou, Stars") == "thankyoustars");
    PHI_CHECK(normalised("thank you stars") == "thankyoustars");

    const std::vector<Track> leaves = parseTracks(kLeaves);
    PHI_CHECK(leaves.size() == 2);
    const Track &stars = leaves.front();
    PHI_CHECK(stars.ratingKey == "3287" && stars.artist == "Katie Melua" && stars.container == "mp4");
    PHI_CHECK(stars.durationMs == 219521);

    // The whole query: artist and title in one breath, in either order.
    PHI_CHECK(scoreTrack(stars, "Katie Melua Thankyou Stars") == 100);
    PHI_CHECK(scoreTrack(stars, "thank you stars katie melua") == 100);
    PHI_CHECK(scoreTrack(stars, "thank you stars") == 100);
    // Extra words around both: still a confident match.
    PHI_CHECK(scoreTrack(stars, "katie melua thank you stars please") == 95);
    // A title longer than what was said, and the reverse.
    PHI_CHECK(scoreTrack(stars, "thankyou") == 80);
    PHI_CHECK(scoreTrack(stars, "thank you stars live") == 70);
    PHI_CHECK(scoreTrack(stars, "bicycles") == 0);
    // The title half of a split query against the artist's tracks.
    PHI_CHECK(scoreTitle(stars, "thank you stars") == 100);
    PHI_CHECK(scoreTitle(leaves.back(), "thank you stars") == 0);
    PHI_CHECK(scoreTitle(leaves.back(), "nine million") == 85);
}

void testSearchPlan()
{
    const SearchHubs artist = parseHubs(kHubsArtist);
    PHI_CHECK(artist.tracks.empty());
    PHI_CHECK(artist.artistKey == "3277" && artist.artistName == "Katie Melua");
    PHI_CHECK(artist.albumKey == "3278");
    const SearchHubs track = parseHubs(kHubsTrack);
    PHI_CHECK(track.tracks.size() == 1 && track.tracks.front().title == "Thankyou, Stars");
    PHI_CHECK(track.artistKey.empty());

    // The splits, longest artist first, bounded.
    const std::vector<Split> s = splits("katy melua thank you stars", 3);
    PHI_CHECK(s.size() == 3);
    PHI_CHECK(s[0].artist == "katy melua thank you" && s[0].title == "stars");
    PHI_CHECK(s[1].artist == "katy melua thank" && s[1].title == "you stars");
    PHI_CHECK(s[2].artist == "katy melua" && s[2].title == "thank you stars");
    PHI_CHECK(splits("jazz", 3).empty());
    PHI_CHECK(splits("a b", 3).size() == 1);

    PHI_CHECK(hubsSearchPath("katy melua", 3) == "/hubs/search?query=katy%20melua&limit=3&includeCollections=0&includeExternalMedia=0");
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
    const Playable direct = playableFor(leaves.back(), s, "DELEGATED");
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

} // namespace

int main()
{
    testTransportCommands();
    testSeekAndPlayMedia();
    testIdentityParsing();
    testDeviceCapabilities();
    testPercentEncoding();
    testHttpStatusMapping();
    testSearchNormalisationAndScores();
    testSearchPlan();
    testPlayableUrls();
    testServerOnlyInstance();
    return phi::testing::report("plex_behaviour_tests");
}

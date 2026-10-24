#include <phi/adapter/testing/check.h>

#include "plex_model.h"
#include "plex_protocol.h"
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
    value.cmdId = 7;
    value.deviceExternalId = "player-id";
    value.action = action;
    return value;
}

bool hasHeader(const phicore::adapter::net::HttpClient::Call &call,
               const std::string &name, const std::string &value)
{
    return std::any_of(call.headers.begin(), call.headers.end(), [&](const auto &header) {
        return header.first == name && header.second == value;
    });
}

void testTransportCommands()
{
    const auto play = buildMediaCommand(settings(), request(v1::MediaPlayerAction::Play), 11);
    PHI_CHECK(play.ok);
    PHI_CHECK(play.call.url ==
              "http://10.0.0.20:32500/player/playback/play?commandID=11&type=video");
    PHI_CHECK(hasHeader(play.call, "X-Plex-Token", "secret token"));
    PHI_CHECK(hasHeader(play.call, "X-Plex-Target-Client-Identifier", "player-id"));

    const auto next = buildMediaCommand(settings(), request(v1::MediaPlayerAction::Next), 12);
    PHI_CHECK(next.ok && next.call.url.find("/skipNext?") != std::string::npos);
    const auto previous = buildMediaCommand(settings(), request(v1::MediaPlayerAction::Previous), 13);
    PHI_CHECK(previous.ok && previous.call.url.find("/skipPrevious?") != std::string::npos);
}

void testSeekAndPlayMedia()
{
    auto seek = request(v1::MediaPlayerAction::SeekTo);
    seek.positionMs = 12345;
    const auto builtSeek = buildMediaCommand(settings(), seek, 20);
    PHI_CHECK(builtSeek.ok && builtSeek.call.url.find("offset=12345") != std::string::npos);

    auto media = request(v1::MediaPlayerAction::PlayMedia);
    media.mediaId = "456";
    media.mediaType = "movie";
    media.positionMs = 900;
    const auto built = buildMediaCommand(settings(), media, 21);
    PHI_CHECK(built.ok);
    PHI_CHECK(built.call.url.find("key=%2Flibrary%2Fmetadata%2F456") != std::string::npos);
    PHI_CHECK(built.call.url.find("machineIdentifier=server-id") != std::string::npos);
    PHI_CHECK(built.call.url.find("token=secret%20token") != std::string::npos);

    media.mediaId = "https://example.invalid/movie.mp4";
    PHI_CHECK(!buildMediaCommand(settings(), media, 22).ok);
    media.mediaId = "456";
    media.enqueue = v1::MediaPlayerEnqueue::Add;
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
    PHI_CHECK(device.externalId == "player-id");
    PHI_CHECK(device.mediaPlayerFeatures == expected);

    PlexSettings notReady = settings();
    notReady.serverMachineIdentifier.clear();
    const auto expectedNotReady = v1::MediaPlayerFeature::Play
        | v1::MediaPlayerFeature::Pause
        | v1::MediaPlayerFeature::Stop
        | v1::MediaPlayerFeature::Next
        | v1::MediaPlayerFeature::Previous
        | v1::MediaPlayerFeature::SeekTo;
    PHI_CHECK(buildDevice(notReady).mediaPlayerFeatures == expectedNotReady);
}

void testPercentEncoding()
{
    PHI_CHECK(urlEncode("/library/metadata/1") == "%2Flibrary%2Fmetadata%2F1");
    PHI_CHECK(urlEncode("a b+c") == "a%20b%2Bc");
}

void testHttpStatusMapping()
{
    phicore::adapter::net::HttpClient::Result unauthorized;
    unauthorized.status = 401;
    PHI_CHECK(statusForHttp(unauthorized) == v1::CmdStatus::NotAuthorized);

    phicore::adapter::net::HttpClient::Result rejected;
    rejected.status = 404;
    PHI_CHECK(statusForHttp(rejected) == v1::CmdStatus::Failure);

    phicore::adapter::net::HttpClient::Result offline;
    PHI_CHECK(statusForHttp(offline) == v1::CmdStatus::TemporarilyOffline);
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
    return phi::testing::report("plex_behaviour_tests");
}

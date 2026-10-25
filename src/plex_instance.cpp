#include "plex_instance.h"

#include <cstdint>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <random>
#include <vector>
#include <utility>

#include "phi/adapter/net/http_client.h"
#include "phi/runtime/loop.h"

#include "phi/runtime/str.h"

#include "plex_json.h"
#include "plex_model.h"
#include "plex_protocol.h"
#include "plex_search.h"
#include "plex_server.h"
#include "plex_settings.h"

namespace phicore::plex::ipc {

namespace net = phicore::adapter::net;
namespace sdk = phicore::adapter::sdk;
namespace v1 = phicore::adapter::v1;

namespace {

std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

class PlexInstance final : public sdk::AdapterInstance
{
protected:
    bool start() override
    {
        m_loop = phi::runtime::Loop::current();
        if (!m_loop)
            return false;
        m_http.emplace(*m_loop);
        m_lifecycle = Lifecycle::Running;
        return true;
    }

    void stop() override
    {
        m_lifecycle = Lifecycle::Stopped;
        m_sessionsPoll.reset();
        cancelAll("Instance stopped");
        setConnected(false);
        m_http.reset();
        m_loop = nullptr;
    }

    void onDisconnected() override
    {
        m_lifecycle = Lifecycle::Paused;
        m_sessionsPoll.reset();
        cancelAll("Instance disconnected");
        setConnected(false);
    }

    void onConfigChanged(const sdk::ConfigChangedRequest &request) override
    {
        if (m_lifecycle == Lifecycle::Stopped || !m_http)
            return;
        cancelAll("Config changed");
        m_lifecycle = Lifecycle::Running;
        m_settings = settingsFromAdapter(request.adapter(),
                                         parseObject(request.adapter().metaJson()));
        m_artists.clear();
        m_sessionsPoll.reset();
        // A server-only instance announces no device: it is a library to
        // search, not a player (ROADMAP M9 (h)).
        if (m_settings.hasPlayer())
            emitDevice();
        setConnected(false);
        if (m_settings.hasServer())
            probeServer();
    }

    void onAdapterActionInvoke(const sdk::AdapterActionInvokeRequest &request) override
    {
        if (request.actionId() != kSearchAction) {
            answerAction(request.cmdId(), v1::CmdStatus::NotSupported, "Unsupported action");
            return;
        }
        if (m_lifecycle != Lifecycle::Running || !m_http) {
            answerAction(request.cmdId(), v1::CmdStatus::TemporarilyOffline, "Instance is not running");
            return;
        }
        if (!m_settings.hasServer()) {
            answerAction(request.cmdId(), v1::CmdStatus::InvalidArgument,
                         "Plex server host, port and token are required for a search");
            return;
        }
        const std::string query = phi::str::trimmed(jsonString(parseObject(request.paramsJson()), "query"));
        if (query.empty()) {
            answerAction(request.cmdId(), v1::CmdStatus::InvalidArgument, "query is required");
            return;
        }
        auto search = std::make_shared<Search>();
        search->cmdId = request.cmdId();
        search->query = query;
        searchWhole(std::move(search));
    }

    void onMediaPlayerInvoke(const sdk::MediaPlayerInvokeRequest &request) override
    {
        if (m_lifecycle != Lifecycle::Running || !m_http) {
            answer(request.cmdId(), v1::CmdStatus::TemporarilyOffline, "Instance is not running");
            return;
        }
        if (request.deviceExternalId() != m_settings.deviceId()) {
            answer(request.cmdId(), v1::CmdStatus::NotSupported, "Unknown Plex player");
            return;
        }

        const CommandBuildResult built = buildMediaCommand(m_settings, request, ++m_commandId);
        if (!built.ok) {
            answer(request.cmdId(), built.status, built.error);
            return;
        }
        enqueue(request.cmdId(), built.call, [this, cmdId = request.cmdId()](net::HttpClient::Result result) {
            if (result.ok()) {
                setConnected(true);
                answer(cmdId, v1::CmdStatus::Success, {});
                return;
            }
            if (result.status() == 0)
                setConnected(false);
            answer(cmdId, statusForHttp(result), errorForHttp(result, "Plex player command failed"));
        });
    }

private:
    enum class Lifecycle { Idle, Running, Paused, Stopped };

    static constexpr const char *kSearchAction = "media.search";
    static constexpr int kHubLimit = 10;
    static constexpr std::size_t kItemLimit = 5;
    /// Only an exact artist-and-title hit needs no artist reading on top: a
    /// title alone that matches ("Barbra Streisand", a song by Duck Sauce)
    /// must meet the artist of that name (bench, 2026-10-25).
    static constexpr int kConfident = 100;
    /// How many artists' tracks are read for one query.
    static constexpr std::size_t kArtistFetches = 2;

    struct Pending {
        v1::CmdId cmdId = 0;
        /// An action is answered with an ActionResponse, a command with a
        /// CmdResponse; a cancelled one has to know which.
        bool action = false;
        net::HttpClient::Call call;
        net::HttpClient::Done done;
    };

    /// One `media.search` in flight: the whole query, then the artists'
    /// readings (plex_search.h), then the token for the answer.
    struct Search {
        v1::CmdId cmdId = 0;
        std::string query;
        std::vector<ArtistGuess> guesses;
        std::vector<Artist> toFetch;
        std::vector<Track> found;
    };

    void searchWhole(std::shared_ptr<Search> search)
    {
        enqueueAction(search->cmdId, serverCall(m_settings, hubsSearchPath(search->query, kHubLimit)),
                      [this, search](net::HttpClient::Result result) {
            if (!result.ok()) {
                failSearch(*search, result);
                return;
            }
            int best = 0;
            for (Track &track : parseHubs(result.body()).tracks) {
                track.score = scoreTrack(track, search->query);
                best = std::max(best, track.score);
                if (track.score > 0) search->found.push_back(track);
            }
            if (best >= kConfident)
                finishSearch(std::move(search));
            else
                withArtists(std::move(search));
        });
    }

    /// The library's artists, read when missing or old, then the readings.
    void withArtists(std::shared_ptr<Search> search)
    {
        // The artist list is read once and kept as long as the settings say:
        // a library grows by an album now and then, not by the minute.
        if (!m_artists.empty() && nowMs() - m_artistsAtMs < std::int64_t{m_settings.artistCacheMinutes} * 60 * 1000) {
            searchArtists(std::move(search));
            return;
        }
        enqueueAction(search->cmdId, serverCall(m_settings, sectionsPath()),
                      [this, search](net::HttpClient::Result result) {
            if (!result.ok()) {
                failSearch(*search, result);
                return;
            }
            std::vector<std::string> keys = parseMusicSections(result.body());
            if (!m_settings.musicSections.empty())
                std::erase_if(keys, [this](const std::string &key) {
                    return std::find(m_settings.musicSections.begin(), m_settings.musicSections.end(), key)
                        == m_settings.musicSections.end();
                });
            auto sections = std::make_shared<std::vector<std::string>>(std::move(keys));
            auto artists = std::make_shared<std::vector<Artist>>();
            readArtistSections(std::move(search), std::move(sections), std::move(artists));
        });
    }

    void readArtistSections(std::shared_ptr<Search> search, std::shared_ptr<std::vector<std::string>> sections,
                            std::shared_ptr<std::vector<Artist>> artists)
    {
        if (sections->empty()) {
            m_artists = std::move(*artists);
            m_artistsAtMs = nowMs();
            searchArtists(std::move(search));
            return;
        }
        const std::string key = sections->back();
        sections->pop_back();
        enqueueAction(search->cmdId, serverCall(m_settings, artistsPath(key)),
                      [this, search, sections, artists](net::HttpClient::Result result) {
            if (!result.ok()) {
                failSearch(*search, result);
                return;
            }
            std::vector<Artist> more = parseArtists(result.body());
            artists->insert(artists->end(), more.begin(), more.end());
            readArtistSections(search, sections, artists);
        });
    }

    void searchArtists(std::shared_ptr<Search> search)
    {
        search->guesses = guessArtists(m_artists, search->query);
        search->toFetch = artistsToFetch(search->guesses, kArtistFetches);
        fetchNextArtist(std::move(search));
    }

    void fetchNextArtist(std::shared_ptr<Search> search)
    {
        if (search->toFetch.empty()) {
            finishSearch(std::move(search));
            return;
        }
        const Artist artist = search->toFetch.front();
        search->toFetch.erase(search->toFetch.begin());
        enqueueAction(search->cmdId, serverCall(m_settings, artistTracksPath(artist.ratingKey)),
                      [this, search, artist](net::HttpClient::Result result) {
            if (!result.ok()) {
                failSearch(*search, result);
                return;
            }
            std::vector<Track> matched = matchArtistTracks(parseTracks(result.body()), search->guesses,
                                                           artist.ratingKey, [this](std::size_t n) {
                                                               return std::uniform_int_distribution<std::size_t>(0, n - 1)(m_random);
                                                           }, static_cast<std::size_t>(m_settings.artistPickCount));
            search->found.insert(search->found.end(), matched.begin(), matched.end());
            fetchNextArtist(search);
        });
    }

    void finishSearch(std::shared_ptr<Search> search)
    {
        if (search->found.empty()) {
            sendAction(v1::ActionResponse::success(search->cmdId)
                           .setData(dump(itemsJson({}, m_settings, {}, kItemLimit))));
            return;
        }
        // A token minted for this answer: the server's own never leaves here.
        enqueueAction(search->cmdId, serverCall(m_settings, delegationTokenPath()),
                      [this, search](net::HttpClient::Result result) {
            if (!result.ok()) {
                failSearch(*search, result);
                return;
            }
            const std::string token = parseDelegationToken(result.body());
            if (token.empty()) {
                answerAction(search->cmdId, v1::CmdStatus::Failure,
                             "The server did not mint a delegation token");
                return;
            }
            sendAction(v1::ActionResponse::success(search->cmdId)
                           .setData(dump(itemsJson(search->found, m_settings, token, kItemLimit))));
        });
    }

    void failSearch(const Search &search, const net::HttpClient::Result &result)
    {
        if (result.status() == 0)
            setConnected(false);
        answerAction(search.cmdId, statusForHttp(result), errorForHttp(result, "Plex search failed"));
    }

    void enqueueAction(v1::CmdId cmdId, net::HttpClient::Call call, net::HttpClient::Done done)
    {
        m_queue.push_back({cmdId, true, std::move(call), std::move(done)});
        pump();
    }

    void answerAction(v1::CmdId cmdId, v1::CmdStatus status, const std::string &errorText)
    {
        sendAction(v1::ActionResponse::failure(cmdId, status, errorText));
    }

    void sendAction(const v1::ActionResponse &response)
    {
        v1::Utf8String error;
        if (!sendResult(response, &error))
            log(sdk::LogLevel::Error, sdk::LogCategory::Internal,
                "failed to answer Plex action: %1", {error});
    }

    void emitDevice()
    {
        v1::Utf8String error;
        if (!sendDeviceUpdated(buildDevice(m_settings), {}, &error))
            log(sdk::LogLevel::Error, sdk::LogCategory::Internal,
                "failed to publish Plex player: %1", {error});
    }

    void probeServer()
    {
        enqueue(0, serverCall(m_settings, "/identity"), [this](net::HttpClient::Result result) {
            if (!result.ok()) {
                setConnected(false);
                return;
            }
            const std::string detected = machineIdentifierFromIdentity(result.body());
            if (!detected.empty() && detected != m_settings.serverMachineIdentifier) {
                m_settings.serverMachineIdentifier = detected;
                if (m_settings.hasPlayer())
                    emitDevice();
            }
            // Without a player the server answering is the connection.
            if (m_settings.hasPlayer())
                probePlayer();
            else
                setConnected(true);
            readServer();
        });
    }

    /// The server as a device: what it is, then what its players play, asked
    /// every ten seconds - a Plex app reports its timeline about as often.
    void readServer()
    {
        enqueue(0, serverCall(m_settings, serverRootPath()), [this](net::HttpClient::Result result) {
            if (!result.ok())
                return;
            m_server = parseServerRoot(result.body());
            v1::Utf8String error;
            if (!sendDeviceUpdated(buildServerDevice(m_server), serverChannels(), &error))
                log(sdk::LogLevel::Error, sdk::LogCategory::Internal, "failed to publish the Plex server: %1", {error});
            m_serverPublished = true;
            publishServerConnectivity();
            readSessions();
            if (m_loop && !m_sessionsPoll)
                m_sessionsPoll = m_loop->timerEvery(std::chrono::seconds(10), [this]() { readSessions(); });
        });
    }

    void readSessions()
    {
        if (m_lifecycle != Lifecycle::Running || !m_http || !m_serverPublished)
            return;
        enqueue(0, serverCall(m_settings, sessionsPath()), [this](net::HttpClient::Result result) {
            if (!result.ok()) {
                if (result.status() == 0)
                    setConnected(false);
                return;
            }
            if (!m_settings.hasPlayer())
                setConnected(true);
            const SessionsSummary now = parseSessions(result.body());
            const std::int64_t ts = nowMs();
            const auto send = [&](const char *channel, const v1::ScalarValue &value) {
                v1::Utf8String error;
                sendChannelStateUpdated(kServerDeviceId, channel, value, ts, &error);
            };
            send(serverch::Sessions, static_cast<std::int64_t>(now.count));
            send(serverch::Playing, now.playing);
            send(serverch::Title, now.title);
            send(serverch::Artist, now.artist);
            send(serverch::PlayedOn, now.playedOn);
            send(serverch::Duration, now.durationS);
            // Where it stands now: a client counts on from this report while it
            // plays, so a paused stream is only written when it changes.
            if (now.playing || now.positionS != m_lastSessions.positionS || now.title != m_lastSessions.title)
                send(serverch::Position, now.positionS);
            m_lastSessions = now;
        });
    }

    void publishServerConnectivity()
    {
        if (!m_serverPublished)
            return;
        v1::Utf8String error;
        sendChannelStateUpdated(kServerDeviceId, serverch::Connectivity,
                                static_cast<std::int64_t>(m_connected ? v1::ConnectivityStatus::Connected
                                                                      : v1::ConnectivityStatus::Disconnected),
                                nowMs(), &error);
    }

    void probePlayer()
    {
        const std::string path = "/resources?commandID=" + std::to_string(++m_commandId);
        enqueue(0, playerCall(m_settings, path), [this](net::HttpClient::Result result) {
            setConnected(result.ok());
        });
    }

    void enqueue(v1::CmdId cmdId, net::HttpClient::Call call, net::HttpClient::Done done)
    {
        m_queue.push_back({cmdId, false, std::move(call), std::move(done)});
        pump();
    }

    void pump()
    {
        if (!m_http || m_http->busy() || m_active || m_queue.empty()
            || m_lifecycle != Lifecycle::Running)
            return;
        m_active = std::move(m_queue.front());
        m_queue.pop_front();
        const bool issued = m_http->send(m_active->call, [this](net::HttpClient::Result result) {
            net::HttpClient::Done done = std::move(m_active->done);
            m_active.reset();
            if (done)
                done(std::move(result));
            pump();
        });
        if (!issued) {
            Pending failed = std::move(*m_active);
            m_active.reset();
            net::HttpClient::Result result;
            result.setError("HTTP request could not be issued");
            if (failed.done)
                failed.done(std::move(result));
            pump();
        }
    }

    void cancelAll(const std::string &reason)
    {
        if (m_http)
            m_http->cancel();
        const auto refuse = [&](const Pending &pending) {
            if (pending.cmdId == 0)
                return;
            if (pending.action)
                answerAction(pending.cmdId, v1::CmdStatus::Failure, reason);
            else
                answer(pending.cmdId, v1::CmdStatus::Failure, reason);
        };
        if (m_active)
            refuse(*m_active);
        m_active.reset();
        std::deque<Pending> queued = std::move(m_queue);
        m_queue.clear();
        for (const Pending &pending : queued)
            refuse(pending);
    }

    void setConnected(bool connected)
    {
        if (m_connected == connected)
            return;
        m_connected = connected;
        v1::Utf8String error;
        if (!sendConnectionStateChanged(connected, &error))
            log(sdk::LogLevel::Error, sdk::LogCategory::Internal,
                "failed to publish Plex connection state: %1", {error});
        publishServerConnectivity();
    }

    void answer(v1::CmdId cmdId, v1::CmdStatus status, const std::string &errorText)
    {
        v1::CmdResponse response;
        response.setId(cmdId).setTsMs(nowMs()).setStatus(status).setError(errorText);
        v1::Utf8String error;
        if (!sendResult(response, &error))
            log(sdk::LogLevel::Error, sdk::LogCategory::Internal,
                "failed to answer Plex command: %1", {error});
    }

    phi::runtime::Loop *m_loop = nullptr;
    std::optional<net::HttpClient> m_http;
    Lifecycle m_lifecycle = Lifecycle::Idle;
    PlexSettings m_settings;
    std::deque<Pending> m_queue;
    std::optional<Pending> m_active;
    std::uint64_t m_commandId = 0;
    bool m_connected = false;
    std::vector<Artist> m_artists;
    std::int64_t m_artistsAtMs = 0;
    ServerInfo m_server;
    bool m_serverPublished = false;
    SessionsSummary m_lastSessions;
    phi::runtime::Timer m_sessionsPoll;
    std::mt19937 m_random{std::random_device{}()};
};

} // namespace

std::unique_ptr<sdk::AdapterInstance> makeInstance()
{
    return std::make_unique<PlexInstance>();
}

} // namespace phicore::plex::ipc

#include "plex_instance.h"

#include <cstdint>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

#include "phi/adapter/net/http_client.h"
#include "phi/runtime/loop.h"

#include "plex_json.h"
#include "plex_model.h"
#include "plex_protocol.h"
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
        cancelAll("Instance stopped");
        setConnected(false);
        m_http.reset();
        m_loop = nullptr;
    }

    void onDisconnected() override
    {
        m_lifecycle = Lifecycle::Paused;
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
        emitDevice();
        setConnected(false);
        if (m_settings.hasServer() && m_settings.hasPlayer())
            probeServer();
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

    struct Pending {
        v1::CmdId cmdId = 0;
        net::HttpClient::Call call;
        net::HttpClient::Done done;
    };

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
                emitDevice();
            }
            probePlayer();
        });
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
        m_queue.push_back({cmdId, std::move(call), std::move(done)});
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
        if (m_active && m_active->cmdId != 0)
            answer(m_active->cmdId, v1::CmdStatus::Failure, reason);
        m_active.reset();
        std::deque<Pending> queued = std::move(m_queue);
        m_queue.clear();
        for (const Pending &pending : queued) {
            if (pending.cmdId != 0)
                answer(pending.cmdId, v1::CmdStatus::Failure, reason);
        }
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
};

} // namespace

std::unique_ptr<sdk::AdapterInstance> makeInstance()
{
    return std::make_unique<PlexInstance>();
}

} // namespace phicore::plex::ipc

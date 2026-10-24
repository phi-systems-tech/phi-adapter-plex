#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "phi/adapter/net/http_client.h"
#include "phi/adapter/sdk/loop_execution_backend.h"
#include "phi/adapter/sdk/sidecar.h"
#include "phi/runtime/loop.h"

#include "plex_instance.h"
#include "plex_json.h"
#include "plex_protocol.h"
#include "plex_schema.h"
#include "plex_settings.h"

namespace net = phicore::adapter::net;
namespace sdk = phicore::adapter::sdk;
namespace v1 = phicore::adapter::v1;
using namespace phicore::plex::ipc;

namespace {

class PlexFactory final : public sdk::AdapterFactory
{
protected:
    void onBootstrap(const sdk::BootstrapRequest &request) override
    {
        m_settings = settingsFromAdapter(request.adapter, parseObject(request.adapter.metaJson));
    }

    void onFactoryConfigChanged(const sdk::ConfigChangedRequest &request) override
    {
        m_settings = settingsFromAdapter(request.adapter, parseObject(request.adapter.metaJson));
    }

    std::unique_ptr<sdk::InstanceExecutionBackend> createFactoryExecutionBackend() override
    {
        return sdk::createLoopExecutionBackend("plex-factory");
    }

    std::unique_ptr<sdk::InstanceExecutionBackend> createInstanceExecutionBackend(
        const sdk::ExternalId &externalId) override
    {
        (void)externalId;
        return sdk::createLoopExecutionBackend("plex-instance");
    }

    v1::Utf8String pluginType() const override { return kPluginType; }
    v1::Utf8String displayName() const override { return phicore::plex::ipc::displayName(); }
    v1::Utf8String description() const override { return phicore::plex::ipc::description(); }
    v1::Utf8String iconSvg() const override { return phicore::plex::ipc::iconSvg(); }
    v1::Utf8String apiVersion() const override { return "1.0.0"; }
    int timeoutMs() const override { return 15000; }
    int maxInstances() const override { return 0; }

    v1::AdapterCapabilities capabilities() const override
    {
        return phicore::plex::ipc::capabilities();
    }

    std::optional<v1::AdapterConfigSchema> configSchema() const override
    {
        return phicore::plex::ipc::configSchema();
    }

    std::unique_ptr<sdk::AdapterInstance> createInstance(const sdk::ExternalId &externalId) override
    {
        (void)externalId;
        return makeInstance();
    }

    void onFactoryActionInvoke(const sdk::AdapterActionInvokeRequest &request) override
    {
        if (request.actionId != "probe") {
            answer(request.cmdId, v1::CmdStatus::NotSupported, "Unsupported factory action");
            return;
        }
        PlexSettings settings = m_settings;
        applyProbeParams(parseObject(request.paramsJson), settings);
        if (!settings.hasServer()) {
            answer(request.cmdId, v1::CmdStatus::InvalidArgument,
                   "Plex server host, port and token are required");
            return;
        }
        if (!m_http) {
            phi::runtime::Loop *loop = phi::runtime::Loop::current();
            if (!loop) {
                answer(request.cmdId, v1::CmdStatus::Failure, "No factory event loop");
                return;
            }
            m_http.emplace(*loop);
        }
        if (m_http->busy()) {
            answer(request.cmdId, v1::CmdStatus::Failure, "A Plex probe is already running");
            return;
        }

        const v1::CmdId cmdId = request.cmdId;
        const bool issued = m_http->send(serverCall(settings, "/identity"),
                                         [this, cmdId](net::HttpClient::Result result) {
            if (!result.ok) {
                answer(cmdId, statusForHttp(result),
                       errorForHttp(result, "Plex server probe failed"));
                return;
            }
            const std::string machineId = machineIdentifierFromIdentity(result.body);
            if (machineId.empty()) {
                answer(cmdId, v1::CmdStatus::Failure,
                       "The endpoint answered but did not identify a Plex server");
                return;
            }
            v1::ActionResponse response;
            response.id = cmdId;
            response.tsMs = 0;
            response.status = v1::CmdStatus::Success;
            response.resultType = v1::ActionResultType::String;
            response.resultValue = machineId;
            response.formValues.push_back({"serverMachineIdentifier",
                                           v1::ScalarValue(machineId)});
            sendAction(std::move(response));
        });
        if (!issued)
            answer(cmdId, v1::CmdStatus::Failure, "Plex probe could not be issued");
    }

    void onFactoryStopping() override
    {
        if (m_http)
            m_http->cancel();
        m_http.reset();
    }

private:
    void answer(v1::CmdId cmdId, v1::CmdStatus status, const std::string &error)
    {
        v1::ActionResponse response;
        response.id = cmdId;
        response.tsMs = 0;
        response.status = status;
        response.error = error;
        response.resultType = v1::ActionResultType::None;
        sendAction(std::move(response));
    }

    void sendAction(v1::ActionResponse response)
    {
        v1::Utf8String error;
        if (!sendResult(response, &error))
            log(sdk::LogLevel::Error, sdk::LogCategory::Internal,
                "failed to send Plex factory result: %1", {error});
    }

    PlexSettings m_settings;
    std::optional<net::HttpClient> m_http;
};

} // namespace

int main(int argc, char **argv)
{
    const char *envSocketPath = std::getenv("PHI_ADAPTER_SOCKET_PATH");
    const v1::Utf8String socketPath = argc > 1
        ? argv[1]
        : (envSocketPath ? envSocketPath : v1::Utf8String("/tmp/phi-adapter-plex-ipc.sock"));
    PlexFactory factory;
    sdk::SidecarHost host(socketPath, factory);
    return sdk::runSidecarMain(host);
}

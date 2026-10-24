# phi-adapter-plex

Local Plex Media Server / Plex Companion adapter for phi-core.

The adapter represents one configured Plex player as a media-player device.
The Plex Media Server supplies the library identity and metadata keys; playback
commands are sent to the selected player's Companion HTTP endpoint.

## Supported commands

- Play, pause and stop
- Next and previous queue item
- Absolute seek in milliseconds
- Play a Plex library item by numeric `ratingKey` or
  `/library/metadata/...` key

Relative seek, arbitrary media URLs and enqueue modes are deliberately not
advertised. Plex `playMedia` needs a server metadata key and routing context;
queue mutation additionally needs Plex play-queue management.

## Configuration

- `serverHost`, `serverPort` (normally `32400`), `serverUseTls`
- `token`: Plex access token
- `serverMachineIdentifier`: optional; detected from `/identity` when empty
- `playerHost`, `playerPort` (often `32500` or `32400`), `playerUseTls`
- `playerClientIdentifier`: target player's Plex client identifier
- `playerName`: optional display name
- `controllerIdentifier`: optional stable controller identifier

Plex Companion support and ports vary by client. A successful command means
the player accepted the HTTP request; it is not a playback-state confirmation.
For `PlayMedia`, the Plex protocol passes a token to the target player so it can
fetch the selected item from the configured server. Prefer a trusted local
network and HTTPS where the client supports it.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The adapter uses the local `../phi-adapter-sdk` checkout by default.

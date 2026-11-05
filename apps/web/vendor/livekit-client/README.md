# livekit-client (vendored)

The official LiveKit JavaScript client SDK, used by the web app's Group mode (C19, #1164). A pinned copy is served
from this directory, so the page loads no code from a CDN at runtime.

| | |
|---|---|
| Package | [`livekit-client`](https://www.npmjs.com/package/livekit-client) 2.23.0 (npm) |
| Tarball | `https://registry.npmjs.org/livekit-client/-/livekit-client-2.23.0.tgz` |
| Tarball integrity (npm `dist.integrity`) | `sha512-j9Kg86nJjBjNFUD+ukp2jwMCGa0k+YD5gU//RzAM+ZY3pbG631QCte7fQjwqWMmtBzhDJL9nKYuOLMv/9IbAxg==` |
| File | `package/dist/livekit-client.umd.js`, unchanged (global `LivekitClient`) |
| License | Apache-2.0 ([LICENSE](LICENSE), the package's own). The bundle includes the package's dependencies (`@livekit/protocol`, `events`, `jose`, `loglevel`, `sdp-transform`, `tslib`, `webrtc-adapter` and others), each under its own MIT, BSD or Apache-2.0 license. |

`SHA256SUMS` lists the vendored files. `unit.SignalingService` checks them, and `fetch.sh` downloads the tarball
again, checks it against the integrity above and compares the files:

```sh
apps/web/vendor/livekit-client/fetch.sh            # verify
apps/web/vendor/livekit-client/fetch.sh --update   # replace the files from the tarball (after changing VERSION/INTEGRITY in fetch.sh)
```

To upgrade, change `VERSION` and `INTEGRITY` in `fetch.sh` (from `https://registry.npmjs.org/livekit-client/<version>`),
run it with `--update`, update this file and `SHA256SUMS`, and run the Group tests (`interop.WebApp.group*`) against
the server version deployed (`docs/deploy/livekit.md`).

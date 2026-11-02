# A self-hosted TURN server on Amazon Lightsail (coturn)

The TURN relay for milestone 3 (#944), decided by the Owner on 2026-11-01. Development and the
test bench don't need it: the bench emulates TURN. It is for testing on real networks (#944 step
4) and for production.

Replace `turn.example.com` with your hostname and the angle-bracket values with yours throughout.

## 1. Instance, address and name

1. Lightsail, **Create instance**: Linux, **Ubuntu 24.04 LTS**, a plan with 1-2 GB RAM, in the
   region closest to your users. TURN needs little CPU. The cost to watch is data transfer (see 7).
2. **Networking, Create static IP**, attached to the instance. Note both the public IP and the
   instance's private IP (`ip -4 addr` on the instance). Lightsail puts the instance behind a 1:1
   NAT, so coturn needs both.
3. At your DNS provider: an `A` record `turn.example.com` pointing to the static IP.

## 2. Firewall (instance, Networking, IPv4 firewall)

| Protocol | Port | What for |
|---|---|---|
| TCP | 22 | SSH (Lightsail's default; restrict it to your IP if you can) |
| TCP | 80 | Let's Encrypt's HTTP check, while the certificate is issued and renewed |
| UDP | 3478 | STUN and TURN over UDP |
| TCP | 3478 | TURN over TCP |
| TCP | 443 | **TURN over TLS: what gets through networks that block UDP.** Keep 443 for coturn on this machine. |
| UDP | 49152-50000 | The relay ports; they must match `min-port`/`max-port` below |

## 3. Install coturn and get a certificate

```sh
sudo apt update && sudo apt install -y coturn certbot
sudo certbot certonly --standalone -d turn.example.com --agree-tos -m <you@example.com> -n
```

coturn runs as the `turnserver` user, which can't read Let's Encrypt's private key, so copy the
certificate to it on every renewal:

```sh
sudo mkdir -p /etc/coturn/certs
sudo tee /etc/letsencrypt/renewal-hooks/deploy/coturn.sh >/dev/null <<'EOF'
#!/bin/sh
cp /etc/letsencrypt/live/turn.example.com/fullchain.pem /etc/coturn/certs/fullchain.pem
cp /etc/letsencrypt/live/turn.example.com/privkey.pem /etc/coturn/certs/privkey.pem
chown turnserver:turnserver /etc/coturn/certs/*.pem
chmod 600 /etc/coturn/certs/privkey.pem
systemctl restart coturn
EOF
sudo chmod +x /etc/letsencrypt/renewal-hooks/deploy/coturn.sh
sudo /etc/letsencrypt/renewal-hooks/deploy/coturn.sh
```

Port 443 is below 1024, so give coturn the right to bind it:

```sh
sudo mkdir -p /etc/systemd/system/coturn.service.d
printf '[Service]\nAmbientCapabilities=CAP_NET_BIND_SERVICE\n' | sudo tee /etc/systemd/system/coturn.service.d/override.conf
sudo systemctl daemon-reload
```

## 4. `/etc/turnserver.conf`

Generate the shared secret with `openssl rand -hex 32`. Keep it out of the repository: the
credential service in step 6 needs the same value.

```ini
# Addresses: Lightsail's 1:1 NAT.
listening-port=3478
tls-listening-port=443
listening-ip=<private-ip>
relay-ip=<private-ip>
external-ip=<public-ip>/<private-ip>
min-port=49152
max-port=50000

# Identity and TLS.
realm=turn.example.com
server-name=turn.example.com
cert=/etc/coturn/certs/fullchain.pem
pkey=/etc/coturn/certs/privkey.pem
no-tlsv1
no-tlsv1_1

# Short-lived credentials (the "TURN REST API" scheme that browsers and libwebrtc support).
use-auth-secret
static-auth-secret=<shared-secret>
fingerprint

# Don't let the relay reach private networks (your AWS internals among them).
no-multicast-peers
denied-peer-ip=0.0.0.0-0.255.255.255
denied-peer-ip=10.0.0.0-10.255.255.255
denied-peer-ip=100.64.0.0-100.127.255.255
denied-peer-ip=127.0.0.0-127.255.255.255
denied-peer-ip=169.254.0.0-169.254.255.255
denied-peer-ip=172.16.0.0-172.31.255.255
denied-peer-ip=192.168.0.0-192.168.255.255

# Limits: sessions per user, sessions in all, and bits per second per session.
user-quota=12
total-quota=1200
max-bps=4000000
stale-nonce=600

# Operations.
no-cli
log-file=/var/log/turnserver/turn.log
simple-log
```

Then:

```sh
sudo mkdir -p /var/log/turnserver && sudo chown turnserver:turnserver /var/log/turnserver
sudo sed -i 's/^#\?TURNSERVER_ENABLED=.*/TURNSERVER_ENABLED=1/' /etc/default/coturn
sudo systemctl enable --now coturn
sudo systemctl status coturn --no-pager
```

## 5. Check it

On your computer, open WebRTC's **Trickle ICE** sample page
(`webrtc.github.io/samples/src/content/peerconnection/trickle-ice/`) and add the server with a
credential from step 6. Try `turn:turn.example.com:3478?transport=udp` and then
`turns:turn.example.com:443?transport=tcp`. Gathering should list **`relay`** candidates with the
static IP for both. If no relay candidate appears:

- Check the firewall rows.
- Check that `external-ip` and `relay-ip` are right.
- Read `/var/log/turnserver/turn.log`.

## 6. Issuing credentials (your backend)

Each user gets a username and password valid for a short time, made from the shared secret. The
TURN server checks them without keeping a user list.

```python
import base64, hashlib, hmac, time

TURN_SECRET = "<shared-secret>"  # From your secret store, not from code.

def turn_credentials(user_id: str, ttl_s: int = 3600) -> dict:
    username = f"{int(time.time()) + ttl_s}:{user_id}"
    password = base64.b64encode(hmac.new(TURN_SECRET.encode(), username.encode(), hashlib.sha1).digest()).decode()
    return {
        "urls": [
            "turn:turn.example.com:3478?transport=udp",
            "turn:turn.example.com:3478?transport=tcp",
            "turns:turn.example.com:443?transport=tcp",
        ],
        "username": username,
        "credential": password,
    }
```

Hand this to the app with the call's signaling, behind the user's login. A browser puts it in
`RTCPeerConnection`'s `iceServers`, and our library takes the same fields.

## 7. Running it

- **Data transfer is the cost.** TURN relays all media of the calls that use it, in both directions.
  A 1:1 video call at about 1.5 Mbps each way moves roughly 1.3 GB per hour through the server.
  - Lightsail plans include a monthly transfer allowance and charge for overage per GB; check the
    current plan.
  - Usually only a minority of peer-to-peer calls (often 10-20%) need the relay.
  - The AI server has a public address, so it needs the relay only for clients on networks that
    block UDP.
- **Updates:** `sudo apt upgrade` regularly, or turn on unattended upgrades.
- **Watch:** the instance's network metrics in Lightsail, and the log for authentication failures.
- **Later:** relays in more regions, or a hosted TURN service charged per GB, if relayed traffic grows.

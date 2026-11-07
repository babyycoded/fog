# Fog presence server

Lets Fog clients recognise each other: coloured, animated names and the Fog mark before
the name are shown only to other Fog players, and only through this server.

- No dependencies: Python 3.8+ standard library.
- Nothing is written to disk. A player is forgotten 75 seconds after their last heartbeat.
- What a client sends: a random install id, the player name, the DDNet server address and the
  name style it chose. It receives the Fog players on the same DDNet server.
- Names are not verified, so anyone could claim a name. The data is purely cosmetic.

## Run

```
python3 server.py --host 0.0.0.0 --port 8787
```

DDNet clients only talk HTTPS, so put the server behind a reverse proxy with HTTPS (Caddy,
nginx, Cloudflare Tunnel). In every client set `fog_presence_url "https://your.host"`
(Settings -> Fog -> Players). For a local test over plain HTTP, also set
`http_allow_insecure 1` in the client.

## Check

```
curl http://localhost:8787/v1/health
```

## Cloudflare Worker (free, always on, no own domain)

`worker/` holds the same server as a Cloudflare Worker with one Durable Object. It gets a
permanent `https://fog-presence.<account>.workers.dev` address and runs while your PC is off.

```
cd worker
node test.mjs              # logic check, no Cloudflare needed
npx wrangler login         # opens the browser on this computer
npx wrangler deploy        # prints the workers.dev address
```

Put the printed address into `fog_presence_url`. A custom domain or a VPS can replace it later:
clients only need the new address.

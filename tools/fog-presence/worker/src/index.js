// Fog client presence server, Cloudflare Worker edition. Same protocol as ../server.py:
//   POST /v1/heartbeat {id, name, server, style} -> {players: [...]} on the same DDNet server
//   GET  /v1/health -> {ok, players}
// All players live in one Durable Object, in memory. If Cloudflare evicts it when idle,
// the list simply rebuilds from the next heartbeats (every 20 s).

const TTL_MS = 75_000;
const MIN_INTERVAL_MS = 4_000;
const MAX_ENTRIES = 50_000;
const MAX_BODY = 4096;
const HEX = /^[0-9a-fA-F]{6}$/;
const ID = /^[0-9a-zA-Z]{8,64}$/;
const SERVER = /^[0-9a-zA-Z\[\]:.\-]{3,64}$/;
const TEE = /^[0-9a-zA-Z_,;|.\-]{0,1024}$/; // tee studio look: numbers and accessory ids

function json(status, payload) {
	return new Response(JSON.stringify(payload), {
		status,
		headers: { "content-type": "application/json; charset=utf-8" },
	});
}

export function cleanStyle(style) {
	if (!style || typeof style !== "object") style = {};
	const color = (key, fallback) => (typeof style[key] === "string" && HEX.test(style[key]) ? style[key] : fallback);
	const mode = Number.isInteger(style.mode) && style.mode >= 0 && style.mode <= 5 ? style.mode : 0;
	const speed = Number.isInteger(style.speed) && style.speed >= 25 && style.speed <= 400 ? style.speed : 100;
	const out = { mode, c1: color("c1", "c8d4e0"), c2: color("c2", "9fb0c8"), speed, badge: style.badge !== false };
	if (typeof style.tee === "string" && TEE.test(style.tee)) out.tee = style.tee;
	return out;
}

export class PresenceState {
	constructor() {
		this.entries = new Map(); // id -> {name, server, style, seen}
	}

	expire(now) {
		for (const [key, value] of this.entries) if (now - value.seen > TTL_MS) this.entries.delete(key);
	}

	health(now) {
		this.expire(now);
		return json(200, { ok: true, players: this.entries.size });
	}

	heartbeat(data, now) {
		const { id, name, server } = data || {};
		if (!(typeof id === "string" && ID.test(id) && typeof name === "string" && name.length > 0 && name.length <= 32 && typeof server === "string" && SERVER.test(server)))
			return json(400, { error: "bad fields" });
		this.expire(now);
		const previous = this.entries.get(id);
		let status = 200;
		if (previous && now - previous.seen < MIN_INTERVAL_MS && previous.server === server) status = 429;
		else {
			if (!previous && this.entries.size >= MAX_ENTRIES) return json(503, { error: "full" });
			this.entries.set(id, { name, server, style: cleanStyle(data.style), seen: now });
		}
		const players = [];
		for (const [key, value] of this.entries) if (value.server === server && key !== id) players.push({ name: value.name, style: value.style });
		return json(status, { players });
	}
}

export class Presence {
	constructor(ctx, env) {
		this.state = new PresenceState();
	}

	async fetch(request) {
		const url = new URL(request.url);
		const now = Date.now();
		if (request.method === "GET" && url.pathname === "/v1/health") return this.state.health(now);
		if (request.method === "POST" && url.pathname === "/v1/heartbeat") {
			const length = Number(request.headers.get("content-length") || 0);
			if (length > MAX_BODY) return json(413, { error: "body too large" });
			const text = await request.text();
			if (text.length === 0 || text.length > MAX_BODY) return json(413, { error: "body too large" });
			let data;
			try {
				data = JSON.parse(text);
			} catch {
				return json(400, { error: "bad json" });
			}
			return this.state.heartbeat(data, now);
		}
		return json(404, { error: "not found" });
	}
}

export default {
	async fetch(request, env) {
		// one object for everyone, so every Fog player sees every other one
		const stub = env.PRESENCE.get(env.PRESENCE.idFromName("global"));
		return stub.fetch(request);
	},
};

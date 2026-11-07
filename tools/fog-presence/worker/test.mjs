// quick check of the Worker logic in plain Node (no Cloudflare needed): node test.mjs
import { Presence } from "./src/index.js";
const p = new Presence();
const post = (body) => p.fetch(new Request("https://x/v1/heartbeat", { method: "POST", body: JSON.stringify(body) }));
const a = await (await post({ id: "aaaaaaaaaaaa", name: "Alice", server: "1.2.3.4:8303", style: { mode: 1, c1: "ff00aa", c2: "00ffcc", speed: 150, badge: true, tee: "1|1,2,3,4,100,0,1,5,60,100,30,40,30|1,5,100,100,100,0,1,7,3,0|halo,0,-660,550,0,2,1,3" } })).json();
const b = await (await post({ id: "bbbbbbbbbbbb", name: "Bob", server: "1.2.3.4:8303", style: { mode: 9, tee: "<script>" } })).json();
const bad = await post({ id: "x", name: "", server: "" });
const fast = await post({ id: "aaaaaaaaaaaa", name: "Alice", server: "1.2.3.4:8303" });
const other = await (await post({ id: "cccccccccccc", name: "Carol", server: "9.9.9.9:8303" })).json();
const health = await (await p.fetch(new Request("https://x/v1/health"))).json();
const ok = a.players.length === 0 && b.players.length === 1 && b.players[0].name === "Alice" && b.players[0].style.mode === 1 && b.players[0].style.tee.startsWith("1|") && bad.status === 400 && fast.status === 429 && other.players.length === 0 && health.players === 3;
console.log(JSON.stringify({ a, b, badStatus: bad.status, fastStatus: fast.status, other, health }));
console.log(ok ? "ALL OK" : "FAILED");
process.exit(ok ? 0 : 1);

# Enhancement Requests

## Base Map Cache Separation

### Problem

Map layer refreshes are slow because each RADAR, CLOUDS, and RAIN render builds
a complete full-screen sprite from scratch. For every visible tile, the render
task fetches and decodes both:

- the base map tile for the active map style and zoom
- the weather/radar overlay tile for the active layer

The current implementation renders each layer as a complete composite image in
its own PSRAM sprite. This keeps layer switching simple, but it also means the
same base map tiles are fetched repeatedly for different weather layers even
when the zoom, map style, and location have not changed.

The fetch path is also fully sequential. Each tile request creates a new HTTPS
client, performs a TLS handshake, downloads the PNG, decodes it, then moves to
the next tile. Overlay layers double that request count.

### Goal

Reduce layer render time and network traffic without making display updates less
stable.

The main target is to avoid refetching and decoding base map tiles when only
the weather overlay changes.

### Proposed Approach

Separate the rendered base map from weather overlays:

1. Add a PSRAM-backed base map cache sprite.
   - Key it by zoom, map style, and map center.
   - Invalidate it only when zoom, map style, or location changes.

2. Render base map tiles once into the base map cache.
   - RADAR, CLOUDS, and RAIN renders start by copying the cached base map into
     the render scratch sprite.
   - Only the selected weather overlay is fetched and drawn on top.

3. Keep the existing per-layer composite caches.
   - `cacheRadar`, `cacheClouds`, and `cacheRain` can still store the final
     displayed sprites for fast layer switching.
   - Their cache freshness remains tied to overlay age.

4. Add render timing instrumentation before and after the change.
   - Track base fetch time, overlay fetch time, decode time, tile count, request
     count, and total render time.
   - Keep detailed timing behind `DEBUG_LEVEL >= 4`.
   - Keep a compact render summary at `DEBUG_LEVEL == 3`.

5. Refresh stale inactive overlays in the background when practical.
   - After the active layer finishes, schedule stale inactive layers one at a
     time.
   - Avoid interrupting touch response or OTA updates.

### Expected Benefits

- Fewer external tile requests during layer refresh.
- Less repeated TLS handshake overhead.
- Faster CLOUDS/RAIN/RADAR renders after the base map is warm.
- More predictable render times when only weather overlays are stale.

### Risks And Tradeoffs

- Requires another full-screen PSRAM sprite for the base map cache.
- Cache state becomes more complex: base map freshness and overlay freshness
  must be tracked separately.
- The render task must avoid copying or drawing from a sprite while another path
  is mutating it.
- If PSRAM pressure becomes a problem, a tile-level base cache or single base
  sprite plus final layer cache tradeoff may be needed instead.

### Follow-Up Options

- Test persistent HTTP/1.1 connections after the cache separation is stable.
- Consider a local LAN tile proxy/cache to remove repeated public HTTPS tile
  handshakes from the ESP32 path.
- Reduce the per-tile `delay(50)` if heap stability remains acceptable.
- Add optional per-server timing metrics to identify slow tile providers.

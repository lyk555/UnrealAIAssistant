/**
 * Fetch mocking utilities for tests.
 *
 * Provides helpers to stub the global `fetch` function with
 * URL-based routing, rejection, and hang (timeout) behaviors.
 */

/**
 * Create a Response-like object matching the Fetch API surface.
 * @param {*} body - JSON-serializable body
 * @param {object} opts - { status, statusText, ok }
 */
export function mockResponse(body, opts = {}) {
  const status = opts.status ?? 200;
  const statusText = opts.statusText ?? "OK";
  const ok = opts.ok ?? (status >= 200 && status < 300);
  return {
    ok,
    status,
    statusText,
    json: async () => body,
    text: async () => JSON.stringify(body),
  };
}

/**
 * Install a URL-pattern-based fetch mock.
 *
 * @param {Array<{ pattern: RegExp|string, response: object, body: * }>} routes
 *   Each route has:
 *   - `pattern` — RegExp or string to match against the URL
 *   - `body` — JSON body returned by response.json()
 *   - `response` — optional overrides passed to mockResponse (status, statusText, ok)
 *
 * Returns the vi.fn() spy so callers can assert on call args.
 */
export function installFetchMock(routes) {
  const spy = vi.fn(async (url, _options) => {
    for (const route of routes) {
      const match =
        typeof route.pattern === "string"
          ? url.includes(route.pattern)
          : route.pattern.test(url);

      if (match) {
        return mockResponse(route.body, route.response ?? {});
      }
    }
    // No route matched — return 404
    return mockResponse({ error: "not found" }, { status: 404, statusText: "Not Found" });
  });
  vi.stubGlobal("fetch", spy);
  return spy;
}

/**
 * Install a fetch mock that always rejects with the given error.
 * @param {Error|string} error
 */
export function installFetchReject(error) {
  const err = typeof error === "string" ? new Error(error) : error;
  const spy = vi.fn(async () => {
    throw err;
  });
  vi.stubGlobal("fetch", spy);
  return spy;
}

/**
 * Install a fetch mock that never resolves (for timeout testing).
 * Returns the spy and a cleanup function to avoid leaked promises.
 */
export function installFetchHang() {
  let rejectHanging;
  const spy = vi.fn(() => {
    return new Promise((_resolve, reject) => {
      rejectHanging = reject;
    });
  });
  vi.stubGlobal("fetch", spy);
  return { spy, cleanup: () => rejectHanging && rejectHanging(new Error("cleanup")) };
}

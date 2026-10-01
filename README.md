# c-web-server

A small HTTP/1.1 static file server in C. It listens on IPv4 and IPv6, hands each connection to its own thread, and serves files from a `static/` directory next to the process working directory.

## Build

You need a C compiler, [Meson](https://mesonbuild.com/) 1.3 or newer, and Ninja. POSIX threads (`pthread`) are required.

```sh
meson setup build
meson compile -C build
```

The binary is `build/web-server`.

## Run

Start it from the project root so `static/` resolves:

```sh
./build/web-server          # port 8080
./build/web-server 3000     # custom port
```

Then open [http://localhost:8080/](http://localhost:8080/). `/` serves `static/index.html`.

```
Usage: web-server [port]
```

The process stays in the foreground. Stop it with Ctrl-C. The listen socket uses `SO_REUSEADDR`, so a restart can bind the same port immediately.

## What it serves

Put files under `static/`. A request path is mapped onto that directory:

| Request | File |
| --- | --- |
| `GET /` | `static/index.html` |
| `GET /docs/` | `static/docs/index.html` |
| `GET /app.js` | `static/app.js` |

Query strings and fragments are ignored. Percent-encoding is decoded. `.` segments are skipped.

Paths that would leave the static root are rejected. That includes `..`, backslashes, control characters, and symlinks whose target sits outside `static/`. The server canonicalizes the path with `realpath` before opening the file.

Only regular files are sent. Directories are served only through `index.html`.

### Methods and connections

`GET` is the only supported method. Anything else gets `405` with `Allow: GET`.

HTTP/1.1 connections stay open by default. HTTP/1.0 connections close unless the client sends `Connection: keep-alive`. The server also honors `Connection: close`. A connection is dropped after 100 requests or 15 seconds of idle time.

Responses include `Content-Length` and `Content-Type`. Known types:

| Extension | Content-Type |
| --- | --- |
| `.html`, `.htm` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js` | `text/javascript; charset=utf-8` |
| `.json` | `application/json` |
| `.txt` | `text/plain; charset=utf-8` |
| `.png` | `image/png` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.gif` | `image/gif` |
| `.svg` | `image/svg+xml` |
| `.ico` | `image/x-icon` |
| `.webp` | `image/webp` |

Anything else is `application/octet-stream`.

### Status codes

| Code | When |
| --- | --- |
| 200 | File sent |
| 400 | Malformed request or unsafe path |
| 404 | File does not exist |
| 405 | Method is not `GET` |
| 431 | Headers exceed 8 KiB |
| 500 | The file could not be read |

Error bodies are a single plain-text line, for example `404 Not Found`.

## Layout

```
include/     public headers
src/         server, HTTP parsing, and main
static/      files served to clients
meson.build
```

`src/http.c` parses the request line, decides keep-alive, maps URLs onto the static root, and reads and writes on the socket. `src/server.c` binds, accepts, and serves files.

## Limits

This is a teaching server, not a production one.

- No TLS, virtual hosts, or request bodies.
- No `HEAD`, `POST`, ranges, or compression.
- The static root is the relative path `static`, not a flag.
- Each connection is a detached thread. A slow client occupies that thread until the request finishes or the socket times out.

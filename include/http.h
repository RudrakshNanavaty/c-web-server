#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/* Bytes of header data accepted before responding with 431. */
#define HTTP_HEADER_MAX 8192
/* Storage for an origin-form request target, including the NUL. */
#define HTTP_PATH_MAX 512

enum {
	HTTP_READ_OK = 0,
	HTTP_READ_TOO_LARGE = 1,
	HTTP_READ_INCOMPLETE = 2,
	HTTP_READ_CLOSED = 3,
	HTTP_READ_ERROR = -1,
};

typedef struct {
	char method[16];
	char path[HTTP_PATH_MAX];
	char version[16];
	/* Non-zero when the response should close the connection afterward. */
	int close_connection;
} HttpRequest;

// Parse the request line (METHOD PATH VERSION) and Connection policy.
// Returns 0 on success, -1 on failure.
int http_parse_request(const char *raw, HttpRequest *req);

// Map an origin-form URL onto a path beneath root.
// "/" becomes "<root>/index.html". Query strings and fragments are ignored.
// Returns 0 on success, -1 if the URL is unsafe or does not fit.
int http_map_static_path(
	const char *url_path, const char *root, char *out, size_t out_size
);

// Return 1 when canonical path is root or a file inside root.
// Both arguments must already be absolute canonical paths.
int http_path_is_inside_root(const char *root, const char *path);

// MIME type for a file path. Unknown types are application/octet-stream.
const char *http_mime_type(const char *path);

// Read from fd until the header terminator or capacity is exhausted.
// capacity includes room for the terminating NUL.
int http_read_headers(int fd, char *buf, size_t capacity);

// Send len bytes, retrying short writes and EINTR.
// Returns 0 on success, -1 on disconnect or error.
int http_send_all(int fd, const void *buf, size_t len);

// keep_alive non-zero emits Connection: keep-alive; otherwise Connection: close.
// extra_headers is NULL or a list of lines that each end in CRLF.
// Returns 0 on success, -1 on error.
int http_send_headers(
	int fd, int status, const char *reason, const char *content_type,
	unsigned long long content_length, const char *extra_headers, int keep_alive
);

#endif

#include "http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

// Parse the request line (METHOD PATH VERSION) from a raw HTTP request.
int http_parse_request(const char *raw, HttpRequest *req) {
	if (raw == NULL || req == NULL) {
		return -1;
	}

	// Request line is everything before the first '\r' or '\n'.
	// Example: "GET /index.html HTTP/1.1"
	char request_line[1024];
	size_t i = 0;
	while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n' &&
		   i < sizeof(request_line) - 1) {
		request_line[i] = raw[i];
		i++;
	}
	request_line[i] = '\0';

	if (i == 0) {
		return -1; // No request line found
	}

	// Width limits match HttpRequest field sizes minus the null terminator.
	int matched = sscanf(
		request_line,
		"%15s %511s %15s",
		req->method,
		req->path,
		req->version
	);
	if (matched != 3) {
		return -1;
	}

	return 0;
}

static int hex_value(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static int
url_decode(const char *in, size_t in_len, char *out, size_t out_size) {
	size_t written = 0;
	for (size_t i = 0; i < in_len; i++) {
		unsigned char value = (unsigned char)in[i];
		if (in[i] == '%') {
			if (i + 2 >= in_len) {
				return -1;
			}
			int high = hex_value(in[i + 1]);
			int low = hex_value(in[i + 2]);
			if (high < 0 || low < 0) {
				return -1;
			}
			value = (unsigned char)((high << 4) | low);
			i += 2;
		}

		// A decoded NUL would truncate the path and hide the rest of it.
		if (value == '\0' || written + 1 >= out_size) {
			return -1;
		}
		out[written++] = (char)value;
	}
	out[written] = '\0';
	return 0;
}

static int append_text(
	char *out, size_t out_size, size_t *used, const char *text, size_t text_len
) {
	if (*used + text_len >= out_size) {
		return -1;
	}
	memcpy(out + *used, text, text_len);
	*used += text_len;
	out[*used] = '\0';
	return 0;
}

// Skip "." segments and reject "..". Returns 1 when the segment is skipped.
static int classify_segment(const char *segment, size_t length) {
	if (length == 1 && segment[0] == '.') {
		return 1;
	}
	if (length == 2 && segment[0] == '.' && segment[1] == '.') {
		return -1;
	}
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)segment[i];
		if (c < 0x20 || c == 0x7f || c == '\\') {
			return -1;
		}
	}
	return 0;
}

static int
normalize_url_path(const char *decoded, char *relative, size_t relative_size) {
	size_t decoded_len = strlen(decoded);
	int directory = decoded_len > 0 && decoded[decoded_len - 1] == '/';
	size_t used = 0;
	relative[0] = '\0';

	const char *cursor = decoded;
	while (*cursor != '\0') {
		while (*cursor == '/') {
			cursor++;
		}
		if (*cursor == '\0') {
			break;
		}

		const char *start = cursor;
		while (*cursor != '\0' && *cursor != '/') {
			cursor++;
		}
		size_t segment_len = (size_t)(cursor - start);
		int kind = classify_segment(start, segment_len);
		if (kind == -1) {
			return -1;
		}
		if (kind == 1) {
			continue;
		}
		if (used > 0 &&
			append_text(relative, relative_size, &used, "/", 1) == -1) {
			return -1;
		}
		if (append_text(relative, relative_size, &used, start, segment_len) ==
			-1) {
			return -1;
		}
	}

	if (used == 0) {
		return append_text(relative, relative_size, &used, "index.html", 10);
	}
	if (directory) {
		if (append_text(relative, relative_size, &used, "/index.html", 11) ==
			-1) {
			return -1;
		}
	}
	return 0;
}

int http_map_static_path(
	const char *url_path, const char *root, char *out, size_t out_size
) {
	if (url_path == NULL || root == NULL || root[0] == '\0' || out == NULL ||
		out_size == 0) {
		return -1;
	}

	size_t target_len = 0;
	while (url_path[target_len] != '\0' && url_path[target_len] != '?' &&
		   url_path[target_len] != '#') {
		target_len++;
	}

	char decoded[HTTP_PATH_MAX];
	if (url_decode(url_path, target_len, decoded, sizeof decoded) == -1) {
		return -1;
	}
	if (decoded[0] != '/') {
		return -1;
	}

	char relative[HTTP_PATH_MAX];
	if (normalize_url_path(decoded, relative, sizeof relative) == -1) {
		return -1;
	}

	int written;
	if (root[strlen(root) - 1] == '/') {
		written = snprintf(out, out_size, "%s%s", root, relative);
	} else {
		written = snprintf(out, out_size, "%s/%s", root, relative);
	}
	if (written < 0 || (size_t)written >= out_size) {
		return -1;
	}
	return 0;
}

int http_path_is_inside_root(const char *root, const char *path) {
	if (root == NULL || path == NULL || root[0] == '\0' || path[0] == '\0') {
		return 0;
	}
	if (strstr(path, "/../") != NULL || strstr(path, "/./") != NULL) {
		return 0;
	}

	size_t path_len = strlen(path);
	if (path_len >= 3 && strcmp(path + path_len - 3, "/..") == 0) {
		return 0;
	}
	if (path_len >= 2 && strcmp(path + path_len - 2, "/.") == 0) {
		return 0;
	}

	size_t root_len = strlen(root);
	if (root_len > 1 && root[root_len - 1] == '/') {
		root_len--;
	}
	if (strncmp(path, root, root_len) != 0) {
		return 0;
	}
	return path[root_len] == '\0' || path[root_len] == '/';
}

static int extension_is(const char *extension, const char *expected) {
	while (*expected != '\0') {
		char c = *extension;
		if (c >= 'A' && c <= 'Z') {
			c = (char)(c - 'A' + 'a');
		}
		if (c != *expected) {
			return 0;
		}
		extension++;
		expected++;
	}
	return *extension == '\0';
}

const char *http_mime_type(const char *path) {
	if (path == NULL) {
		return "application/octet-stream";
	}

	const char *base = strrchr(path, '/');
	base = (base == NULL) ? path : base + 1;
	const char *dot = strrchr(base, '.');
	if (dot == NULL || dot == base || dot[1] == '\0') {
		return "application/octet-stream";
	}

	const char *extension = dot + 1;
	if (extension_is(extension, "html") || extension_is(extension, "htm")) {
		return "text/html; charset=utf-8";
	}
	if (extension_is(extension, "css")) {
		return "text/css; charset=utf-8";
	}
	if (extension_is(extension, "js")) {
		return "text/javascript; charset=utf-8";
	}
	if (extension_is(extension, "json")) {
		return "application/json";
	}
	if (extension_is(extension, "txt")) {
		return "text/plain; charset=utf-8";
	}
	if (extension_is(extension, "png")) {
		return "image/png";
	}
	if (extension_is(extension, "jpg") || extension_is(extension, "jpeg")) {
		return "image/jpeg";
	}
	if (extension_is(extension, "gif")) {
		return "image/gif";
	}
	if (extension_is(extension, "svg")) {
		return "image/svg+xml";
	}
	if (extension_is(extension, "ico")) {
		return "image/x-icon";
	}
	if (extension_is(extension, "webp")) {
		return "image/webp";
	}
	return "application/octet-stream";
}

int http_read_headers(int fd, char *buf, size_t capacity) {
	if (fd < 0 || buf == NULL || capacity < 5) {
		errno = EINVAL;
		return HTTP_READ_ERROR;
	}

	size_t used = 0;
	buf[0] = '\0';
	while (used + 1 < capacity) {
		ssize_t n = recv(fd, buf + used, capacity - 1 - used, 0);
		if (n == -1) {
			if (errno == EINTR) {
				continue;
			}
			return HTTP_READ_ERROR;
		}
		if (n == 0) {
			return used == 0 ? HTTP_READ_CLOSED : HTTP_READ_INCOMPLETE;
		}

		used += (size_t)n;
		buf[used] = '\0';
		if (strstr(buf, "\r\n\r\n") != NULL) {
			return HTTP_READ_OK;
		}
	}

	return HTTP_READ_TOO_LARGE;
}

int http_send_all(int fd, const void *buf, size_t len) {
	const char *bytes = buf;
	size_t sent = 0;
	if (fd < 0 || (buf == NULL && len > 0)) {
		errno = EINVAL;
		return -1;
	}

	while (sent < len) {
		ssize_t n = send(fd, bytes + sent, len - sent, MSG_NOSIGNAL);
		if (n == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (n == 0) {
			errno = EPIPE;
			return -1;
		}
		sent += (size_t)n;
	}
	return 0;
}

int http_send_headers(
	int fd, int status, const char *reason, const char *content_type,
	unsigned long long content_length, const char *extra_headers
) {
	char header[1024];
	if (reason == NULL || reason[0] == '\0') {
		errno = EINVAL;
		return -1;
	}
	if (content_type == NULL) {
		content_type = "application/octet-stream";
	}
	if (extra_headers == NULL) {
		extra_headers = "";
	}

	int written = snprintf(
		header,
		sizeof header,
		"HTTP/1.1 %d %s\r\n"
		"Content-Length: %llu\r\n"
		"Content-Type: %s\r\n"
		"Connection: close\r\n"
		"%s"
		"\r\n",
		status,
		reason,
		content_length,
		content_type,
		extra_headers
	);
	if (written < 0 || (size_t)written >= sizeof header) {
		errno = EMSGSIZE;
		return -1;
	}
	return http_send_all(fd, header, (size_t)written);
}

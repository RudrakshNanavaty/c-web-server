#include "http.h"

#include <stdio.h>
#include <string.h>

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
		return -1;
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

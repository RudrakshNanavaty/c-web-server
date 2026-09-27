#ifndef HTTP_H
#define HTTP_H

typedef struct {
	char method[16];
	char path[512];
	char version[16];
} HttpRequest;

// Parse the request line (METHOD PATH VERSION) from a raw HTTP request.
// Returns 0 on success, -1 on failure.
int http_parse_request(const char *raw, HttpRequest *req);

#endif

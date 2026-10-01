#include "http.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT(condition)                                                      \
	do {                                                                       \
		if (!(condition)) {                                                    \
			fprintf(                                                           \
				stderr,                                                        \
				"%s:%d: expected %s\n",                                        \
				__FILE__,                                                      \
				__LINE__,                                                      \
				#condition                                                     \
			);                                                                 \
			failures++;                                                        \
		}                                                                      \
	} while (0)

static void expect_map(const char *url, const char *expected) {
	char out[256];
	int rc = http_map_static_path(url, "static", out, sizeof out);
	if (expected == NULL) {
		EXPECT(rc == -1);
		return;
	}
	EXPECT(rc == 0);
	if (rc == 0 && strcmp(out, expected) != 0) {
		fprintf(
			stderr,
			"map %s -> \"%s\", expected \"%s\"\n",
			url,
			out,
			expected
		);
		failures++;
	}
}

static void test_parse(void) {
	HttpRequest request;
	EXPECT(
		http_parse_request(
			"GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n",
			&request
		) == 0
	);
	EXPECT(strcmp(request.method, "GET") == 0);
	EXPECT(strcmp(request.path, "/index.html") == 0);
	EXPECT(strcmp(request.version, "HTTP/1.1") == 0);
	EXPECT(http_parse_request("GET /\r\n", &request) == -1);
	EXPECT(http_parse_request("", &request) == -1);
	EXPECT(http_parse_request(NULL, &request) == -1);
}

static void test_paths(void) {
	expect_map("/", "static/index.html");
	expect_map("/index.html", "static/index.html");
	expect_map("/css/app.css?v=1#top", "static/css/app.css");
	expect_map("/docs/", "static/docs/index.html");
	expect_map("/a//b/./c", "static/a/b/c");
	expect_map("/my%20file.txt", "static/my file.txt");
	expect_map("/static/", "static/static/index.html");

	char rooted[64];
	EXPECT(
		http_map_static_path("/a.txt", "static/", rooted, sizeof rooted) == 0
	);
	EXPECT(strcmp(rooted, "static/a.txt") == 0);

	expect_map(NULL, NULL);
	expect_map("index.html", NULL);
	expect_map("/../etc/passwd", NULL);
	expect_map("/foo/../../etc/passwd", NULL);
	expect_map("/%2e%2e/secret", NULL);
	expect_map("/%2E%2E/secret", NULL);
	expect_map("/foo/%2e%2e/secret", NULL);
	expect_map("/%", NULL);
	expect_map("/%zz", NULL);
	expect_map("/a\\b", NULL);
	expect_map("/%00hidden", NULL);

	EXPECT(http_path_is_inside_root("/var/www", "/var/www") == 1);
	EXPECT(http_path_is_inside_root("/var/www", "/var/www/index.html") == 1);
	EXPECT(http_path_is_inside_root("/var/www/", "/var/www/a") == 1);
	EXPECT(http_path_is_inside_root("/var/www", "/var/www-extra") == 0);
	EXPECT(http_path_is_inside_root("/var/www", "/var/www/../etc") == 0);
	EXPECT(http_path_is_inside_root("/var/www", "/etc/passwd") == 0);
	EXPECT(http_path_is_inside_root(NULL, "/var/www") == 0);
}

static void test_mime(void) {
	EXPECT(
		strcmp(
			http_mime_type("static/index.html"),
			"text/html; charset=utf-8"
		) == 0
	);
	EXPECT(
		strcmp(http_mime_type("INDEX.HTML"), "text/html; charset=utf-8") == 0
	);
	EXPECT(
		strcmp(http_mime_type("dir/app.css"), "text/css; charset=utf-8") == 0
	);
	EXPECT(
		strcmp(http_mime_type("app.js"), "text/javascript; charset=utf-8") == 0
	);
	EXPECT(
		strcmp(http_mime_type("note.txt"), "text/plain; charset=utf-8") == 0
	);
	EXPECT(strcmp(http_mime_type("photo.jpeg"), "image/jpeg") == 0);
	EXPECT(
		strcmp(http_mime_type("archive.tar.gz"), "application/octet-stream") ==
		0
	);
	EXPECT(strcmp(http_mime_type("README"), "application/octet-stream") == 0);
	EXPECT(
		strcmp(
			http_mime_type("dir.with.dot/file"),
			"application/octet-stream"
		) == 0
	);
	EXPECT(strcmp(http_mime_type(NULL), "application/octet-stream") == 0);
}

static int socket_pair(int ends[2]) {
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == -1) {
		perror("socketpair");
		return -1;
	}
	return 0;
}

static void test_read_headers(void) {
	int ends[2];
	char buf[128];
	const char *request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

	EXPECT(socket_pair(ends) == 0);
	EXPECT(
		write(ends[1], request, strlen(request)) == (ssize_t)strlen(request)
	);
	EXPECT(http_read_headers(ends[0], buf, sizeof buf) == HTTP_READ_OK);
	EXPECT(strstr(buf, "\r\n\r\n") != NULL);
	close(ends[0]);
	close(ends[1]);

	EXPECT(socket_pair(ends) == 0);
	EXPECT(write(ends[1], "GET /", 5) == 5);
	close(ends[1]);
	EXPECT(http_read_headers(ends[0], buf, sizeof buf) == HTTP_READ_INCOMPLETE);
	close(ends[0]);

	EXPECT(socket_pair(ends) == 0);
	close(ends[1]);
	EXPECT(http_read_headers(ends[0], buf, sizeof buf) == HTTP_READ_CLOSED);
	close(ends[0]);

	char huge[64];
	memset(huge, 'A', sizeof huge);
	EXPECT(socket_pair(ends) == 0);
	EXPECT(write(ends[1], huge, sizeof huge) == (ssize_t)sizeof huge);
	EXPECT(http_read_headers(ends[0], buf, 16) == HTTP_READ_TOO_LARGE);
	close(ends[0]);
	close(ends[1]);

	EXPECT(http_read_headers(-1, buf, sizeof buf) == HTTP_READ_ERROR);
}

static void test_send(void) {
	int ends[2];
	char buf[256];

	EXPECT(socket_pair(ends) == 0);
	EXPECT(http_send_all(ends[0], "abc", 3) == 0);
	EXPECT(read(ends[1], buf, sizeof buf) == 3);
	EXPECT(memcmp(buf, "abc", 3) == 0);
	close(ends[0]);
	close(ends[1]);

	EXPECT(socket_pair(ends) == 0);
	EXPECT(
		http_send_headers(
			ends[0],
			404,
			"Not Found",
			"text/plain; charset=utf-8",
			10,
			NULL
		) == 0
	);
	ssize_t n = read(ends[1], buf, sizeof buf - 1);
	EXPECT(n > 0);
	if (n > 0) {
		buf[n] = '\0';
		EXPECT(strstr(buf, "HTTP/1.1 404 Not Found\r\n") != NULL);
		EXPECT(strstr(buf, "Content-Length: 10\r\n") != NULL);
		EXPECT(strstr(buf, "Connection: close\r\n") != NULL);
	}
	close(ends[0]);
	close(ends[1]);

	EXPECT(http_send_all(-1, "abc", 3) == -1);
}

int main(void) {
	test_parse();
	test_paths();
	test_mime();
	test_read_headers();
	test_send();

	if (failures != 0) {
		fprintf(stderr, "%d test failure(s)\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}

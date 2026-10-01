#include "server.h"
#include "http.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BACKLOG 10
#define STATIC_ROOT "static"
#define FILE_CHUNK_SIZE 8192

static int send_text_status(
	int client_socket, int status, const char *reason, const char *extra_headers
) {
	char body[128];
	int written = snprintf(body, sizeof body, "%d %s\n", status, reason);
	if (written < 0 || (size_t)written >= sizeof body) {
		return -1;
	}

	if (http_send_headers(
			client_socket,
			status,
			reason,
			"text/plain; charset=utf-8",
			(unsigned long long)written,
			extra_headers
		) == -1) {
		return -1;
	}
	return http_send_all(client_socket, body, (size_t)written);
}

static int not_found_error(int error) {
	return error == ENOENT || error == ENOTDIR;
}

static int canonicalize_inside_root(
	const char *root, const char *path, char *out, size_t out_size
) {
	char resolved[PATH_MAX];
	if (realpath(path, resolved) == NULL) {
		return -1;
	}
	// realpath removes ".", "..", and symlink escapes. Reject anything that
	// no longer sits under the static root.
	if (!http_path_is_inside_root(root, resolved)) {
		errno = ENOENT;
		return -1;
	}
	if (strlen(resolved) + 1 > out_size) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(out, resolved, strlen(resolved) + 1);
	return 0;
}

static int resolve_static_file(
	const char *root, const char *mapped, char *resolved, size_t resolved_size
) {
	struct stat info;
	if (stat(mapped, &info) == -1) {
		return -1;
	}

	char candidate[PATH_MAX];
	if (S_ISDIR(info.st_mode)) {
		int written = snprintf(
			candidate,
			sizeof candidate,
			"%s/index.html",
			mapped
		);
		if (written < 0 || (size_t)written >= sizeof candidate) {
			errno = ENAMETOOLONG;
			return -1;
		}
	} else {
		if (strlen(mapped) >= sizeof candidate) {
			errno = ENAMETOOLONG;
			return -1;
		}
		memcpy(candidate, mapped, strlen(mapped) + 1);
	}

	if (canonicalize_inside_root(root, candidate, resolved, resolved_size) ==
		-1) {
		return -1;
	}
	if (stat(resolved, &info) == -1) {
		return -1;
	}
	if (!S_ISREG(info.st_mode)) {
		errno = ENOENT;
		return -1;
	}
	return 0;
}

static int send_file(int client_socket, const char *path) {
	int file = open(path, O_RDONLY | O_CLOEXEC);
	if (file == -1) {
		return -1;
	}

	struct stat info;
	if (fstat(file, &info) == -1 || !S_ISREG(info.st_mode) ||
		info.st_size < 0) {
		int error = errno;
		close(file);
		errno = error == 0 ? EINVAL : error;
		return -1;
	}

	if (http_send_headers(
			client_socket,
			200,
			"OK",
			http_mime_type(path),
			(unsigned long long)info.st_size,
			NULL
		) == -1) {
		close(file);
		return -1;
	}

	unsigned long long remaining = (unsigned long long)info.st_size;
	char chunk[FILE_CHUNK_SIZE];
	while (remaining > 0) {
		size_t want = sizeof chunk;
		if ((unsigned long long)want > remaining) {
			want = (size_t)remaining;
		}

		ssize_t n = read(file, chunk, want);
		if (n == -1) {
			if (errno == EINTR) {
				continue;
			}
			close(file);
			return -1;
		}
		if (n == 0) {
			close(file);
			errno = EIO;
			return -1;
		}
		if (http_send_all(client_socket, chunk, (size_t)n) == -1) {
			close(file);
			return -1;
		}
		remaining -= (unsigned long long)n;
	}

	close(file);
	return 0;
}

static void handle_client(int client_socket, const char *static_root) {
	char buffer[HTTP_HEADER_MAX + 1];
	int read_status = http_read_headers(client_socket, buffer, sizeof buffer);
	if (read_status == HTTP_READ_CLOSED) {
		close(client_socket);
		return;
	}
	if (read_status == HTTP_READ_ERROR) {
		perror("server: recv");
		close(client_socket);
		return;
	}
	if (read_status == HTTP_READ_TOO_LARGE) {
		send_text_status(
			client_socket,
			431,
			"Request Header Fields Too Large",
			NULL
		);
		close(client_socket);
		return;
	}
	if (read_status == HTTP_READ_INCOMPLETE) {
		send_text_status(client_socket, 400, "Bad Request", NULL);
		close(client_socket);
		return;
	}

	HttpRequest request;
	if (http_parse_request(buffer, &request) == -1) {
		send_text_status(client_socket, 400, "Bad Request", NULL);
		close(client_socket);
		return;
	}

	printf(
		"Parsed: method=%s path=%s version=%s\n",
		request.method,
		request.path,
		request.version
	);

	if (strcmp(request.method, "GET") != 0) {
		send_text_status(
			client_socket,
			405,
			"Method Not Allowed",
			"Allow: GET\r\n"
		);
		close(client_socket);
		return;
	}

	char mapped[PATH_MAX];
	if (http_map_static_path(
			request.path,
			static_root,
			mapped,
			sizeof mapped
		) == -1) {
		send_text_status(client_socket, 400, "Bad Request", NULL);
		close(client_socket);
		return;
	}

	char resolved[PATH_MAX];
	if (resolve_static_file(static_root, mapped, resolved, sizeof resolved) ==
		-1) {
		if (not_found_error(errno)) {
			send_text_status(client_socket, 404, "Not Found", NULL);
		} else {
			perror("server: file");
			send_text_status(client_socket, 500, "Internal Server Error", NULL);
		}
		close(client_socket);
		return;
	}

	if (send_file(client_socket, resolved) == -1) {
		perror("server: send");
	}
	close(client_socket);
}

void run_server(const char *port) {
	struct addrinfo hints, *address_results;

	// Hints are used to tell getaddrinfo what kind of socket we want.
	// Think of them as address filters for the getaddrinfo function.
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;	 // IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP
	// Use a wildcard address of the host machine
	hints.ai_flags = AI_PASSIVE; // (0.0.0.0 or ::)

	// getaddrinfo gets the address info. from the OS instead of hardcoding.
	// It returns a linked list of address results.
	// AF_UNSPEC gives us both IPv4 and IPv6 addresses (can mean >= 2 results).
	int status = getaddrinfo(NULL, port, &hints, &address_results);
	if (status != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		exit(1);
	}

	// Try all address results and use the first socket we can bind.
	struct addrinfo *result;
	int http_socket = -1;
	for (result = address_results; result != NULL; result = result->ai_next) {
		// socket() returns a file descriptor (fd).
		// An fd is an integer index to the process' open-file table.
		// Everything in Unix is a file. So fd can be a socket, pipe, file, etc.
		http_socket = socket(
			result->ai_family,	 // Address family (IPv4 or IPv6)
			result->ai_socktype, // Socket type: Stream
			result->ai_protocol	 // Protocol: TCP
		);
		if (http_socket == -1) {
			perror("server: socket");
			continue; // Try the next result
		}

		// Allow rebinding the port immediately after the server restarts.
		// Without this, the OS holds the port in TIME_WAIT and bind fails
		// with "Address already in use".
		int reuse = 1;
		int setsockopt_status = setsockopt(
			http_socket,
			SOL_SOCKET,
			SO_REUSEADDR,
			&reuse,
			sizeof(reuse)
		);
		if (setsockopt_status == -1) {
			perror("Failed to set socket options");
			close(http_socket);
			continue;
		}

		// Bind the socket to an address and port for it to be accessible by
		// other devices on the network.
		int bind_status = bind(
			http_socket,
			result->ai_addr,
			result->ai_addrlen
		);
		if (bind_status == -1) {
			perror("server: bind");
			close(http_socket);
			continue;
		}

		// Use the first result that successfully binds.
		break;
	}

	freeaddrinfo(address_results);

	// If no result successfully binds, exit with error.
	if (result == NULL) {
		fprintf(stderr, "Server: failed to bind\n");
		close(http_socket);
		exit(1);
	}

	// Start listening for incoming connections.
	if (listen(http_socket, BACKLOG) == -1) {
		perror("server: listen");
		close(http_socket);
		exit(1);
	}

	// A dropped client should not kill the process with SIGPIPE.
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("server: signal");
		close(http_socket);
		exit(1);
	}

	char static_root[PATH_MAX];
	if (realpath(STATIC_ROOT, static_root) == NULL) {
		perror("server: static");
		close(http_socket);
		exit(1);
	}

	printf("Server listening on port %s...\n", port);
	printf("Serving files from %s\n", static_root);

	while (true) {
		int client_socket = accept(http_socket, NULL, NULL);
		if (client_socket == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("server: accept");
			continue;
		}

		handle_client(client_socket, static_root);
	}

	close(http_socket);
}

#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <varlink.h>

#include "kanshi.h"
#include "ipc.h"

static void reload_config_done(void *data, struct wl_callback *callback,
		uint32_t serial) {
	VarlinkCall *call = data;
	varlink_call_reply(call, NULL, 0);
	wl_callback_destroy(callback);
}

static struct wl_callback_listener reload_config_listener = {
	.done = reload_config_done
};

static long handle_reload(VarlinkService *service, VarlinkCall *call,
		VarlinkObject *parameters, uint64_t flags, void *userdata) {
	struct kanshi_state *state = userdata;
	kanshi_reload_config(state);
	// this only ensures that the server has received the configuration request,
	// the server is free to wait an arbitrary amount of time before applying the configuration
	// TODO: use the wlr-output-management event instead
	struct wl_callback *callback = wl_display_sync(state->display);
	wl_callback_add_listener(callback, &reload_config_listener, call);
	return 0;
}

static int set_cloexec(int fd) {
	int flags = fcntl(fd, F_GETFD);
	if (flags < 0) {
		perror("fnctl(F_GETFD) failed");
		return -1;
	}
	if (fcntl(fd, F_SETFD, flags | O_CLOEXEC) < 0) {
		perror("fnctl(F_SETFD) failed");
		return -1;
	}
	return 0;
}

int kanshi_init_ipc(struct kanshi_state *state, int listen_fd) {
	if (listen_fd >= 0 && set_cloexec(listen_fd) < 0) {
		return -1;
	}

	VarlinkService *service;
	char address[PATH_MAX];
	if (get_ipc_address(address, sizeof(address)) < 0) {
		return -1;
	}
	if (varlink_service_new(&service,
			"emersion", "kanshi", KANSHI_VERSION, "https://wayland.emersion.fr/kanshi/",
			address, listen_fd) < 0) {
		fprintf(stderr, "Couldn't start kanshi varlink service at %s.\n"
				"Is the kanshi daemon already running?\n", address);
		return -1;
	}

	const char *interface = "interface fr.emersion.kanshi\n"
		"method Reload() -> ()";

	long result = varlink_service_add_interface(service, interface,
			"Reload", handle_reload, state,
			NULL);
	if (result != 0) {
		fprintf(stderr, "varlink_service_add_interface failed: %s\n",
				varlink_error_string(-result));
		varlink_service_free(service);
		return -1;
	}

	state->service = service;

	return 0;
}

void kanshi_free_ipc(struct kanshi_state *state) {
	if (state->service) {
		varlink_service_free(state->service);
		state->service = NULL;
	}
}

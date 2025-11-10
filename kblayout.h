#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <json-c/json_tokener.h>
#include <json-c/json_object.h>
#include "util.h"

const char ipc_magic[] = {'i', '3', '-', 'i', 'p', 'c'};
#define IPC_HEADER_SIZE (sizeof(ipc_magic) + 8)

/* obtained from man sway-ipc */
#define SUBSCRIBE	2
#define GET_INPUTS	100

struct kblayout_data {
	int fd;
	char **layouts;
	int nlayouts;
	int curr_idx, prev_idx;
	struct element *ctx;
};

int sway_ipc_connect(const char *socket_path) {
	int sockfd;
	struct sockaddr_un addr;
	int ret;

	if (!socket_path)
		return -1;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd == -1)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	ret = connect(sockfd, (const struct sockaddr *) &addr, sizeof(addr));
	if (ret == -1) {
		close(sockfd);
		return -1;
	}

	return sockfd;
}

void sway_ipc_send(int ipc_fd, int type, const char *payload) {
	char msg[IPC_HEADER_SIZE];
	int len = payload ? strlen(payload) : 0;
	int ret;

	memcpy(msg, ipc_magic, sizeof(ipc_magic));
	memcpy(msg + sizeof(ipc_magic), &len, sizeof(len));
	memcpy(msg + sizeof(ipc_magic) + sizeof(len), &type, sizeof(type));
	write(ipc_fd, msg, sizeof(msg));

	if (payload)
		write(ipc_fd, payload, len);
}

void sway_ipc_recv_payload(int ipc_fd, char **payload) {
	char reply[IPC_HEADER_SIZE];
	int *len = (int *) (reply + sizeof(ipc_magic));

	read(ipc_fd, reply, sizeof(reply));

	*payload = realloc(*payload, *len + 1);
	*(*payload + *len) = '\0';
	read(ipc_fd, *payload, *len);
}

char *kblayout_desc_to_short(const char *desc) {
	/* Converts a descriptive layout name to a more concise version,
	 * using the man page of xkeyboard-config as a mapping:
	 *   "English (US, intl., with dead keys)" -> "us(intl)"
	 */
	FILE *f;
	char *buffer = NULL; /* allocated by getline() */
	size_t buf_len = 0, line_len, short_name_len = 0;
	char *sw; /* second word in the line */
	char *short_name = NULL;

	if (!desc)
		return NULL;

	if (!(f = fopen("/usr/share/man/man7/xkeyboard-config.7", "r")))
		return NULL;

	while (getline(&buffer, &buf_len, f) != -1) {
		line_len = strlen(buffer);
		for (sw = buffer; sw < buffer + line_len && !WHITESPACE(*sw); sw++);
		for (; sw < buffer + line_len && WHITESPACE(*sw); sw++);
		if (!*sw) /* reached end of line */
			continue;

		if (strncmp(sw, desc, MIN(buffer + line_len - sw, strlen(desc))) == 0) {
			for (; !WHITESPACE(*(buffer + short_name_len)); short_name_len++);
			short_name = strndup(buffer, short_name_len);
			break;
		}
	}

	free(buffer);
	fclose(f);
	return short_name;
}

int get_layouts(json_object *keyboard, char ***layouts, int *nlayouts) {
	json_object *layouts_obj = NULL, *arr_elem;
	int arraylen;
	const char *layout_name;
	char *short_name;

	json_object_object_get_ex(keyboard, "xkb_layout_names", &layouts_obj);
	if (!layouts_obj)
		return -1;

	arraylen = json_object_array_length(layouts_obj);
	*nlayouts = arraylen;
	if (*layouts)
		free(*layouts);
	*layouts = malloc(*nlayouts * sizeof(char *));

	for (int i = 0; i < arraylen; i++) {
		arr_elem = json_object_array_get_idx(layouts_obj, i);
		layout_name = json_object_get_string(arr_elem);
		*(*layouts + i) = kblayout_desc_to_short(layout_name);
	}

	return 0;
}

int get_layout_idx(json_object *keyboard, int *idx) {
	json_object *layout_idx_obj;

	json_object_object_get_ex(keyboard, "xkb_active_layout_index", &layout_idx_obj);
	if (!json_object_is_type(layout_idx_obj, json_type_int))
		return -1;
	*idx = json_object_get_int(layout_idx_obj);

	return 0;
}

int parse_json_first_run(const char *json_data_str, struct kblayout_data *kdata) {
	json_object *json_data, *arr_elem, *type_obj;
	int arraylen;
	const char *device_type;
	int ret = -1;

	json_data = json_tokener_parse(json_data_str);

	arraylen = json_object_array_length(json_data);
	for (int i = 0; i < arraylen; i++) {
		arr_elem = json_object_array_get_idx(json_data, i);
		json_object_object_get_ex(arr_elem, "type", &type_obj);
		device_type = json_object_get_string(type_obj);

		if (!device_type || strcmp(device_type, "keyboard") != 0)
			continue;

		ret = 0;
		ret |= get_layouts(arr_elem, &kdata->layouts, &kdata->nlayouts);
		ret |= get_layout_idx(arr_elem, &kdata->curr_idx);
		break;
	}

	json_object_put(json_data); /* frees json_data */
	return ret;
}

void parse_json_subscription(const char *json_data_str, struct kblayout_data *kdata) {
	json_object *json_data, *change_obj, *input_obj, *type_obj;
	const char *change, *type;

	json_data = json_tokener_parse(json_data_str);
	json_object_object_get_ex(json_data, "change", &change_obj);
	change = json_object_get_string(change_obj);
	if (!change || strcmp(change, "xkb_layout") != 0)
		return;

	json_object_object_get_ex(json_data, "input", &input_obj);
	json_object_object_get_ex(input_obj, "type", &type_obj);
	type = json_object_get_string(type_obj);
	if (!type || strcmp(type, "keyboard") != 0)
		return;

	kdata->prev_idx = kdata->curr_idx;
	get_layout_idx(input_obj, &kdata->curr_idx);

	json_object_put(json_data); /* frees json_data */
}

void destroy_kblayout_data(struct kblayout_data *kdata) {
	if (kdata->layouts) {
		for (int i = 0; i < kdata->nlayouts; i++)
			free(*(kdata->layouts + i));
		free(kdata->layouts);
	}
}

int kblayout_setup(struct element *ctx, struct kblayout_data *kdata) {
	const char *socket_path = getenv("SWAYSOCK");
	char *reply = NULL;
	json_object *json_reply, *success_obj;
	int success;

	if ((kdata->fd = sway_ipc_connect(socket_path)) == -1)
		return -1;

	sway_ipc_send(kdata->fd, GET_INPUTS, NULL);
	sway_ipc_recv_payload(kdata->fd, &reply);
	kdata->layouts = NULL;
	kdata->prev_idx = -1;
	if (parse_json_first_run(reply, kdata) == -1) {
		free(reply);
		close(kdata->fd);
		destroy_kblayout_data(kdata);
		return -1;
	}

	ctx->data = kdata;
	kdata->ctx = ctx;
	kdata->ctx->func(kdata->ctx);

	sway_ipc_send(kdata->fd, SUBSCRIBE, "['input']");
	sway_ipc_recv_payload(kdata->fd, &reply);
	json_reply = json_tokener_parse(reply);
	free(reply);
	json_object_object_get_ex(json_reply, "success", &success_obj);
	success = json_object_get_boolean(success_obj);
	json_object_put(json_reply);
	if (!success) {
		close(kdata->fd);
		destroy_kblayout_data(kdata);
		return -1;
	}

	return kdata->fd;
}

void kblayout_quit(struct kblayout_data *kdata) {
	close(kdata->fd);
	destroy_kblayout_data(kdata);
}

int kblayout_handle(struct kblayout_data *kdata) {
	char *reply = NULL;

	sway_ipc_recv_payload(kdata->fd, &reply);
	parse_json_subscription(reply, kdata);
	free(reply);

	if (kdata->curr_idx == kdata->prev_idx)
		return 0;

	kdata->ctx->func(kdata->ctx);
	return 1;
}

void kblayout(struct element *ctx) {
	struct kblayout_data *kdata = ctx->data;

	sprintf(ctx->buf, ctx->fmt1, kdata->layouts[kdata->curr_idx]);
}

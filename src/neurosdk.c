#include <neurosdk.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tinycthread.h"

#if defined(__GNUC__)
#define unreachable() (__builtin_unreachable())
#elif defined(_MSC_VER)
#define unreachable() (__assume(false))
#else
[[noreturn]] inline void unreachable_impl() { }
#define unreachable() (unreachable_impl())
#endif

#include "json.h"
#include "mongoose.h"

#define ENVIRONMENT_VARIABLE_NAME "NEURO_SDK_WS_URL"
#define MESSAGE_QUEUE_SIZE 10

#ifndef LIB_VERSION
#error "LIB_VERSION is not defined!"
#endif
#ifndef LIB_BUILD_HASH
#error "LIB_BUILD_HASH is not defined!"
#endif
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define LOG_DEBUG(context, ...)                                        \
	if (context->debug_prints && aprintf(&context->logm, __VA_ARGS__)) { \
		context->callback_log(NeuroSDK_Severity_Debug, context->logm,      \
		                      context->user_data);                         \
		free(context->logm);                                               \
	}

#define LOG_INFO(context, ...)                                              \
	if (context->validation_layers && aprintf(&context->logm, __VA_ARGS__)) { \
		context->callback_log(NeuroSDK_Severity_Info, context->logm,            \
		                      context->user_data);                              \
		free(context->logm);                                                    \
	}

#define LOG_WARN(context, ...)                                              \
	if (context->validation_layers && aprintf(&context->logm, __VA_ARGS__)) { \
		context->callback_log(NeuroSDK_Severity_Warn, context->logm,            \
		                      context->user_data);                              \
		free(context->logm);                                                    \
	}

#define LOG_ERROR(context, ...)                                             \
	if (context->validation_layers && aprintf(&context->logm, __VA_ARGS__)) { \
		context->callback_log(NeuroSDK_Severity_Error, context->logm,           \
		                      context->user_data);                              \
		free(context->logm);                                                    \
	}

static void default_logger(neurosdk_severity_e severity,
                           char *message,
                           void *user_data) {
#ifdef _WIN32
	HANDLE h_console = GetStdHandle(STD_OUTPUT_HANDLE);

	CONSOLE_SCREEN_BUFFER_INFO console_info;
	GetConsoleScreenBufferInfo(h_console, &console_info);
	WORD original_color = console_info.wAttributes;
#define RED \
	SetConsoleTextAttribute(h_console, FOREGROUND_RED | FOREGROUND_INTENSITY)
#define YELLOW             \
	SetConsoleTextAttribute( \
	    h_console, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define BLUE \
	SetConsoleTextAttribute(h_console, FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define GRAY SetConsoleTextAttribute(h_console, FOREGROUND_INTENSITY)
#define RESET SetConsoleTextAttribute(h_console, original_color)
#else
#define RED printf("\033[31;1m")
#define YELLOW printf("\033[33;1m")
#define BLUE printf("\033[34;1m")
#define GRAY printf("\033[90m")
#define RESET printf("\033[0m")
#endif
	if (severity == NeuroSDK_Severity_Debug) {
		GRAY;
		printf("NeuroSDK Validation Layer: DEBUG: ");
	} else if (severity == NeuroSDK_Severity_Info) {
		BLUE;
		printf("NeuroSDK Validation Layer: INFO: ");
	} else if (severity == NeuroSDK_Severity_Warn) {
		YELLOW;
		printf("NeuroSDK Validation Layer: WARN: ");
	} else if (severity == NeuroSDK_Severity_Error) {
		RED;
		printf("NeuroSDK Validation Layer: ERROR: ");
	} else {
		unreachable();
	}

	printf("%s\n", message);

	RESET;
}

typedef struct context {
	char const *game_name;  // This is escaped
	int poll_ms;

	void *user_data;

	neurosdk_callback_log_t callback_log;
	char *logm;

	neurosdk_error_e conn_err;
	bool connected;

	neurosdk_message_t *message_queue;
	int message_queue_size;
	int message_queue_cap;

	struct mg_mgr mgr;
	struct mg_connection *conn;

	mtx_t out_mtx;
	char **pending_messages;
	int pending_messages_size;
	int pending_messages_cap;

	bool debug_prints : 1;
	bool validation_layers : 1;
} context_t;

static char *escape_string(char const *str) {
	if (!str)
		return NULL;

	size_t len = strlen(str);
	char *escaped = malloc(len * 4 + 1);
	if (!escaped)
		return NULL;

	char *dst = escaped;
	while (*str) {
		switch (*str) {
			case '\n':
				*dst++ = '\\';
				*dst++ = 'n';
				break;
			case '\t':
				*dst++ = '\\';
				*dst++ = 't';
				break;
			case '\r':
				*dst++ = '\\';
				*dst++ = 'r';
				break;
			case '\\':
				*dst++ = '\\';
				*dst++ = '\\';
				break;
			case '\"':
				*dst++ = '\\';
				*dst++ = '\"';
				break;
			case '\'':
				*dst++ = '\\';
				*dst++ = '\'';
				break;
			default:
				if ((unsigned char)*str < 32 || (unsigned char)*str > 126) {
					dst += sprintf(dst, "\\x%02X", (unsigned char)*str);
				} else {
					*dst++ = *str;
				}
		}
		str++;
	}
	*dst = '\0';
	return escaped;
}

#if defined(_MSC_VER)
static int vasprintf(char **strp, const char *fmt, va_list ap) {
	va_list ap_copy;
	int formattedLength, actualLength;
	size_t requiredSize;
	*strp = NULL;
	va_copy(ap_copy, ap);
	formattedLength = _vscprintf(fmt, ap_copy);
	va_end(ap_copy);
	if (formattedLength < 0) {
		return -1;
	}
	requiredSize = ((size_t)formattedLength) + 1;
	*strp = (char *)malloc(requiredSize);
	if (*strp == NULL) {
		errno = ENOMEM;
		return -1;
	}
	actualLength = vsnprintf_s(*strp, requiredSize, requiredSize - 1, fmt, ap);
	if (actualLength != formattedLength) {
		free(*strp);
		*strp = NULL;
		errno = 1;
		return -1;
	}
	return formattedLength;
}
#endif

static int aprintf(char **strp, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int bytes = vasprintf(strp, fmt, args);
	va_end(args);
	return bytes;
}

NEUROSDK_EXPORT char const *neurosdk_version(void) {
	return STR(LIB_VERSION);
}
NEUROSDK_EXPORT char const *neurosdk_git_hash(void) {
	return STR(LIB_BUILD_HASH);
}

NEUROSDK_EXPORT char const *neurosdk_error_string(neurosdk_error_e err) {
	switch (err) {
		case NeuroSDK_None:
			return "None.";
		case NeuroSDK_Internal:
			return "An internal error occurred.";
		case NeuroSDK_Uninitialized:
			return "Component is not initialized.";
		case NeuroSDK_NoGameName:
			return "Game name is missing.";
		case NeuroSDK_OutOfMemory:
			return "Memory allocation failed.";
		case NeuroSDK_NoURL:
			return "No URL provided.";
		case NeuroSDK_ConnectionError:
			return "Failed to establish a connection.";
		case NeuroSDK_MessageQueueFull:
			return "Message queue is full.";
		case NeuroSDK_ReceivedBinary:
			return "Unexpected binary data received.";
		case NeuroSDK_InvalidJSON:
			return "Received malformed JSON.";
		case NeuroSDK_UnknownCommand:
			return "Unknown command received.";
		case NeuroSDK_InvalidMessage:
			return "Message format is invalid.";
		case NeuroSDK_CommandNotAvailable:
			return "The requested command is not available in this context.";
		case NeuroSDK_SendFailed:
			return "Failed to send message.";
		default:
			return "Unknown error code.";
	}
}

static neurosdk_error_e parse_s2c_json(context_t *ctx,
                                       neurosdk_message_t *msg,
                                       char const *json,
                                       int len) {
	if (!ctx) {
		return NeuroSDK_Uninitialized;
	}
	if (!json || len <= 0) {
		LOG_ERROR(ctx, "[parse_s2c_json] Provided JSON is empty or null.");
		return NeuroSDK_InvalidJSON;
	}

	neurosdk_error_e res = NeuroSDK_None;
	json_value_t *root = json_parse(json, len);
	if (!root) {
		LOG_ERROR(ctx, "[parse_s2c_json] Could not parse message: invalid JSON.");
		return NeuroSDK_InvalidJSON;
	}
	if (root->type != json_type_object) {
		LOG_ERROR(ctx, "[parse_s2c_json] Parsed JSON root is not an object.");
		res = NeuroSDK_InvalidJSON;
		goto cleanup;
	}

	json_object_t *root_obj = (json_object_t *)root->payload;
	json_object_element_t *root_elem = root_obj->start;

	neurosdk_message_kind_e kind = 0xFFFF;

	while (root_elem) {
		if (!strcmp(root_elem->name->string, "command")) {
			if (root_elem->value->type != json_type_string) {
				LOG_ERROR(ctx, "[parse_s2c_json] 'command' field is not a string.");
				res = NeuroSDK_InvalidJSON;
				goto cleanup;
			}
			json_string_t *value_str = (json_string_t *)root_elem->value->payload;

			if (!strcmp(value_str->string, "action")) {
				kind = NeuroSDK_MessageKind_Action;
			} else {
				LOG_ERROR(ctx, "[parse_s2c_json] Unknown command '%s'.",
				          value_str->string);
				res = NeuroSDK_UnknownCommand;
				goto cleanup;
			}
		}
		root_elem = root_elem->next;
	}

	if (kind == 0xFFFF) {
		LOG_ERROR(ctx, "[parse_s2c_json] Missing or invalid 'command' field.");
		res = NeuroSDK_InvalidJSON;
		goto cleanup;
	}

	if (kind == NeuroSDK_MessageKind_Action) {
		root_elem = root_obj->start;
		while (root_elem) {
			if (!strcmp(root_elem->name->string, "data")) {
				if (root_elem->value->type != json_type_object) {
					LOG_ERROR(ctx, "[parse_s2c_json] 'data' field is not an object.");
					res = NeuroSDK_InvalidJSON;
					goto cleanup;
				}
				json_object_t *data_obj = (json_object_t *)root_elem->value->payload;
				json_object_element_t *obj_root = data_obj->start;

				char *id = NULL, *name = NULL, *data = NULL;

				while (obj_root) {
					if (!strcmp(obj_root->name->string, "id")) {
						if (obj_root->value->type != json_type_string) {
							LOG_ERROR(ctx, "[parse_s2c_json] 'id' field must be a string.");
							res = NeuroSDK_InvalidJSON;
							goto parse_cleanup;
						}
						json_string_t *str = (json_string_t *)obj_root->value->payload;
						id = strdup(str->string);
					} else if (!strcmp(obj_root->name->string, "name")) {
						if (obj_root->value->type != json_type_string) {
							LOG_ERROR(ctx, "[parse_s2c_json] 'name' field must be a string.");
							res = NeuroSDK_InvalidJSON;
							goto parse_cleanup;
						}
						json_string_t *str = (json_string_t *)obj_root->value->payload;
						name = strdup(str->string);
					} else if (!strcmp(obj_root->name->string, "data")) {
						if (obj_root->value->type == json_type_null) {
							data = NULL;
						} else if (obj_root->value->type == json_type_string) {
							json_string_t *str = (json_string_t *)obj_root->value->payload;
							data = strdup(str->string);
						} else {
							LOG_ERROR(
							    ctx,
							    "[parse_s2c_json] 'data' field must be a string or null.");
							res = NeuroSDK_InvalidJSON;
							goto parse_cleanup;
						}
					}
					obj_root = obj_root->next;
				}

				if (!id || !name) {
					LOG_ERROR(ctx,
					          "[parse_s2c_json] 'data' object for 'action' must contain "
					          "'id' and 'name'.");
					res = NeuroSDK_InvalidJSON;
					goto parse_cleanup;
				}

				msg->kind = NeuroSDK_MessageKind_Action;
				msg->value.action = (neurosdk_message_action_t){
				    .id = id,
				    .name = name,
				    .data = data,
				};
				goto cleanup;
			parse_cleanup:
				if (id)
					free(id);
				if (name)
					free(name);
				if (data)
					free(data);
				goto cleanup;
			}
			root_elem = root_elem->next;
		}
	} else {
		LOG_ERROR(ctx, "[parse_s2c_json] Received an unhandled S2C command.");
		unreachable();
	}

cleanup:
	free(root);
	return res;
}

static void connection_fn_(struct mg_connection *c, int ev, void *ev_data) {
	context_t *ctx = (context_t *)c->fn_data;

	ctx->conn_err = NeuroSDK_None;

	if (ev == MG_EV_HTTP_MSG) {
		mg_ws_upgrade(c, ev_data, NULL);
		return;
	}
	if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
		LOG_WARN(ctx,
		         "Connection closed or error occurred (ev=%d). Marking "
		         "as disconnected.",
		         ev);
		ctx->connected = false;
		return;
	}
	if (ev == MG_EV_WS_OPEN) {
		LOG_INFO(ctx, "Websocket connection opened successfully.");
		ctx->connected = true;
		return;
	}
	if (ev == MG_EV_WS_MSG) {
		struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
		for (size_t i = 0; i < wm->data.len; i++) {
			if (!isprint((unsigned char)wm->data.buf[i]) &&
			    !isspace((unsigned char)wm->data.buf[i])) {
				LOG_ERROR(ctx, "Received binary (non-plaintext) data from server!");
				ctx->conn_err = NeuroSDK_ReceivedBinary;
				return;
			}
		}
		neurosdk_message_t msg;
		LOG_DEBUG(ctx, "Received message: %.*s", (int)wm->data.len, wm->data.buf);
		ctx->conn_err = parse_s2c_json(ctx, &msg, wm->data.buf, (int)wm->data.len);
		if (!ctx->conn_err) {
			if (ctx->message_queue_size == ctx->message_queue_cap) {
				LOG_ERROR(ctx, "Message queue is full! (NeuroSDK_MessageQueueFull).");
				ctx->conn_err = NeuroSDK_MessageQueueFull;
				return;
			}
			ctx->message_queue[ctx->message_queue_size++] = msg;
		}

		c->recv.len = 0;
	} else if (ev == MG_EV_WAKEUP) {
		mtx_lock(&ctx->out_mtx);
		for (int i = 0; i < ctx->pending_messages_size; i++) {
			char *msg = ctx->pending_messages[i];
			LOG_DEBUG(ctx, "Sending message: %s", msg);
			mg_ws_send(c, msg, strlen(msg), WEBSOCKET_OP_TEXT);
			free(msg);
		}
		ctx->pending_messages_size = 0;
		mtx_unlock(&ctx->out_mtx);
	}
}

NEUROSDK_EXPORT neurosdk_error_e
neurosdk_context_create(neurosdk_context_t *ctx,
                        neurosdk_context_create_desc_t *desc) {
	neurosdk_error_e res = NeuroSDK_None;
	context_t *context = malloc(sizeof(context_t));
	if (!context) {
		return NeuroSDK_OutOfMemory;
	}
	memset(context, 0, sizeof(*context));

	if (!desc->game_name || !strlen(desc->game_name)) {
		free(context);
		return NeuroSDK_NoGameName;
	}
	context->game_name = escape_string(desc->game_name);
	context->poll_ms = desc->poll_ms;

	context->user_data = desc->user_data;
	context->callback_log = desc->callback_log;
	if (!context->callback_log) {
		context->callback_log = default_logger;
	}

	context->debug_prints = desc->flags & NeuroSDK_ContextCreateFlags_DebugPrints;
	context->validation_layers =
	    desc->flags & NeuroSDK_ContextCreateFlags_ValidationLayers;

	context->pending_messages_cap = MESSAGE_QUEUE_SIZE;
	context->pending_messages_size = 0;
	context->pending_messages =
	    malloc(context->pending_messages_cap * sizeof(char *));

	context->message_queue_cap = MESSAGE_QUEUE_SIZE;
	context->message_queue_size = 0;
	context->message_queue =
	    malloc(context->message_queue_cap * sizeof(neurosdk_message_t));
	if (!context->message_queue) {
		free((void *)context->game_name);
		free(context);
		return NeuroSDK_OutOfMemory;
	}

	char *fetched_url = (char *)desc->url;
	if (!fetched_url) {
		fetched_url = getenv(ENVIRONMENT_VARIABLE_NAME);
	}
	if (!fetched_url) {
		res = NeuroSDK_NoURL;
		goto cleanup;
	}

	mg_mgr_init(&context->mgr);
	mg_log_set(MG_LL_NONE);
	mg_wakeup_init(&context->mgr);

	if (mtx_init(&context->out_mtx, mtx_plain) != thrd_success) {
		res = NeuroSDK_Internal;
		goto cleanup2;
	}

	struct mg_str host = mg_url_host(fetched_url);
	unsigned short port = mg_url_port(fetched_url);
	context->conn =
	    mg_ws_connect(&context->mgr, fetched_url, connection_fn_, (void *)context,
	                  "Host: %.*s:%u\r\n", (int)host.len, host.buf, port);

	if (!context->conn) {
		res = NeuroSDK_ConnectionError;
		goto cleanup3;
	}

	for (int i = 0; i < 10 && !context->connected; i++) {
		mg_mgr_poll(&context->mgr, 300);
	}
	if (!context->connected) {
		res = NeuroSDK_ConnectionError;
		goto cleanup3;
	}

	(*ctx) = (neurosdk_context_t)context;
	return res;

cleanup3:
	mtx_destroy(&context->out_mtx);
cleanup2:
	mg_mgr_free(&context->mgr);
cleanup:
	free(context->pending_messages);
	free(context->message_queue);
	free((void *)context->game_name);
	free(context);
	return res;
}

NEUROSDK_EXPORT neurosdk_error_e
neurosdk_context_destroy(neurosdk_context_t *ctx) {
	if (!ctx || !(*ctx)) {
		return NeuroSDK_Uninitialized;
	}
	context_t *context = (context_t *)(*ctx);

	LOG_DEBUG(context, "Destroying NeuroSDK context.");

	mtx_destroy(&context->out_mtx);
	mg_mgr_free(&context->mgr);

	free(context->pending_messages);
	free(context->message_queue);
	free((void *)context->game_name);
	free(context);

	*ctx = NULL;
	return NeuroSDK_None;
}

NEUROSDK_EXPORT neurosdk_error_e
neurosdk_context_poll(neurosdk_context_t *ctx,
                      OUT neurosdk_message_t **messages,
                      OUT int *count) {
	if (!ctx || !(*ctx)) {
		return NeuroSDK_Uninitialized;
	}
	context_t *context = (context_t *)(*ctx);

	if (!context->conn) {
		LOG_ERROR(context,
		          "neurosdk_context_poll called but 'conn' is NULL. Context may "
		          "be uninitialized.");
		return NeuroSDK_Uninitialized;
	}

	LOG_DEBUG(context, "Polling context for new messages.");

	mg_mgr_poll(&context->mgr, context->poll_ms);

	if (context->conn_err) {
		LOG_ERROR(context, "Connection error during poll: %s",
		          neurosdk_error_string(context->conn_err));
		return context->conn_err;
	}

	*messages = context->message_queue;
	*count = context->message_queue_size;
	context->message_queue_size = 0;

	return NeuroSDK_None;
}

static void make_array(char **strings, int count, OUT char **json_str) {
	int total = 2;  // '[' and ']'
	for (int i = 0; i < count; i++) {
		// "..." => 2 quotes + length
		total += 2 + (int)strlen(strings[i]);
		if (i < count - 1)
			total++;  // comma
	}
	*json_str = malloc(total + 1);
	if (!*json_str)
		return;

	char *dst = *json_str;
	*dst++ = '[';
	for (int i = 0; i < count; i++) {
		*dst++ = '"';
		strcpy(dst, strings[i]);
		dst += strlen(strings[i]);
		*dst++ = '"';
		if (i < count - 1) {
			*dst++ = ',';
		}
	}
	*dst++ = ']';
	*dst = '\0';
}

NEUROSDK_EXPORT neurosdk_error_e
neurosdk_context_send(neurosdk_context_t *ctx, neurosdk_message_t *msg) {
	if (!ctx || !(*ctx)) {
		return NeuroSDK_Uninitialized;
	}
	context_t *context = (context_t *)(*ctx);
	if (!context->conn) {
		LOG_ERROR(context,
		          "neurosdk_context_send: invalid context (conn is NULL).");
		return NeuroSDK_Uninitialized;
	}
	if (!neurosdk_context_connected(ctx)) {
		LOG_ERROR(context,
		          "neurosdk_context_send: cannot send message because we are "
		          "not connected.");
		return NeuroSDK_ConnectionError;
	}

	char *str = NULL;
	int bytes = 0;

	switch (msg->kind) {
		case NeuroSDK_MessageKind_Action:
			LOG_ERROR(context,
			          "Cannot send NeuroSDK_MessageKind_Action from the client. "
			          "Command is not available in this direction.");
			return NeuroSDK_CommandNotAvailable;

		case NeuroSDK_MessageKind_Startup:
			bytes = aprintf(&str, "{\"command\":\"startup\",\"game\":\"%s\"}",
			                context->game_name);
			break;

		case NeuroSDK_MessageKind_Context: {
			if (!msg->value.context.message) {
				LOG_ERROR(context,
				          "MessageKind_Context: 'message' is required and is NULL.");
				return NeuroSDK_InvalidMessage;
			}
			if (msg->value.context.silent != true &&
			    msg->value.context.silent != false) {
				msg->value.context.silent = false;
			}
			char *escaped_str = escape_string(msg->value.context.message);
			if (!escaped_str) {
				LOG_ERROR(context,
				          "Out of memory while escaping 'message' for context.");
				return NeuroSDK_OutOfMemory;
			}
			bytes = aprintf(&str,
			                "{\"command\":\"context\",\"game\":\"%s\",\"data\":{"
			                "\"message\":\"%s\",\"silent\":%s}}",
			                context->game_name, escaped_str,
			                msg->value.context.silent ? "true" : "false");
			free(escaped_str);
		} break;

		case NeuroSDK_MessageKind_ActionsRegister: {
			if (msg->value.actions_register.actions_len <= 0) {
				LOG_WARN(context,
				         "MessageKind_ActionsRegister called with zero actions. "
				         "Nothing to register?");
			}
			int len = msg->value.actions_register.actions_len;
			char **json_actions = malloc(sizeof(char *) * (size_t)len);
			if (!json_actions) {
				LOG_ERROR(context,
				          "Out of memory while building actions register array.");
				return NeuroSDK_OutOfMemory;
			}

			int total_size = 0;
			for (int i = 0; i < len; i++) {
				neurosdk_action_t *action = &msg->value.actions_register.actions[i];
				if (!action->name) {
					LOG_ERROR(context,
					          "Action register: action->name is NULL at index %d.", i);
					free(json_actions);
					return NeuroSDK_InvalidMessage;
				}
				if (!action->description) {
					LOG_WARN(context,
					         "Action register: action->description is NULL at index "
					         "%d, using empty string.",
					         i);
				}
				char *name_escaped = escape_string(action->name);
				char *desc_escaped =
				    escape_string(action->description ? action->description : "");
				if (!name_escaped || !desc_escaped) {
					LOG_ERROR(context, "Out of memory escaping action fields.");
					free(name_escaped);
					free(desc_escaped);
					free(json_actions);
					return NeuroSDK_OutOfMemory;
				}
				char *schema = action->json_schema ? action->json_schema : "{}";

				int part_bytes =
				    aprintf(&json_actions[i],
				            "{\"name\":\"%s\",\"description\":\"%s\",\"schema\":%s}",
				            name_escaped, desc_escaped, schema);
				free(desc_escaped);
				free(name_escaped);
				if (part_bytes < 0) {
					LOG_ERROR(context,
					          "Out of memory building single action register payload.");
					for (int k = 0; k <= i; k++) {
						if (json_actions[k])
							free(json_actions[k]);
					}
					free(json_actions);
					return NeuroSDK_OutOfMemory;
				}
				total_size += part_bytes;
			}

			int approx_size =
			    total_size + (len - 1) + 2 + 1;  // +2 for '[]', +1 for null-term
			char *actions_array = malloc((size_t)approx_size);
			if (!actions_array) {
				LOG_ERROR(context, "Out of memory building final actions array JSON.");
				for (int i = 0; i < len; i++) {
					free(json_actions[i]);
				}
				free(json_actions);
				return NeuroSDK_OutOfMemory;
			}
			char *ptr = actions_array;
			*ptr++ = '[';
			for (int i = 0; i < len; i++) {
				int frag_len = (int)strlen(json_actions[i]);
				memcpy(ptr, json_actions[i], (size_t)frag_len);
				ptr += frag_len;
				if (i < len - 1) {
					*ptr++ = ',';
				}
				free(json_actions[i]);
			}
			*ptr++ = ']';
			*ptr = '\0';
			free(json_actions);

			bytes = aprintf(&str,
			                "{\"command\":\"actions/"
			                "register\",\"game\":\"%s\",\"data\":{\"actions\":%s}}",
			                context->game_name, actions_array);
			free(actions_array);
		} break;

		case NeuroSDK_MessageKind_ActionsUnregister: {
			if (msg->value.actions_unregister.action_names_len <= 0) {
				LOG_WARN(context,
				         "MessageKind_ActionsUnregister called with zero action "
				         "names. Nothing to unregister?");
			}
			char *json_str = NULL;
			make_array(msg->value.actions_unregister.action_names,
			           msg->value.actions_unregister.action_names_len, &json_str);
			if (!json_str) {
				LOG_ERROR(context,
				          "Out of memory building actions/unregister array JSON.");
				return NeuroSDK_OutOfMemory;
			}
			bytes = aprintf(
			    &str,
			    "{\"command\":\"actions/"
			    "unregister\",\"game\":\"%s\",\"data\":{\"action_names\":%s}}",
			    context->game_name, json_str);
			free(json_str);
		} break;

		case NeuroSDK_MessageKind_ActionsForce: {
			char *query = msg->value.actions_force.query;
			if (!query) {
				LOG_ERROR(context, "actions/force: 'query' is required but is NULL.");
				return NeuroSDK_InvalidMessage;
			}
			char **action_names = msg->value.actions_force.action_names;
			if (!action_names || msg->value.actions_force.action_names_len <= 0) {
				LOG_ERROR(context,
				          "actions/force: 'action_names' is required but is empty.");
				return NeuroSDK_InvalidMessage;
			}

			bool ephemeral_null = false;
			if (msg->value.actions_force.ephemeral_context != true &&
			    msg->value.actions_force.ephemeral_context != false) {
				ephemeral_null = true;
			}

			char *state = msg->value.actions_force.state;
			if (state) {
				char *escaped = escape_string(state);
				if (!escaped) {
					LOG_ERROR(context, "Out of memory escaping 'state'.");
					return NeuroSDK_OutOfMemory;
				}
				char *temp = NULL;
				if (aprintf(&temp, "\"%s\"", escaped) < 0) {
					free(escaped);
					LOG_ERROR(context, "Out of memory building 'state' JSON part.");
					return NeuroSDK_OutOfMemory;
				}
				free(escaped);
				state = temp;
			} else {
				state = strdup("null");
				if (!state) {
					LOG_ERROR(context, "Out of memory setting default state to null.");
					return NeuroSDK_OutOfMemory;
				}
			}

			char *ephemeral_context_str = "null";
			if (!ephemeral_null) {
				ephemeral_context_str =
				    msg->value.actions_force.ephemeral_context ? "true" : "false";
			}

			char const *priority = "low";
			switch (msg->value.actions_force.priority) {
				case NeuroSDK_Priority_Medium:
					priority = "medium";
					break;
				case NeuroSDK_Priority_High:
					priority = "high";
					break;
				case NeuroSDK_Priority_Critical:
					priority = "critical";
					break;
				case NeuroSDK_Priority_Low:
				default:
					priority = "low";
			}

			char *json_str = NULL;
			make_array(action_names, msg->value.actions_force.action_names_len,
			           &json_str);
			if (!json_str) {
				free(state);
				LOG_ERROR(
				    context,
				    "Out of memory building action_names array in actions/force.");
				return NeuroSDK_OutOfMemory;
			}

			bytes = aprintf(
			    &str,
			    "{\"command\":\"actions/"
			    "force\",\"game\":\"%s\",\"data\":{\"state\":%s,\"query\":\"%s\","
			    "\"ephemeral_context\":%s,\"action_names\":%s,\"priority\":\"%s\"}}",
			    context->game_name, state, query, ephemeral_context_str, json_str,
			    priority);

			free(json_str);
			free(state);
		} break;

		case NeuroSDK_MessageKind_ActionResult: {
			if (!msg->value.action_result.id) {
				LOG_ERROR(context, "action/result: 'id' is required but is NULL.");
				return NeuroSDK_InvalidMessage;
			}
			if (msg->value.action_result.success != true &&
			    msg->value.action_result.success != false) {
				msg->value.action_result.success = true;
			}

			char *message = strdup("null");
			if (!message) {
				LOG_ERROR(context, "Out of memory duplicating 'null' string.");
				return NeuroSDK_OutOfMemory;
			}
			if (msg->value.action_result.message) {
				free(message);
				char *tmp = escape_string(msg->value.action_result.message);
				if (!tmp) {
					LOG_ERROR(context, "Out of memory escaping 'action_result.message'.");
					return NeuroSDK_OutOfMemory;
				}
				if (aprintf(&message, "\"%s\"", tmp) < 0) {
					LOG_ERROR(context, "Out of memory building 'action_result.message'.");
					free(tmp);
					return NeuroSDK_OutOfMemory;
				}
				free(tmp);
			}

			bytes =
			    aprintf(&str,
			            "{\"command\":\"action:result\",\"game\":\"%s\",\"data\":{"
			            "\"id\":\"%s\",\"success\":%s,\"message\":%s}}",
			            context->game_name, msg->value.action_result.id,
			            msg->value.action_result.success ? "true" : "false", message);
			free(message);
		} break;

		default:
			LOG_ERROR(context, "Unknown or unhandled message kind: %d.", msg->kind);
			return NeuroSDK_UnknownCommand;
	}

	if (!str || bytes <= 0) {
		LOG_ERROR(context,
		          "Failed to build JSON message for sending (aprintf error).");
		free(str);
		return NeuroSDK_InvalidMessage;
	}

	LOG_DEBUG(context, "Queueing message for send: %s (%d bytes)", str, bytes);

	mtx_lock(&context->out_mtx);
	if (context->pending_messages_size < context->pending_messages_cap) {
		context->pending_messages[context->pending_messages_size++] = str;
	} else {
		mtx_unlock(&context->out_mtx);
		LOG_ERROR(context, "Out of memory: pending messages buffer is full.");
		free(str);
		return NeuroSDK_OutOfMemory;
	}
	mtx_unlock(&context->out_mtx);

	mg_wakeup(&context->mgr, context->conn->id, NULL, 0);

	mg_mgr_poll(&context->mgr, context->poll_ms);
	mg_mgr_poll(&context->mgr, context->poll_ms);

	return NeuroSDK_None;
}

NEUROSDK_EXPORT bool neurosdk_context_connected(neurosdk_context_t *ctx) {
	if (!ctx || !(*ctx)) {
		return false;
	}
	return ((context_t *)*ctx)->connected;
}

NEUROSDK_EXPORT neurosdk_error_e
neurosdk_message_destroy(neurosdk_message_t *msg) {
	if (!msg) {
		return NeuroSDK_Uninitialized;
	}
	if (msg->kind == NeuroSDK_MessageKind_Action) {
		neurosdk_message_action_t *action = &msg->value.action;
		free(action->id);
		free(action->name);
		if (action->data)
			free(action->data);
	} else {
		return NeuroSDK_UnknownCommand;
	}
	return NeuroSDK_None;
}

#include "mongoose.c"
#include "tinycthread.c"

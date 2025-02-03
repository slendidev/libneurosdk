#include <neurosdk.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#define unreachable() (__builtin_unreachable())
#elif defined(_MSC_VER)
#define unreachable() (__assume(false))
#else
[[noreturn]] inline void unreachable_impl() {}
#define unreachable() (unreachable_impl())
#endif

#include "json.h"
#include "mongoose.h"

#define ENVIRONMENT_VARIABLE_NAME "NEURO_SDK_WS_URL"
#define MESSAGE_QUEUE_SIZE 10

char *escape_string(const char *str) {
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

int aprintf(char **strp, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  int size = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  if (size < 0) {
    return -1;
  }

  *strp = (char *)malloc(size + 1);
  if (!*strp) {
    return -1;
  }

  va_start(args, fmt);
  int result = vsnprintf(*strp, size + 1, fmt, args);
  va_end(args);

  return result;
}

typedef struct context {
  char const *game_name;
  int poll_ms;

  struct mg_mgr mgr;
  struct mg_connection *conn;

  neurosdk_error_e conn_err;
  bool connected;

  neurosdk_message_t *message_queue;
  int message_queue_size;
  int message_queue_cap;
} context_t;

static neurosdk_error_e parse_s2c_json(neurosdk_message_t *msg, char const *json, int len) {
  neurosdk_error_e res = NeuroSDK_None;

  json_value_t *root = json_parse(json, len);
  if (!root) {
    return NeuroSDK_InvalidJSON;
  }

  if (root->type != json_type_object) {
    res = NeuroSDK_InvalidJSON;
    goto cleanup;
  }

  json_object_t *root_obj = root->payload;
  json_object_element_t *root_elem = root_obj->start;
  neurosdk_message_kind_e kind = 0xFFFF;
  while (root_elem) {
    if (!strcmp(root_elem->name->string, "command")) {
      if (root_elem->value->type != json_type_string) {
        res = NeuroSDK_InvalidJSON;
        goto cleanup;
      }
      json_string_t *value_str = root_elem->value->payload;

      if (!strcmp(value_str->string, "action")) {
        kind = NeuroSDK_Action;
      } else {
        res = NeuroSDK_UnknownCommand;
        goto cleanup;
      }
    }

    root_elem = root_elem->next;
  }
  char *id = NULL, *name = NULL, *data = NULL;

  if (kind == NeuroSDK_Action) {
    root_elem = root_obj->start;
    while (root_elem) {
      if (!strcmp(root_elem->name->string, "data")) {
        if (root_elem->value->type != json_type_object) {
          res = NeuroSDK_InvalidJSON;
          goto cleanup;
        }

        json_object_t *data_obj = root_elem->value->payload;

        json_object_element_t *obj_root = data_obj->start;
        while (obj_root) {
          if (!strcmp(obj_root->name->string, "id")) {
            if (obj_root->value->type != json_type_string) {
              res = NeuroSDK_InvalidJSON;
              goto cleanup2;
            }
            json_string_t *str = obj_root->value->payload;
            id = strdup(str->string);
          } else if (!strcmp(obj_root->name->string, "name")) {
            if (obj_root->value->type != json_type_string) {
              res = NeuroSDK_InvalidJSON;
              goto cleanup2;
            }
            json_string_t *str = obj_root->value->payload;
            name = strdup(str->string);
          } else if (!strcmp(obj_root->name->string, "data")) {
            if (obj_root->value->type == json_type_null) {
              data = NULL;
            } else if (obj_root->value->type != json_type_string) {
              res = NeuroSDK_InvalidJSON;
              goto cleanup2;
            } else {
              json_string_t *str = obj_root->value->payload;
              data = strdup(str->string);
            }
          }

          obj_root = obj_root->next;
        }

        if (!id || !name) {
          res = NeuroSDK_InvalidJSON;
          goto cleanup2;
        }

        msg->kind = kind;
        msg->value.action = (neurosdk_message_action_t){
            .id = id,
            .name = name,
            .data = data,
        };

        goto cleanup;
      }

      root_elem = root_elem->next;
    }
  } else if (kind == 0xFFFF) {
    res = NeuroSDK_InvalidJSON;
    goto cleanup;
  } else {
    unreachable();
  }

cleanup2:
  if (id)
    free(id);
  if (name)
    free(name);
  if (data)
    free(data);

cleanup:
  free(root);
  return res;
}

static void connection_fn_(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  context_t *ctx = (context_t *)fn_data;

  ctx->conn_err = NeuroSDK_None;

  if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    ctx->connected = false;
    return;
  }

  if (ev == MG_EV_WS_OPEN) {
    ctx->connected = true;
    return;
  }

  if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;

    for (size_t i = 0; i < wm->data.len; i++) {
      if (!isprint(wm->data.buf[i])) {
        ctx->conn_err = NeuroSDK_ReceivedBinary;
        return;
      }
    }

    neurosdk_message_t msg;
    ctx->conn_err = parse_s2c_json(&msg, wm->data.buf, wm->data.len);
    if (!ctx->conn_err) {
      if (ctx->message_queue_size == ctx->message_queue_cap) {
        ctx->conn_err = NeuroSDK_MessageQueueFull;
        return;
      }

      ctx->message_queue[ctx->message_queue_size++] = msg;
    }
  }
}

neurosdk_error_e
neurosdk_context_create(neurosdk_context_t *ctx, neurosdk_context_create_desc_t desc) {
  neurosdk_error_e res = NeuroSDK_None;

  context_t *context = (context_t *)calloc(1, sizeof(context_t));
  if (!context) {
    return NeuroSDK_OutOfMemory;
  }

  if (!desc.game_name || !strlen(desc.game_name)) {
    return NeuroSDK_NoGameName;
  }
  context->game_name = escape_string(desc.game_name);
  context->poll_ms = desc.poll_ms;

  context->message_queue_cap = MESSAGE_QUEUE_SIZE;
  context->message_queue_size = 0;
  context->message_queue = malloc(context->message_queue_cap * sizeof(neurosdk_message_t));

  char *fetched_url = (char *)desc.url;
  if (!fetched_url) {
    fetched_url = getenv(ENVIRONMENT_VARIABLE_NAME);
  }

  if (!fetched_url) {
    res = NeuroSDK_NoURL;
    goto cleanup;
  }

  mg_mgr_init(&context->mgr);
  context->conn = mg_ws_connect(&context->mgr, fetched_url, (mg_event_handler_t)(void *)connection_fn_, (void *)context, NULL);
  if (!context->conn) {
    res = NeuroSDK_ConnectionError;
    goto cleanup2;
  }

  // Wait for connection.
  while (!context->connected)
    mg_mgr_poll(&context->mgr, 1000);

  *ctx = (neurosdk_context_t)res;

  return res;

cleanup2:
  mg_mgr_free(&context->mgr);

cleanup:
  free(context->message_queue);
  free((void *)context->game_name);

  return res;
}

neurosdk_error_e neurosdk_context_destroy(neurosdk_context_t *ctx) {
  if (!ctx || !(*ctx)) {
    return NeuroSDK_Uninitialized;
  }

  context_t *context = (context_t *)(*ctx);

  mg_mgr_free(&context->mgr);
  free(context->message_queue);
  free((void *)context->game_name);

  *ctx = NULL;
  return NeuroSDK_None;
}

neurosdk_error_e neurosdk_context_poll(neurosdk_context_t *ctx, OUT neurosdk_message_t **messages, OUT int *count) {
  if (!ctx || !(*ctx)) {
    return NeuroSDK_Uninitialized;
  }
  context_t *context = (context_t *)ctx;

  context->conn_err = NeuroSDK_None;

  mg_mgr_poll(&context->mgr, context->poll_ms);

  if (context->conn_err) {
    return context->conn_err;
  }

  *messages = context->message_queue;
  *count = context->message_queue_size;

  context->message_queue_size = 0;

  return NeuroSDK_None;
}

neurosdk_error_e neurosdk_context_send(neurosdk_context_t *ctx, neurosdk_message_t *msg) {
  // FIXME: Make all allocations here use a dynamically growing arena, that gets freed on each call of this function.

  if (!ctx || !(*ctx)) {
    return NeuroSDK_Uninitialized;
  }
  context_t *context = (context_t *)ctx;
  if (!context->conn) {
    return NeuroSDK_Uninitialized;
  }

  char *str = NULL;
  int bytes = 0;
  if (msg->kind == NeuroSDK_Action) {
    return NeuroSDK_CommandNotAvailable;
  } else if (msg->kind == NeuroSDK_Startup) {
    bytes = aprintf(&str, "{\"command\":\"startup\",\"game\":\"%s\"}", context->game_name);
  } else if (msg->kind == NeuroSDK_Context) {
    char *escaped_str = escape_string(msg->value.context.message);
    bytes = aprintf(&str, "{\"command\":\"context\",\"game\":\"%s\",\"data\":\"{\"message\":\"%s\",\"silent\":%s}\"}", context->game_name, escaped_str, msg->value.context.silent ? "true" : "false");
    free(escaped_str);
  } else if (msg->kind == NeuroSDK_ActionsRegister) {
    char **json_actions = malloc(sizeof(char *) * msg->value.actions_register.actions_len);
    int total_size = 0;
    for (int i = 0; i < msg->value.actions_register.actions_len; i++) {
      neurosdk_action_t *action = &msg->value.actions_register.actions[i];
      char *name_escaped = escape_string(action->name);
      char *desc_escaped = escape_string(action->description);
      // FIXME: Validate schema keywords are not used.
      char *schema = action->json_schema;
      if (!schema)
        schema = "{}";
      int bytes = aprintf(&json_actions[i], "{\"name\":\"%s\",\"description\":\"%s\",\"schema\":%s}", name_escaped, desc_escaped, schema);
      free(desc_escaped);
      free(name_escaped);
      total_size += bytes;
    }

    int json_array_size = total_size + msg->value.actions_register.actions_len;
    char *json_array = malloc(json_array_size);
    json_array[json_array_size - 1] = 0;

    int j = 0;
    for (int i = 0; i < msg->value.actions_register.actions_len; i++) {
      int len = strlen(json_actions[i]);
      memcpy(&json_array[j], json_actions[i], len);
      j += len;
      if (i < msg->value.actions_register.actions_len - 1) {
        json_array[j++] = ',';
      }

      free(json_actions[i]);
    }
    free(json_actions);

    bytes = aprintf(&str, "{\"command\":\"actions/register\",\"game\":\"%s\",\"data\":{\"actions\":[%s]}}", context->game_name, json_array);

    free(json_array);
  } else if (msg->kind == NeuroSDK_ActionsUnregister) {

  } else {
    return NeuroSDK_UnknownCommand;
  }

  if (!str || !bytes) {
    return NeuroSDK_InvalidMessage;
  }

  bytes = mg_ws_send(context->conn, str, bytes, WEBSOCKET_OP_TEXT);
  if (!bytes) {
    return NeuroSDK_SendFailed;
  }
  return NeuroSDK_None;
}

bool neurosdk_context_connected(neurosdk_context_t *ctx) {
  if (!ctx) {
    return false;
  }
  return ((context_t *)ctx)->connected;
}

neurosdk_error_e neurosdk_message_destroy(neurosdk_message_t *msg) {
  if (!msg) {
    return NeuroSDK_Uninitialized;
  }

  if (msg->kind == NeuroSDK_Action) {
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

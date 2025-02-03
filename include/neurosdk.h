#ifndef _NEURO_SDK_H
#define _NEURO_SDK_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdbool.h>

#define OUT

typedef void *neurosdk_context_t;

typedef enum neurosdk_error {
  NeuroSDK_None = 0,
  NeuroSDK_Uninitialized,
  NeuroSDK_NoGameName,
  NeuroSDK_OutOfMemory,
  NeuroSDK_NoURL,
  NeuroSDK_ConnectionError,
  NeuroSDK_MessageQueueFull,
  NeuroSDK_ReceivedBinary,
  NeuroSDK_InvalidJSON,
  NeuroSDK_UnknownCommand,
  NeuroSDK_InvalidMessage,
  NeuroSDK_CommandNotAvailable,
  NeuroSDK_SendFailed,
} neurosdk_error_e;

typedef struct neurosdk_action {
  char *name;
  char *description;
  char *json_schema;
} neurosdk_action_t;

typedef struct neurosdk_message_startup {
} neurosdk_message_startup_t;

typedef struct neurosdk_message_context {
  char *message;
  bool silent;
} neurosdk_message_context_t;

typedef struct neurosdk_message_actions_register {
  neurosdk_action_t *actions;
  int actions_len;
} neurosdk_message_actions_register_t;

typedef struct neurosdk_message_actions_unregister {
  char **action_names;
  int action_names_len;
} neurosdk_message_actions_unregister_t;

typedef struct neurosdk_message_action {
  char *id;
  char *name;
  char *data; // Can be NULL.
} neurosdk_message_action_t;

typedef enum neurosdk_message_kind {
  NeuroSDK_Unknown = 0,
  // Game to Neuro (C2S)
  NeuroSDK_Startup,
  NeuroSDK_Context,
  NeuroSDK_ActionsRegister,
  NeuroSDK_ActionsUnregister,
  // Neuro to Game (S2C)
  NeuroSDK_Action,
} neurosdk_message_kind_e;

typedef struct neurosdk_message {
  neurosdk_message_kind_e kind;
  union {
    // Game to Neuro (C2S)
    neurosdk_message_startup_t startup;
    neurosdk_message_context_t context;
    neurosdk_message_actions_register_t actions_register;
    neurosdk_message_actions_unregister_t actions_unregister;
    // Neuro to Game (S2C)
    neurosdk_message_action_t action;
  } value;
} neurosdk_message_t;

typedef struct neurosdk_context_create_desc {
  char const *url; // If NULL, then the NEURO_SDK_WS_URL environment variable will be used.
  char const *game_name;
  int poll_ms;
} neurosdk_context_create_desc_t;

// This function hangs until it receives a connection.
neurosdk_error_e neurosdk_context_create(neurosdk_context_t *ctx, neurosdk_context_create_desc_t desc);
neurosdk_error_e neurosdk_message_destroy(neurosdk_message_t *msg);

neurosdk_error_e neurosdk_context_destroy(neurosdk_context_t *ctx);
// Requires the entries to be destroyed!
// You cannot re-use messages unless doing an explicit memcpy().
neurosdk_error_e neurosdk_context_poll(neurosdk_context_t *ctx, OUT neurosdk_message_t **messages, OUT int *count);
neurosdk_error_e neurosdk_context_send(neurosdk_context_t *ctx, neurosdk_message_t *msg);
bool neurosdk_context_connected(neurosdk_context_t *ctx);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _NEURO_SDK_H

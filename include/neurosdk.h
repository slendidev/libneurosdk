#ifndef _NEURO_SDK_H
#define _NEURO_SDK_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdbool.h>

#if defined(__linux__) || defined(__APPLE__)
#define NEUROSDK_EXPORT
#else
#define NEUROSDK_EXPORT __declspec(dllexport)
#endif

#define OUT

typedef void *neurosdk_context_t;

typedef enum neurosdk_error {
  NeuroSDK_None = 0,
  NeuroSDK_Internal,
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

typedef struct neurosdk_message_actions_force {
  char *state;
  char *query;
  bool ephemeral_context; // Set to anything but true or false to set it null.
  char **action_names;
  int action_names_len;
} neurosdk_message_actions_force_t;

typedef struct neurosdk_message_action_result {
  char *id;
  bool success;
  char *message; // May be NULL.
} neurosdk_message_action_result_t;

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
  NeuroSDK_ActionsForce,
  NeuroSDK_ActionResult,
  // Neuro to Game (S2C)
  NeuroSDK_Action,
} neurosdk_message_kind_e;

typedef struct neurosdk_message {
  neurosdk_message_kind_e kind;
  union {
    // Game to Neuro (C2S)
    neurosdk_message_context_t context;
    neurosdk_message_actions_register_t actions_register;
    neurosdk_message_actions_unregister_t actions_unregister;
    neurosdk_message_actions_force_t actions_force;
    neurosdk_message_action_result_t action_result;
    // Neuro to Game (S2C)
    neurosdk_message_action_t action;
  } value;
} neurosdk_message_t;

typedef struct neurosdk_context_create_desc {
  char const *url; // If NULL, then the NEURO_SDK_WS_URL environment variable will be used.
  char const *game_name;
  int poll_ms;
} neurosdk_context_create_desc_t;

NEUROSDK_EXPORT char const *neurosdk_version(void);
NEUROSDK_EXPORT char const *neurosdk_git_hash(void);

NEUROSDK_EXPORT neurosdk_error_e neurosdk_message_destroy(neurosdk_message_t *msg);

// This function hangs until it receives a connection.
NEUROSDK_EXPORT neurosdk_error_e neurosdk_context_create(neurosdk_context_t *ctx, neurosdk_context_create_desc_t *desc);
NEUROSDK_EXPORT neurosdk_error_e neurosdk_context_destroy(neurosdk_context_t *ctx);
// Requires the entries to be destroyed!
// You cannot re-use messages unless doing an explicit memcpy().
NEUROSDK_EXPORT neurosdk_error_e neurosdk_context_poll(neurosdk_context_t *ctx, OUT neurosdk_message_t **messages, OUT int *count);
NEUROSDK_EXPORT neurosdk_error_e neurosdk_context_send(neurosdk_context_t *ctx, neurosdk_message_t *msg);
NEUROSDK_EXPORT bool neurosdk_context_connected(neurosdk_context_t *ctx);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _NEURO_SDK_H

#ifndef _NEURO_SDK_H
#define _NEURO_SDK_H

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#include <stdbool.h>

#if defined(__linux__) || defined(__APPLE__)
#define NEUROSDK_EXPORT
#else
#define NEUROSDK_EXPORT __declspec(dllexport)
#endif

#define OUT

////////////////////////////////
// Handles and error handling //
////////////////////////////////

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

//////////////
// Messages //
//////////////

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
	bool ephemeral_context;  // Set to anything but true or false to set it NULL.
	char **action_names;
	int action_names_len;
} neurosdk_message_actions_force_t;

typedef struct neurosdk_message_action_result {
	char *id;
	bool success;
	char *message;  // May be NULL.
} neurosdk_message_action_result_t;

typedef struct neurosdk_message_action {
	char *id;
	char *name;
	char *data;  // Can be NULL.
} neurosdk_message_action_t;

typedef enum neurosdk_message_kind {
	NeuroSDK_MessageKind_Unknown = 0,
	// Game to Neuro (C2S)
	NeuroSDK_MessageKind_Startup,
	NeuroSDK_MessageKind_Context,
	NeuroSDK_MessageKind_ActionsRegister,
	NeuroSDK_MessageKind_ActionsUnregister,
	NeuroSDK_MessageKind_ActionsForce,
	NeuroSDK_MessageKind_ActionResult,
	// Neuro to Game (S2C)
	NeuroSDK_MessageKind_Action,
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

////////////
// Others //
////////////

typedef enum neurosdk_severity {
	NeuroSDK_Severity_Debug,
	NeuroSDK_Severity_Info,
	NeuroSDK_Severity_Warn,
	NeuroSDK_Severity_Error,
} neurosdk_severity_e;

typedef void (*neurosdk_callback_log_t)(neurosdk_severity_e severity,
                                        char *message,
                                        void *user_data);

/////////////////
// Descriptors //
/////////////////

typedef enum neurosdk_context_create_flags {
	NeuroSDK_ContextCreateFlags_None = 0,
	NeuroSDK_ContextCreateFlags_DebugPrints = (1 << 0),
	NeuroSDK_ContextCreateFlags_ValidationLayers = (1 << 1),
} neurosdk_context_create_flags_e;

#define NEUROSDK_CONTEXT_CREATE_FLAGS_DEBUG  \
	(NeuroSDK_ContextCreateFlags_DebugPrints | \
	 NeuroSDK_ContextCreateFlags_ValidationLayers)

typedef struct neurosdk_context_create_desc {
	char const *url;  // If NULL, then the NEURO_SDK_WS_URL environment variable
	                  // will be used.
	char const *game_name;
	int poll_ms;
	void *user_data;

	neurosdk_context_create_flags_e flags;
	neurosdk_callback_log_t callback_log;
} neurosdk_context_create_desc_t;

///////////////
// Functions //
///////////////

// Get the version of the currently loaded library.
NEUROSDK_EXPORT char const *neurosdk_version(void);

// Get the checked out git commit hash of the currently loaded library.
NEUROSDK_EXPORT char const *neurosdk_git_hash(void);

// Get an error message from error enum.
NEUROSDK_EXPORT char const *neurosdk_error_string(neurosdk_error_e err);

// Free up resources for a message (Neuro to Game).
NEUROSDK_EXPORT neurosdk_error_e
neurosdk_message_destroy(neurosdk_message_t *msg);

// Create a new context. This function hangs until it receives a connection.
NEUROSDK_EXPORT neurosdk_error_e
neurosdk_context_create(neurosdk_context_t *ctx,
                        neurosdk_context_create_desc_t *desc);

// Free up resources of a context.
NEUROSDK_EXPORT neurosdk_error_e
neurosdk_context_destroy(neurosdk_context_t *ctx);

// Poll any messages received on the websocket and parse them.
// Requires the entries to be destroyed! (But not the vector!)
// You cannot re-use messages unless doing an explicit memcpy().
NEUROSDK_EXPORT neurosdk_error_e
neurosdk_context_poll(neurosdk_context_t *ctx,
                      OUT neurosdk_message_t **messages,
                      OUT int *count);

// Send a message to Neuro.
NEUROSDK_EXPORT neurosdk_error_e neurosdk_context_send(neurosdk_context_t *ctx,
                                                       neurosdk_message_t *msg);

// Check if you are still connected to Neuro.
NEUROSDK_EXPORT bool neurosdk_context_connected(neurosdk_context_t *ctx);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // _NEURO_SDK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define usleep(x) Sleep((x) / 1000)
#else
#include <unistd.h>
#endif

#include <neurosdk.h>

#define GAME_NAME "TestGame"

int main() {
	neurosdk_context_t ctx;
	neurosdk_context_create_desc_t desc = {
	    .url = "ws://localhost:8000", .game_name = GAME_NAME, .poll_ms = 100};

	neurosdk_error_e err = neurosdk_context_create(&ctx, &desc);
	if (err != NeuroSDK_None) {
		printf("Failed to create context: %d\n", err);
		return 1;
	}

	printf("Connected to Neuro!\n");

	neurosdk_message_t startup_msg = {.kind = NeuroSDK_Startup};
	err = neurosdk_context_send(&ctx, &startup_msg);
	if (err != NeuroSDK_None) {
		printf("Failed to send startup message: %d\n", err);
		neurosdk_context_destroy(&ctx);
		return 1;
	}
	printf("Sent startup message.\n");

	neurosdk_action_t action = {.name = "choose_name",
	                            .description = "Pick a username",
	                            .json_schema = "{}"};

	neurosdk_message_t reg_msg = {.kind = NeuroSDK_ActionsRegister};
	reg_msg.value.actions_register.actions = &action;
	reg_msg.value.actions_register.actions_len = 1;

	err = neurosdk_context_send(&ctx, &reg_msg);
	if (err != NeuroSDK_None) {
		printf("Failed to register action: %d\n", err);
		neurosdk_context_destroy(&ctx);
		return 1;
	}
	printf("Registered action.\n");

	neurosdk_message_t force_msg = {.kind = NeuroSDK_ActionsForce};
	force_msg.value.actions_force.state = "Simulation running";
	force_msg.value.actions_force.query = "Please execute choose_name";
	force_msg.value.actions_force.ephemeral_context = false;
	force_msg.value.actions_force.action_names = (char *[]){"choose_name"};
	force_msg.value.actions_force.action_names_len = 1;

	err = neurosdk_context_send(&ctx, &force_msg);
	if (err != NeuroSDK_None) {
		printf("Failed to force action: %d\n", err);
		neurosdk_context_destroy(&ctx);
		return 1;
	}
	printf("Requested action execution.\n");

	while (neurosdk_context_connected(&ctx)) {
		neurosdk_message_t *messages = NULL;
		int count = 0;

		err = neurosdk_context_poll(&ctx, &messages, &count);
		if (err == NeuroSDK_None && count > 0) {
			for (int i = 0; i < count; i++) {
				if (messages[i].kind == NeuroSDK_Action) {
					printf("- ID: %s\n", messages[i].value.action.id);
					printf("- Name: %s\n", messages[i].value.action.name);
					printf("- Data: %s\n", messages[i].value.action.data);

					neurosdk_message_t res_msg = {.kind = NeuroSDK_ActionResult};
					res_msg.value.action_result.id = "choose_name";
					res_msg.value.action_result.success = true;
					res_msg.value.action_result.message = "Action executed successfully";

					err = neurosdk_context_send(&ctx, &res_msg);
					if (err != NeuroSDK_None) {
						printf("Failed to send preemptive action result: %d\n", err);
						neurosdk_context_destroy(&ctx);
						return 1;
					}
					printf("Sent preemptive action result.\n");
				}
			}

			for (int i = 0; i < count; i++) {
				neurosdk_message_destroy(&messages[i]);
			}

			break;
		}
		usleep(500000);
	}

	neurosdk_context_destroy(&ctx);
	printf("Disconnected from Neuro.\n");

	return 0;
}

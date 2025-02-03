#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <neurosdk.h>

#define PLAYER 'X'
#define NEURO 'O'

void print_board(char board[9]) {
	printf("\n");
	for (int i = 0; i < 9; i++) {
		printf(" %c ", board[i] == ' ' ? ' ' : board[i]);
		if (i % 3 != 2)
			printf("|");
		else if (i != 8)
			printf("\n---+---+---\n");
	}
	printf("\n");
}

int check_win(char board[9]) {
	int wins[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
	                  {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
	for (int i = 0; i < 8; i++) {
		int a = wins[i][0], b = wins[i][1], c = wins[i][2];
		if (board[a] != ' ' && board[a] == board[b] && board[b] == board[c])
			return board[a] == PLAYER ? 1 : 2;
	}
	return 0;
}

bool board_full(char board[9]) {
	for (int i = 0; i < 9; i++) {
		if (board[i] == ' ')
			return false;
	}
	return true;
}

int main() {
	neurosdk_context_t ctx;
	neurosdk_context_create_desc_t desc = {
	    .url = NULL, .game_name = "TicTacToe", .poll_ms = 1000};

	neurosdk_error_e err;
	if ((err = neurosdk_context_create(&ctx, &desc)) != NeuroSDK_None) {
		printf("Failed to create Neuro context: %s\n", neurosdk_error_string(err));
		return 1;
	}

	neurosdk_message_t startup_msg;
	startup_msg.kind = NeuroSDK_Startup;
	if ((err = neurosdk_context_send(&ctx, &startup_msg)) != NeuroSDK_None) {
		printf("Failed to send startup message: %s\n", neurosdk_error_string(err));
		return 1;
	}

	neurosdk_message_t register_msg;
	register_msg.kind = NeuroSDK_ActionsRegister;
	neurosdk_action_t move_action = {
	    .name = "move",
	    .description = "Your move. Choose an empty cell index (0-8).",
	    .json_schema =
	        "{"
	        "  \"type\": \"integer\","
	        "  \"minimum\": 0,"
	        "  \"maximum\": 8"
	        "}",
	};
	register_msg.value.actions_register.actions = &move_action;
	register_msg.value.actions_register.actions_len = 1;

	if ((err = neurosdk_context_send(&ctx, &register_msg)) != NeuroSDK_None) {
		printf("Failed to register actions: %s\n", neurosdk_error_string(err));
		return 1;
	}

	char board[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
	int turn = 0;

	while (true) {
		print_board(board);
		if (turn == 0) {
			int move;
			printf("Your move (0-8): ");
			if (scanf("%d", &move) != 1) {
				printf("Invalid input. Try again.\n");
				while (getchar() != '\n')
					;
				continue;
			}
			if (move < 0 || move > 8 || board[move] != ' ') {
				printf("Invalid move. Try again.\n");
				continue;
			}
			board[move] = PLAYER;
		} else {
			printf("Waiting for Neuro's move...\n");

			// Force Neuro to make a move.
			neurosdk_message_t force_msg;
			force_msg.kind = NeuroSDK_ActionsForce;
			force_msg.value.actions_force.state = NULL;
			force_msg.value.actions_force.query =
			    "Your move. Choose an empty cell index (0-8).";
			force_msg.value.actions_force.ephemeral_context = false;
			char *action_names[1] = {"move"};
			force_msg.value.actions_force.action_names = action_names;
			force_msg.value.actions_force.action_names_len = 1;
			neurosdk_context_send(&ctx, &force_msg);

			bool move_received = false;
			while (!move_received) {
				neurosdk_message_t *messages = NULL;
				int count = 0;
				if ((err = neurosdk_context_poll(&ctx, &messages, &count)) !=
				    NeuroSDK_None) {
					printf("Error polling messages: %s\n", neurosdk_error_string(err));
					break;
				}
				for (int i = 0; i < count; i++) {
					if (messages[i].kind == NeuroSDK_Action) {
						char *data_str = messages[i].value.action.data;
						int neuro_move = -1;
						if (data_str != NULL)
							neuro_move = atoi(data_str);
						neurosdk_message_t action_result;
						action_result.kind = NeuroSDK_ActionResult;
						action_result.value.action_result.id = messages[i].value.action.id;
						if (neuro_move < 0 || neuro_move > 8 || board[neuro_move] != ' ') {
							action_result.value.action_result.success = false;
							action_result.value.action_result.message =
							    "Invalid move. Try again.";
							neurosdk_context_send(&ctx, &action_result);
						} else {
							board[neuro_move] = NEURO;
							action_result.value.action_result.success = true;
							action_result.value.action_result.message = "Move accepted.";
							neurosdk_context_send(&ctx, &action_result);
							move_received = true;
							break;
						}
					}
				}
				for (int i = 0; i < count; i++)
					neurosdk_message_destroy(&messages[i]);
			}
		}

		int winner = check_win(board);
		if (winner == 1) {
			print_board(board);
			printf("You win!\n");
			break;
		} else if (winner == 2) {
			print_board(board);
			printf("Neuro wins!\n");
			break;
		} else if (board_full(board)) {
			print_board(board);
			printf("It's a draw!\n");
			break;
		}

		turn = 1 - turn;
	}

	neurosdk_context_destroy(&ctx);
	return 0;
}

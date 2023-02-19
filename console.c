#include "console.h"


/** linked list of command structures */
static SLIST_HEAD(cmd_list_, cmd_item_) s_cmd_list;

/** run-time configuration options */
static console_config_t s_config;

/** temporary buffer used for command line parsing */
static char *s_tmp_line_buf;

static const cmd_item_t *find_command_by_name(const char *name);
size_t console_split_argv(char *line, char **argv, size_t argv_size);

int console_deinit(void)
{
    if (!s_tmp_line_buf) {
        return -1;
    }
    free(s_tmp_line_buf);
    s_tmp_line_buf = NULL;
    cmd_item_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &s_cmd_list, next, tmp) {
        SLIST_REMOVE(&s_cmd_list, it, cmd_item_, next);
        free(it->hint);
        free(it);
    }
    return ESP_OK;
}

int console_cmd_register(const console_cmd_t *cmd)
{
    cmd_item_t *item = NULL;
    if (!cmd || cmd->command == NULL) {
        return -1;
    }
    if (strchr(cmd->command, ' ') != NULL) {
        return -1;
    }
    item = (cmd_item_t *)find_command_by_name(cmd->command);
    if (!item) {
        // not registered before
        item = calloc(1, sizeof(*item));
        if (item == NULL) {
            return -2;
        }
    } else {
        // remove from list and free the old hint, because we will alloc new hint for the command
        SLIST_REMOVE(&s_cmd_list, item, cmd_item_, next);
        free(item->hint);
    }
    item->command = cmd->command;
    item->help = cmd->help;
    if (cmd->hint) {
        /* Prepend a space before the hint. It separates command name and
         * the hint. arg_print_syntax below adds this space as well.
         */
        int unused __attribute__((unused));
        unused = asprintf(&item->hint, " %s", cmd->hint);
    } else if (cmd->argtable) {
        /* Generate hint based on cmd->argtable */
        char *buf = NULL;
        size_t buf_size = 0;
        FILE *f = open_memstream(&buf, &buf_size);
        if (f != NULL) {
            arg_print_syntax(f, cmd->argtable, NULL);
            fclose(f);
        }
        item->hint = buf;
    }
    item->argtable = cmd->argtable;
    item->func = cmd->func;
    cmd_item_t *last = SLIST_FIRST(&s_cmd_list);
    if (last == NULL) {
        SLIST_INSERT_HEAD(&s_cmd_list, item, next);
    } else {
        cmd_item_t *it;
        while ((it = SLIST_NEXT(last, next)) != NULL) {
            last = it;
        }
        SLIST_INSERT_AFTER(last, item, next);
    }
    return 0;
}

void console_get_completion(const char *buf, linenoiseCompletions *lc)
{
    size_t len = strlen(buf);
    if (len == 0) {
        return;
    }
    cmd_item_t *it;
    SLIST_FOREACH(it, &s_cmd_list, next) {
        /* Check if command starts with buf */
        if (strncmp(buf, it->command, len) == 0) {
            linenoiseAddCompletion(lc, it->command);
        }
    }
}

const char *console_get_hint(const char *buf, int *color, int *bold)
{
    int len = strlen(buf);
    cmd_item_t *it;
    SLIST_FOREACH(it, &s_cmd_list, next) {
        if (strlen(it->command) == len &&
                strncmp(buf, it->command, len) == 0) {
            *color = s_config.hint_color;
            *bold = s_config.hint_bold;
            return it->hint;
        }
    }
    return NULL;
}

static const cmd_item_t *find_command_by_name(const char *name)
{
    const cmd_item_t *cmd = NULL;
    cmd_item_t *it;
    int len = strlen(name);
    SLIST_FOREACH(it, &s_cmd_list, next) {
        if (strlen(it->command) == len &&
                strcmp(name, it->command) == 0) {
            cmd = it;
            break;
        }
    }
    return cmd;
}

int console_run(const char *cmdline, int *cmd_ret)
{
    if (s_tmp_line_buf == NULL) {
        return -1;
    }
    char **argv = (char **) calloc(s_config.max_cmdline_args, sizeof(char *));
    if (argv == NULL) {
        return -1;
    }
    strlcpy(s_tmp_line_buf, cmdline, s_config.max_cmdline_length);

    size_t argc = esp_console_split_argv(s_tmp_line_buf, argv,
                                         s_config.max_cmdline_args);
    if (argc == 0) {
        free(argv);
        return -1;
    }
    const cmd_item_t *cmd = find_command_by_name(argv[0]);
    if (cmd == NULL) {
        free(argv);
        return -2;
    }
    *cmd_ret = (*cmd->func)(argc, argv);
    free(argv);
    return 0;
}

static int help_command(int argc, char **argv)
{
    cmd_item_t *it;

    /* Print summary of each command */
    SLIST_FOREACH(it, &s_cmd_list, next) {
        if (it->help == NULL) {
            continue;
        }
        /* First line: command name and hint
         * Pad all the hints to the same column
         */
        const char *hint = (it->hint) ? it->hint : "";
        printf("%-s %s\n", it->command, hint);
        /* Second line: print help.
         * Argtable has a nice helper function for this which does line
         * wrapping.
         */
        printf("  "); // arg_print_formatted does not indent the first line
        arg_print_formatted(stdout, 2, 78, it->help);
        /* Finally, print the list of arguments */
        if (it->argtable) {
            arg_print_glossary(stdout, (void **) it->argtable, "  %12s  %s\n");
        }
        printf("\n");
    }
    return 0;
}

int console_register_help_command(void)
{
    console_cmd_t command = {
        .command = "help",
        .help = "Print the list of registered commands",
        .func = &help_command
    };
    return console_cmd_register(&command);
}

int console_init(const console_config_t *config)
{
    if (!config) {
        return -1;
    }
    if (s_tmp_line_buf) {
        return -2;
    }
    memcpy(&s_config, config, sizeof(s_config));
    if (s_config.hint_color == 0) {
        s_config.hint_color = ANSI_COLOR_DEFAULT;
    }
    s_tmp_line_buf = calloc(config->max_cmdline_length, 1);
    if (s_tmp_line_buf == NULL) {
        return -3;
    }
    return 0;
}

static void initialize_console()
{
    /* Disable buffering on stdin and stdout */
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Initialize the console */
    console_config_t console_config = {
            .max_cmdline_args = 8,
            .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
            .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    int res =  esp_console_init(&console_config);

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);

#if CONFIG_STORE_HISTORY
    /* Load command history from filesystem */
    linenoiseHistoryLoad(HISTORY_PATH);
#endif
}

#define PROMPT_COL LOG_COLOR(LOG_COLOR_PURPLE)
void ConsoleInit(void* pvDat)
{
	initialize_console();
	esp_console_register_help_command();
	register_system();
	//xEventGroupWaitBits(Node.AppEvGrp,APP_READY,pdFALSE,pdTRUE,portMAX_DELAY);
	//register_wifi();
	/* Prompt to be printed before each line.
	     * This can be customized, made dynamic, etc.
	     */
	const char* prompt = PROMPT_COL "ArtNode> " LOG_RESET_COLOR;
    printf("\n"
           "This is ArtNode console.\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n");

    /* Figure out if the terminal supports escape sequences */
    int probe_status = linenoiseProbe();
    if (probe_status) { /* zero indicates success */
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");
        linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        prompt = "ArtNode> ";
#endif //CONFIG_LOG_COLORS
    }
    /* Main loop */
    while(true) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char* line = linenoise(prompt);
        if (line == NULL) { /* Ignore empty lines */
            continue;
        }
        /* Add the command to the history */
        linenoiseHistoryAdd(line);
#if CONFIG_STORE_HISTORY
        /* Save command history to filesystem */
        linenoiseHistorySave(HISTORY_PATH);
#endif

        /* Try to run the command */
        int ret;
        int err = console_run(line, &ret);
        if (err == -2) {
            printf("Unrecognized command, try '?' or 'help'\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(err));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }
}


#define SS_FLAG_ESCAPE 0x8

typedef enum {
    /* parsing the space between arguments */
    SS_SPACE = 0x0,
    /* parsing an argument which isn't quoted */
    SS_ARG = 0x1,
    /* parsing a quoted argument */
    SS_QUOTED_ARG = 0x2,
    /* parsing an escape sequence within unquoted argument */
    SS_ARG_ESCAPED = SS_ARG | SS_FLAG_ESCAPE,
    /* parsing an escape sequence within a quoted argument */
    SS_QUOTED_ARG_ESCAPED = SS_QUOTED_ARG | SS_FLAG_ESCAPE,
} split_state_t;

/* helper macro, called when done with an argument */
#define END_ARG() do { \
    char_out = 0; \
    argv[argc++] = next_arg_start; \
    state = SS_SPACE; \
} while(0)

size_t console_split_argv(char *line, char **argv, size_t argv_size)
{
    const int QUOTE = '"';
    const int ESCAPE = '\\';
    const int SPACE = ' ';
    split_state_t state = SS_SPACE;
    int argc = 0;
    char *next_arg_start = line;
    char *out_ptr = line;
    for (char *in_ptr = line; argc < argv_size - 1; ++in_ptr) {
        int char_in = (unsigned char) *in_ptr;
        if (char_in == 0) {
            break;
        }
        int char_out = -1;

        switch (state) {
        case SS_SPACE:
            if (char_in == SPACE) {
                /* skip space */
            } else if (char_in == QUOTE) {
                next_arg_start = out_ptr;
                state = SS_QUOTED_ARG;
            } else if (char_in == ESCAPE) {
                next_arg_start = out_ptr;
                state = SS_ARG_ESCAPED;
            } else {
                next_arg_start = out_ptr;
                state = SS_ARG;
                char_out = char_in;
            }
            break;

        case SS_QUOTED_ARG:
            if (char_in == QUOTE) {
                END_ARG();
            } else if (char_in == ESCAPE) {
                state = SS_QUOTED_ARG_ESCAPED;
            } else {
                char_out = char_in;
            }
            break;

        case SS_ARG_ESCAPED:
        case SS_QUOTED_ARG_ESCAPED:
            if (char_in == ESCAPE || char_in == QUOTE || char_in == SPACE) {
                char_out = char_in;
            } else {
                /* unrecognized escape character, skip */
            }
            state = (split_state_t) (state & (~SS_FLAG_ESCAPE));
            break;

        case SS_ARG:
            if (char_in == SPACE) {
                END_ARG();
            } else if (char_in == ESCAPE) {
                state = SS_ARG_ESCAPED;
            } else {
                char_out = char_in;
            }
            break;
        }
        /* need to output anything? */
        if (char_out >= 0) {
            *out_ptr = char_out;
            ++out_ptr;
        }
    }
    /* make sure the final argument is terminated */
    *out_ptr = 0;
    /* finalize the last argument */
    if (state != SS_SPACE && argc < argv_size - 1) {
        argv[argc++] = next_arg_start;
    }
    /* add a NULL at the end of argv */
    argv[argc] = NULL;

    return argc;
}

#ifndef CONSOLE_H_INCLUDED
#define CONSOLE_H_INCLUDED


#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "linenoise.h"
#include "argtable3.h"

typedef struct {
    size_t max_cmdline_length;  //!< length of command line buffer, in bytes
    size_t max_cmdline_args;    //!< maximum number of command line arguments to parse
    int hint_color;             //!< ASCII color code of hint text
    int hint_bold;              //!< Set to 1 to print hint text in bold
}console_config_t;

typedef int (*console_cmd_func_t)(int argc, char **argv);

typedef struct cmd_item_ {
    /**
     * Command name (statically allocated by application)
     */
    const char *command;
    /**
     * Help text (statically allocated by application), may be NULL.
     */
    const char *help;
    /**
     * Hint text, usually lists possible arguments, dynamically allocated.
     * May be NULL.
     */
    char *hint;
    console_cmd_func_t func;    //!< pointer to the command handler
    void *argtable;                 //!< optional pointer to arg table
    SLIST_ENTRY(cmd_item_) next;    //!< next command in the list
} cmd_item_t;
#endif // CONSOLE_H_INCLUDED

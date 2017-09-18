#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "config.h"
#include "platform/platform.h"

enum
{
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_UINT,
    CONFIG_TYPE_FLOAT,
};

struct ConfigOption
{
    const char *name;
    unsigned int type;
    union
    {
        bool *boolValue;
        unsigned int *uintValue;
        float *floatValue;
    };
};

// Initialize with default configuration options
struct Config gConfig =
{
    .windowWidth = 412,
    .windowHeight = 371,
#ifdef FRONTEND_WINDOWS
    .showMenuBar = true,
    .fixedAspectRatio = true,
    .snapWindowSize = true,
    .colorPalette = 0,
#endif
    .keys =
    {
        .a = 46,
        .b = 45,
        .start = 28,
        .select = 14,
        .up = 328,
        .down = 336,
        .left = 331,
        .right = 333,
        .fastFwd = 42,
    },
};

static const struct ConfigOption options[] = {
    {.name = "window_width",        .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.windowWidth},
    {.name = "window_height",       .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.windowHeight},
#ifdef FRONTEND_WINDOWS
    {.name = "show_menu_bar",       .type = CONFIG_TYPE_BOOL, .boolValue = &gConfig.showMenuBar},
    {.name = "fixed_aspect_ratio",  .type = CONFIG_TYPE_BOOL, .boolValue = &gConfig.fixedAspectRatio},
    {.name = "snap_window_size",    .type = CONFIG_TYPE_BOOL, .boolValue = &gConfig.snapWindowSize},
    {.name = "color_palette",       .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.colorPalette},
#endif
    {.name = "key_a",               .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.a},
    {.name = "key_b",               .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.b},
    {.name = "key_start",           .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.start},
    {.name = "key_select",          .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.select},
    {.name = "key_up",              .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.up},
    {.name = "key_down",            .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.down},
    {.name = "key_left",            .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.left},
    {.name = "key_right",           .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.right},
    {.name = "key_fastfwd",         .type = CONFIG_TYPE_UINT, .uintValue = &gConfig.keys.fastFwd},
};

//------------------------------------------------------------------------------
// Utility functions
//------------------------------------------------------------------------------

static char *skip_whitespace(char *str)
{
    while (isspace(*str))
        str++;
    return str;
}

static char *util_word_split(char *str)
{
    // Precondition: str must not start with a space
    assert(!isspace(*str));
    while (!isspace(*str) && *str != '\0')
        str++;
        
    // Don't go past the end of the string
    if (*str == '\0')
        return str;

    // Terminate this word.
    *(str++) = '\0';
    
    // Skip any remaining space
    return skip_whitespace(str);
}

static unsigned int util_tokenize_string(char *str, int numTokens, char **tokens)
{
    int count = 0;
    
    str = skip_whitespace(str);
    while (str[0] != '\0' && count < numTokens)
    {
        tokens[count] = str;
        str = util_word_split(str);
        count++;
    }
    return count;
}

static char *util_read_file_line(FILE *file)
{
    char *buffer;
    size_t bufferSize = 8;
    size_t offset = 0;
    
    buffer = malloc(bufferSize);
    while (1)
    {
        if (fgets(buffer + offset, bufferSize - offset, file) == NULL)
        {
            free(buffer);
            return NULL;  // Nothing could be read.
        }
        offset = strlen(buffer);
        assert(offset > 0);
        if (buffer[offset - 1] == '\n')
        {
            buffer[offset - 1] = '\0';
            break;
        }
        if (feof(file))
            break;
        
        // Buffer too small, make it bigger.
        bufferSize *= 2;
        buffer = realloc(buffer, bufferSize);
        assert(buffer != NULL);
    }
        
    return buffer;
}

//------------------------------------------------------------------------------
// Config loading and saving
//------------------------------------------------------------------------------

void config_load(const char *filename)
{
    FILE *file = fopen(filename, "r");
    char *line;

    if (file == NULL)
    {
        dbg_printf("Config file '%s' not found. Creating it.\n", filename);
        config_save(filename);
        return;
    }
    
    while ((line = util_read_file_line(file)) != NULL)
    {
        char *p = line;
        char *tokens[2];
        int numTokens;
        
        while (isspace(*p))
            p++;
        numTokens = util_tokenize_string(p, 2, tokens);
        if (numTokens != 0)
        {
            if (numTokens == 2)
            {
                const struct ConfigOption *option = NULL;
                
                for (unsigned int i = 0; i < ARRAY_COUNT(options); i++)
                {
                    if (strcmp(tokens[0], options[i].name) == 0)
                    {
                        option = &options[i];
                        break;
                    }
                }
                if (option == NULL)
                    platform_fatal_error("Unknown option '%s'\n", tokens[0]);
                else
                {
                    switch (option->type)
                    {
                      case CONFIG_TYPE_BOOL:
                        if      (strcmp(tokens[1], "true") == 0)
                            *option->boolValue = true;
                        else if (strcmp(tokens[1], "false") == 0)
                            *option->boolValue = false;
                        break;
                      case CONFIG_TYPE_UINT:
                        sscanf(tokens[1], "%u", option->uintValue);
                        break;
                      case CONFIG_TYPE_FLOAT:
                        sscanf(tokens[1], "%f", option->floatValue);
                        break;
                      default:
                        assert(0);  // Should not happen
                    }
                }
            }
            else
                puts("error: expected value");
        }
        free(line);
    }
    fclose(file);
}

void config_save(const char *filename)
{
    FILE *file = fopen(filename, "w");
    
    if (file == NULL)
    {
        platform_fatal_error("Could not open config file '%s' for writing. %s\n", strerror(errno));
        return;
    }
    
    dbg_printf("gConfig.colorPalette = %u\n", gConfig.colorPalette);
    for (unsigned int i = 0; i < ARRAY_COUNT(options); i++)
    {
        const struct ConfigOption *option = &options[i];
        
        switch (option->type)
        {
          case CONFIG_TYPE_BOOL:
            fprintf(file, "%s %s\n", option->name, *option->boolValue ? "true" : "false");
            break;
          case CONFIG_TYPE_UINT:
            fprintf(file, "%s %u\n", option->name, *option->uintValue);
            break;
          case CONFIG_TYPE_FLOAT:
            fprintf(file, "%s %f\n", option->name, *option->floatValue);
            break;
          default:
            assert(0);  // unknown type
        }
    }
    fclose(file);
    dbg_puts("wrote config");
}

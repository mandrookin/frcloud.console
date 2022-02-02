#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "frcloud.h"

#include <readline/readline.h>

int load_token(char * tokenfilename, char * auth)
{
    char * key_token;

    if (access(tokenfilename, R_OK) == -1)
    {
        int i, len, key_fd;

    not_correct_input:

        key_token = readline(
            "Security token not found. You may enter token now and it will be stored to "
            KEY_FILE
            " file in current directory.\nPress enter without input to exit console now.\n\x1B[32m\x1B[32m\x1B[1mToken> \x1B[0m");
        if (key_token == NULL) {
            fprintf(stderr, "Programm aborted by what?\n");
            return -1;
        }
        len = strlen(key_token);
        if (len == 0) {
            free(key_token);
            fprintf(stderr, "Programm aborted by user request - no token provided\n");
            return -1;
        }
        for (i = 0; i < 52; i++) {
            char c = key_token[i];
            if (isdigit(c) || (isascii(c) && islower(c)))
                continue;
            free(key_token);
            goto not_correct_input;
        }
        key_token[i] = 0;
        key_fd = open(tokenfilename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (key_fd < 0)
        {
            free(key_token);
            fprintf(stderr, "Unable create key token file: %s", KEY_FILE);
            return -2;
        }
        write(key_fd, key_token, strlen(key_token));
        if (fchmod(key_fd, S_IRUSR) == -1) {
            free(key_token);
            fprintf(stderr, "Set S_IRUSR attrib failed ob: %s", KEY_FILE);
            return -3;
        }
        close(key_fd);
        strcat(auth, key_token);
        free(key_token);
    }
    else
    {
        FILE * key_file;
        char    buff[128];
        key_file = fopen(tokenfilename, "r");
        if (fgets(buff, sizeof(buff), key_file) == NULL)
        {
            fprintf(stderr, "Unable read token\n");
            fclose(key_file);
            return -2;
        }
        fclose(key_file);
        if (strlen(buff) != 52)
        {
            fprintf(stderr, "Access token size error\n");
            return -3;
        }
        strcat(auth, buff);
    }
    return 0;
}
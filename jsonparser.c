#define _CRT_SECURE_NO_WARNINGS 1

#include "jsmn.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <unistd.h>

#include "frcloud.h"

/* Function realloc_it() is a wrapper function for standard realloc()
 * with one difference - it frees old memory pointer in case of realloc
 * failure. Thus, DO NOT use old data pointer in anyway after call to
 * realloc_it(). If your code has some kind of fallback algorithm if
 * memory can't be re-allocated - use standard realloc() instead.
 */
static inline void *realloc_it(void *ptrmem, size_t size) {
    void *p = realloc(ptrmem, size);
    if (!p) {
        free(ptrmem);
        fprintf(stderr, "realloc(): errno=%d\n", errno);
    }
    return p;
}


/*
 * An example of reading JSON from stdin and printing its content to stdout.
 * The output looks like YAML, but I'm not sure if it's really compatible.
 */

static int dump(const char *js, jsmntok_t *t, size_t count, int indent, char * holder)
{
    int i, j, k;
    jsmntok_t *key;
    char buffer[128];
    char name[128];
    char value[256];

    if (count == 0) {
        return 0;
    }
    int len = t->end - t->start;
    if (t->type == JSMN_PRIMITIVE) {
        printf("%.*s", len, js + t->start);
        memcpy(holder, js + t->start, len);
        holder[len] = 0;
        return 1;
    }
    else if (t->type == JSMN_STRING) {
        printf("'%.*s'", len, js + t->start);
        memcpy(holder, js + t->start, len);
        holder[len] = 0;
        return 1;
    }
    else if (t->type == JSMN_OBJECT) {
        //printf("\n");
        j = 0;
        for (i = 0; i < t->size; i++) {
            for (k = 0; k < indent; k++) {
                printf("  ");
            }
            key = t + 1 + j;
            j += dump(js, key, count - j, indent + 1, name);
            if (key->size > 0) {
                printf(": ");
                j += dump(js, t + 1 + j, count - j, indent + 1, value);
            }
            puts("");
            //            printf("%s: %s\n", name, value);
        }
        return j + 1;
    }
    else if (t->type == JSMN_ARRAY) {
        j = 0;
        printf("\n");
        for (i = 0; i < t->size; i++) {
            for (k = 0; k < indent - 1; k++) {
                printf("  ");
            }
            printf("   - ");
            j += dump(js, t + 1 + j, count - j, indent + 1, buffer);
            printf("\n");
        }
        return j + 1;
    }
    return 0;
}

static void get_string(const char *js, jsmntok_t *value, char * str, int size)
{
    int len = value->end - value->start;
    if (len + 1 > size)
    {
        len = size - 1;
        fprintf(stderr, "Buffer is too small for json string. String cutted at %d bytes\n.", len);
    }
    memcpy(str, js + value->start, len);
    str[len] = 0;
}

static int json_text_compare(const char *js, jsmntok_t *t, char * str)
{
    if (t->type != JSMN_STRING)
        return 0;
    int len = t->end - t->start;
    char * p = (char*)js + t->start;
    while (*str)
    {
        if (*p != *str)
            return 0;
        p++;
        str++;
        len--;
    }
    return len == 0;
}

typedef struct frc_direntry {
    struct frc_direntry * next;
    int size;
    int attrib;
    char name[128];
    object_type_t type;
    char id[32];
    char create[32];
    char edited[32];
} directory_record_t;

static int read_frcloud_dir(const char *js, jsmntok_t *t, size_t count)
{
    int i;

    if (count == 0) {
        puts("json node zero token count");
        return 0;
    }
    if (t->type != JSMN_OBJECT || t->size != 4)
        return -10;
    jsmntok_t *key = t + 1;
    if (!json_text_compare(js, key, "files"))
        return -11;
    jsmntok_t *array = key + 1;
    if (array->type != JSMN_ARRAY)
        return -12;

    puts("Date       Time      Unique ID                 Size      Name");
    puts("---------- --------  ------------------------  --------  ----------------------------------");

    directory_record_t dir;
    jsmntok_t *item = array + 1;
    char type[128];

    dir.next = 0;
    for (i = 0; i < array->size; i++)
    {
        if (item->type != JSMN_OBJECT)
            return -13;
        item++;
        if (!json_text_compare(js, item++, "name"))
            return -14;
        get_string(js, item++, dir.name, sizeof(dir.name));

        if (json_text_compare(js, item, "tags"))
        {
            item++;
            get_string(js, item, type, sizeof(type));
            //printf("Skip tags: %s\n", type);
            item += item->size + 1;
        }

        if (!json_text_compare(js, item++, "type"))
            return -15;
        get_string(js, item++, type, sizeof(type));

        dir.type = 0;
        if (strcmp(type, "Folder") == 0)
            dir.type = 2;
        else if (strcmp(type, "File") == 0)
            dir.type = 1;

        if (!json_text_compare(js, item++, "size"))
            return -16;
        get_string(js, item++, type, sizeof(type));
        dir.size = atoi(type);
        if (!json_text_compare(js, item++, "status"))
            return -17;
        get_string(js, item++, type, sizeof(type));
        if (!json_text_compare(js, item++, "statusReason"))
            return -18;
        get_string(js, item++, type, sizeof(type));
        if (!json_text_compare(js, item++, "id"))
            return -19;
        get_string(js, item++, dir.id, sizeof(dir.id));
        if (!json_text_compare(js, item++, "createdTime"))
            return -20;
        get_string(js, item++, dir.create, sizeof(dir.create));
        dir.create[10] = ' ';
        dir.create[19] = 0;
        if (!json_text_compare(js, item++, "editedTime"))
            return -21;
        get_string(js, item++, dir.edited, sizeof(dir.edited));
        dir.edited[10] = ' ';
        dir.edited[19] = 0;
        dir.attrib = 0;

        char flag = dir.type == 2 ? '/' : ' ';
        if (dir.type == 2)
            strcpy(type, "<DIR>");
        else
            snprintf(type, sizeof(type), "%d", dir.size);
        printf("%-20s %-25s %8s %c%-27s\n", dir.edited, dir.id, type, flag, dir.name);
    }

    if (!json_text_compare(js, item++, "count"))
        return -22;
    get_string(js, item++, type, sizeof(type));
    int total = atoi(type);

    if (!json_text_compare(js, item++, "skip"))
        return -23;
    get_string(js, item++, type, sizeof(type));
    int skip = atoi(type);

    if (!json_text_compare(js, item++, "take"))
        return -24;
    get_string(js, item++, type, sizeof(type));
    int take = atoi(type);

    printf("Count %d Skip %d Take %d", total, skip, take);
    return 0;
}

static jsmntok_t * prepare_json(char *js, size_t jslen, int * count)
{
    jsmn_parser p;
    jsmntok_t *tok;
    size_t tokcount = 16;
    int r;

    /* Prepare parser */
    jsmn_init(&p);

    /* Allocate some tokens as a start */
    tok = malloc(sizeof(*tok) * tokcount);
    if (tok == NULL) {
        fprintf(stderr, "malloc(): errno=%d\n", errno);
        return NULL;
    }

again:
    r = jsmn_parse(&p, js, jslen, tok, tokcount);
    //    fprintf(stderr, "jsmnParse=%d\n", r);
    if (r < 0) {
        if (r == JSMN_ERROR_NOMEM) {
            tokcount = tokcount * 2;
            tok = realloc_it(tok, sizeof(*tok) * tokcount);
            if (tok == NULL) {
                return NULL;
            }
            goto again;
        }
        return NULL;
    }

    *count = p.toknext;

    return tok;
}

int draw_json_ListFolderAndFiles(char *js, size_t jslen)
{
    int eof_expected = 0;
    int count, status;

    jsmntok_t * tok = prepare_json(js, jslen, &count);

    if (tok != NULL) {
        // dump(js, tok, count, 0, alloca(1024));
        status = read_frcloud_dir(js, tok, count);
        free(tok);
        if (status < 0)
            printf("JSON parser return error: %d\n", status);
        else
            eof_expected = 1;
    }
    return eof_expected;
}

int draw_json_Breadcrumbs(char *js, size_t jslen)
{
    int i, k;
    int eof_expected = 0;
    int count;

    jsmntok_t * tok = prepare_json(js, jslen, &count);
    jsmntok_t * t = tok;

    if (tok != NULL) {
        //        dump(js, tok, count, 0, alloca(1024));

        if (count == 0) {
            puts("Breadcrumbs json zero token count");
        }
        else {
            char name[128];
            char id[32];
            char type[128];

            if (t->type != JSMN_OBJECT || t->size != 1)
                return -10;
            jsmntok_t *key = t + 1;
            if (!json_text_compare(js, key, "breadcrumbs"))
                return -11;
            jsmntok_t *array = key + 1;
            if (array->type != JSMN_ARRAY)
                return -12;
            jsmntok_t *item = array + 1;

            for (i = 0; i < array->size; i++)
            {
                if (item->type != JSMN_OBJECT)
                    return -13;
                item++;
                if (!json_text_compare(js, item++, "id"))
                    return -14;
                get_string(js, item++, id, sizeof(id));
                if (!json_text_compare(js, item++, "name"))
                    return -15;
                get_string(js, item++, name, sizeof(name));

                for (k = 0; k < i; k++) {
                    printf("    ");
                }
                printf("\x1B[33m\x1B[1m%s\x1B[0m (%s)\n", name, id);
            }

            free(tok);
            eof_expected = 1;
        }
    }

    return eof_expected;
}

int json_FileInfo(char *js, size_t jslen, command_context_t * context)
{
    int i, k, status = -3;
    int eof_expected = 0;
    int count;
    char    tmp_buff[1024];

    //    printf("+++ %s\n", js);

    if (context->last_object.name != NULL) {
        printf("Release name: %s\n", context->last_object.name);
        free(0);
    }
    memset(&context->last_object, 0, sizeof(context->last_object));

    jsmntok_t * t = prepare_json(js, jslen, &count);
    if (!t) {
        fprintf(stderr, "Unable parse JSON in json_FileInfo()\n");
        return -13;
    }

    jsmntok_t * rob = t;

    do {

        if (t->type != JSMN_OBJECT) {
            fprintf(stderr, "JSON format error\n");
            status = -14;
            break;
        }
        t++;
        if (json_text_compare(js, t, "format")) {
            t++;
            get_string(js, t++, tmp_buff, sizeof(tmp_buff));
            fprintf(stderr, "TODO: parse format %s\n", tmp_buff);
        }
        if (json_text_compare(js, t, "templateId")) {
            t++;
            get_string(js, t++, tmp_buff, sizeof(tmp_buff));
            fprintf(stderr, "TODO: parse teplateId %s\n", tmp_buff);
        }
        if (json_text_compare(js, t, "reportId")) {
            t++;
            get_string(js, t++, tmp_buff, sizeof(tmp_buff));
            fprintf(stderr, "TODO: parse reportId %s\n", tmp_buff);
        }
        if (json_text_compare(js, t, "reportInfo")) {
            t++;
            get_string(js, t, tmp_buff, sizeof(tmp_buff));
            fprintf(stderr, "TODO: parse reportInfo %s\n", tmp_buff);
            t += (t->size << 1) + 1;
        }

        if (!json_text_compare(js, t++, "name")) {
            fprintf(stderr, "Object's name not present in response\n");
            status =  -15;
            break;
        }
        get_string(js, t++, tmp_buff, sizeof(tmp_buff));
        int len = strlen(tmp_buff);
        if (len == 0)
            fprintf(stderr, "Got emtpy name of report object\n");
        else {
            context->last_object.name = malloc(len + 1);
            if (context->last_object.name == NULL) {
                fprintf(stderr, "Unable allocate memory for name of report object. Lost name: %s\n", tmp_buff);
            }
            else {
                strcpy(context->last_object.name, tmp_buff);
            }
            //        printf("Name: %s\n", context->last_object.name);
        }

        if (json_text_compare(js, t, "parentId")) {
            t++;
            get_string(js, t++, context->last_object.parent, sizeof(context->last_object.parent));
        }

        if (!json_text_compare(js, t++, "type")) {
            fprintf(stderr, "Object's type not present in response\n");
            status = -17;
            break;
        }
        get_string(js, t++, tmp_buff, sizeof(tmp_buff));
        if (strcmp("File", tmp_buff) == 0)
            context->last_object.type = File;
        else if (strcmp("Folder", tmp_buff) == 0)
            context->last_object.type = Folder;
        else {
            fprintf(stderr, "Object type not parsed: %s\n", tmp_buff);
            status = -30;
            break;
        }

        if (json_text_compare(js, t, "size")) {
            t++;
            get_string(js, t++, tmp_buff, sizeof(tmp_buff));

            if (1 != sscanf(tmp_buff, "%u", &context->last_object.size))
                fprintf(stderr, "Size : %s   sz = %d\n", tmp_buff, context->last_object.size);
        }

        if (json_text_compare(js, t, "subscriptionId")) {
            t++;
            get_string(js, t++, context->last_object.subscription, sizeof(context->last_object.subscription));
        }

        if (!json_text_compare(js, t++, "status")) {
            status = -20;
            break;
        }
        get_string(js, t++, context->last_object.status, sizeof(context->last_object.status));

        if (!json_text_compare(js, t++, "statusReason")) {
            status = -21;
            break;
        }
        get_string(js, t++, context->last_object.reason, sizeof(context->last_object.reason));

        if (!json_text_compare(js, t++, "id")) {
            fprintf(stderr, "Object's UUID not present in response\n");
            status = -22;
            break;
        }
        get_string(js, t++, context->last_object.uuid, sizeof(context->last_object.uuid));

        if (json_text_compare(js, t, "createdTime")) {
            t++;
            get_string(js, t++, context->last_object.edited, sizeof(context->last_object.edited));
        }

        if (json_text_compare(js, t, "creatorUserId")) {
            t++;
            get_string(js, t++, context->last_object.creator, sizeof(context->last_object.creator));
        }

        if (json_text_compare(js, t, "editedTime")) {
            t++;
            get_string(js, t++, context->last_object.edited, sizeof(context->last_object.edited));
        }

        if (json_text_compare(js, t, "editorUserId")) {
            t++;
            get_string(js, t++, context->last_object.editor, sizeof(context->last_object.editor));
        }
        status = 0;

    } while (0);

    free(rob);

    return status;
}

int json_SelectFile(char *js, size_t jslen, command_context_t * context)
{
    int status, count;
    char * sub_json;
    int length;
    jsmntok_t * tok = prepare_json(js, jslen, &count);
    jsmntok_t * t = tok;

    do {
        if (t->type != JSMN_OBJECT) {
            status = -6;
            break;
        }
        if (!json_text_compare(js, ++t, "files")) {
            status = -7;
            break;
        }
        ++t;
        if (t->type != JSMN_ARRAY || t->size != 1) {
            status = -8;
            //            printf("TYPE %d  SIZE %d %s\n", t->type, t->size, js);
            break;
        }
        ++t;
        sub_json = js + t->start;
        length = t->end - t->start;

        status = json_FileInfo(sub_json, length, context);

    } while (0);

    free(tok);
    return status;

}
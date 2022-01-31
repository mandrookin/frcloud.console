#define _CRT_SECURE_NO_WARNINGS 1

#include "jsmn.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <unistd.h>

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
    //printf("%.*s", len, js + t->start);
    memcpy(holder, js + t->start, len);
    holder[len] = 0;
    return 1;
  } else if (t->type == JSMN_STRING) {
    //printf("'%.*s'", len, js + t->start);
    memcpy(holder, js + t->start, len);
    holder[len] = 0;
    return 1;
  } else if (t->type == JSMN_OBJECT) {
    printf("\n");
    j = 0;
    for (i = 0; i < t->size; i++) {
      for (k = 0; k < indent; k++) {
        printf("  ");
      }
      key = t + 1 + j;
      j += dump(js, key, count - j, indent + 1, name);
      if (key->size > 0) {
        //printf(": ");
        j += dump(js, t + 1 + j, count - j, indent + 1, value);
      }
      printf("%s: %s\n", name, value);
    }
    return j + 1;
  } else if (t->type == JSMN_ARRAY) {
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

static void get_string(const char *js, jsmntok_t *value, char * str)
{
    int len = value->end - value->start;
    memcpy(str, js + value->start, len);
    str[len] = 0;
}

static int json_text_compare(const char *js, jsmntok_t *t, char * str)
{
    if (t->type != JSMN_STRING)
        return 0;
    int len = t->end - t->start;
    char * p = (char*) js + t->start;
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

typedef enum { Unknown, File, Folder } dirtype_t;
typedef struct frc_direntry {
    struct frc_direntry * next;
    int size;
    int attrib;
    char name[128];
    dirtype_t type;
    char id[32];
    char create[32];
    char edited[32];
} directory_record_t;

static int read_frcloud_dir(const char *js, jsmntok_t *t, size_t count, int indent, char * holder)
{
    int i;

    if (count == 0) {
        puts("json node zero token count");
        return 0;
    }
    if (t->type != JSMN_OBJECT || t->size != 4)
        return -10;
    jsmntok_t *key = t + 1;
    if ( ! json_text_compare(js, key, "files") )
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
        get_string(js, item++, dir.name);
        if (!json_text_compare(js, item++, "type"))
            return -15;
        get_string(js, item++, type);

        dir.type = 0;
        if (strcmp(type, "Folder") == 0)
            dir.type = 2;
        else if (strcmp(type, "File") == 0)
            dir.type = 1;

        if (!json_text_compare(js, item++, "size"))
            return -16;
        get_string(js, item++, type);
        dir.size = atoi(type);
        if (!json_text_compare(js, item++, "status"))
            return -16;
        get_string(js, item++, type);
        if (!json_text_compare(js, item++, "statusReason"))
            return -16;
        get_string(js, item++, type);
        if (!json_text_compare(js, item++, "id"))
            return -14;
        get_string(js, item++, dir.id);
        if (!json_text_compare(js, item++, "createdTime"))
            return -14;
        get_string(js, item++, dir.create);
        dir.create[10] = ' ';
        dir.create[19] = 0;
        if (!json_text_compare(js, item++, "editedTime"))
            return -14;
        get_string(js, item++, dir.edited);
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

    return 0;
}

int draw_json_response(char *js, size_t jslen)
{
  int r;
  int eof_expected = 0;
//  char buf[BUFSIZ];

  jsmn_parser p;
  jsmntok_t *tok;
  size_t tokcount = 2;

  /* Prepare parser */
  jsmn_init(&p);

  /* Allocate some tokens as a start */
  tok = malloc(sizeof(*tok) * tokcount);
  if (tok == NULL) {
    fprintf(stderr, "malloc(): errno=%d\n", errno);
    return 3;
  }

  again:
    r = jsmn_parse(&p, js, jslen, tok, tokcount);
//    fprintf(stderr, "jsmnParse=%d\n", r);
    if (r < 0) {
      if (r == JSMN_ERROR_NOMEM) {
        tokcount = tokcount * 2;
        tok = realloc_it(tok, sizeof(*tok) * tokcount);
        if (tok == NULL) {
          return 3;
        }
        goto again;
      }
    } else {
        char buff[1024];
//      dump(js, tok, p.toknext, 0, buff);
      read_frcloud_dir(js, tok, p.toknext, 0, buff);
      eof_expected = 1;
    }
    return eof_expected;
}

static int original_main() {
  int r;
  int eof_expected = 0;
  char *js = NULL;
  size_t jslen = 0;
  char buf[BUFSIZ];

  jsmn_parser p;
  jsmntok_t *tok;
  size_t tokcount = 2;

  /* Prepare parser */
  jsmn_init(&p);

  /* Allocate some tokens as a start */
  tok = malloc(sizeof(*tok) * tokcount);
  if (tok == NULL) {
    fprintf(stderr, "malloc(): errno=%d\n", errno);
    return 3;
  }

  for (;;) {
    /* Read another chunk */
    r = fread(buf, 1, sizeof(buf), stdin);
    if (r < 0) {
      fprintf(stderr, "fread(): %d, errno=%d\n", r, errno);
      return 1;
    }
    if (r == 0) {
      if (eof_expected != 0) {
        return 0;
      } else {
        fprintf(stderr, "fread(): unexpected EOF\n");
        return 2;
      }
    }

    js = realloc_it(js, jslen + r + 1);
    if (js == NULL) {
      return 3;
    }
    strncpy(js + jslen, buf, r);
    jslen = jslen + r;

  again:
    r = jsmn_parse(&p, js, jslen, tok, tokcount);
    if (r < 0) {
      if (r == JSMN_ERROR_NOMEM) {
        tokcount = tokcount * 2;
        tok = realloc_it(tok, sizeof(*tok) * tokcount);
        if (tok == NULL) {
          return 3;
        }
        goto again;
      }
    } else {
        char buff[1024];
      dump(js, tok, p.toknext, 0, buff);
      eof_expected = 1;
    }
  }

  return EXIT_SUCCESS;
}

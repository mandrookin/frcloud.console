#ifndef _FRCLOUD_H_
#define _FRCLOUD_H_

#include <curl/curl.h>


// Peplace with suitable key file name
#define KEY_FILE            "FastReport.Cloud.key"
// FastReport cloud server address
#define DEFAULT_SERVER      "https://fastreport.cloud"

typedef unsigned long uint64_t;

typedef struct {
    char        remote_fname[4096];
    void    *   pointer_to_curl;
//    char        url[4096];
//    FILE        *stream;
//    FILE        *dbg_stream;
//    uint64_t    file_sz;
} dnld_params_t;

typedef enum { Templates, Reports, Exports} domain_t;

typedef struct {
    CURL    *   curl;
    char    *   command;
    char    *   words[8];
    int         words_count;
} command_context_t;

typedef void(*cloud_command_t)(command_context_t *);

typedef struct {
    char const * const  command_name;
    cloud_command_t     run;
    char const * const  short_help;
    char const * const  long_help;
} command_record_t;

int load_token(char * tokenfilename, char * auth);

#endif
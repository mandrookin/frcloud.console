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
    void    *   curl;
} dnld_params_t;

typedef struct json_chunk_header {
    struct json_chunk_header    *   next_chunk;
    int                             size;
} json_chunk_header_t;

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

int draw_json_ListFolderAndFiles(char *js, size_t jslen);
int draw_json_Breadcrumbs(char *js, size_t jslen);


char *base64_encode(const unsigned char *data,
    size_t input_length,
    size_t *output_length);

unsigned char *base64_decode(const char *data,
    size_t input_length,
    size_t *output_length);

void build_decoding_table();
void base64_cleanup();

#endif

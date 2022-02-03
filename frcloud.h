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

typedef enum { Templates, Reports, Exports} namespace_t;

#define ID_BUFF_SIZE  26 // 24 characters and two bytes pad

typedef struct {
    CURL    *   curl;
    namespace_t    session_namespace;;
    char    *   command;
    char    *   words[8];
    int         words_count;
    int         take_count;
    uint                         received_json_size;
    json_chunk_header_t    *     json_chunks_head;
    json_chunk_header_t    *     json_chunks_tail;
    char    reports_root_folder[ID_BUFF_SIZE];
    char    reports_current_folder[ID_BUFF_SIZE];
    char    templates_root_folder[ID_BUFF_SIZE];
    char    templates_current_folder[ID_BUFF_SIZE];
    char    exports_root_folder[ID_BUFF_SIZE];
    char    exports_current_folder[ID_BUFF_SIZE];
    char    active_object_uuid[ID_BUFF_SIZE];
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
int json_ReportInfo(char *js, size_t jslen, command_context_t * context);


char *base64_encode(const unsigned char *data,
    size_t input_length,
    size_t *output_length);

unsigned char *base64_decode(const char *data,
    size_t input_length,
    size_t *output_length);

void build_decoding_table();
void base64_cleanup();

#endif

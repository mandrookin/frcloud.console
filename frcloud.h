#ifndef _FRCLOUD_H_
#define _FRCLOUD_H_

#include <curl/curl.h>


// Peplace with suitable key file name
#define KEY_FILE            "FastReport.Cloud.key"
// FastReport cloud server address
#define DEFAULT_SERVER      "https://fastreport.cloud"
#define ID_SIZE 24

// Undefine this to disable storing history to file
#define HISTORY_FILE    ".frcloud.history"

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
typedef enum { Unknown, File, Folder } object_type_t;
typedef enum { Fpx, Pdf, Html, Docx} export_type_t;

#define ID_BUFF_SIZE  26 // 24 characters and two bytes pad

typedef struct {
    object_type_t   type;
    char           *name;
    char            uuid[ID_BUFF_SIZE];
    char            template[ID_BUFF_SIZE];
    char            parent[ID_BUFF_SIZE];
    char            subscription[ID_BUFF_SIZE];
    uint            size;
    char            status[32];
    char            reason[32];
    char            created[32];
    char            edited[32];
    char            creator[64];
    char            editor[64];
} report_info_t;

typedef struct {
    CURL    *   curl;
    int             verbose;
    namespace_t     session_namespace;;
    report_info_t   last_object;
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
int json_SelectFile(char *js, size_t jslen, command_context_t * context);
int json_FileInfo(char *js, size_t jslen, command_context_t * context);

char *base64_encode(const unsigned char *data,
    size_t input_length,
    size_t *output_length);

unsigned char *base64_decode(const char *data,
    size_t input_length,
    size_t *output_length);

void build_decoding_table();
void base64_cleanup();

#endif

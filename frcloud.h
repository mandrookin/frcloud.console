#ifndef _FRCLOUD_H_
#define _FRCLOUD_H_

// Pplace for key file
#define KEY_FILE    "FastReport.Cloud.key"

typedef unsigned long uint64_t;

typedef struct {
    char        remote_fname[4096];
    void    *   pointer_to_curl;
    
//    char        url[4096];
//    FILE        *stream;
//    FILE        *dbg_stream;
    uint64_t    file_sz;
} dnld_params_t;

typedef enum { Templates, Reports, Exports} domain_t;

int load_token(char * tokenfilename, char * auth);

#endif
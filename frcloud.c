#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <readline/readline.h>
#include <readline/history.h>

#define STDOUT 1

#include "frcloud.h"

static int get_oname_from_cd(char const*const cd, char *oname)
{
    char    const*const cdtag = "Content-disposition:";
    char    const*const key = "filename=";
    char    const*const unikey = "filename*=UTF-8''";
    int     ret = 0;
    char    *val = NULL;
    char    ch;


    /* If filename is unicode utf-8 string */
    val = strcasestr(cd, unikey);
    if (val) {
        enum { ASCII, FirstNibble, SecondNibble } state = ASCII;
        char * str = oname;
        val += strlen(unikey);
        while (*val != '\0' && *val != ';' && *val >= ' ') {
            switch (state)
            {
            case ASCII:
                if (*val == '%') {
                    val++;
                    state = FirstNibble;
                    continue;
                }
                if (*val == '!') { // Are you Ok?
                    val++;
                    *str++ = '_';
                    continue;
                }
                if (*val == '?') { // Really?
                    val++;
                    *str++ = '-';
                    continue;
                }
                *str++ = *val++;
                continue;
            case FirstNibble:
                if (isdigit(*val)) ch = (*val - '0');
                else if ((*val >= 'A' && *val <= 'F')) ch = 0xa + (*val - 'A');
                else
                {
                    fprintf(stderr, "%s header fornat error\n", unikey);
                    return -2;
                }
                ch <<= 4;
                state = SecondNibble;
                val++;
                continue;
            case SecondNibble:
                if (isdigit(*val)) ch |= (*val - '0');
                else if ((*val >= 'A' && *val <= 'F')) ch |= 0xa + (*val - 'A');
                else
                {
                    fprintf(stderr, "%s header fornat error\n", unikey);
                    return -2;
                }
                state = ASCII;
                *str++ = ch;
                val++;
                continue;
            }
        }
        *str++ = '\0';
        //        puts(oname);
        return ret;
    }

    /* Example Content-Disposition: filename=name1367; charset=funny; option=strange */

    /* If filename is present */
    val = strcasestr(cd, key);
    if (!val) {
        printf("No key-value for \"%s\" in \"%s\"", key, cdtag);
        ret = -1;
        goto bail;
    }

    /* Move to value */
    val += strlen(key);

    while (*val != '\0' && *val != ';') {
        /* FR stuff */
        switch (*val)
        {
        case '!':
        case '?':
            *val = '_';
            break;
        }
        //printf (".... %c\n", *val);
        *oname++ = *val++;
    }
    *oname = '\0';

bail:
    return ret;
}

static const char const * const modes[3] = {
    "\n\x1B[32m\x1B[1mTemplates> \x1B[0m",
    "\n\x1B[32m\x1B[1mReports> \x1B[0m",
    "\n\x1B[32m\x1B[1mExports> \x1B[0m"
};

static int  verbose = 0;
static int stop;
static int file_body;
static dnld_params_t dnld_params;
static int   download_size;

static char * GetCurrentFolder(command_context_t * context)
{
    switch (context->session_namespace)
    {
    case Templates: return context->templates_current_folder;
    case Reports: return context->reports_current_folder;
    case Exports: return context->exports_current_folder;
    }
    return context->templates_current_folder;
}

static char * GetRootFolder(command_context_t * context)
{
    switch (context->session_namespace)
    {
    case Templates: return context->templates_root_folder;
    case Reports: return context->reports_root_folder;
    case Exports: return context->exports_root_folder;
    }
    return context->templates_root_folder;
}

static char * GetDomainMode(command_context_t * context)
{
    char    *   domain_mode;

    switch (context->session_namespace)
    {
    case Reports:   domain_mode = "Reports"; break;
    case Exports:   domain_mode = "Exports"; break;
    case Templates:
    default:
        domain_mode = "Templates";
    }
    return domain_mode;
}

size_t dnld_header_parse(void *hdr, size_t size, size_t nmemb, void *userdata)
{
    const   size_t  cb = size * nmemb;
    const   char    *hdr_str = hdr;
    dnld_params_t *dnld_params = (dnld_params_t*)userdata;
    char const*const cdtag = "Content-disposition:";

    int http_code;
    curl_easy_getinfo(dnld_params->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200)
    {
        fprintf(stderr, "Request error code: %d\n", http_code);
        return -1;
    }

    /* Example:
     * ...
     * Content-Type: text/html
     * Content-Disposition: filename=name1367; charset=funny; option=strange
     */

    if (strstr(hdr_str, "Content-disposition:")) {
        fprintf(stderr, "has c-d: %s\n", hdr_str);
    }

    if (!strncasecmp(hdr_str, cdtag, strlen(cdtag))) {
        printf("Found c-d: %s\n", hdr_str);
        int ret = get_oname_from_cd(hdr_str + strlen(cdtag), dnld_params->remote_fname);
        if (ret) {
            fprintf(stderr, "ERR: bad remote name");
            return 0;
        }
        file_body = open(dnld_params->remote_fname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    }

    return cb;
}

char * readline_gets(const char * prompt)
{
    static char *line_read = (char *)NULL;
    char  * line_ptr;

    if (line_read)
    {
        free(line_read);
        line_read = (char *)NULL;
    }

    line_ptr = line_read = readline(prompt);

    if (line_ptr)
    {
        while (isspace(*line_ptr))
            line_ptr++;
        if (*line_ptr)
            add_history(line_ptr);
    }

    return (line_ptr);
}

static uint write_json_chunk(char *in, uint size, uint nmemb, char *out)
{
    uint r = size * nmemb;
    command_context_t * context = (command_context_t*)out;

#if PAYLOAD_DEBUG
    FILE * fp = fopen("folder.json", "w+");
    fwrite(in, size, nmemb, fp);
    fclose(fp);
#endif

    json_chunk_header_t    *    chunk = malloc(r + sizeof(json_chunk_header_t));
    if (chunk == NULL) {
        fprintf(stderr, "Unable allocate memory for json chunk\n");
        return 0;
    }

    chunk->next_chunk = NULL;
    chunk->size = r;
    memcpy(chunk + 1, in, r);
    context->received_json_size += r;

    if (context->json_chunks_head == NULL)
        context->json_chunks_head = chunk;
    else
        context->json_chunks_tail->next_chunk = chunk;
    context->json_chunks_tail = chunk;

    //    printf("Receive chunk %d bytes. Total %d bytes\n", r, context->received_json_size);
    return r;
}

static void describe_object(command_context_t * context)
{
    CURLcode res;
    char request[512];

    snprintf(request, sizeof(request), "%s/api/rp/v1/%s/File/%s",
        DEFAULT_SERVER,
        GetDomainMode(context),
        context->words[0]);

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, write_json_chunk);

    context->received_json_size = 0;
    context->json_chunks_head = NULL;
    context->json_chunks_tail = NULL;

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, context);

    res = curl_easy_perform(context->curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    if (context->received_json_size > 0)
    {
        char * json_stream = alloca(context->received_json_size);
        char * ptr = json_stream;
        int check_counter = context->received_json_size;

        while (context->json_chunks_head) {
            memcpy(ptr, context->json_chunks_head + 1, context->json_chunks_head->size);
            ptr += context->json_chunks_head->size;
            check_counter -= context->json_chunks_head->size;
            //            printf("Append chunk %d bytes\n", json_chunks_head->size);
            context->json_chunks_tail = context->json_chunks_head->next_chunk;
            free(context->json_chunks_head);
            context->json_chunks_head = context->json_chunks_tail;
        }
#if !DEBUG_DIRECTORY_JSON
        json_stream[context->received_json_size] = 0;
        puts(json_stream);
#endif
//        json_ReportInfo(json_stream, context->received_json_size, context);
    }
    else {
        fprintf(stderr, "Unable receive file '%s' properties\n", context->words[0]);
    }
}

static void prepare_report(command_context_t * context)
{
    CURLcode    res;
    char        request[512];

    if (context->words_count != 1) {
        puts("Not enough arguments. Use:\n> prepare 60758ec7377eaa000171a5ec\nwhere '60758ec7377eaa000171a5ec' is uuid of template");
        return;
    }

    snprintf(request, 512, "%s/api/rp/v1/Templates/File/%s/Prepare",
        DEFAULT_SERVER,
        context->words[0]);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json-patch+json");
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, NULL);

#define PREPARE_JSON_MAX_SIZE   1024

    char * post = alloca(PREPARE_JSON_MAX_SIZE);
    snprintf(post, PREPARE_JSON_MAX_SIZE,
        "{ \"name\": \"alman_prepared_report.fpx\", \"parentFolderId\": \"%s\"}",
        context->reports_current_folder);

    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, post);

    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, write_json_chunk);

    context->received_json_size = 0;
    context->json_chunks_head = NULL;
    context->json_chunks_tail = NULL;

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, context);

    res = curl_easy_perform(context->curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POST, 0);

    if (context->received_json_size > 0)
    {
        char * json_stream = alloca(context->received_json_size + 1);
        char * ptr = json_stream;
        int check_counter = context->received_json_size;

        while (context->json_chunks_head) {
            memcpy(ptr, context->json_chunks_head + 1, context->json_chunks_head->size);
            ptr += context->json_chunks_head->size;
            check_counter -= context->json_chunks_head->size;
            //            printf("Append chunk %d bytes\n", json_chunks_head->size);
            context->json_chunks_tail = context->json_chunks_head->next_chunk;
            free(context->json_chunks_head);
            context->json_chunks_head = context->json_chunks_tail;
        }
        json_ReportInfo(json_stream, context->received_json_size, context);
    }
    else
        fprintf(stderr, "Empty response on preparing report\n");
}

void show_directory(command_context_t * context)
{
    CURLcode res;
    char    *   dir_uuid;
    char        search_pattern[128];
    char        request[512];

    search_pattern[0] = '\0';
    if (context->words_count == 0) {
        snprintf(search_pattern, sizeof(search_pattern), "?skip=0&take=%u",
            context->take_count);
        dir_uuid = GetCurrentFolder(context);
    }
    else if (context->words_count == 1) {
        if (strcmp(context->command, "ls") == 0) {
            dir_uuid = context->words[0];
        }
        else if (strcmp(context->command, "search") == 0) {
            dir_uuid = GetCurrentFolder(context);
            snprintf(search_pattern, sizeof(search_pattern), "?skip=0&take=%u&searchPattern=%s",
                context->take_count,
                context->words[0]);
        }
        else {
            fprintf(stderr, "Unknown command extension: %s\n", context->command);
            return;
        }
    }
    else {
        fprintf(stderr, "change dir command supports only one or zero arguments\n");
        return;
    }

    snprintf(request, 512, "%s/api/rp/v1/%s/Folder/%s/ListFolderAndFiles%s",
        DEFAULT_SERVER,
        GetDomainMode(context),
        dir_uuid,
        search_pattern);

    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, write_json_chunk);

    context->received_json_size = 0;
    context->json_chunks_head = NULL;
    context->json_chunks_tail = NULL;

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, context);

    res = curl_easy_perform(context->curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    if (context->received_json_size > 0)
    {
        char * json_stream = alloca(context->received_json_size);
        char * ptr = json_stream;
        int check_counter = context->received_json_size;

        while (context->json_chunks_head) {
            memcpy(ptr, context->json_chunks_head + 1, context->json_chunks_head->size);
            ptr += context->json_chunks_head->size;
            check_counter -= context->json_chunks_head->size;
            //            printf("Append chunk %d bytes\n", json_chunks_head->size);
            context->json_chunks_tail = context->json_chunks_head->next_chunk;
            free(context->json_chunks_head);
            context->json_chunks_head = context->json_chunks_tail;
        }
#if DEBUG_DIRECTORY_JSON
        json_stream[context->received_json_size] = 0;
        puts(json_stream);
#endif
        draw_json_ListFolderAndFiles(json_stream, context->received_json_size);
    }
    else {
        printf("Folder '%s' is empty\n", dir_uuid);
    }
}

/* curl write callback, to fill tidy's input buffer...  */
static uint write_cb(char *in, uint size, uint nmemb, char *out)
{
    uint r = size * nmemb;

    if (file_body > 0)
    {
        int l = write(file_body, in, r);
        if (l < 0) {
            printf("written %d bytes errno %d\n", l, errno);
            r = 0;
        }
        else
            download_size += l;
    }
    else
    {
        printf("Unable open file '%s' errno %d\n", dnld_params.remote_fname, errno);
    }
    //    write(STDOUT, in, r);
    return r;
}

static void download_file(command_context_t * context)
{
    CURLcode res;
    char * uuid = NULL;
    char request[512];
    char op;

    if (context->words_count == 0) {
        if (context->active_object_uuid[0] == 0) {
            puts("Active object is not available yet");
            return;
        }
        // Take UUID from resuilt of previous command
        printf("UUID: %s\n", context->active_object_uuid);
        uuid = context->active_object_uuid;
    }
    else // requires switch session_namespace
        uuid = context->words[0];
    
    switch (context->session_namespace) {
    case Templates:
        op = 't';
        break;
    case Reports:
        op = 'r';
        break;
    case Exports:
        op = 'e';
        break;
    }
    snprintf(request, 512, "%s/download/%c/%s", DEFAULT_SERVER, op, uuid);

    // 
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: text/html, application/xhtml+xml, application/xml, application/octet-stream");
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(context->curl, CURLOPT_HEADERFUNCTION, dnld_header_parse);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, write_cb);
    dnld_params.curl = context->curl;
    curl_easy_setopt(context->curl, CURLOPT_HEADERDATA, &dnld_params);
    curl_easy_setopt(context->curl, CURLOPT_URL, request);

    file_body = -1;
    download_size = 0;
    res = curl_easy_perform(context->curl);
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, NULL);

    if (file_body > 0)
    {
        close(file_body);
        printf("Load %d bytes of '%s'\n", download_size, dnld_params.remote_fname);
        file_body = -1;
    }
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

static void upload_file(command_context_t * context)
{
    CURLcode    res;
    char    *   filename;
    char        request[512];

    if (context->words_count != 1) {
        printf("Use> put filename\n  where 'filename' is path to local file\n");
        return;
    }
    filename = context->words[0];
    if (access(filename, R_OK) == -1) {
        fprintf(stderr, "File not exist or access denied for: %s", filename);
        return;
    }

    size_t encoded_size;

    int source_file = open(filename, O_RDONLY);
    if (source_file < 0) {
        fprintf(stderr, "Unable open source file: %s", filename);
        return;
    }
    int source_len = lseek(source_file, 0, SEEK_END);
    lseek(source_file, 0, SEEK_SET);
    if (source_len > 30 * 1024)
    {
        printf("Source file size exceeds 30Kb size limmit\n");
    }

    char * input = alloca(source_len);
    if (input == NULL)
    {
        fprintf(stderr, "Unable allocate memory on stack\n");
        return;
    }
    int source_size = read(source_file, input, source_len);
    close(source_file);
    if (source_size < 0) {
        fprintf(stderr, "Unable read source file: %s", filename);
        return;
    }
    if (source_size != source_len) {
        fprintf(stderr, "Unable read (%d != %d) source file: %s", source_size, source_len, filename);
        return;
    }

    char * content = base64_encode(input, source_size, &encoded_size);

#define REQUEST_BUFF_SIZE (encoded_size + 192)

    char * post = alloca(REQUEST_BUFF_SIZE);
    snprintf(post, REQUEST_BUFF_SIZE, "{ \"name\": \"%s\", \"content\": \"%s\"}",
        filename,
        content);
    free(content);

    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, post);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json-patch+json");
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, headers);

    snprintf(request, 512, "%s/api/rp/v1/%s/Folder/%s/File",
        DEFAULT_SERVER,
        GetDomainMode(context),
        GetCurrentFolder(context));

    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, write_json_chunk);

    context->received_json_size = 0;
    context->json_chunks_head = NULL;
    context->json_chunks_tail = NULL;

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, context);

    res = curl_easy_perform(context->curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POST, 0);

    if (context->received_json_size > 0)
    {
        char * json_stream = alloca(context->received_json_size);
        char * ptr = json_stream;
        int check_counter = context->received_json_size;

        while (context->json_chunks_head) {
            memcpy(ptr, context->json_chunks_head + 1, context->json_chunks_head->size);
            ptr += context->json_chunks_head->size;
            check_counter -= context->json_chunks_head->size;
            //            printf("Append chunk %d bytes\n", json_chunks_head->size);
            context->json_chunks_tail = context->json_chunks_head->next_chunk;
            free(context->json_chunks_head);
            context->json_chunks_head = context->json_chunks_tail;
        }

        json_ReportInfo(json_stream, context->received_json_size, context);
    }
}

static void delete_remote_object(command_context_t * context)
{
    CURLcode res;
    char * object_type;
    char request[512];

    if (context->words_count != 1) {
        puts("Use rm/rmdir 'uuid' to delete file/directory. Where 'uuid' is unique identifier of file");
        return;
    }

    if (strcmp(context->command, "rm") == 0)
        object_type = "File";
    else if (strcmp(context->command, "rmdir") == 0)
        object_type = "Folder";
    else {
        fprintf(stderr, "Unknown delete command: %s\n", context->command);
        return;
    }

    snprintf(request, 512, "%s/api/rp/v1/%s/%s/%s",
        DEFAULT_SERVER,
        GetDomainMode(context),
        object_type,
        context->words[0]);

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(context->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(context->curl, CURLOPT_URL, request);

    res = curl_easy_perform(context->curl);

    curl_easy_setopt(context->curl, CURLOPT_CUSTOMREQUEST, NULL);

    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

static void create_folder(command_context_t * context)
{
#define CREATE_BUFF_SIZE 4096
    CURLcode res;
    char request[512];

    if (context->words_count != 1) {
        printf("Use> mkdir name\n  where 'name' is a folder name\n");
        return;
    }

    char * filename = context->words[0];
    snprintf(request, sizeof(request), "%s/api/rp/v1/%s/Folder/%s/Folder",
        DEFAULT_SERVER,
        GetDomainMode(context),
        GetCurrentFolder(context));

    char * post = alloca(CREATE_BUFF_SIZE);
    snprintf(post, CREATE_BUFF_SIZE,
        "{ \"name\": \"%s\", \"tags\" : [ \"%s\" ], \"icon\" : \"%s\" }",
        filename, "console", "help");

    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, post);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json-patch+json");
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    //////////curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, context);

    res = curl_easy_perform(context->curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POST, 0);
}

static void show_information(command_context_t * context)
{
    CURLcode res;
    puts("----------------------- SERVER CONFIGURATION ----------------------");

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, NULL);

    curl_easy_setopt(context->curl, CURLOPT_URL, DEFAULT_SERVER "/api/v1/Configuration");

    res = curl_easy_perform(context->curl);
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    puts("\n----------------------- SHOW PROFILE ----------------------");
    curl_easy_setopt(context->curl, CURLOPT_URL, DEFAULT_SERVER "/api/manage/v1/UserProfile");

    res = curl_easy_perform(context->curl);
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    puts("\n----------------------- USER SETTINGS  ----------------------");
    curl_easy_setopt(context->curl, CURLOPT_URL, DEFAULT_SERVER "/api/manage/v1/UserSettings");

    res = curl_easy_perform(context->curl);
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

static uint parse_working_directory_json(char *in, uint size, uint nmemb, char *out)
{
    uint r = size * nmemb;
#if PAYLOAD_DEBUG
    FILE * fp = fopen("pwd.json", "w");
    fwrite(in, size, nmemb, fp);
    fclose(fp);
#endif
    draw_json_Breadcrumbs(in, r);
    return r;
}

static void show_working_dicrectory_path(command_context_t * context)
{
    CURLcode res;
    char request[512];

    snprintf(request, sizeof(request), "%s/api/rp/v1/%s/Folder/%s/Breadcrumbs",
        DEFAULT_SERVER,
        GetDomainMode(context),
        GetCurrentFolder(context));

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, parse_working_directory_json);
    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    res = curl_easy_perform(context->curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, NULL);
}

static void change_directory(command_context_t * context)
{
    if (context->words_count == 0)
    {
        puts("Current dir changed to root folder. Use folder 'uuid' as an argument for cd command to change current folder");
        strcpy(GetCurrentFolder(context), GetRootFolder(context));
        return;
    }
    if (strlen(context->words[0]) != strlen("606335ef377eaa000171a5ba"))
    {
        puts("This version supports UUID only as argument. Or no argument to change current folder to session_namespace's root");
        return;
    }
    strcpy(GetCurrentFolder(context), context->words[0]);
    printf("Directory changed to: %s", GetCurrentFolder(context));
}

static void logout_cloud(command_context_t * context)
{
    stop = 1;
}
static void local_dir_list(command_context_t * context)
{
    system("ls -l");
}
static void switch_verbosity(command_context_t * context)
{
    verbose = verbose ? 0 : 1;
    curl_easy_setopt(context->curl, CURLOPT_VERBOSE, verbose);
    printf("curl verbose mode set to %s", verbose ? "Enabled" : "Disabled");
}
static void list_screen_limit(command_context_t * context)
{
    int value;
    switch (context->words_count)
    {
    case 0:
        printf("Will request %d direcory items", context->take_count);
        break;
    case 1:
        if ((sscanf(context->words[0], "%d", &value) != 1) || value < 5 || value > 120)
            fprintf(stderr, "Value %d out of range 5..120", value);
        else
            context->take_count = value;
        break;
    default:
        fprintf(stderr, "Command 'limit' supports one or zero arguments\n");
        break;
    }
}

static void select_namespace(command_context_t * context);
static void help(command_context_t * context);

#include "help_rus.h"

command_record_t    commands[] = {
    {"help",    help, "shows list of supported commands or command description", HELP_HELP},
    {"prepare", prepare_report, "prepare report by it's UUID", NULL},
    {"ls",      show_directory, "show directory context", HELP_LS},
    {"search",  show_directory, "show directory context by mask", NULL},
    {"cd",      change_directory, "change current directory by it's UUID", NULL},
    {"get",     download_file, "download template, report or document by it's UUID", NULL},
    {"put",     upload_file, "upload template, report or document to cloud", NULL},
    {"pwd",     show_working_dicrectory_path, "print working directory path", NULL},
    {"exit",    logout_cloud, "exit from FastReport.Cloud console. See help", HELP_EXIT},
    {"templates", select_namespace, "switch to templates namespace, can be used as prefix", HELP_NAMESPSACES},
    {"reports", select_namespace, "switch to reports namespace, can be used as prefix", HELP_NAMESPSACES},
    {"exports", select_namespace, "switch to exports namespace, can be used as prefix", HELP_NAMESPSACES},
    {"info",    show_information, "show various info.", NULL},
    {"lls",     local_dir_list, "list of local directory", NULL},
    {"rm",      delete_remote_object, "delete file by it's UUID", NULL},
    {"mkdir",   create_folder, "creaate folder", NULL},
    {"rmdir",   delete_remote_object, "delete non-empty folder by it's UUID", NULL },
    {"verbose", switch_verbosity, "toggle curl verbose mode ON/OFF", NULL},
    {"limit",   list_screen_limit, "show/set max count of items of 'ls' and 'search' commands", NULL},
    {"desc",    describe_object, "show object properties by it's UUID ", NULL},
    {NULL, NULL, NULL, NULL}
};

static void help(command_context_t * context)
{
    command_record_t   * ptr = commands;
    if (context->words_count == 1) {
        while (ptr->command_name != NULL) {
            if (strcmp(ptr->command_name, context->words[0]) != 0) {
                ptr++;
                continue;
            }
            if (ptr->long_help)
                puts(ptr->long_help);
            else
                printf("Help not found.\n%s - %s", ptr->command_name, ptr->short_help);
            break;
        }
    }
    else {
        printf("List of supported commands:");
        while (ptr->command_name != NULL) {

            printf("\n\x1B[33m\x1B[1m %-10s\x1B[0m    %s", ptr->command_name, ptr->short_help);
            ptr++;
        }
    }
}

static void select_namespace(command_context_t * context)
{
    command_record_t    *   cmd_ptr;

    if (strcmp(context->command, "templates") == 0)
        context->session_namespace = Templates;
    else if (strcmp(context->command, "reports") == 0)
        context->session_namespace = Reports;
    else if (strcmp(context->command, "exports") == 0)
        context->session_namespace = Exports;
    else {
        fprintf(stderr, "Namespace %s not defined\n", context->command);
        return;
    }

    if (context->words_count == 1) {
        context->command = context->words[0];
        char * ptr = strpbrk(context->words[0], " \t");
        if (ptr != NULL) {
            *ptr++ = 0;
            while (isspace(*ptr))
                ptr++;
            if (*ptr != 0) {
                context->words[0] = ptr;
            }
        }
        else {
            context->words[0] = 0;
            context->words_count = 0;
        }
        for (cmd_ptr = commands; cmd_ptr->command_name != NULL; cmd_ptr++)
        {
            if (strcmp(context->command, cmd_ptr->command_name) != 0)
                continue;
            cmd_ptr->run(context);
            break;
        }
    }
}

static void user_interface(command_context_t * context)
{
    command_record_t    *   cmd_ptr;
    char                *   ptr;

    puts("Welcome to \x1B[36m\x1B[1mFastReport.Cloud\x1B[0m shell. Type 'help' to see list of builtin commands.");
    stop = 0;
    context->take_count = 16;
    do {
        context->words_count = 0;
        context->command = readline_gets(modes[context->session_namespace]);
        if (context->command == NULL) {
            stop = 1;
            puts("");
            continue;
        }
        ptr = strpbrk(context->command, " \t");
        if (ptr != NULL) {
            *ptr++ = 0;
            while (isspace(*ptr))
                ptr++;
            if (*ptr != 0) {
                context->words[context->words_count] = ptr;
                context->words_count++;
            }
        }
        if (context->command[0] == 0)
            continue;

        for (cmd_ptr = commands; cmd_ptr->command_name != NULL; cmd_ptr++)
        {
            if (strcmp(context->command, cmd_ptr->command_name) != 0)
                continue;
            cmd_ptr->run(context);
            break;
        }
        if (cmd_ptr->command_name != NULL)
            continue;
        printf("Unknown command: '%s' Type 'help' and press <Enter> to see list of built-in commands", context->command);
    } while (!stop);
}

uint init_cb(char *in, uint size, uint nmemb, char *out)
{
    uint r = size * nmemb;
    char * end = NULL;
    command_context_t * context = (command_context_t*)out;
    char * ptr = strstr(in, "\"id\":\"");  // "id":"

    if (ptr == NULL) {
        fprintf(stderr, "Siggnature not found");
    }
    else {
        ptr += 6;
        end = strchr(ptr, '"');
        if (!end) {
            fprintf(stderr, "Signature not terminated");
        }
    }

    if (!ptr || !end || (end - ptr != 24)) {
        fprintf(stderr, "Unable connect to cloud server: signature error\n");
        exit(-50);
    }
    *end = 0;
    switch (context->session_namespace)
    {
    case Templates:
        strcpy(context->templates_root_folder, ptr);
        strcpy(context->templates_current_folder, ptr);
        break;
    case Reports:
        strcpy(context->reports_root_folder, ptr);
        strcpy(context->reports_current_folder, ptr);
        break;
    case Exports:
        strcpy(context->exports_root_folder, ptr);
        strcpy(context->exports_current_folder, ptr);
        break;
    }
    return r;
}

void user_init(command_context_t * context, char * auth)
{
    CURLcode res;
    int i;
    const namespace_t dlist[3] = { Templates, Reports, Exports };
    char request[512];

    curl_easy_setopt(context->curl, CURLOPT_USERPWD, auth);
    curl_easy_setopt(context->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(context->curl, CURLOPT_USERAGENT, "FastReport.Cloud/0.2 (Linux) libcurl");
    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, init_cb);
    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, context);

    for(i=0; i<3; i++)
    { 
        context->session_namespace = dlist[i];

        snprintf(request, sizeof(request), "%s/api/rp/v1/%s/Root",
            DEFAULT_SERVER,
            GetDomainMode(context));

        curl_easy_setopt(context->curl, CURLOPT_URL, request);
        res = curl_easy_perform(context->curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    }
    context->session_namespace = Templates;
}

int main(void)
{
    CURLcode                res;
    command_context_t       context;
    char                    auth[256];

    memset(&context, 0, sizeof(command_context_t));

    strcpy(auth, "apikey:");
    if (load_token(KEY_FILE, auth) < 0) {
        fprintf(stderr, "Unable set security token\n");
        return EXIT_FAILURE;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    context.curl = curl_easy_init();
    if (context.curl) {
        user_init(&context, auth);
        memset(auth, 0, sizeof(auth));
        user_interface(&context);
        curl_easy_cleanup(context.curl);
    }
    curl_global_cleanup();
    return 0;
}

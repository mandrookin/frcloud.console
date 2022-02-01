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

#define ID_BUFF_SIZE  26

static const char const * const modes[3] = {
    "\n\x1B[32m\x1B[1mTemplates> \x1B[0m",
    "\n\x1B[32m\x1B[1mReports> \x1B[0m",
    "\n\x1B[32m\x1B[1mExports> \x1B[0m"
};

static char *line_read = (char *)NULL;
static int  verbose = 0;
static int stop;
static int file_body;
static dnld_params_t dnld_params;
static domain_t domain = Templates;
static int   download_size;
static char  reports_root_folder[ID_BUFF_SIZE];
static char  reports_current_folder[ID_BUFF_SIZE];
static char  templates_root_folder[ID_BUFF_SIZE];
static char  templates_current_folder[ID_BUFF_SIZE];
static char  exports_root_folder[ID_BUFF_SIZE];
static char  exports_current_folder[ID_BUFF_SIZE];

static char * GetCurrentFolder()
{
    switch (domain)
    {
    case Templates: return templates_current_folder;
    case Reports: return reports_current_folder;
    case Exports: return exports_current_folder;
    }
    return templates_current_folder;
}

static char * GetRootFolder()
{
    switch (domain)
    {
    case Templates: return templates_root_folder;
    case Reports: return reports_root_folder;
    case Exports: return exports_root_folder;
    }
    return templates_root_folder;
}

size_t dnld_header_parse(void *hdr, size_t size, size_t nmemb, void *userdata)
{
    const   size_t  cb = size * nmemb;
    const   char    *hdr_str = hdr;
    dnld_params_t *dnld_params = (dnld_params_t*)userdata;
    char const*const cdtag = "Content-disposition:";

    int http_code;
    curl_easy_getinfo(dnld_params->pointer_to_curl, CURLINFO_RESPONSE_CODE, &http_code);
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

char * readline_gets()
{
    /* If the buffer has already been allocated, return the memory
       to the free pool. */
    if (line_read)
    {
        free(line_read);
        line_read = (char *)NULL;
    }

    /* Get a line from the user. */
    line_read = readline(/*"\n@>"*/ modes[domain]);

    /* If the line has any text in it, save it on the history. */
    if (line_read && *line_read)
        add_history(line_read);

    return (line_read);
}

extern int draw_json_response(char *js, size_t jslen);

uint parse_folders_and_files_json(char *in, uint size, uint nmemb, char *out)
{
    uint r = size * nmemb;
#if PAYLOAD_DEBUG
    FILE * fp = fopen("folder.json", "w");
    fwrite(in, size, nmemb, fp);
    fclose(fp);
#endif
    draw_json_response(in, r);
    return r;
}

void show_directory(command_context_t * context)
{
    CURLcode res;
    char    *   domain_mode;
    char    *   dir_uuid;
    char  url[512];

    if (context->words_count == 0)
        dir_uuid = GetCurrentFolder();
    else if (context->words_count == 1)
        dir_uuid = context->words[0];
    else {
        fprintf(stderr, "change dir command supports only one or zero arguments\n");
        return;
    }

    switch (domain)
    {
    case Reports:   domain_mode = "Reports"; break;
    case Exports:   domain_mode = "Exports"; break;
    case Templates:
    default:
        domain_mode = "Templates";
    }

    snprintf(url, 512, "%s/api/rp/v1/%s/Folder/%s/ListFolderAndFiles",
        DEFAULT_SERVER,
        domain_mode,
        dir_uuid);

    curl_easy_setopt(context->curl, CURLOPT_URL, url);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, parse_folders_and_files_json);

    /* Perform the request, res will get the return code */
    res = curl_easy_perform(context->curl);
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

/* curl write callback, to fill tidy's input buffer...  */
uint write_cb(char *in, uint size, uint nmemb, char *out)
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

void download_file(command_context_t * context)
{
    CURLcode res;
    char request[512];
    char op;

    if (context->words_count != 1) {
        puts("Not enough arguments. Use:\n get 60758ec7377eaa000171a5ec\n where 60758ec7377eaa000171a5ec is uuid of file");
        return;
    }

    switch (domain) {
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
    snprintf(request, 512, "%s/download/%c/%s", DEFAULT_SERVER, op, context->words[0]);

    // 
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: text/html, application/xhtml+xml, application/xml, application/octet-stream");
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(context->curl, CURLOPT_HEADERFUNCTION, dnld_header_parse);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, write_cb);
    dnld_params.pointer_to_curl = context->curl;
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

void upload_file(command_context_t * context)
{
    CURLcode    res;
    char    *   domain_mode;
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

    switch (domain)
    {
    case Reports:   domain_mode = "Reports"; break;
    case Exports:   domain_mode = "Exports"; break;
    case Templates:
    default:
        domain_mode = "Templates";
    }

    snprintf(request, 512, "%s/api/rp/v1/%s/Folder/%s/File",
        DEFAULT_SERVER,
        domain_mode,
        GetCurrentFolder());

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json-patch+json");
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, NULL);

    char * post = "{ \"name\": \"alman_remove_asap.frx\", \"content\": \"77u/PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0idXRmLTgiPz4NCjxSZXBvcnQgU2NyaXB0TGFuZ3VhZ2U9IkNTaGFycCIgUmVwb3J0SW5mby5DcmVhdGVkPSIxMi8wNC8yMDIwIDEwOjU4OjU3IiBSZXBvcnRJbmZvLk1vZGlmaWVkPSIxMi8wNC8yMDIwIDExOjAwOjIwIiBSZXBvcnRJbmZvLkNyZWF0b3JWZXJzaW9uPSIyMC4yMC40LjEiPg0KICA8RGljdGlvbmFyeS8+DQogIDxSZXBvcnRQYWdlIE5hbWU9IlBhZ2UxIiBXYXRlcm1hcmsuRm9udD0iQXJpYWwsIDYwcHQiPg0KICAgIDxSZXBvcnRUaXRsZUJhbmQgTmFtZT0iUmVwb3J0VGl0bGUxIiBXaWR0aD0iNzE4LjIiIEhlaWdodD0iMzcuOCIvPg0KICAgIDxQYWdlSGVhZGVyQmFuZCBOYW1lPSJQYWdlSGVhZGVyMSIgVG9wPSI0MSIgV2lkdGg9IjcxOC4yIiBIZWlnaHQ9IjI4LjM1Ii8+DQogICAgPERhdGFCYW5kIE5hbWU9IkRhdGExIiBUb3A9IjcyLjU1IiBXaWR0aD0iNzE4LjIiIEhlaWdodD0iNzUuNiI+DQogICAgICA8VGV4dE9iamVjdCBOYW1lPSJUZXh0MSIgV2lkdGg9IjcxOC4yIiBIZWlnaHQ9Ijc1LjYiIFRleHQ9IkhlbGxvLCBGYXN0UmVwb3J0IENsb3VkISEhIiBIb3J6QWxpZ249IkNlbnRlciIgVmVydEFsaWduPSJDZW50ZXIiIEZvbnQ9IkFyaWFsLCAxMHB0Ii8+DQogICAgPC9EYXRhQmFuZD4NCiAgICA8UGFnZUZvb3RlckJhbmQgTmFtZT0iUGFnZUZvb3RlcjEiIFRvcD0iMTUxLjM1IiBXaWR0aD0iNzE4LjIiIEhlaWdodD0iMTguOSIvPg0KICA8L1JlcG9ydFBhZ2U+DQo8L1JlcG9ydD4NCg==\"}";
    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, post);

    res = curl_easy_perform(context->curl);

    curl_slist_free_all(headers);
    curl_easy_setopt(context->curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(context->curl, CURLOPT_POST, 0);

    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

void delete_file(command_context_t * context)
{
    CURLcode res;
    char    *   domain_mode;
    char request[512];

    if (context->words_count != 1) {
        puts("Use rm 'uuid' to delete file. Where 'uuid' is unique identifier of file");
        return;
    }

    switch (domain)
    {
    case Reports:   domain_mode = "Reports"; break;
    case Exports:   domain_mode = "Exports"; break;
    case Templates:
    default:
        domain_mode = "Templates";
    }
    snprintf(request, 512, "%s/api/rp/v1/%s/File/%s", 
        DEFAULT_SERVER, 
        domain_mode, 
        context->words[0]);

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

static void show_profile(command_context_t * context)
{
    CURLcode res;
    puts("----------------------- SHOW PROFILE ----------------------");

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(context->curl, CURLOPT_URL, DEFAULT_SERVER "/api/manage/v1/UserProfile");

    /* Perform the request, res will get the return code */
    res = curl_easy_perform(context->curl);
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

static void show_working_dicrectory_path(command_context_t * context)
{
    CURLcode res;
    char * domain_mode;
    char request[512];

    switch (domain)
    {
    case Reports:   domain_mode = "Reports"; break;
    case Exports:   domain_mode = "Exports"; break;
    case Templates:
    default:
        domain_mode = "Templates";
    }

    snprintf(request, sizeof(request), "%s/api/rp/v1/%s/Folder/%s/Breadcrumbs",
        DEFAULT_SERVER,
        domain_mode,
        GetCurrentFolder());

    curl_easy_setopt(context->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(context->curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(context->curl, CURLOPT_URL, request);
    res = curl_easy_perform(context->curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

static void change_directory(command_context_t * context)
{
    if (context->words_count == 0)
    {
        puts("Current dir changed to root folder. Use folder 'uuid' as an argument for cd command to change current folder");
        strcpy(GetCurrentFolder(), GetRootFolder());
        return;
    }
    if (strlen(context->words[0]) != strlen("606335ef377eaa000171a5ba"))
    {
        puts("This version supports UUID only as argument. Or no argument to chnage current folder to domain's root");
        return;
    }
    strcpy(GetCurrentFolder(), context->words[0]);
    printf("Directory changed to: %s", GetCurrentFolder());
}

static void logout_cloud(command_context_t * context) 
{ 
    stop = 1; 
}
static void select_templates(command_context_t * context) 
{ 
    domain = Templates; 
}
static void select_reports(command_context_t * context) 
{ 
    domain = Reports; 
}
static void select_exports(command_context_t * context) 
{ 
    domain = Exports; 
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

static void help(command_context_t * context);

command_record_t    commands[] = {
    {"ls", show_directory, "show directory context"},
    {"cd", change_directory, "change current directory by it's UUID"},
    {"get", download_file, "download template, report or document by it's UUID"},
    {"put", upload_file, "upload template, report or document to cloud"},
    {"pwd",     show_working_dicrectory_path, "print working directory path", NULL},
    {"help",    help, "shows list of supported commands or comand description", NULL},
    {"exit", logout_cloud, "exit from FRCloud console"},
    {"templates", select_templates, "switch to templates domain"},
    {"reports", select_reports, "switch to reports domain"},
    {"exports", select_exports, "switch to exports domain"},
    {"lls", local_dir_list, "list of local directory"},
    {"rm", delete_file, "delete file by it's UUID"},
    {"verbose", switch_verbosity, "Toggle curl verbose mode ON/OFF"},
    {"profile", show_profile, "show user profile", NULL},
    {NULL, NULL, NULL, NULL}
};

static void help(command_context_t * context)
{
    command_record_t   * ptr = commands;
    puts("List of supported commands:");
    while (ptr->command_name != NULL) 
    {
        printf(" %-10s    %s\n", ptr->command_name, ptr->short_help);
        ptr++;
    }
}

void user_interface(CURL * curl)
{
    stop = 0;
    command_context_t       context;
    command_record_t    *   cmd_ptr;

    puts("Welcome to FastReport.Cloud shell. Type 'help' to see list of builtin commands");
    do {
        context.curl = curl;
        context.words_count = 0;
        context.command = readline_gets();
        context.words[context.words_count] = strpbrk(context.command, " \t");
        if (context.words[context.words_count] != NULL)
        {
            *context.words[context.words_count] = 0;
            context.words[context.words_count]++;
            context.words_count++;
            printf("Found arg: %s\n", context.words[0]);
        }

        if (context.command[0] == 0)
            continue;

        for (cmd_ptr = commands; cmd_ptr->command_name; cmd_ptr++)
        {
            if (strcmp(context.command, cmd_ptr->command_name) != 0)
                continue;
            cmd_ptr->run(&context);
            break;
        }

        if (cmd_ptr->command_name != NULL)
            continue;

        printf("Unknown command: '%s' Type 'help' and press <Enter> to see list of built-in commands", context.command);

    } while (!stop);
}

uint init_cb(char *in, uint size, uint nmemb, char *out)
{
    uint r = size * nmemb;
    char * ptr = strstr(in, "\"id\":\"");
    char * end = NULL;
    if (!ptr) {
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
    switch (domain)
    {
    case Templates:
        strcpy(templates_root_folder, ptr);
        strcpy(templates_current_folder, ptr);
        break;
    case Reports:
        strcpy(reports_root_folder, ptr);
        strcpy(reports_current_folder, ptr);
        break;
    case Exports:
        strcpy(exports_root_folder, ptr);
        strcpy(exports_current_folder, ptr);
        break;
    }
    return r;
}

void user_init(CURL * curl, char * auth)
{
    CURLcode res;

    memset(reports_root_folder, 0, sizeof(reports_root_folder));
    memset(reports_current_folder, 0, sizeof(reports_current_folder));
    memset(templates_root_folder, 0, sizeof(templates_root_folder));
    memset(templates_current_folder, 0, sizeof(templates_current_folder));
    memset(exports_root_folder, 0, sizeof(exports_root_folder));
    memset(exports_current_folder, 0, sizeof(exports_current_folder));

    curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "FastReport.Cloud/0.1 (Linux) libcurl");
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, init_cb);

    curl_easy_setopt(curl, CURLOPT_URL, DEFAULT_SERVER "/api/rp/v1/Templates/Root");
    domain = Templates;
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    curl_easy_setopt(curl, CURLOPT_URL, DEFAULT_SERVER "/api/rp/v1/Reports/Root");
    domain = Reports;
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    curl_easy_setopt(curl, CURLOPT_URL, DEFAULT_SERVER "/api/rp/v1/Exports/Root");
    domain = Exports;
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    domain = Templates;
}

int main(void)
{
    CURL *curl;
    CURLcode res;
    char    auth[256];

    strcpy(auth, "apikey:");
    if (load_token(KEY_FILE, auth) < 0) {
        fprintf(stderr, "Unable set tocken\n");
        return EXIT_FAILURE;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    if (curl) {
        user_init(curl, auth);
        memset(auth, 0, sizeof(auth));
        user_interface(curl);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();

    return 0;
}

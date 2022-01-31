#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <curl/curl.h>

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
        switch(*val)
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

static const char const * const modes[3] = { "\nTemplates>", "\nReports>", "\nExports>" };
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
/* A static variable for holding the line. */
static char *line_read = (char *)NULL;
static int  verbose = 1;

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
    draw_json_response(in, r);
    return r;
}

void show_directory(CURL * curl, char * dir_uuid)
{
    CURLcode res;
    char    *   domain_mode;
    char  url[512];

    switch (domain)
    {
    case Reports:   domain_mode = "Reports"; break;
    case Exports:   domain_mode = "Exports"; break;
    case Templates: 
    default:
        domain_mode = "Templates";
    }

    snprintf(url, 512, "https://fastreport.cloud/api/rp/v1/%s/Folder/%s/ListFolderAndFiles", 
        domain_mode, 
        dir_uuid);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_folders_and_files_json);

    /* Perform the request, res will get the return code */
    res = curl_easy_perform(curl);
    /* Check for errors */
    if(res != CURLE_OK)
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
        if(l < 0) {
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

void download_report(CURL * curl, char * uuid)
{
    CURLcode res;
    char request[512];

    switch (domain)
    {
    case Templates:
        snprintf(request, 512, "https://fastreport.cloud/download/t/%s", uuid);
        break;
    case Reports:
        snprintf(request, 512, "https://fastreport.cloud/download/r/%s", uuid);
        break;
    case Exports:
        snprintf(request, 512, "https://fastreport.cloud/download/e/%s", uuid);
        break;
    }
    //
    // 
//    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 5.1; rv:21.0) Gecko/20130401 Firefox/21.0");
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: text/html, application/xhtml+xml, application/xml, application/octet-stream");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, dnld_header_parse);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    dnld_params.pointer_to_curl = curl;
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &dnld_params);
    curl_easy_setopt(curl, CURLOPT_URL, request);

    file_body = -1;
    download_size = 0;
    res = curl_easy_perform(curl);
    if (file_body > 0)
    {
        close(file_body);
        printf("Load %d bytes of '%s'\n", download_size, dnld_params.remote_fname);
        file_body = -1;
    }
    /* Check for errors */
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
}

void upload_file(CURL * curl, char * filename)
{
    CURLcode res;
    char    *   domain_mode;
    char request[512];

    if (access(filename, R_OK) == -1)
    {
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

    snprintf(request, 512, "https://fastreport.cloud/api/rp/v1/%s/Folder/%s/File",
        domain_mode,
        GetCurrentFolder());

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json-patch+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, request);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);

    char * post = "{ \"name\": \"alman_halfway.frx\", \"content\": \"77u/PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0idXRmLTgiPz4NCjxSZXBvcnQgU2NyaXB0TGFuZ3VhZ2U9IkNTaGFycCIgUmVwb3J0SW5mby5DcmVhdGVkPSIxMi8wNC8yMDIwIDEwOjU4OjU3IiBSZXBvcnRJbmZvLk1vZGlmaWVkPSIxMi8wNC8yMDIwIDExOjAwOjIwIiBSZXBvcnRJbmZvLkNyZWF0b3JWZXJzaW9uPSIyMC4yMC40LjEiPg0KICA8RGljdGlvbmFyeS8+DQogIDxSZXBvcnRQYWdlIE5hbWU9IlBhZ2UxIiBXYXRlcm1hcmsuRm9udD0iQXJpYWwsIDYwcHQiPg0KICAgIDxSZXBvcnRUaXRsZUJhbmQgTmFtZT0iUmVwb3J0VGl0bGUxIiBXaWR0aD0iNzE4LjIiIEhlaWdodD0iMzcuOCIvPg0KICAgIDxQYWdlSGVhZGVyQmFuZCBOYW1lPSJQYWdlSGVhZGVyMSIgVG9wPSI0MSIgV2lkdGg9IjcxOC4yIiBIZWlnaHQ9IjI4LjM1Ii8+DQogICAgPERhdGFCYW5kIE5hbWU9IkRhdGExIiBUb3A9IjcyLjU1IiBXaWR0aD0iNzE4LjIiIEhlaWdodD0iNzUuNiI+DQogICAgICA8VGV4dE9iamVjdCBOYW1lPSJUZXh0MSIgV2lkdGg9IjcxOC4yIiBIZWlnaHQ9Ijc1LjYiIFRleHQ9IkhlbGxvLCBGYXN0UmVwb3J0IENsb3VkISEhIiBIb3J6QWxpZ249IkNlbnRlciIgVmVydEFsaWduPSJDZW50ZXIiIEZvbnQ9IkFyaWFsLCAxMHB0Ii8+DQogICAgPC9EYXRhQmFuZD4NCiAgICA8UGFnZUZvb3RlckJhbmQgTmFtZT0iUGFnZUZvb3RlcjEiIFRvcD0iMTUxLjM1IiBXaWR0aD0iNzE4LjIiIEhlaWdodD0iMTguOSIvPg0KICA8L1JlcG9ydFBhZ2U+DQo8L1JlcG9ydD4NCg==\"}";
//    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "{\"hi\" : \"there\"}");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);

    fprintf(stderr, "%s\n", request);

    res = curl_easy_perform(curl);
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

void show_profile(CURL * curl)
{
    CURLcode res;
    puts("----------------------- SHOW PROFILE ----------------------\n\n");

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_URL, "https://fastreport.cloud/api/manage/v1/UserProfile");

    /* Perform the request, res will get the return code */
    res = curl_easy_perform(curl);
    /* Check for errors */
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
}

void show_working_dicrectory_path(CURL * curl)
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

    snprintf(request, sizeof(request), "https://fastreport.cloud/api/rp/v1/%s/Folder/%s/Breadcrumbs",
        domain_mode,
        GetCurrentFolder());

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_URL, request);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
}

// --------------------- Ниже цикл в котором пользователь вводит команды -------------
void user_interface(CURL * curl)
{
    int stop = 0;
    int arg_counter = 0;
    char *  user_input;
    char *  words[8];

    do {

       arg_counter = 0;
       user_input = readline_gets();
       words[arg_counter] = strpbrk(user_input, " \t");
       if(words[arg_counter] != NULL)
       {
         *words[arg_counter] = 0;
         words[arg_counter]++;
         arg_counter++;
         printf("Found arg: %s\n", words[0]);
       }

       if(strcmp(user_input, "exit") == 0)
       {
          stop = 1;
//          printf("Длина введённой строки: %ld\n", strlen(user_input));
       }
       else if( strcmp(user_input, "ls") == 0 )
       {
           show_directory(curl, GetCurrentFolder() );
       }
       else if (strcmp(user_input, "lls") == 0)
       {
           system("ls -l");
       }
       else if (strcmp(user_input, "reports") == 0)
       {
           domain = Reports;
       }
       else if (strcmp(user_input, "templates") == 0)
       {
           domain = Templates;
       }
       else if (strcmp(user_input, "exports") == 0)
       {
           domain = Exports;
       }
       else if (strcmp(user_input, "verbose") == 0)
       {
       verbose = verbose ? 0 : 1;
       curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);
       printf("curl verbose mode set to %s", verbose ? "Enabled" : "Disabled");
       }
       else if (strcmp(user_input, "cd") == 0)
       {
       if (arg_counter != 1)
       {
           puts("Current dir changed to root folder. Use folder id as argument for cd command to change current folder");
           strcpy(GetCurrentFolder(), GetRootFolder());
           continue;
       }
       strcpy(GetCurrentFolder(), words[0]);
       printf("Directory changed to: %s", GetCurrentFolder());
       }
       else if (strcmp(user_input, "pwd") == 0)
       {
       show_working_dicrectory_path(curl);
       }
       else if (strcmp(user_input, "get") == 0)
       {
       if (arg_counter == 1)
       {
           download_report(curl, words[0]);
       }
       else
           download_report(curl, "60758ec7377eaa000171a5ec");
       }
       else if (strcmp(user_input, "put") == 0)
       {
       if (arg_counter != 1)
           printf("Use> put filename\n  where 'filename' is path to local file\n");
       else
           upload_file(curl, words[0]);
       }
       else if (strcmp(user_input, "profile") == 0)
       {
       show_profile(curl);
       }
       else
       printf("%s", user_input);

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



void user_init(CURL * curl)
{
    CURLcode res;
    char * key_token;
    char    auth[256];

    strcpy(auth, "apikey:");

    if (access(KEY_FILE, R_OK) == -1)
    {
        int i;

        int key_fd = open(KEY_FILE, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (key_fd > 0)
        {
        not_correct_input:
            key_token = readline(
                "Security token not found. You may enter tocken now and it will be stored to "
                KEY_FILE
                " file in current directory.\nPress enter without input to exit console now.\nToken>");
            int len = strlen(key_token);
            if (len == 0)
            {
                curl_global_cleanup();
                close(key_fd);
                remove(KEY_FILE);
                fprintf(stderr, "Programm aborted by user request - no token provided\n");
                exit(1);
            }
            for (i = 0; i < 52; i++) {
                char c = key_token[i];
                if (isdigit(c) || (isascii(c) && islower(c)))
                    continue;
                goto not_correct_input;
            }
            key_token[i] = 0;
            write(key_fd, key_token, strlen(key_token));
            strcat(auth, key_token);
            close(key_fd);
        }
        else
        {
            fprintf(stderr, "Unable create access key template: %s", KEY_FILE);
            exit(4);
        }
    }
    else
    {
        FILE * key_file;
        char    buff[128];
        key_file = fopen(KEY_FILE, "r");
        if (fgets(buff, sizeof(buff), key_file) == NULL)
        {
            fprintf(stderr, "Unable read token\n");
            exit(2);
        }
        fclose(key_file);
        if (strlen(buff) != 52)
        {
            fprintf(stderr, "Access token size error\n");
            exit(3);
        }
        strcat(auth, buff);
    }

    curl_easy_setopt(curl, CURLOPT_USERPWD, auth);

    memset(reports_root_folder, 0, sizeof(reports_root_folder));

    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, init_cb);

    curl_easy_setopt(curl, CURLOPT_URL, "https://fastreport.cloud/api/rp/v1/Templates/Root");
    domain = Templates;
    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));

    curl_easy_setopt(curl, CURLOPT_URL, "https://fastreport.cloud/api/rp/v1/Reports/Root");
    domain = Reports;
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    curl_easy_setopt(curl, CURLOPT_URL, "https://fastreport.cloud/api/rp/v1/Exports/Root");
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

  curl_global_init(CURL_GLOBAL_DEFAULT);

  curl = curl_easy_init();
  if(curl) {
    user_init(curl);
    user_interface(curl);
    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();

  return 0;
}

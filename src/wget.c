/* Leaf's limited PICO-8 wget adapter. Uses the firmware's HTTPS-capable libcurl.
   Only the GET and --post-file forms used by PICO-8 are accepted. */
#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *url = NULL, *output = NULL, *post = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) continue;
        if (!strcmp(argv[i], "-O") && !output && i + 1 < argc) output = argv[++i];
        else if (!strncmp(argv[i], "--post-file=", 12) && !post && argv[i][12]) post = argv[i] + 12;
        else if ((!strncmp(argv[i], "https://", 8) || !strncmp(argv[i], "http://", 7)) && !url) url = argv[i];
        else { fputs("Unsupported PICO-8 download arguments\n", stderr); return 2; }
    }
    if (!url || !output || !*output) return 2;
    char *body = NULL;
    size_t size = 0;
    if (post) {
        struct stat st;
        if (stat(post, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 1024 * 1024) return 2;
        size = (size_t)st.st_size;
        body = malloc(size + 1);
        FILE *fp = fopen(post, "rb");
        if (!body || !fp) { if (fp) fclose(fp); free(body); return 2; }
        size_t read_size = fread(body, 1, size, fp);
        fclose(fp);
        if (read_size != size) { free(body); return 2; }
        body[size] = '\0';
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) { free(body); return 1; }
    CURL *curl = curl_easy_init();
    FILE *fp = NULL;
    char temporary[4096] = {0};
    CURLcode result = CURLE_FAILED_INIT;
    if (!curl) goto done;
#define OPTION(key, value) do { result = curl_easy_setopt(curl, key, value); if (result != CURLE_OK) goto done; } while (0)
    OPTION(CURLOPT_URL, url);
    /* Stock MLP1 libcurl/nghttp2 disagree on an HTTP/2 symbol. PICO-8's
       requests need only HTTP/1.1; keep that firmware path out of this helper. */
    OPTION(CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    OPTION(CURLOPT_PROTOCOLS_STR, "http,https");
    OPTION(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    OPTION(CURLOPT_FOLLOWLOCATION, 1L);
    OPTION(CURLOPT_MAXREDIRS, 5L);
    OPTION(CURLOPT_FAILONERROR, 1L);
    OPTION(CURLOPT_CONNECTTIMEOUT, 10L);
    OPTION(CURLOPT_TIMEOUT, 30L);
    OPTION(CURLOPT_NOSIGNAL, 1L);
    OPTION(CURLOPT_SSL_VERIFYPEER, 1L);
    OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
    OPTION(CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
    if (post) {
        OPTION(CURLOPT_POSTFIELDS, body);
        OPTION(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)size);
    }
    int n = snprintf(temporary, sizeof(temporary), "%s.part-XXXXXX", output);
    if (n < 0 || n >= (int)sizeof(temporary)) { temporary[0] = 0; result = CURLE_WRITE_ERROR; goto done; }
    int fd = mkstemp(temporary);
    if (fd < 0) { temporary[0] = 0; result = CURLE_WRITE_ERROR; goto done; }
    fp = fdopen(fd, "wb");
    if (!fp) { close(fd); result = CURLE_WRITE_ERROR; goto done; }
    OPTION(CURLOPT_WRITEDATA, fp);
    result = curl_easy_perform(curl);
done:
    if (fp && fclose(fp) != 0 && result == CURLE_OK) result = CURLE_WRITE_ERROR;
    if (result == CURLE_OK && rename(temporary, output) != 0) result = CURLE_WRITE_ERROR;
    if (temporary[0]) unlink(temporary);
    if (result != CURLE_OK) fprintf(stderr, "PICO-8 download: %s\n", curl_easy_strerror(result));
    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(body);
    return result == CURLE_OK ? 0 : 1;
}

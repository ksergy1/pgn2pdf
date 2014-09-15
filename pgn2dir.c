#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <limits.h>
#include <errno.h>

void standardName(char *to, char *outDir, int k)
{
    sprintf(to, "%s/game-%d.pgn", outDir, k);
}

void whiteWinName(char *to, char *outDir, int k)
{
    sprintf(to, "%s/game-%d-ww.pgn", outDir, k);
}

void blackWinName(char *to, char *outDir, int k)
{
    sprintf(to, "%s/game-%d-bw.pgn", outDir, k);
}

void drawName(char *to, char *outDir, int k)
{
    sprintf(to, "%s/game-%d-00.pgn", outDir, k);
}

int
main (int argc, char **argv)
{
    FILE *in;
    FILE *out;
    char outName[PATH_MAX];
    struct stat st;
    char *file_data;
    char *game, *game2, *result;
    char *outDir;
    int k = 0;
    char *buf;
    int lenToCopy;
    int startNum = 0, endNum = INT_MAX;
    const char GameStart[] = "[Event";
    const char GameResult[] = "[Result";

    if (argc < 3) {
        fprintf(stdout,
                "usage:    %s <in-pgn> <out-dir> [start_num end_num]\n"
                "usage: or %s <in-pgn> <out-dir> [end_num] // start_num = 0\n"
                "usage: or %s <in-pgn> <out-dir> // start_num = 0, end_num = INT_MAX\n"
                "start_num and end_num are 0-based\n",
                argv[0], argv[0], argv[0]);
        return 0;
    }

    if (argc == 4)
        endNum = atoi(argv[3]) + 1;
    if (argc == 5) {
        startNum = atoi(argv[3]);
        endNum = atoi(argv[4]) + 1;
    }

    if (stat(argv[1], &st) < 0) {
        perror("cant stat input file");
        return 1;
    }

    in = fopen(argv[1], "r");
    if (!in) {
        perror("fopen");
        return 1;
    }

    rewind(in);
    file_data = malloc(st.st_size + 1);
    fread(file_data, 1, st.st_size, in);
    file_data[st.st_size] = '\0';
    fclose(in);

    outDir = argv[2];
    if (outDir[strlen(outDir) - 1] == '/')
        outDir[strlen(outDir) - 1] = '\0';

    game = strstr(file_data, GameStart);

    /* skip to startNum */
    while ((game) &&
           (k < startNum)) {
        game2 = strstr(game + strlen(GameStart), GameStart);
        game = game2;
        ++k;
    }

    while ((game) &&
           (k < endNum)) {
        game2 = strstr(game + strlen(GameStart), GameStart);

        lenToCopy = game2 ? game2 - game : strlen(game);
        buf = malloc(sizeof(char) * (lenToCopy + 1));
        memcpy(buf, game, lenToCopy);
        buf[lenToCopy] = '\0';
        memcpy(buf, game, sizeof(char)*lenToCopy);

        result = strstr(game, GameResult);
        if (!result)
            standardName(outName, outDir, k);
        else {
            char *subbuf;
            int subbuf_len;
            char *nl = strstr(result + strlen(GameResult), "]");
            char *q, *q2;
            if (!nl)
                standardName(outName, outDir, k);
            else {
                subbuf_len = nl - result;
                subbuf = malloc(sizeof(char) * (subbuf_len + 1));
                memcpy(subbuf, result, subbuf_len);
                subbuf[subbuf_len] = '\0';

                q = strstr(subbuf, "\"");
                if (!q)
                    standardName(outName, outDir, k);
                else {
                    *q = '\0';
                    ++q;
                    q2 = strstr(q, "\"");
                    if (!q2)
                        standardName(outName, outDir, k);
                    else {
                        *q2 = '\0';
                        if (strcmp(q, "1-0") == 0)
                            whiteWinName(outName, outDir, k);
                        else if (strcmp(q, "0-1") == 0)
                            blackWinName(outName, outDir, k);
                        else if (strcmp(q, "1/2-1/2") == 0)
                            drawName(outName, outDir, k);
                        else
                            standardName(outName, outDir, k);
                    }
                }

                free(subbuf);
            }
        }

        out = fopen(outName, "w");
        if (!out) {
            fprintf(stderr, "fopen failed for '%s': %s\n",
                    outName, strerror(errno));
            free(buf);
            return 2;
        }

        fprintf(out, "%s\n", buf);

        free(buf);

        fclose(out);
        game = game2;
        ++k;
    };

    free(file_data);

    return 0;
}

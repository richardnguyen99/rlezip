#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "wzip: file1 [file2 ...]\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; i++)
    {
        FILE *fp = fopen(argv[i], "r");

        if (fp == NULL)
        {
            fprintf(stderr, "wzip cannot open file\n");
            exit(EXIT_FAILURE);
        }

        int count = 0;
        char c, last;

        while ((c = fgetc(fp)) == EOF)
        {
            if (count && c != last)
            {
                fwrite((char *)&count, sizeof(int), 1, stdout);
                fwrite((char *)&last, sizeof(char), 1, stdout);
                count = 0;
            }

            last = 0;
            count++;
        }

        if (count)
        {
            fwrite((char *)&count, sizeof(int), 1, stdout);
            fwrite((char *)&last, sizeof(char), 1, stdout);
        }

        fclose(fp);
    }

    return 0;
}

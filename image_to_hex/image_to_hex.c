/**
 * Copyright (C), 2015, Lovelacelee.com Tech.
 * @file name:  favicon_gen.c
 * @author:     Lee
 * @version:    1.0
 * @created on: 2015-10-16
 * Just input a picture then you get a const char data head file.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEAD "\n\
#ifndef __FAVICON_H__\n\
#define __FAVICON_H__\n\
\n\
char favicon[] = {\n"

#define END "\n}\n\
\n\
#endif /* __FAVICON_H__ */\n"

int main(int argc, char *argv[])
{
    FILE *input = NULL;
    FILE *output = NULL;
    char *file_name = NULL;
    char outfname[128] = "";
    unsigned char *bmp_data = NULL;
    int i;
    int file_size = 0;

    if (argc != 2)
    {
        printf("Usage: favicon_gen [favicon.ico]\r\n");
        return -1;
    }
    file_name = argv[1];
    snprintf(outfname, 128, "%s.h", file_name);
    output = fopen(outfname, "wb");
    if (!output)
    {
        fprintf(stderr, "[%s] Cannot open file!\n",outfname);
        return -1;
    }
    input = fopen(file_name, "rb");
    if (!input)
    {
        fprintf(stderr, "[%s] Cannot open file!\n",file_name);
        return -1;
    }
    fseek(input, 0, SEEK_END);
    file_size = ftell(input);
    bmp_data = (unsigned char *)malloc(file_size + 1);
    fseek(input, 0, SEEK_SET);
    fread(bmp_data, 1, file_size, input);
    bmp_data[file_size] = '\n';
    printf("%s", HEAD);
    fwrite(HEAD, 1, sizeof(HEAD), output);
    for(i = 0; i < file_size; i++)
    {
        if (i % 16 == 0) {
            printf("\n");
            fwrite("\n", 1, 1, output);
        }
        printf("0x%.2x", bmp_data[i]);
        char value[16] = "0";
        snprintf(value, 16, "0x%.2x", bmp_data[i]);
        fwrite(value, 1, strlen(value), output);
        if (i != file_size-1) 
        {
            printf(",");
            fwrite(",", 1, 1, output);
        }
    }
    printf("%s", END);
    fwrite(END, 1, sizeof(END), output);
    printf("\r\n");
    fclose(input);
    fclose(output);
    return 0;
 
}
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "db_count.h"
/**
 * 用在16Khz的单声道或者8Khz的双声道的pcm数据的音量计算. 对数函数是log10,不是log.
 */

int pcm16_db_count(const unsigned char* ptr, size_t size)
{
    int ndb = 0;

    short int value;

    size_t i;
    long v = 0;
    for(i=0; i<size; i+=2)
    {   
        memcpy((char*)&value, ptr+i, 1); 
        memcpy((char*)&value+1, ptr+i+1, 1); 
        v += abs(value);
    }   

    v = v/(size/2);

    if(v != 0) {
        ndb = (int)(20.0*log10((double)v / 65535.0 )); 
    }   
    else {
        ndb = -96;
    }   

    return ndb;
}

unsigned long pcm8_db_count(const unsigned char* ptr, size_t size)
{
    size_t i;
    unsigned long v = 0;
    size_t nsize = size;
    for(i=0; i<size; i++)
    {   
        char sample = ptr[i];
        if(abs(sample) <= 1)
        {
            sample = 0;
            nsize--;
        }
        v += abs(sample);
    }   

    return nsize ==0 ? 0 : (v/nsize);
}

int pcms8_mono_volume(const char *pcms8_file, const char *output_pcms8_file, int volume_power)
{
    FILE *fp=fopen(pcms8_file,"rb+");  
    FILE *fp1=fopen(output_pcms8_file,"wb+");  
    int cnt=0;  
    char sample = 0;  
    while(!feof(fp))
    {  

        fread(&sample,1,1,fp);  
        if(abs(sample) <= 1)
        {
            sample = 0;
        }
        if(sample != 0)
            sample = sample * volume_power;
        fwrite(&sample,1,1,fp1);  
  
        cnt++;  
    }  
    printf("Sample Cnt:%d\n",cnt);  
  
    fclose(fp);  
    fclose(fp1);  
    return 0; 
}

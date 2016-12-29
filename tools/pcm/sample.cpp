#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <list>

#include "db_count.h"

#define SAMPLE_HZ            8000
#define BIT_PER_MS          (SAMPLE_HZ/1000)
#define BITS_PER_WINDOW     (BIT_PER_MS*20*3)

int trace_queue(std::list<unsigned long >q)
{
    std::list<unsigned long >::iterator it;
    int left = 0;
    int right = 0;
    size_t i = 0;
    for(it = q.begin(); it != q.end(); ++it)
    {
        if(i < q.size()/2)
        {
            if((*it) > 0)
                left++;
        }
        else{
            if((*it) > 0)
                right++;
        }
        printf("%lu->", (*it));
        i++;
    }
    //printf("left %d right %d\n", left, right);
    if(left <= 1 && right >= 4)
    {
        return 1;
    }
    else if(left >=3 && right == 0)
        return 2;
    return 0;
}

int main(int argc, char **argv)
{
    FILE *fp = fopen("signed_pcm_8.pcm", "r+");
    char second[BITS_PER_WINDOW] = "";
    int i = 0;
    unsigned int pts = 0;
    
    std::list<unsigned long>db_queue;
    
    while(fread(second, BITS_PER_WINDOW, 1, fp) > 0)
    {
        unsigned long db = pcm8_db_count((const unsigned char*)second, BITS_PER_WINDOW);

        db_queue.push_back(db);
        if(db_queue.size() > 9)
        {
            db_queue.pop_front();
        }
        int rising_edge = trace_queue(db_queue);
        printf("db = %ld pts = %02.2f================%s===\n", db, (pts + (i++)*60)/1000.0, rising_edge == 1?"^":(rising_edge == 2)?"V":"=");
        memset(second, 0x0, sizeof(second));
        
    }
    
    fclose(fp);
    
    pcms8_mono_volume("signed_pcm_8.pcm", "signed_pcm_8_volume.pcm", 5);
    return 0;
}


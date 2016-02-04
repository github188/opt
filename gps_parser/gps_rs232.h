/*******************************************************************************
* Copyright (C), 2000-2015,  HuaMai Communication Technology Co., Ltd.
* Filename :   gps_rs232.h
* Author   :   Lee
* Version  :   1.1.0
* Date     :   2015-6-3
* BriefInfo:   common gps structure
*******************************************************************************/

#ifndef __GPS_RS232_H__
#define __GPS_RS232_H__

#include <pthread.h>

#define TTY_GPS_DATA_LEN    256
#define TTY_GPS_CMD_COUNT   32
#define GPS_VERTEX_COUNT 32
#define GPS_MAX_CACHE_COUNT (100*1024*1024)
#define GPS_CONNECT_TIMEOUT 10
#define GPS_SEND_TIMEOUT 10


#define GPS_CONFIG_FILE     "/mnt/mtd/gps_config.conf"
#define HM_GPS_NET_CONFIG   "/mnt/mtd/netset.conf"

#define TTY_GPS_DEV         "/dev/ttyS1"

#define GPS_CACHE_FILE      "/mnt/disk0/gps_cache"

#define GPS_CMD_GGA         "$GPGGA"
#define GNS_CMD_GGA         "$GNGGA"

#define GPS_CMD_RMC         "$GPRMC"
#define GNS_CMD_RMC         "$GNRMC"

#define GPS_CMD_VTG         "$GPVTG"
#define GNS_CMD_VTG         "$GNVTG"

#define GPS_CMD_GSA         "$GPGSA"
#define GNS_CMD_GSA         "$GNGSA"

#define GPS_CMD_GSV         "$GPGSV"
#define GNS_CMD_GSV         "$GNGSV"

#define GPS_CRLF            "\r\n"

enum GPS_SYMBOL {
    GPS_COMMA = ',',
    GPS_CHECKSUM_MARK = '*',
    GPS_START = '$'
};

enum GPS_SENT {
    GPS_NA = 0,
    GPS_VALID = 1
};

typedef struct {
    int gps_year;  
    int gps_mon; 
    int gps_day;
    int reserved;
} gps_date_t;

typedef struct {
    int gps_hour;
    int gps_min;
    int gps_sec;
    int gps_msec;
} gps_time_t;

typedef struct {
    double lati;
    double longi;
} lati_longi_cood_t;

typedef struct {
    uint32_t enable;
    char ip[40];
    uint32_t port;
    uint32_t push_fr;
    uint32_t vertex_cnt;
    time_t timeout; /* park alarm timeout */
    lati_longi_cood_t park_sensitivity;
    lati_longi_cood_t geo_area[GPS_VERTEX_COUNT];
    pthread_mutex_t mtx;
} gps_config_t;

typedef struct {
    gps_date_t gps_dt;
    gps_time_t gps_tm;
    double latitude;
    int lati_degree;
    double lati_cent;
    double longitude;
    int longi_degree;
    double longi_cent;
    double speed;
    double direction;
    double water_lvl;
    double elevation;
    unsigned char EW;
    unsigned char NS;
    unsigned char status; /* A-located V-not located */
    unsigned char reserved;
    int satellite_count;
} gps_data_t;

typedef struct {
    unsigned char buffer_len;
    char buffer[TTY_GPS_DATA_LEN];
    char frame[TTY_GPS_DATA_LEN];
    char *cmd_list[TTY_GPS_CMD_COUNT];
} gps_buffer_t;

typedef struct {
    float latitude;
    float longitude;
    int gps_msec;
    time_t pts;
    int smu_flag; /* 0-not valid, 1-used & valid for send out */
} gps_data_cache_t; /* Used for cache to file */

typedef struct {
    gps_date_t gps_dt;
    gps_time_t gps_tm;
    float latitude;
    float longitude;
    float speed; /* unit knot 1.852km */
    unsigned char status; /* A-located V-not located */
    unsigned char recording; /* 1-recording 0-not recording */
} gps_info_t; /* Used for hm_client */

typedef void *(*thread_rountine)(void *);

typedef void (*gps_cb)(gps_info_t *);

extern int32_t register_gps_cb(gps_cb fun);
extern int32_t fill_extended_xmlmsg(char *, char *buffer, size_t, void *);
extern int gps_enable();
extern int gps_set_fence(lati_longi_cood_t *area, uint32_t vertex_cnt) ;
extern int gps_fence_off(uint32_t vertex_cnt);
extern int gps_set_park_sensitivity(lati_longi_cood_t stop_sens, time_t timeout);
extern gps_config_t* gps_get_config(gps_config_t *buffer);

#endif /* __GPS_RS232_H__ */


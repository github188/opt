#ifndef __GPS_PARSER_H__
#define __GPS_PARSER_H__

#define TTY_GPS_DATA_LEN    256

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
    int gps_usec;
} gps_time_t;

typedef struct {
    gps_date_t gps_dt;
	gps_time_t gps_tm;
	double latitude;
	int lati_degree;
	int lati_cent;
	int lati_second;
	double longitude;
	int longi_degree;
	int longi_cent;
	int longi_second;
	float speed;
	float direction;
	float water_lvl;
	float elevation;
	unsigned char EW;
	unsigned char NS;
    unsigned char state; /* A-located V-not located */
    unsigned char reserved;
} gps_data_t;

typedef struct {
    unsigned char msg_len;
    char buffer[TTY_GPS_DATA_LEN];
    char message[TTY_GPS_DATA_LEN];
} gps_buffer_t;


#endif /*__GPS_PARSER_H__*/


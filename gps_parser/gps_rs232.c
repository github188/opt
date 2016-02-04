/*******************************************************************************
* Copyright (C), 2000-2015,  HuaMai Communication Technology Co., Ltd.
* Filename :   gps_rs232.c
* Author   :   Lee
* Version  :   1.1.0
* Date     :   2015-6-3
* BriefInfo:   Read GPS data (NMEA-1803 protocal) & parse data & send out
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h> 
#include <termios.h>  
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <math.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <sys/file.h>

#include <poll.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "common/hm_net.h"
#include "common/hm_config.h"
#include "common/pipe_list.h"
#include "hardware/hardware.h"
#include "config/config_module.h"
#include "gps_rs232.h"
#include "alarm/alarm.h"
#include "client/osd.h"
#include "gps_utf8_status.h"
#include "watchdog/watchdog.h"

// gps data collection pthread
static gps_data_t gps_data;
static time_t last_gps_data_pts = 0;
static gps_buffer_t gps_buffer;
static gps_config_t gps_config;
static time_t last_push_tm = 0;
static int osd_server_socket = -1;
static osd_info_t osd_gps;

typedef struct {
    volatile int32_t inited;
    int32_t fd;
    pipe_list_t* pipe_list;
    List cb_list;
    pthread_mutex_t list_mtx;
} gps_ctrl_t;

static gps_ctrl_t gps_ctrl = { 
    .list_mtx = PTHREAD_MUTEX_INITIALIZER, 
    .inited = 0 
};
// end of gps data collection pthread

// gps data alarm check pthread
typedef struct {
    gps_data_t data;
    int status;
    pthread_mutex_t mtx;
} gps_alm_check_t;
static gps_alm_check_t gps_data_alm_check = {
    .mtx = PTHREAD_MUTEX_INITIALIZER
};

static lati_longi_cood_t last_cood;
static time_t last_parktimout_ts = 0;
static time_t park_time = 0;
// End of gps data alarm check pthread

static void gps_data_cache(gps_data_cache_t * pgdc) ;
static void gps_send_out(void);
static int send_osd_msg(int sock);
extern int32_t fill_extended_xmlmsg(char *function, char *buffer, size_t size, void *info);

void update_alm_gps_checking_data(gps_data_t *pdata)
{
    if(pdata)
    {
        pthread_mutex_lock(&gps_data_alm_check.mtx);
        memcpy(&gps_data_alm_check.data, pdata, sizeof(gps_data_t));
        gps_data_alm_check.status = 1;
        pthread_mutex_unlock(&gps_data_alm_check.mtx);
    }
}

double llmin(double x, double y) {
    if (x < y)
        return x;
    else
        return y;
}

double llmax(double x, double y) {
    if (x > y)
        return x;
    else
        return y;
}

uint32_t rgb_color(uint8_t r, uint8_t g, uint8_t b){
    uint32_t color = 0;
    color |= r << 16;
    color |= g << 8;
    color |= b;
    return color;
}

int is_inline(lati_longi_cood_t Q, lati_longi_cood_t Pi, lati_longi_cood_t Pj) {
    if((Q.longi-Pi.longi)*(Pj.lati-Pi.lati) == (Pj.longi-Pi.longi)*(Q.lati-Pi.lati) && 
        llmin(Pi.longi,Pj.longi) <= Q.longi && 
        Q.longi <= llmax(Pi.longi, Pj.longi) &&
        llmin(Pi.lati, Pj.lati)<=Q.lati && 
        Q.lati<=llmax(Pi.lati, Pj.lati)) {
        return 1;
    }
    return 0;
}

int is_inside(lati_longi_cood_t *set, int count, lati_longi_cood_t p){
    int i, j;
    double angle = 0.0;
    int inside = 0;
    const double PI = 3.141592653589793;
    TraceWarn("==========GPS area count %d , current gps[%lf, %lf]=======\n", count, p.lati, p.longi);
    for(i=0,j=count-1; i<count; j=i++){
        //TraceWarn("==lat %lf, long %lf==\n", set[i].lati, set[i].longi);
        if(set[i].longi == p.longi && set[i].lati == p.lati){
            TraceWarn("==========GPS data same as vertex=======\n");
            inside = 1;
            break;
        }
        else if(is_inline(p, set[i], set[j])){
            TraceWarn("==========GPS data is inline=======\n");
            inside = 1;
            break;
        }
        double x1, y1, x2, y2;
        x1 = set[i].longi - p.longi;
        y1 = set[i].lati - p.lati;
        x2 = set[j].longi - p.longi;
        y2 = set[j].lati - p.lati;
        double radian = atan2(y1, x1) - atan2(y2, x2);
        radian = fabs(radian);
        if(radian > PI){
            radian = 2*PI - radian;
        }
        angle += radian;
    }
    if(fabs(6.283185307179586 - angle < 1e-5)){
        TraceWarn("==========GPS data is inside=======\n");
        inside = 1;
    }
    if (inside == 0) {
        TraceWarn("==========GPS data is outside=======\n");
    }
    return inside;
}

int is_stop(gps_config_t *pconfig, lati_longi_cood_t p , time_t tmnow) {
    TraceInfo("lati %lf(<%lf), longi %lf(<%lf)\n", fabs(p.lati-last_cood.lati), 
            pconfig->park_sensitivity.lati, fabs(p.longi-last_cood.longi),
            pconfig->park_sensitivity.longi);

    if (fabs(p.lati-last_cood.lati) < pconfig->park_sensitivity.lati &&
        fabs(p.longi-last_cood.longi) < pconfig->park_sensitivity.longi) 
    {
        if (!park_time) 
        {
            last_parktimout_ts = tmnow;
            TraceWarn("stop care start---\n");
            park_time = 1;
            return 0;
        }
        park_time = abs(last_parktimout_ts - tmnow);
        TraceInfo("======parking time %d======\n", (uint32_t)park_time);
        if (park_time >= pconfig->timeout - 1) {
            last_cood = p;
            park_time = 0;
            last_parktimout_ts = 0;
            TraceWarn("stop car ok---\n");
            return 1;
        }

    } 
    else 
    {
        last_cood = p;
        park_time = 0;
        last_parktimout_ts = 0;
        TraceWarn("stop car clean---\n");
    }
    return 0;
}

void gps_fence_check(gps_config_t *pconfig) {
    lati_longi_cood_t cood;
    base_alarm_info_t info;
    uint32_t alarm_interval = 30;
    static time_t last_fence_alarm_tm = 0;
    
    if (!pconfig->enable)
        return;
    
    time_t tmnow = time(NULL);

    int status = 0;
    static int nogps_count = 0;
    pthread_mutex_lock(&gps_data_alm_check.mtx);
    cood.lati = gps_data_alm_check.data.latitude;
    cood.longi = gps_data_alm_check.data.longitude;
    status = gps_data_alm_check.status;
    pthread_mutex_unlock(&gps_data_alm_check.mtx);

    if (!status) 
    {
        if(++nogps_count > 180)
        {
            memset(&info, 0, sizeof(info));
            snprintf(info.SensorID, sizeof(info.SensorID), virtual_id_ttygps, 0);
            info.SensorType = ALM_DEV_TYPE_GPS;
            info.SensorAction = ALM_TYPE_GPS_ERR;
            info.time = tmnow;

            info.base_channel = 0;
            TraceErr("==============NO NEW GPS DATA==========\n");
            send_alarm_to_center(&info);
            nogps_count = 0;
        }
        return;
    }
    if (is_stop(pconfig, cood, tmnow)) {
        memset(&info, 0, sizeof(info));
        snprintf(info.SensorID, sizeof(info.SensorID), virtual_id_ttygps, 0);
        info.SensorType = ALM_DEV_TYPE_GPS;
        info.SensorAction = ALM_TYPE_GPS_PARK;
        info.time = tmnow;

        info.base_channel = 0;
        TraceErr("==============PARKING timeout ch %d==========\n", 0);
        send_alarm_to_center(&info);
    }

    if (pconfig->vertex_cnt >= 3)/* valid for fence alarm */
    {
        if (!is_inside(pconfig->geo_area, pconfig->vertex_cnt, cood)) {
            memset(&info, 0, sizeof(info));
            snprintf(info.SensorID, sizeof(info.SensorID), virtual_id_ttygps, 0);
            info.SensorType = ALM_DEV_TYPE_GPS;
            info.SensorAction = ALM_TYPE_GPS_FENCE;
            info.time = tmnow;
            info.base_channel = 0;
            if (alarm_handle->config.alarmintervalenable)
            {
                if (alarm_handle->config.alarminterval > 0)
                    alarm_interval = alarm_handle->config.alarminterval;
            }
            if (tmnow - last_fence_alarm_tm > alarm_interval - 1)/* interval check */
            {
                info.base_channel = 0;
                TraceErr("*************FENCE alarm ch %d************\n", 0);
                send_alarm_to_center(&info);
                last_fence_alarm_tm = tmnow;
            }
            else 
            {
                TraceInfo("fence alarm but not fit for interval %d\n", (int)(tmnow - last_fence_alarm_tm));
            }
        }
    }
}

static int32_t gps_alarm_config_load(gps_config_t *pconfig) {
    // load gps alarm config
    hm_bool_t ret;
    hm_config_t * pcfg = hm_config_open(GPS_CONFIG_FILE);
    if (!pcfg || !pconfig) {
        TraceErr("Open gps alarm cfg error\n");
        return -1;
    }

    float fval = 0.0;
    uint32_t timeout = 0;
    ret = hm_config_get_float(pcfg, "park_sens_lati", &fval);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }
    pconfig->park_sensitivity.lati = fval;
    ret = hm_config_get_float(pcfg, "park_sens_longi", &fval);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }
    pconfig->park_sensitivity.longi = fval;
    ret = hm_config_get_uint(pcfg, "park_timeout", &timeout);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }
    pconfig->timeout = timeout;
    ret = hm_config_get_uint(pcfg, "vertex_cnt", &pconfig->vertex_cnt);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }
    int i = 0;
    for(i = 0; i < pconfig->vertex_cnt; i++) {
        char point_x_name[16] = {0};
        char point_y_name[16] = {0};
        snprintf(point_x_name, sizeof(point_x_name), "point%d_lati", i+1);
        snprintf(point_y_name, sizeof(point_y_name), "point%d_longi", i+1);
        ret = hm_config_get_float(pcfg, point_x_name, &fval);
        if (!ret) {
            TraceErr("%s error\n", __FUNCTION__);
        }
        pconfig->geo_area[i].lati = fval;
        ret = hm_config_get_float(pcfg, point_y_name, &fval);
        if (!ret) {
            TraceErr("%s error\n", __FUNCTION__);
        }
        pconfig->geo_area[i].longi = fval;
    }
    hm_config_close(pcfg);
    return -1;
}

static int32_t gps_config_load(gps_t *config, gps_config_t* localconf) {
    if(config && localconf) {
        localconf->enable = config->enable;
        localconf->port = config->port;
        localconf->push_fr = config->push_fr;
        memcpy(localconf->ip, config->ip, sizeof(localconf->ip));
        TraceImport("GPS enable %d ip:port[%s:%d] pushfr %d\n", 
            localconf->enable, localconf->ip, localconf->port, localconf->push_fr);

        gps_alarm_config_load(localconf);
        return 0;
    }
    return -1;
}

static int32_t gps_config_save(gps_config_t *pconfig) {
    hm_bool_t ret;
    hm_config_t * pcfg = hm_config_open(GPS_CONFIG_FILE);
    if (!pcfg) {
        TraceErr("Open gps alarm cfg error\n");
        return -1;
    }
    ret = hm_config_set_float(pcfg, "park_sens_lati", pconfig->park_sensitivity.lati);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }
    ret = hm_config_set_float(pcfg, "park_sens_longi", pconfig->park_sensitivity.longi);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }
    ret = hm_config_set_uint(pcfg, "park_timeout", pconfig->timeout);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }

    ret = hm_config_set_uint(pcfg, "vertex_cnt", pconfig->vertex_cnt);
    if (!ret) {
        TraceErr("%s error\n", __FUNCTION__);
    }
    int i = 0;
    for(i = 0; i < pconfig->vertex_cnt; i++) {
        char point_x_name[16] = {0};
        char point_y_name[16] = {0};
        snprintf(point_x_name, sizeof(point_x_name), "point%d_lati", i+1);
        snprintf(point_y_name, sizeof(point_y_name), "point%d_longi", i+1);
        ret = hm_config_set_float(pcfg, point_x_name, pconfig->geo_area[i].lati);
        if (!ret) {
            TraceErr("%s error\n", __FUNCTION__);
        }
        ret = hm_config_set_float(pcfg, point_y_name, pconfig->geo_area[i].longi);
        if (!ret) {
            TraceErr("%s error\n", __FUNCTION__);
        }
    }
    hm_config_close(pcfg);
    return 0;
}

void *gps_alarm_check_pthread(void *arg) 
{
    pthread_detach(pthread_self());/* detached thread */

    gps_config_t check_conf = {0};
    for( ; ; )
    {
        gps_alarm_config_load(&check_conf);
        pthread_mutex_lock(&gps_config.mtx);
        check_conf.enable = gps_config.enable;
        pthread_mutex_unlock(&gps_config.mtx);
        
        gps_fence_check(&check_conf);/* gps fence alarm */
    
        usleep(1000*1000);
    }
    return NULL;
}

void thread(thread_rountine t)
{
    pthread_t pid;
    TraceImport("=pthread_create call=\n");
    pthread_create(&pid, NULL, t, NULL);
    TraceImport("THREAD %u started\n", (unsigned int)pid);
}

/**
 *@seperator relation to client functions
 */

static int32_t gps_ctrllist_init(void)
{
    pthread_mutex_lock(&gps_ctrl.list_mtx);
    gps_ctrl.pipe_list = pipe_list_new(
        128, sizeof(gps_info_t) * 128, NULL, (PListNodeHandler) hm_free);
    if (!gps_ctrl.pipe_list)
    {
        pthread_mutex_unlock(&gps_ctrl.list_mtx);
        return -1;
    }

    gps_ctrl.fd = pipe_list_get_read_pipe(gps_ctrl.pipe_list);

    list_init(&gps_ctrl.cb_list);
    gps_ctrl.inited = 1;
    pthread_mutex_unlock(&gps_ctrl.list_mtx);

    return 0;
}

int gps_fence_off(uint32_t vertex_cnt) {
    TraceWarn("vertex count %d\n", vertex_cnt);
    if (vertex_cnt <= 0) {
        
        pthread_mutex_lock(&gps_config.mtx);
        
        gps_config.vertex_cnt = 0;
        
        gps_config_save(&gps_config);
        pthread_mutex_unlock(&gps_config.mtx);
        return 0;
    }
    return -1;
}

int gps_set_fence(lati_longi_cood_t *area, uint32_t vertex_cnt) {
    
    if (area && vertex_cnt > 0) {
        if (vertex_cnt > GPS_VERTEX_COUNT) {
            TraceErr("Only %d gps vertex count support now\n", GPS_VERTEX_COUNT);
            return -1;
        }
        pthread_mutex_lock(&gps_config.mtx);
        memcpy(gps_config.geo_area, area, vertex_cnt * sizeof(lati_longi_cood_t));
        gps_config.vertex_cnt = vertex_cnt;
        
        int i = 0;
        for (; i < gps_config.vertex_cnt; i++)
        {
            TraceImport("GPS set fence[%d].[%d]  (lati%lf, longi%lf)\n", 
                gps_config.vertex_cnt, i,
                gps_config.geo_area[i].lati,
                gps_config.geo_area[i].longi);
        }
        gps_config_save(&gps_config);
        pthread_mutex_unlock(&gps_config.mtx);
        return 0;
    }
    return -1;
}

int gps_set_park_sensitivity(lati_longi_cood_t stop_sens, time_t timeout) {
    pthread_mutex_lock(&gps_config.mtx);
    gps_config.park_sensitivity = stop_sens;
    gps_config.timeout = timeout;
    TraceImport("GPS set park sensitivity, lati %lf, longi %lf, timeout %d\n",
        gps_config.park_sensitivity.lati,
        gps_config.park_sensitivity.longi,
        (int)gps_config.timeout);
    gps_config_save(&gps_config);
    pthread_mutex_unlock(&gps_config.mtx);
    return 0;
}

gps_config_t* gps_get_config(gps_config_t *buffer) {
    pthread_mutex_lock(&gps_config.mtx);
    memcpy(buffer, &gps_config, sizeof(gps_config));
    pthread_mutex_unlock(&gps_config.mtx);
    return buffer;
}

int gps_enable() {
    int enable = 0;
    pthread_mutex_lock(&gps_config.mtx);
    enable = gps_config.enable;
    pthread_mutex_unlock(&gps_config.mtx);
    return enable;
}

int32_t register_gps_cb(gps_cb fun)
{
    int32_t rst = -1;
    pthread_mutex_lock(&gps_ctrl.list_mtx);
    if (gps_ctrl.inited)
    {
        list_adddata(&gps_ctrl.cb_list, fun, NULL );
        rst = 0;
    }
    pthread_mutex_unlock(&gps_ctrl.list_mtx);
    return rst;
}

static void foreach_gps_callbacks(gps_cb fun, gps_info_t* info) {
    if (fun && info) {
        fun(info);
    }
}

static void send_gps(gps_info_t* info) {
    if (!info) {
        return;
    }
    TraceInfo("send gps info to NVS/CU\n");
    list_foreach2(&gps_ctrl.cb_list, (PListNodeHandler2) foreach_gps_callbacks, info);
}


/**
 *@seperator relation to gps procedure functions
 */

static int match_cmd(const char *p, const char *q1, const char *q2) {

    if (!strstr(p, "RMC") && !strstr(p, "GGA")) {
        //TraceErr("Not GPS data & drop it\n");
        return 0xff;
    }
    int match_ret1 = strcmp(p, q1);
    int match_ret2 = strcmp(p, q2);
    if (!match_ret1) {
        //TraceErr("Match GPS cmd ===%s===\n", q1);
        return match_ret1;
    }
    if (!match_ret2) {
        //TraceErr("Match GPS cmd ===%s===\n", q2);
        return match_ret2;
    }
    return 0xff;
}

static int  gps_gpgga( void ) {
    if (match_cmd(gps_buffer.cmd_list[0], GPS_CMD_GGA, GNS_CMD_GGA) != 0)
        return 0;

    gps_data.status = atoi(gps_buffer.cmd_list[6]);/* GPS status */

    switch(gps_data.status) {
        case 1:
        case 2:
            gps_data.status = 'A';
            //TraceInfo("GPS status %d\n", gps_data.status);
            break;
        default:
            gps_data.status = 'V';
            //TraceInfo("GPS data not valid, status %d\n", gps_data.status);
    }

    if (isdigit(gps_buffer.cmd_list[1][0])) {/* GPS time h:m:s.ms */
        sscanf(gps_buffer.cmd_list[1], "%02d%02d%02d.%03d",
            &gps_data.gps_tm.gps_hour, 
            &gps_data.gps_tm.gps_min, 
            &gps_data.gps_tm.gps_sec,
            &gps_data.gps_tm.gps_msec);
    }

    if (isdigit(gps_buffer.cmd_list[2][0])) {/* latitude */
        sscanf(gps_buffer.cmd_list[2], "%02d%lf", 
            &gps_data.lati_degree, 
            &gps_data.lati_cent);
        gps_data.latitude = gps_data.lati_degree + gps_data.lati_cent/60;
    }
    
    gps_data.NS = gps_buffer.cmd_list[3][0];/* North or South */
    
    if (isdigit(gps_buffer.cmd_list[4][0])) { /* longitude */
        sscanf(gps_buffer.cmd_list[4], "%03d%lf", 
            &gps_data.longi_degree, 
            &gps_data.longi_cent);
        gps_data.longitude = gps_data.longi_degree + gps_data.longi_cent/60;
    }
    
    gps_data.EW = gps_buffer.cmd_list[5][0];/* Eest or West */

    if (isdigit(gps_buffer.cmd_list[7][0])) {
        sscanf( gps_buffer.cmd_list[7], "%02d", &gps_data.satellite_count);
    }


    TraceWarn("GPS.latitude %lf, %02d:%lf\n", gps_data.latitude, gps_data.lati_degree,gps_data.lati_cent);
    TraceWarn("GPS.latitude %lf, %03d:%lf\n", gps_data.longitude,gps_data.longi_degree,gps_data.longi_cent);

    update_alm_gps_checking_data(&gps_data);
    gps_send_out();
    return 1;

}

static int gps_gprmc( void )
{
    int year;
    if (match_cmd(gps_buffer.cmd_list[0], GPS_CMD_RMC, GNS_CMD_RMC) != 0)
        return 0;
    
    gps_data.status = gps_buffer.cmd_list[2][0];/* GPS status */
    
    if (gps_data.status != 'A') {
        TraceErr("GPS data not valid, status %c\n", gps_data.status);
        //return;
    }

    if (isdigit(gps_buffer.cmd_list[1][0])) {/* GPS time h:m:s.ms */
        sscanf(gps_buffer.cmd_list[1], "%02d%02d%02d.%03d",
            &gps_data.gps_tm.gps_hour, 
            &gps_data.gps_tm.gps_min, 
            &gps_data.gps_tm.gps_sec,
            &gps_data.gps_tm.gps_msec);
    }

    if (isdigit(gps_buffer.cmd_list[9][0])) {/* GPS date */
        sscanf(gps_buffer.cmd_list[9], "%02d%02d%02d",
            &gps_data.gps_dt.gps_day, 
            &gps_data.gps_dt.gps_mon, 
            &year);
        gps_data.gps_dt.gps_year = year + 2000; 
    }

    if (isdigit(gps_buffer.cmd_list[3][0])) {/* latitude */
        sscanf(gps_buffer.cmd_list[3], "%02d%lf", 
            &gps_data.lati_degree,
            &gps_data.lati_cent);
        gps_data.latitude = gps_data.lati_degree + gps_data.lati_cent/60;
    }
    
    gps_data.NS = gps_buffer.cmd_list[4][0];/* North or South */
    
    if (isdigit(gps_buffer.cmd_list[5][0])) { /* longitude */
        sscanf(gps_buffer.cmd_list[5], "%03d%lf", 
            &gps_data.longi_degree, 
            &gps_data.longi_cent);
        gps_data.longitude = gps_data.longi_degree + gps_data.longi_cent/60;
    }
    
    gps_data.EW = gps_buffer.cmd_list[6][0];/* Eest or West */

    if (isdigit(gps_buffer.cmd_list[7][0])) { /* speed */
        gps_data.speed = atof(gps_buffer.cmd_list[7]);
    }

    if (isdigit(gps_buffer.cmd_list[8][0])) { /* direction */
        gps_data.direction = atof(gps_buffer.cmd_list[8]);
    }

    if (isdigit(gps_buffer.cmd_list[10][0])) { /* speed */
    }

    TraceWarn("GPS.longitude %lf, %03d:%lf\n", gps_data.longitude,gps_data.longi_degree,gps_data.longi_cent);
    TraceWarn("GPS.latitude %lf, %02d:%lf\n", gps_data.latitude,gps_data.lati_degree,gps_data.lati_cent);

    update_alm_gps_checking_data(&gps_data);
    gps_send_out();
    return 1;
}

static void gps_gpvtg( void )
{
    if(match_cmd(gps_buffer.cmd_list[0], GPS_CMD_VTG, GNS_CMD_VTG) != 0)
        return;

}

static void gps_gpgsa( void )
{
    if(match_cmd(gps_buffer.cmd_list[0], GPS_CMD_GSA, GNS_CMD_GSA) != 0)
        return;
    
}

static void gps_gpgsv( void )
{
    if(match_cmd(gps_buffer.cmd_list[0], GPS_CMD_GSV, GNS_CMD_GSV) != 0)
        return;
    
}

static int gps_checksum( void )
{
    int i;
    char *ptr1, *ptr2, cs, css[4];

    if (gps_buffer.frame[0] != GPS_START)
        return 0xff;
    ptr1 = &gps_buffer.frame[1]; /* '$' out of checking */
    ptr2 = strchr(gps_buffer.frame, GPS_CHECKSUM_MARK);
    if (!ptr2)
        return 0xff;
    cs = 0;
    for(i=0; i < (ptr2 - ptr1); i++)
    {
        cs ^= ptr1[i];
    }
    sprintf(css, "%02X", cs);
    //TraceInfo("GPS frame check sum value %s\n", css);
    ptr2 ++;
    return (strncmp(css, ptr2, 2) != 0);
}

static void gps_data_cache(gps_data_cache_t * pgdc) {
    gps_data_cache_t gdc_tmp;
    
    int fd = open(GPS_CACHE_FILE, O_RDWR | O_CREAT, 0777);
    if (fd < 0) {
        TraceErr("Open gps cache file(%s) error\n", GPS_CACHE_FILE);
        return;
    }
    if((flock(fd, LOCK_EX))< 0)
    {
        TraceErr("File lock failed\n");
    }

    int rlen = 0;
    int cache_result = 0;
    while((rlen = read(fd, &gdc_tmp, sizeof(gdc_tmp))) == sizeof(gdc_tmp)) {
        if (GPS_NA == gdc_tmp.smu_flag) {
            lseek(fd, -sizeof(gdc_tmp), SEEK_CUR);
            int len = write(fd, pgdc, sizeof(gps_data_cache_t));
            TraceImport("===CACHE=== data %d for SMU(rewrite)\n", len);
            cache_result = 1; /* cache ok */
            break;
        }
    }
    if (!cache_result) { /* cache failed & append new gps data */
        lseek(fd, 0, SEEK_END);
        int wlen = write(fd, pgdc, sizeof(gps_data_cache_t));
        TraceImport("===CACHE=== data %d for SMU(append)\n", wlen);
    }
    if((flock(fd, LOCK_UN))<0)
    {
        TraceErr("File unlock failed\n");
    }

    close(fd);
}

static void gps_send_out(void) {
    gps_data_cache_t gdc;
    gps_info_t gps_info;
    struct timeval tm_now;
    struct tm tm_struct;
    uint32_t push_fr = 10;
#if 0 /* push protocal */
    int32_t sockfd = -1;
    int32_t sended = 0;
    int32_t flags = 1;
    int32_t ret = 0;
    int32_t msg_len = 0;
    char push_buffer[4<<10] = {0};
#endif /* 0 off */
    gettimeofday(&tm_now, NULL);

    time_t gps_pts = gps_data.gps_tm.gps_hour*3600+gps_data.gps_tm.gps_min*60+gps_data.gps_tm.gps_sec;
    if (last_gps_data_pts == gps_pts) {
        TraceErr("GPS data not update yet, wait for new collection\n");
        return;
    }
    last_gps_data_pts = gps_pts;
    
    memset(&gdc, 0, sizeof(gdc));
    gdc.latitude = gps_data.latitude;
    gdc.longitude = gps_data.longitude;
    gdc.gps_msec = tm_now.tv_usec/1000;
    gdc.pts = tm_now.tv_sec;
    gdc.smu_flag = GPS_VALID;

    localtime_r(&tm_now.tv_sec, &tm_struct);
    gps_info.gps_dt.gps_year = tm_struct.tm_year + 1900;/* use system time */
    gps_info.gps_dt.gps_mon = tm_struct.tm_mon + 1;
    gps_info.gps_dt.gps_day = tm_struct.tm_mday;
    gps_info.gps_tm.gps_hour = tm_struct.tm_hour;
    gps_info.gps_tm.gps_min = tm_struct.tm_min;
    gps_info.gps_tm.gps_sec = tm_struct.tm_sec;
    gps_info.gps_tm.gps_msec = 0;

    gps_info.latitude = gps_data.latitude;
    gps_info.longitude = gps_data.longitude;
    gps_info.recording = 0;/* not initialized here, we handle it in client */
    gps_info.status = gps_data.status;
    gps_info.speed = gps_data.speed;

    hm_config_t * pcfg = hm_config_open(HM_GPS_NET_CONFIG);
    if(pcfg)
    {
        int ret = hm_config_get_uint(pcfg, "gps_push_fr", &push_fr);
        if (!ret) {
            TraceErr("%s:%d error\n", __FUNCTION__, __LINE__);
        }
    }
    hm_config_close(pcfg);
    
    TraceInfo("===GPS sendout==%d>%d ?====\n", (uint32_t)labs(gdc.pts-last_push_tm), (uint32_t)labs(push_fr - 1));
    if (labs(gdc.pts-last_push_tm) > labs(push_fr - 1)) {
        
        send_gps(&gps_info);
        
        gps_data_cache(&gdc);/* cache for smu */

        last_push_tm = gdc.pts;

#if 0 /* push protocal */
        // push gps info(huamai protocal) to netserver(configured on IE)
        sockfd = hm_connect(gps_config.ip, gps_config.port, GPS_CONNECT_TIMEOUT);
        if (sockfd < 0)
        {
            TraceErr("GPS push %s:%u connect failed.\n", gps_config.ip, gps_config.port);
            return;
        }

        ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &flags, sizeof(flags));
        if (ret)
        {
            TraceErr("GPS push %s:%d setsockopt failed.\n", gps_config.ip, gps_config.port);
            return;
        }

        msg_len = fill_extended_xmlmsg("GPS", push_buffer, sizeof(push_buffer), &gps_info);
        sended = hm_send(sockfd, push_buffer, msg_len, GPS_SEND_TIMEOUT);
        if (sended != msg_len)
        {
            TraceErr("GPS push information failed.\n");
        }
        else
        {
            TraceInfo("GPS push %s:%d %d bytes OK\n", gps_config.ip, gps_config.port, sended);
        }        
        close(sockfd);
#endif /* 0 off */
    }

    if (osd_server_socket > 0) {
        if (send_osd_msg(osd_server_socket) < 0) {
            close(osd_server_socket);
            osd_server_socket = hm_connect("127.0.0.1", 49998, 10);
            if(osd_server_socket > 0){
                fcntl(osd_server_socket, F_SETFL, fcntl(osd_server_socket, F_GETFL) | O_NONBLOCK);
                TraceWarn("Connect osd server success\n");
            }else{
                TraceErr("Connect osd server faild ret:%d\n", osd_server_socket);
            }
        }
    }
    else {
        osd_server_socket = hm_connect("127.0.0.1", 49998, 10);
        if(osd_server_socket > 0){
            fcntl(osd_server_socket, F_SETFL, fcntl(osd_server_socket, F_GETFL) | O_NONBLOCK);
            TraceWarn("Connect osd server success\n");
        }else{
            TraceErr("Connect osd server faild ret:%d\n", osd_server_socket);
        }
    }
}

static void gps_parse_frame( void ) {
    char *ptr1, *ptr2;
    int i = 0;
    if (gps_checksum())
    {
        //TraceErr("GPS frame checksum error\n");
        return;
    }
    //TraceInfo("GPS frame parse START\n");
    ptr1 = gps_buffer.frame;
    for( ; ; )
    {
        gps_buffer.cmd_list[i] = ptr1;
        ptr2 = strchr(ptr1, GPS_COMMA);
        if (!ptr2)
            break;
        *ptr2 = 0;
        ptr1 = ptr2;
        ptr1 ++;
        i ++;
    }
    //TraceInfo("GPS frame parse END\n");
    ptr1 = strchr(ptr1, GPS_CHECKSUM_MARK);
    if(!ptr1)
        return;
    *ptr1 = 0;
    if ( 0 == i )
        return;/* unrecogonized frame */

    if (gps_gpgga())
    {
        return;
    }
    if (gps_gprmc())
    {
        return;
    }
    
    gps_gpvtg();
    gps_gpgsa();
    gps_gpgsv();

}

static void gps_parse_buffer(void) {
    char *result = strtok(gps_buffer.buffer, GPS_CRLF);
    while(result) {
        memset(gps_buffer.frame, 0, sizeof(gps_buffer.frame));
        memcpy(gps_buffer.frame, result, strlen(result));
        
        //TraceInfo("GPS.frame: %s\n", gps_buffer.frame);
        
        gps_parse_frame();
        
        result = strtok(NULL, GPS_CRLF);
    }
}

/**
 * @brief Set baudrate of TTY.
 */
static int set_tty_baudrate(int fd, int baudrate) {
    int speed_arr[] = { B38400, B19200, B9600, B4800, B2400, B1200, B300 };
    int name_arr[] = { 38400, 19200, 9600, 4800, 2400, 1200, 300 };
    int i;
    int status;
    struct termios Opt;
    tcgetattr(fd, &Opt);
    for (i = 0; i < sizeof(speed_arr) / sizeof(int); i++) {
        if (baudrate == name_arr[i]) {
            tcflush(fd, TCIOFLUSH);
            cfsetispeed(&Opt, speed_arr[i]);
            cfsetospeed(&Opt, speed_arr[i]);
            status = tcsetattr(fd, TCSANOW, &Opt);
            if (status != 0) {
                TraceErr("tcsetattr set baudrate error\n");
                return -1;
            }
            tcflush(fd, TCIOFLUSH);
        }
    }
    return 0;
}

/**
 *@brief Set parameters of TTY.
 *@param fd(int) TTY fd
 *@param databits(int) data bits(7 | 8)
 *@param stopbits(int) stop bits(1 | 2)
 *@param parity(int) parity-check bits(N,E,O,S)
 */
static int 
set_tty_parameters(int fd, int databits, int stopbits, int parity) {
    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        TraceErr("tcgetattr encounter an error\n");
        return (FALSE);
    }
    options.c_cflag &= ~CSIZE;
    switch (databits) {
        case 7:
            options.c_cflag |= CS7;
            break;
        case 8:
            options.c_cflag |= CS8;
            break;
        default:
            fprintf(stderr, "Unsupported data size\n");
            return (FALSE);
    }
    switch (parity) {
        case 'n':
        case 'N':
            options.c_cflag &= ~PARENB; /* Clear parity enable */
            options.c_iflag &= ~INPCK; /* Enable parity checking */
            break;
        case 'o':
        case 'O':
            options.c_cflag |= (PARODD | PARENB); 
            options.c_iflag |= INPCK; /* Disnable parity checking */
            break;
        case 'e':
        case 'E':
            options.c_cflag |= PARENB; /* Enable parity */
            options.c_cflag &= ~PARODD; /* Converted to even parity check */
            options.c_iflag |= INPCK; /* Disnable parity checking */
            break;
        case 'S':
        case 's': /*as no parity*/
            options.c_cflag &= ~PARENB;
            options.c_cflag &= ~CSTOPB;
            break;
        default:
            fprintf(stderr, "Unsupported parity\n");
            return (FALSE);
    }

    switch (stopbits) {
        case 1:
            options.c_cflag &= ~CSTOPB;
            break;
        case 2:
            options.c_cflag |= CSTOPB;
            break;
        default:
            fprintf(stderr, "Unsupported stop bits\n");
            return (FALSE);
    }
    if (parity != 'n') {
        options.c_iflag |= INPCK;
    }

    /* Set input parity option */
    options.c_lflag  &= ~ICANON;
    tcflush(fd, TCIFLUSH);
    options.c_cc[VTIME] = 0; /* Timeout 15 seconds */
    options.c_cc[VMIN] = 0; /* Update the options and do it NOW */
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        TraceErr("tcsetattr encounter an error\n");
        return (FALSE);
    }
    return (TRUE);
}

static size_t 
timeout_read(int fd, void *buf, size_t nbytes, unsigned int timout) {
    int nfds;
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = timout;
    tv.tv_usec = 0;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    nfds = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (nfds <= 0) {
        if (nfds == 0)
            errno = ETIME;
        return (-1);
    }
    return (read(fd, buf, nbytes));
}

static size_t 
timeout_readn(int fd, void *buf, size_t nbytes, unsigned int timout) {
    size_t nleft;
    ssize_t nread;

    nleft = nbytes;

    while (nleft > 0) {
        if ((nread = timeout_read(fd, buf, nleft, timout)) < 0) {
            if (nleft == nbytes)
                return (-1); /* error, return -1 */
            else
                break; /* error, return amount read so far */
        } else if (nread == 0) {
            break; /* EOF */
        }
        nleft -= nread;
        buf += nread;
    }
    return (nbytes - nleft); /* return >= 0 */
}

void data_init() {
    TraceImport("Initialize gps data memory\n");
    memset(&gps_buffer, 0, sizeof(gps_buffer));
    memset(gps_buffer.cmd_list, 0, TTY_GPS_CMD_COUNT);
    memset(&gps_data, 0, sizeof(gps_data));
    memset(&gps_config, 0, sizeof(gps_config));
    pthread_mutex_init(&gps_config.mtx, NULL);
    memset(&last_cood, 0, sizeof(last_cood));

    memset(&gps_data_alm_check.data, 0, sizeof(gps_data_alm_check.data));
    gps_data_alm_check.status = 0;
}

static uint64_t hl64ton(uint64_t host)
{
    uint64_t ret = 0;
    uint32_t high = 0;
    uint32_t low = 0;

    low = host & 0xFFFFFFFF;
    high = (host >> 32) & 0xFFFFFFFF;
    low = htonl(low);
    high = htonl(high);
    ret = low;
    ret <<= 32;
    ret |= high;

    return ret;
}
static int send_osd_msg(int sock)
{
    int i =0;
    int ret = 0;
    for(i = 0; i < hm_env->video_channels; i++) {
        //TraceInfo("Device chnum %d, sending ch %d\n", hm_env->video_channels, i);
        bzero(&osd_gps, sizeof(osd_gps));
        osd_gps.channel = htonl(i);
        osd_gps.spts = hl64ton(av_gettime());
        osd_gps.epts = hl64ton(osd_gps.spts + 30 * 1000);/* 30 s */
        osd_gps.id = htonl((1<<16) | 0x1);/* 0-add new */
        osd_gps.osd_type = htonl(OSD_TYPE);
        uint32_t color = rgb_color(0xff,0xff,0x80);
        color = htonl(color) >> 8;
        //TraceInfo("OSD color %#x\n", color);
        uint32_t len = sprintf(osd_gps.str, 
            "<?xml version=\"1.0\" encoding=\"utf-8\"?> \r\n"
            "<OSD> \r\n"
            "<Font_Name>ËÎÌו</Font_Name>\r\n"
            "<Postion_X>%d</Postion_X>"
            "<Postion_Y>%d</Postion_Y>"
            "<Font_Heigth>16</Font_Heigth>\r\n"
            "<Font_Width>12</Font_Width>\r\n"
            "<Text_Color>%d</Text_Color>\r\n"
            "<Context>%s:%s##%s:%lf##%s:%lf##%s:%lf</Context>  \r\n"
            "</OSD> \r\n", 
            20, 200, color, 
            GPS_UTF8_OSD_STATUS,
            gps_data.status == 'A' ? GPS_UTF8_STATUS_LOCATED : GPS_UTF8_STATUS_LOCATE_NA,
            GPS_UTF8_OSD_LONGI, gps_data.longitude, 
            GPS_UTF8_OSD_LATI, gps_data.latitude,
            GPS_UTF8_OSD_SPEED, gps_data.speed * 1.852);
        TraceInfo("Real osd len %d\n", len);
        osd_gps.str_len = htonl(len+28);//why 28, depends protocal

        ret = hm_send(sock, &osd_gps, sizeof(osd_gps)-sizeof(osd_gps.str)+len, 10);

        //TraceInfo("send ret = %d, send buffer:\r\n%s\r\n\n", ret, osd_gps.str);

    }
    return ret;
}

void gps_data_rdloop(int fd)
{
    int rlen = 0;
    uint32_t watchdog_task_id = 0;
    watchdog_task_id = watchdog_tick_register("gps_thread");

    for( ; ; ) {
        
        watchdog_tick(watchdog_task_id);
        
        //TraceImport("data loop in\n");
        memset(gps_buffer.buffer, 0, TTY_GPS_DATA_LEN);
        
        tcflush(fd, TCIOFLUSH);/* clean TTY IO buffer */
        //TraceImport("data loop before read\n");
        rlen = timeout_readn(fd, gps_buffer.buffer, TTY_GPS_DATA_LEN, 1);
        //TraceImport("data loop after read\n");
        tcflush(fd, TCIOFLUSH);/* clean TTY IO buffer */
        
        if (rlen <= 0) {
            TraceErr("Read data error\n");
            continue;
        }
        else {
            //TraceInfo("Read tty data length %d:\n%s\n", rlen, gps_buffer.buffer);
            //TraceImport("data loop before parser\n");
            gps_parse_buffer();
            //TraceImport("data loop after parser\n");

        }
        
        tcflush(fd, TCIOFLUSH);/* clean TTY IO buffer */

        //TraceImport("data loop out\n");
 
    }
    if (osd_server_socket > 0) {
        close(osd_server_socket);
    }
}

/**
 * @brief: Read gps data from TTY and send to CU & PLATFORM
 */
void* tty_gps_thread(void *args) {
    int ttyfd = -1;
    
    data_init();
    
    gps_ctrllist_init();

    gps_config_load(args, &gps_config);

    thread(gps_alarm_check_pthread);

    char gps_tty_dev[HM_MAX_URL_LEN] = {0};
    uint32_t gps_tty_baud = 9600;
    
    if (!hm_hardware->gps_s) {
        strncpy(gps_tty_dev, TTY_GPS_DEV, sizeof(TTY_GPS_DEV));
        TraceImport("No gpstty config found in hardware config.\n");
    }
    else {
        strncpy(gps_tty_dev, hm_hardware->gps_s, sizeof(gps_tty_dev));
        gps_tty_baud = hm_hardware->gps_b;
    }
    
    TraceImport("TTY device %s, baudrate %d\n", gps_tty_dev, gps_tty_baud);

    ttyfd = open(gps_tty_dev, O_RDWR | O_NOCTTY);/* open tty device */
    if (ttyfd < 0) {

        TraceErr("Open tty device %s for gps failed.\n",gps_tty_dev);
        return NULL ;
    }

    set_tty_baudrate(ttyfd, gps_tty_baud);
    
    if (set_tty_parameters(ttyfd, 8, 1, 'N') == FALSE) {
        TraceErr("Set tty device parameters error!\n");
        return NULL;
    }

    pthread_detach(pthread_self());/* detached thread */

    gps_data_rdloop(ttyfd);
    
    close(ttyfd);
    return NULL;
}




#ifndef __SK_HPP__
#define __SK_HPP__

#include "logger.h"
#include <vector>
#include <opencv2/core/core.hpp>


extern logger_id_t LOGID;

#define CMD_INFO        "info"
#define CMD_STORE 		"store"
#define CMD_ALARM       "alarm"
#define CMD_ALARM_CNTR  "alarm_cntr"
#define CMD_TRACK       "track"
#define CMD_TRACK_FACE  "track_face"
#define CMD_TRACK_BODY  "track_body"
#define CMD_RESET 		"reset"
#define CMD_STATS       "stats"
#define CMD_CLEAN       "clean"
#define CMD_SHUTDOWN    "shutdown"

#define MSG_ALARM       "alarm"
#define MSG_OBJ       	"object"
#define MSG_FACE       	"face"
#define MSG_BODY       	"body"
#define MSG_VCAPTURE	"vcap"
#define MSG_NVIDEO		"nvideo"
#define MSG_SHUTDOWN    "shutdown"

#define INIT_BLOB_ID  	1000000


typedef enum {
	no_direct = -1,		/* No direction */
	right_direct = 0,	/* From right to left */
	up_direct = 1,      /* From upside to down */
	left_direct = 2,    /* From left to right */
	down_direct = 3,    /* From down to upside */
} enter_direct;


typedef struct {
	int vs_type;
	cv::Rect rect;
	cv::Point pnts[32];
	int pn;
	int direct;
} vs_item_t;


typedef struct {
	float confidence;
	float center_x;
	float center_y;
	float width;
} face_rect_t;


typedef struct {
	int blob_id;
	cv::Rect track_win;
	cv::Mat hist;
	float confidence;
	int suspicious;
    int x_var;
    int y_var;
} ms_track_blob;


typedef std::vector<ms_track_blob> ms_blob_vector;

typedef std::vector<cv::Rect> rect_vector;

typedef std::vector<vs_item_t> vs_item_vector;


#define RECT_CENTER(r)			cv::Point((r).x+(r).width/2, (r).y+(r).height/2)
#define RECT_CONTAIN(r1,r2)     ( r1.x<=r2.x && (r1.x+r1.width) >= (r2.x+r2.width) && r1.y<=r2.y && (r1.y+r1.height) >= (r2.y+r2.height) )

#define UPDATE_BLOB_MVAR(b, r)   {   \
    cv::Point p = RECT_CENTER( (b).track_win );  \
    cv::Point c = RECT_CENTER( (r) );       \
    (b).x_var = fmax( (b).x_var, abs(c.x-p.x) ); \
    (b).y_var = fmax( (b).y_var, abs(c.y-p.y) ); \
}

#define MVAR_RANGE  10


#define sk_debug(...)		logger_implementation(LOGID, LOGGER_DEBUG,	 __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define sk_info(...)		logger_implementation(LOGID, LOGGER_INFO, 	 __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define sk_notice(...)		logger_implementation(LOGID, LOGGER_NOTICE,	 __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define sk_warning(...)		logger_implementation(LOGID, LOGGER_WARNING, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define sk_error(...)		logger_implementation(LOGID, LOGGER_ERR,	 __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define sk_crit(...)		logger_implementation(LOGID, LOGGER_CRIT,	 __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define sk_alert(...)		logger_implementation(LOGID, LOGGER_ALERT,	 __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define sk_emerg(...)		logger_implementation(LOGID, LOGGER_EMERG,	 __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)


#define print_rect(m, r) { sk_debug("%s (%d,%d,%d,%d)\n", (m), (r).x, (r).y, (r).width, (r).height); }
#define print_blob(m, b) { sk_debug("%s %d - (%d,%d,%d,%d)\n", (m), (b).blob_id, (b).track_win.x,(b).track_win.y,(b).track_win.width,(b).track_win.height); }


#endif // __SK_HPP__



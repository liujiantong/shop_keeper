#ifndef __SK_STATS_HPP__
#define __SK_STATS_HPP__

#include "sk.hpp"
#include "sqlite3.h"
#include <time.h>


#define default_db_file "shop_keeper.db"
#define new_face_type   0
#define lost_face_type  1

#define new_video_type   0
#define end_video_type   1


class sk_stats_sqlite {
public:
	static void create_db_if_need(char *db_dir);

	static void insert_new_blob(int bid);
	static void insert_lost_blob(int bid);
	static void insert_blobs(std::vector<int> bids, int type);
	static int count_blobs_today();
	static void read_blobs_today(std::vector<std::string> &blob_log);

	static void insert_nvideo(char *vfile, int vtype);

	static void clean_history(int nday_before);

private:
	static inline void insert_one(sqlite3 *db, int bid, int type);

	static inline char *get_today() {
		time_t now;
    	struct tm *localnow;
    	char *date = (char *)calloc(32, sizeof(char));
    	time(&now);
    	localnow = localtime(&now);
    	strftime(date, 32, "%Y-%m-%d", localnow);
    	return date;
	};

};

#endif // __SK_STATS_HPP__


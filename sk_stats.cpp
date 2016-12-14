#include "sk_stats.hpp"
#include <time.h>
#ifdef __WINDOWS__
#include "iconv.h"
#endif

#define create_tbl_sql	"CREATE TABLE IF NOT EXISTS track_faces (blob_id INTEGER,blob_type INTEGER,show_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP);" \
						"CREATE TABLE IF NOT EXISTS video_files (vfile_name VARCHAR(255),vtype INTEGER,vtime TIMESTAMP DEFAULT CURRENT_TIMESTAMP);"

#define new_face_sql	"INSERT INTO track_faces (blob_id, blob_type) VALUES ($blob_id, $blob_type);"
#define new_video_sql   "INSERT INTO video_files (vfile_name, vtype) VALUES ($vfile, $vtype);"

#define count_face_sql 	"SELECT COUNT(blob_id) AS blob_cnt FROM track_faces WHERE blob_type=0 AND show_time>%s AND show_time<CURRENT_TIMESTAMP"
#define select_face_sql "SELECT show_time FROM track_faces WHERE blob_type=0 AND show_time>%s AND show_time<CURRENT_TIMESTAMP"
//#define select_face_sql "SELECT datetime(show_time, 'localtime') FROM track_faces WHERE blob_type=0 AND show_time>%s AND show_time<CURRENT_TIMESTAMP"

#define clean_face_sql  "DELETE FROM track_faces WHERE show_time<'%s';"
#define clean_video_sql "DELETE FROM video_files WHERE vtime<'%s';"


#define BEGIN_TX(db)  sqlite3_exec(db, "BEGIN", 0, 0, 0)
#define COMMIT_TX(db) sqlite3_exec(db, "COMMIT", 0, 0, 0)


static sqlite3 *db;
static char db_file[128];


#ifdef __WINDOWS__
inline int _code_convert(const char *from_charset, const char *to_charset, const char *inbuf, int inlen, char *outbuf, int outlen) {
    iconv_t cd;
    const char **pin = &inbuf;
    char **pout = &outbuf;

    cd = iconv_open(to_charset, from_charset);
    if (cd == 0) return -1;
    
    memset(outbuf, 0, outlen);
    if (iconv(cd, pin, (size_t *)&inlen, pout, (size_t *)&outlen) == -1) {
        iconv_close(cd);
        return -1;
    }
    
    iconv_close(cd);
    return 0;
}

inline int u2g(char *inbuf, int inlen, char *outbuf, int outlen) {
    return _code_convert("utf-8", "gbk", inbuf, inlen, outbuf, outlen);
}

inline int g2u(char *inbuf, size_t inlen, char *outbuf, size_t outlen) {
    return _code_convert("gbk", "utf-8", inbuf, inlen, outbuf, outlen);
}

inline int detgb(char *str, const int len) {
    if (len < 2) return 0;
    
    char *str_ptr = str;
    int idx = 0;
    
    while (idx < len-1) {
		unsigned strv = *str_ptr & 0x00FF;
		// sk_info("idx char: 0x%02x\n", strv);

		if (strv < 0xA1) {
            idx++; str_ptr++;
			continue;
        }
        
		if ((strv > 0xA1 && strv < 0xF7)) {
			strv = *(str_ptr + 1) & 0x00FF;
            if (strv > 0xA0 && strv < 0xFE) {
                //idx += 2; str_ptr += 2;
                return 1;
            }
            else {
                idx++; str_ptr++;
            }
        }
    }
    
    return 0;
}
#endif


void sk_stats_sqlite::create_db_if_need(char *db_dir) {
    int rc = 0;
    
#ifndef __WINDOWS__
    snprintf(db_file, 128, "%s/%s", db_dir, default_db_file);
#else
    int gbc = detgb(db_dir, strlen(db_dir));
    
	if (gbc > 0) {
		sk_debug("detect gb encoding\n");
		char tmp_buf[128];
		snprintf(tmp_buf, 128, "%s/%s", db_dir, default_db_file);
		rc = g2u(tmp_buf, strlen(tmp_buf), db_file, 128);
		if (rc != 0) {
			sk_error("db_file g2u error\n");
			return;
		}
	}
	else {
		snprintf(db_file, 128, "%s/%s", db_dir, default_db_file);
	}
#endif
    
    rc = sqlite3_open(db_file, &db);
	if (rc) {
  		sk_error("Can't open database: %s\n", sqlite3_errmsg(db));
  		sqlite3_close(db);
		return;
	}

	BEGIN_TX(db);
	(SQLITE_OK == sqlite3_exec(db, create_tbl_sql, 0, 0, 0));
	COMMIT_TX(db);

	sqlite3_close(db);
} // sk_stats_sqlite::create_db_if_need


void sk_stats_sqlite::insert_blobs(std::vector<int> bids, int type) {
	if (bids.empty()) return;
	// if ( !(SQLITE_OK == sqlite3_open(default_db_file, &db)) ) {
    if (SQLITE_OK != sqlite3_open(db_file, &db)) {
		sk_error("sqlite3_open error\n");
		return;
	}

	sqlite3_stmt* face_insert_stmt = 0;
	if (SQLITE_OK != sqlite3_prepare_v2(db, new_face_sql, sizeof(new_face_sql), &face_insert_stmt, 0)) {
		sqlite3_close(db);
		return;
	}

	BEGIN_TX(db);
	std::vector<int>::const_iterator itr = bids.begin();
	for (; itr != bids.end(); ++itr) {
		if (*itr <= 0) continue;

		sqlite3_bind_int(face_insert_stmt, 1, *itr);
		sqlite3_bind_int(face_insert_stmt, 2, type);
		(SQLITE_DONE == sqlite3_step(face_insert_stmt));

		sqlite3_reset(face_insert_stmt);
		//sqlite3_clear_bindings(face_insert_stmt);
	}
	COMMIT_TX(db);

	sqlite3_finalize(face_insert_stmt);
	sqlite3_close(db);
} // sk_stats_sqlite::insert_blobs


inline void sk_stats_sqlite::insert_one(sqlite3 *db, int bid, int type) {
	if (bid <= 0) return;

	sqlite3_stmt* face_insert_stmt = 0;
	if (SQLITE_OK != sqlite3_prepare_v2(db, new_face_sql, sizeof(new_face_sql), &face_insert_stmt, 0)) {
		return;
	}

	BEGIN_TX(db);
	sqlite3_bind_int(face_insert_stmt, 1, bid);
	sqlite3_bind_int(face_insert_stmt, 2, type);

	(SQLITE_DONE == sqlite3_step(face_insert_stmt));
	COMMIT_TX(db);

	sqlite3_finalize(face_insert_stmt);
} // sk_stats_sqlite::insert_one


void sk_stats_sqlite::insert_new_blob(int bid) {
	// if (SQLITE_OK == sqlite3_open(default_db_file, &db)) {
    if (SQLITE_OK == sqlite3_open(db_file, &db)) {
		insert_one(db, bid, new_face_type);
		sqlite3_close(db);
	}
}


void sk_stats_sqlite::insert_lost_blob(int bid) {
	// if (SQLITE_OK == sqlite3_open(default_db_file, &db)) {
    if (SQLITE_OK == sqlite3_open(db_file, &db)) {
		insert_one(db, bid, lost_face_type);
		sqlite3_close(db);
	}
}


int sk_stats_sqlite::count_blobs_today() {
	// if ( !(SQLITE_OK == sqlite3_open(default_db_file, &db)) ) {
    if (SQLITE_OK != sqlite3_open(db_file, &db)) {
		sk_error("sqlite3_open error\n");
		return 0;
	}

	char *tday = get_today();
	char the_sql[256];
	snprintf(the_sql, 256, count_face_sql, tday);
	free(tday);

	int count = 0;
	sqlite3_stmt* count_select_stmt = 0;
	if (SQLITE_OK != sqlite3_prepare_v2(db, the_sql, sizeof(the_sql), &count_select_stmt, 0)) {
		sqlite3_close(db);
		return 0;
	}

	if (sqlite3_step(count_select_stmt) == SQLITE_ROW) {
		count = sqlite3_column_int(count_select_stmt, 0);
	}

	sqlite3_finalize(count_select_stmt);
	sqlite3_close(db);
	return count;
} // sk_stats_sqlite::sum_blobs_today


void sk_stats_sqlite::read_blobs_today(std::vector<std::string> &blob_log) {
	// if ( SQLITE_OK != sqlite3_open(default_db_file, &db) ) {
    if ( SQLITE_OK != sqlite3_open(db_file, &db) ) {
		sk_error("sqlite3_open error\n");
		return;
	}

	char *tday = get_today();
	char the_sql[256];
	snprintf(the_sql, 256, select_face_sql, tday);
	free(tday);

	sqlite3_stmt* select_stmt = 0;
	if (SQLITE_OK != sqlite3_prepare_v2(db, the_sql, sizeof(the_sql), &select_stmt, 0)) {
		sqlite3_close(db);
		return;
	}

	if (sqlite3_step(select_stmt) == SQLITE_ROW) {
		char *stime = (char *)sqlite3_column_text(select_stmt, 0);
		blob_log.push_back(std::string(stime));
	}

	sqlite3_finalize(select_stmt);
	sqlite3_close(db);
} // sk_stats_sqlite::read_blobs_today


void sk_stats_sqlite::insert_nvideo(char *vfile, int vtype) {
	if (vfile == NULL) return;
	// if (SQLITE_OK == sqlite3_open(default_db_file, &db)) {
    if (SQLITE_OK == sqlite3_open(db_file, &db)) {
		sqlite3_stmt* insert_stmt = 0;
		if (SQLITE_OK != sqlite3_prepare_v2(db, new_video_sql, sizeof(new_video_sql), &insert_stmt, 0)) {
			sqlite3_close(db);
			return;
		}

		BEGIN_TX(db);
		sqlite3_bind_text(insert_stmt, 1, vfile, strlen(vfile), NULL);
		sqlite3_bind_int(insert_stmt, 2, vtype);

		(SQLITE_DONE == sqlite3_step(insert_stmt));
		COMMIT_TX(db);

		sqlite3_finalize(insert_stmt);
		sqlite3_close(db);
	}
} // sk_stats_sqlite::insert_nvideo


#define SEC_PER_DAY  24 * 60 * 60
void sk_stats_sqlite::clean_history(int nday_before) {
	// if (SQLITE_OK != sqlite3_open(default_db_file, &db)) return;
    if (SQLITE_OK != sqlite3_open(db_file, &db)) return;

	int secs = nday_before * SEC_PER_DAY;
	time_t atime = (time_t)(time(NULL) - secs);
	struct tm *localday = localtime(&atime);

	char adate[64];
    strftime(adate, 64, "%Y-%m-%d", localday);
	char sql_buf[256], sql_buf1[256];

	BEGIN_TX(db);
	snprintf(sql_buf, 256, clean_face_sql, adate);
    (SQLITE_OK == sqlite3_exec(db, sql_buf, 0, 0, 0));

	snprintf(sql_buf1, 256, clean_video_sql, adate);
    (SQLITE_OK == sqlite3_exec(db, sql_buf1, 0, 0, 0));
	COMMIT_TX(db);

	sqlite3_close(db);
} // sk_stats_sqlite::clean_history


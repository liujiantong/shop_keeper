#ifndef __SK_BLOB_TRACKER_HPP__
#define __SK_BLOB_TRACKER_HPP__

#include "sk.hpp"
#include "sk_stats.hpp"


class ms_blob_tracker {
public:
	ms_blob_tracker(int ipcn) : zmq_push(0), ipc_num(ipcn) {
		//sk_debug("ms_blob_tracker started\n");
		the_blobs.reserve(16);
		the_blobs_tocheck.reserve(8);
		//sk_stats_sqlite::create_db_if_need();
		next_blob_id = ipc_num * INIT_BLOB_ID; 
	};

	virtual ~ms_blob_tracker() {
	};


	void process(cv::Mat &image, const rect_vector &in_rects, rect_vector &out_rects);

	int track_faces(cv::Mat &image, const rect_vector &v, vs_item_t &vs_itm);

	void clean();

	inline void add_blob(ms_track_blob &blob) {
		the_blobs.push_back(blob);
	};

	inline bool has_blobs() {
		return !the_blobs.empty();
	};

	inline ms_blob_vector get_blobs() {
		return the_blobs;
	};

    // return blobs and remove them from the_blobs_tocheck
	inline void get_blobs_tocheck(ms_blob_vector &bv) {
		bv = the_blobs_tocheck;
		the_blobs_tocheck.clear();
	};
	inline void set_push(void *zpush) {
		zmq_push = zpush;
	};

	void add_blobs_tocheck(ms_blob_vector &bs);

private:
	ms_blob_vector the_blobs;
	ms_blob_vector the_blobs_tocheck;
	void *zmq_push;
	int next_blob_id;
	int ipc_num;
};

#endif // __SK_BLOB_TRACKER_HPP__


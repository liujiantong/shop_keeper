#include <stdio.h>
#include <czmq.h>


int main(int argc, char **argv) {
	zrex_t *rex = zrex_new ("\\d+-\\d+-\\d+");
	assert (rex);
	assert (zrex_valid (rex));
	bool matches = zrex_matches (rex, "123-456-789");
	assert (matches);
	assert (zrex_hits (rex) == 1);
	assert (streq (zrex_hit (rex, 0), "123-456-789"));
	assert (zrex_hit (rex, 1) == NULL);
	zrex_destroy (&rex);

	//  Here we pick out hits using capture groups
	rex = zrex_new ("(\\d+)-(\\d+)-(\\d+)");
	assert (rex);
	assert (zrex_valid (rex));
	matches = zrex_matches (rex, "123-456-ABC");
	assert (!matches);
	matches = zrex_matches (rex, "123-456-789");
	assert (matches);
	assert (zrex_hits (rex) == 4);
	assert (streq (zrex_hit (rex, 0), "123-456-789"));
	assert (streq (zrex_hit (rex, 1), "123"));
	assert (streq (zrex_hit (rex, 2), "456"));
	assert (streq (zrex_hit (rex, 3), "789"));
	zrex_destroy (&rex);

#define RTSP_PATTERN  "(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)"
	rex = zrex_new(RTSP_PATTERN);
	matches = zrex_matches(rex, "rtsp://192.168.1.112:554");
	assert(matches);
	matches = zrex_matches(rex, "rtsp://admin:admin@192.168.1.112:554");
	assert(matches);
	assert(zrex_hits(rex) == 5);
	//printf("%s\n", zrex_hit(rex, 0));
	assert(streq(zrex_hit(rex, 0), "192.168.1.112"));
	assert(streq(zrex_hit(rex, 1), "192"));
	assert(streq(zrex_hit(rex, 2), "168"));
	assert(streq(zrex_hit(rex, 3), "1"));
	assert(streq(zrex_hit(rex, 4), "112"));

	return 0;
}

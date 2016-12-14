#include <czmq.h>
#include <stdio.h>
//#include <unistd.h>
#include <assert.h>

#include "sk.hpp"

#define DEFAULT_ADMIN_PORT   8964


int main(int argc, char** argv) {
	int adm_port = DEFAULT_ADMIN_PORT;
    if (argc == 3) {
        int p = atoi(argv[2]);
        if (p > 0 && strcmp("-p", argv[1]) == 0)
            adm_port = p;
    }

    char url_buf[64];
    snprintf(url_buf, 64, "tcp://localhost:%d", adm_port);
    
    zctx_t *context = zctx_new();
    void *requester = zsocket_new(context, ZMQ_REQ);
    int rc = zsocket_connect(requester, url_buf);

    zstr_send(requester, "reset");
    char *rsp_str = zstr_recv(requester);
    assert(rsp_str != NULL);
    zstr_free(&rsp_str);

    zclock_sleep(500);

    zstr_send(requester, "shutdown");
    rsp_str = zstr_recv(requester);
    assert(rsp_str != NULL);
    zstr_free(&rsp_str);

    zctx_destroy(&context);
    zclock_sleep(100);
}


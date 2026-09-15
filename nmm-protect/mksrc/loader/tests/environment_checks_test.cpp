#include "EnvironmentChecks.h"
#include <arpa/inet.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
static void check(bool value) { if (!value) std::abort(); }
int main() {
    char previous[16] = {};
    check(prctl(PR_GET_NAME, previous) == 0);
    check(prctl(PR_SET_NAME, "nmmp-test") == 0);
    check(nmmpCheckThreadNames() == NMMP_CHECK_PASS);
    check(prctl(PR_SET_NAME, "GuM-Js-LoOp") == 0);
    check(nmmpCheckThreadNames() == NMMP_CHECK_SIGNAL);
    check(prctl(PR_SET_NAME, previous) == 0);
    check(nmmpProbeLoopbackPort(0) == NMMP_CHECK_NOT_APPLICABLE);
    int server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    check(server >= 0);
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(bind(server, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == 0);
    socklen_t length = sizeof(address);
    check(getsockname(server, reinterpret_cast<struct sockaddr *>(&address), &length) == 0);
    const uint16_t port = ntohs(address.sin_port);
    check(port != 0 && nmmpProbeLoopbackPort(port) == NMMP_CHECK_PASS);
    check(listen(server, 1) == 0);
    check(nmmpProbeLoopbackPort(port) == NMMP_CHECK_SIGNAL);
    close(server);
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Test packet dispatch without opening the live WM socket or taking the TTY. */
#include "../src/cwm_protocol.c"

static char packet[300];

static void create_packet(uint32_t width, uint32_t height, uint32_t scale)
{
    memset(packet, 0, sizeof(packet));
    packet[0] = CWM_PRT_TYPE_CREATE_WINDOW;
    put_u32be((uint8_t *)packet + 1, 40);
    put_u32be((uint8_t *)packet + 5, 40);
    put_u32be((uint8_t *)packet + 9, width);
    put_u32be((uint8_t *)packet + 13, height);
    memcpy(packet + 17, "test", 5);
    put_u32be((uint8_t *)packet + 22, scale);
    server.recv_len = 26;
}

int main(void)
{
    int peers[2];

    assert(windows_init() == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, peers) == 0);
    server.recv_buffer = packet;

    create_packet(100, 80, CWM_RENDER_NATIVE);
    server.recv_len = 17;
    assert(process_create_window(peers[0]) < 0);

    create_packet(100, 80, CWM_RENDER_NATIVE);
    packet[21] = 'x';
    assert(process_create_window(peers[0]) < 0);

    create_packet(100, 80, CWM_RENDER_SCALED);
    assert(process_create_window(peers[0]) < 0);

    create_packet(5000, 80, CWM_RENDER_NATIVE);
    assert(process_create_window(peers[0]) < 0);

    create_packet(100, 80, CWM_RENDER_NATIVE);
    assert(process_create_window(peers[0]) == 0);
    assert(window_owned_by_client(peers[0], 1));

    put_u32be((uint8_t *)packet + 1, 1);
    server.recv_len = 5;
    assert(process_destroy_window(peers[1]) < 0);
    assert(process_commit_window(peers[1]) < 0);
    assert(window_owned_by_client(peers[0], 1));
    assert(process_commit_window(peers[0]) == 0);
    assert(process_destroy_window(peers[0]) == 0);
    assert(!window_owned_by_client(peers[0], 1));

    windows_cleanup();
    close(peers[0]);
    close(peers[1]);
    puts("protocol checks passed");
    return 0;
}

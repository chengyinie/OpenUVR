#include "udp.h"
#include "ouvr_packet.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
//#include <sys/socket.h>
#include <netinet/in.h>
// for memset
#include <string.h>
// for ETH_P_802_EX1
#include <linux/if_ether.h>
// for sockaddr_ll
#include <linux/if_packet.h>

#include <time.h>

#define MY_SLL_IFINDEX 3

#define SEND_SIZE 1450

typedef struct raw_net_context
{
    unsigned char eth_header[14];
    int fd;
    struct sockaddr_ll raw_addr;
    struct msghdr msg;
    struct iovec iov[3];
} raw_net_context;

unsigned char const global_eth_header[14] = {0xb8, 0x27, 0xeb, 0x6c, 0xa7, 0xdd, 0x00, 0x0e, 0x8e, 0x5c, 0x2e, 0x53, 0x88, 0xb5};

static int raw_initialize(struct ouvr_ctx *ctx)
{
    if (ctx->net_priv != NULL)
    {
        free(ctx->net_priv);
    }
    raw_net_context *c = calloc(1, sizeof(raw_net_context));
    ctx->net_priv = c;
    memcpy(c->eth_header, global_eth_header, 14);
    c->fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_802_EX1));
    if (c->fd < 0)
    {
        printf("Couldn't create socket\n");
        return -1;
    }

    //When you send packets, it is enough to specify sll_family, sll_addr, sll_halen, sll_ifindex, and sll_protocol.
    c->raw_addr.sll_family = AF_PACKET;
    //this is the hardcoded index of wlp2s0 device
    c->raw_addr.sll_ifindex = MY_SLL_IFINDEX;
    //ethertype ETH_P_802_EX1 (0x88b5) is reserved for private use
    c->raw_addr.sll_protocol = htons(ETH_P_802_EX1);
    memcpy(c->raw_addr.sll_addr, c->eth_header, 6);
    c->raw_addr.sll_halen = 6;
    c->raw_addr.sll_pkttype = PACKET_OTHERHOST;
    //msg.msg_flags = MSG_DONTWAIT;

    c->iov[0].iov_base = c->eth_header;
    c->iov[0].iov_len = sizeof(c->eth_header);

    // int flags = fcntl(fd, F_GETFL, 0);
    // fcntl(fd, F_SETFL, flags | (int)O_NONBLOCK);

    c->msg.msg_name = &c->raw_addr;
    c->msg.msg_namelen = sizeof(c->raw_addr);
    c->msg.msg_control = 0;
    c->msg.msg_controllen = 0;
    c->msg.msg_iov = c->iov;
    c->msg.msg_iovlen = 3;
    return 0;
}

static int raw_send_packet(struct ouvr_ctx *ctx, struct ouvr_packet *pkt)
{
    raw_net_context *c = ctx->net_priv;
    register ssize_t r;
    unsigned char *start_pos = pkt->data;
    int offset = 0;
    c->iov[1].iov_len = sizeof(pkt->size);
    c->iov[1].iov_base = &(pkt->size);
    c->iov[2].iov_len = SEND_SIZE;
    while (offset < pkt->size)
    {
        c->iov[2].iov_base = start_pos + offset;
        r = sendmsg(c->fd, &c->msg, 0);
        if (r < -1)
        {
            printf("Error on sendmsg: %ld\n", r);
            return -1;
        }
        else if (r > 0)
        {
            offset += c->iov[2].iov_len;
            if (offset + SEND_SIZE > pkt->size)
            {
                c->iov[2].iov_len = pkt->size - offset;
            }
        }
    }
    return 0;
}

static int raw_deinitialize(struct ouvr_ctx *ctx)
{
    raw_net_context *c = ctx->net_priv;
    close(c->fd);
    free(ctx->net_priv);
}

struct ouvr_network raw_handler = {
    .init = raw_initialize,
    .send_packet = raw_send_packet,
    .deinit = raw_deinitialize,
};
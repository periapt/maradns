/* Copyright (c) 2007-2009 Sam Trenholme and others
 *
 * TERMS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * This software is provided 'as is' with no guarantees of correctness or
 * fitness for purpose.
 */

#include "DwSocket.h"
#include "DwTcpSocket.h"
#include "DwCompress.h"
#include "DwDnsStr.h"

/* Mararc parameters that are set in DwMararc.c */
extern dw_str *key_s[];
extern dw_str *key_d[];
extern int32_t key_n[];

/* Parameters set in DwSys.c */
extern int64_t the_time;
extern dwr_rg *rng_seed;
extern dw_hash *cache;

/* List of addresses we will bind to */
extern ip_addr_T bind_address[];
extern ip_addr_T upstream_address[];

/* List of active sockets */
extern SOCKET b_local[];
extern SOCKET *b_remote;

/* The list of pending remote connections */
extern remote_T *rem;

/* The list of active in-flight connections */
dw_hash *inflight;

/* The numeric mararc parameters */
extern int maxprocs;
extern int timeout_seconds;
extern int dns_port;
extern int upstream_port;
extern int handle_overload;
extern int resurrections;
extern int min_bind;
extern int num_ports;
extern int num_retries;
extern int deliver_all;

/* Other mararc parameters */
dwd_dict *blacklist_dict;

#ifdef MINGW
/* Needed for the Windows way of making a socket non-blocking */
extern u_long dont_block;
#endif /*MINGW*/

/* Initialize the inflight hash */
void init_inflight_hash() {
        inflight = 0;
        if(key_n[DWM_N_max_inflights] < 2) {
                return; /* No inflight merge desired */
        }
        inflight = dwh_hash_init(key_n[DWM_N_maxprocs] + 10);
}

/* The upstream server we will connect to (round robin rotated) */

/* Bind to all IP addresses we are to bind to and return the number of
 * IP addresses we got or INVALID_SOCKET on error */
int bind_all_udp() {
        int a = 0;
        int count = 0;
        for(a = 0; a < DW_MAXIPS; a++) {
                if(bind_address[a].len != 0) {
                        b_local[a] = do_bind(&bind_address[a],SOCK_DGRAM);
                        if(b_local[a] != INVALID_SOCKET) {
                                count++;
                        }
                } else {
                        b_local[a] = INVALID_SOCKET;
                }
        }
        return count;
}

/* Create a sockaddr_in that will be bound to a given port; this is
 * used by the code that binds to a randomly chosen port */
void setup_bind(sockaddr_all_T *dns_udp, uint16_t port, int len) {
        if(dns_udp == 0) {
                return;
        }
        memset(dns_udp,0,sizeof(dns_udp));
        if(len == 4) {
                dns_udp->V4.sin_family = AF_INET;
                dns_udp->V4.sin_addr.s_addr = htonl(INADDR_ANY);
                dns_udp->V4.sin_port = htons(port);
#ifdef IPV6
        } else if(len == 16) {
                dns_udp->V6.sin6_family = AF_INET6;
                dns_udp->V6.sin6_addr = in6addr_any;
                dns_udp->V6.sin6_port = htons(port);
#endif
        } else { /* Bad ip */
                return;
        }
        return;
}

/* This tries to bind to a random port up to 10 times; should it fail
 * after 10 times, it returns a -1 */
int do_random_bind(SOCKET s, int len) {
        sockaddr_all_T dns_udp;
        int a = 0;
        int success = 0;
        socklen_t len_inet = sizeof(struct sockaddr_in);

        for(a = 0; a < 10; a++) {
                /* Set up random source port to bind to */
                setup_bind(&dns_udp,
                           min_bind + (dwr_rng(rng_seed) & num_ports), len);
#ifdef IPV6
                if (dns_udp.Family == AF_INET6)
                        len_inet = sizeof(struct sockaddr_in6);
#endif
                /* Try to bind to that port */
                if(bind(s, (struct sockaddr *)&dns_udp,
                   len_inet) != -1) {
                        success = 1;
                        break;
                }
        }
        if(success == 0) { /* Bind to random port failed */
                return -1;
        }
        return 1;
}

/* Create a sockaddr_in that will connect to a given address; -1 on fail;
 * this is used by the code that "connects" to the remote DNS server */
SOCKET setup_server(sockaddr_all_T *server, ip_addr_T *addr) {
        int s = INVALID_SOCKET;
        if(server == 0 || addr == 0) {
                return INVALID_SOCKET;
        }

        memset(server,0,sizeof(server));
        if ( addr->len == 4 ) {
                server->V4.sin_family = AF_INET;
                server->V4.sin_port = htons(upstream_port);
                memcpy(&(server->V4.sin_addr),addr->ip,4);
                s = socket(AF_INET,SOCK_DGRAM,0);
#ifdef IPV6
        } else if ( addr->len == 16 ) {
                server->V6.sin6_family = AF_INET6;
                server->V6.sin6_port = htons(upstream_port);
                memcpy(&(server->V6.sin6_addr),addr->ip,16);
                s = socket(AF_INET6,SOCK_DGRAM,0);
#endif
        } else {
                return INVALID_SOCKET;
        }
        return s;
}

/* Given a remote number, a C-string with the packet to send them, and
 * the length of that string, make a connection to a remote server */
void make_remote_connection(int32_t n, unsigned char *packet, int len,
     dw_str *query) {
        sockaddr_all_T server;
        SOCKET s = 0;
        int32_t rnum = -1;
        ip_addr_T addr = {0,{0,0}};
        socklen_t inet_len = sizeof(struct sockaddr_in);

        if(rem[n].socket != INVALID_SOCKET) { /* Already used (sanity check) */
                return;
        }

        /* Get a random query ID to send to the remote server */
        rnum = set_dns_qid(packet,len,dwr_rng(rng_seed));
        if(rnum == -1) {
                return;
        }

        /* Get IP of remote server and open a socket to them */
        addr = get_upstream_ip(query);
        s = setup_server(&server,&addr);
        if(s == INVALID_SOCKET) { /* Failed to make socket */
                return;
        }

#ifdef IPV6
        if (server.Family == AF_INET6)
                 inet_len = sizeof(struct sockaddr_in6);
#endif

        make_socket_nonblock(s); /* Linux kernel bug */

        /* Bind to source port; "connect" to remote server; send packet */
        if ((do_random_bind(s,addr.len) == -1) ||
            (connect(s, (struct sockaddr *)&server, inet_len) == -1) ||
            (send(s,packet,len,0) < 0)) {
                closesocket(s);
                return;
        }

        /* OK, now note the pending connection */
        b_remote[n] = s;
        rem[n].socket = s;
        rem[n].remote_id = rnum;
}

/* Send a server failure back to the client when the server is overloaded.
 * This is a bit of a hack; we just take the client's DNS packet, and send
 * it as-is with QR set to 1 (a response) and RCODE set to 2 (server
 * failure) */
void send_server_fail(sockaddr_all_T *client,unsigned char *a, int len,
                      SOCKET sock, int tcp_num) {
        socklen_t c_len = sizeof(struct sockaddr_in);

        if(len < 12) {
                return;
        }

#ifdef IPV6
        if (client->Family == AF_INET6)
                c_len = sizeof(struct sockaddr_in6);
#endif
        a[2] |= 0x80; /* Set QR (reply, not question) */
        a[3] &= 0xf0; a[3] |= 0x02; /* Set RCODE */

        if(tcp_num == -1) {
                sendto(sock,(void *)a,len,0,(struct sockaddr *)client, c_len);
        }

}

/* See if there is a "inflight" query already handling the query we
 * are processing.  If not (or if we don't merge inflights), return -1.
 * If so, return the UDP connection number of the already-inflight query
 */

int find_inflight_query(unsigned char *a, int len) {
        dw_str *query = 0, *answer = 0;
        int ret = -1;

        if(len < 12 || a == 0 || key_n[DWM_N_max_inflights] < 2) {
                goto catch_find_inflight_query;
        }

        query = dw_get_dname_type(a,12,len);
        if(query == 0) {
                goto catch_find_inflight_query;
        }

        answer = dwh_get(inflight, query, 0, 1);
        if(answer == 0) { /* Nothing in-flight */
                goto catch_find_inflight_query;
        }

        ret = dw_fetch_u16(answer,0);

        if(rem[ret].socket == INVALID_SOCKET) { /* Not valid query */
                ret = -1;
                goto catch_find_inflight_query;
        }

        if(dw_issame(query,rem[ret].query) != 1) { /* Actually not same */
                dwh_zap(inflight,query, 0, 1); /* Remove corrupt data */
                ret = -1;
        }

catch_find_inflight_query:
        if(query != 0) {
                dw_destroy(query);
        }
        if(answer != 0) {
                dw_destroy(answer);
        }
        return ret;
}

void zap_inflight(dw_str *query) {
        if(key_n[DWM_N_max_inflights] > 1) {
                dwh_zap(inflight,query, 0, 1);
        }
}

/* Make a new UDP connection on remote pending connection b */
void make_new_udp_connect(int b, unsigned char *a, int len, int num_alloc) {
        dw_str *answer = 0, *check = 0;
        int counter = 0;

        /* Make a new remote connection */
        if(rem[b].socket != INVALID_SOCKET) { /* Sanity check */
                return;
        }
        rem[b].query = dw_get_dname_type(a,12,len);
        make_remote_connection(b,a,len,rem[b].query);

        rem[b].local = malloc(num_alloc * sizeof(local_T *));
        for(counter = 0; counter < num_alloc; counter++) {
                rem[b].local[counter] = 0;
        }
        rem[b].num_locals = 1;
        if(key_n[DWM_N_max_inflights] > 1) {
                answer = dw_create(3);
                check = dwh_get(inflight, rem[b].query, 0, 1);
                if(check == 0) { /* Mark query in inflight hash */
                        dw_put_u16(answer,b,0);
                        dwh_add(inflight, rem[b].query, answer, 120, 1);
                } else { /* This query is already inflight */
                        dw_destroy(check);
                }
                dw_destroy(answer);
        }
}

/* Send a local DNS request to the upstream (remote) server; this
 * requires a few parameters having to do with the connection
 * to be passed to the function. */
int forward_local_udp_packet(SOCKET sock, int32_t local_id,
     ip_addr_T *from_ip, uint16_t from_port, unsigned char *a,
     int len, int tcp_num) {
        int32_t b = 0;
        int num_alloc = 0;

        num_alloc = key_n[DWM_N_max_inflights];
        if(num_alloc < 1) {
                num_alloc = 1;
        } else if(num_alloc > 32000) {
                num_alloc = 32000;
        }
        num_alloc++; /* Stop off-by-one attacks */

        b = find_inflight_query(a,len);
        if(b == -1) {
                b = find_free_remote();
                if(b == -1) { /* We're out of remote pending connections */
                        return -1;
                } else { /* Create new connection */
                        reset_rem(b);
                        make_new_udp_connect(b,a,len,num_alloc);
                }
        } else { /* Add new local to already inflight connection */
                if(rem[b].num_locals >= num_alloc - 2) {
                        return -1; /* No more inflights for this query */
                }
                rem[b].num_locals++;
#ifdef INFLIGHT_VERBOSE
                printf("Connection %d has %d locals\n",b,rem[b].num_locals);
                fflush(stdout);
#endif /* INFLIGHT_VERBOSE */
        }
        if(rem[b].socket == INVALID_SOCKET) {
                reset_rem(b);
                return -1;
        }

        rem[b].local[rem[b].num_locals - 1] = malloc(sizeof(local_T));
        rem[b].local[rem[b].num_locals - 1]->from_socket = sock;
        rem[b].local[rem[b].num_locals - 1]->tcp_num = tcp_num;
        if(from_ip != 0) {
                rem[b].local[rem[b].num_locals - 1]->ip = *from_ip;
        }
        rem[b].local[rem[b].num_locals - 1]->port = from_port;
        rem[b].local[rem[b].num_locals - 1]->local_id = local_id;
        rem[b].die = get_time() + ((int64_t)timeout_seconds << 8);
        return 0;
}

/* Create a DNS header suitable for giving back to the client */
dw_str *make_dns_header(int32_t id, int16_t flags, int32_t ancount,
                        int32_t nscount, int32_t arcount) {
        dw_str *out = 0;

        if(id < 0 || ancount < 0 || nscount < 0 || arcount < 0) {
                return 0; /* Sanity check */
        }

        /* Destroyed after giving the client the reply */
        out = dw_create(515);

        /* Query ID; echo from client */
        if(dw_put_u16(out, id, -1) == -1) {
                goto catch_make_dns_header;
        }

        /* QR; Opcode; AA; TC; RD; RA; Z; RCODE */
        if(dw_put_u16(out, flags, -1) == -1) {
                goto catch_make_dns_header;
        }

        /* QDCOUNT = 1 */
        if(dw_put_u16(out,1,-1) == -1) {
                goto catch_make_dns_header;
        }

        /* And, finally, ANCOUNT, NSCOUNT, and ARCOUNT */
        if(dw_put_u16(out,ancount,-1) == -1 ||
           dw_put_u16(out,nscount,-1) == -1 ||
           dw_put_u16(out,arcount,-1) == -1) {
                goto catch_make_dns_header;
        }

        return out;

catch_make_dns_header:
        if(out != 0) {
                dw_destroy(out);
                out = 0;
        }
        return 0;
}

/* Given two dw_strings with the question and answer, make a DNS packet
 * we can give back to the client; this will multilate answer so be
 * careful */
dw_str *make_dns_packet(dw_str *question, dw_str *answer, int32_t id) {
        int32_t ancount = 0, nscount = 0, arcount = 0;
        int is_nxdomain = 0;
        dw_str *out = 0;

        is_nxdomain = dw_pop_u8(answer);
        arcount = dw_pop_u16(answer);
        nscount = dw_pop_u16(answer);
        ancount = dw_pop_u16(answer);

        if(is_nxdomain == 0) {
                /* 0x8180: QR = 1; Opcode = 0; AA = 0; TC = 0; RD = 1; RA = 1;
                 * Z = 0; RCODE = 0 */
                out = make_dns_header(id,0x8180,ancount,nscount,arcount);
        } else if(is_nxdomain == 1) {
                /* Same header as before, but with RCODE of "name error" */
                out = make_dns_header(id,0x8183,ancount,nscount,arcount);
        } else {
                goto catch_make_dns_packet;
        }

        if(dw_append(question,out) == -1 ||
           dw_put_u16(out,1,-1) == -1 || /* QCLASS: 1 */
           dw_append(answer,out) == -1) {
                goto catch_make_dns_packet;
        }

        return out;

catch_make_dns_packet:
        if(out != 0) {
                dw_destroy(out);
                out = 0;
        }
        return 0;
}

/* See if a given reply is in the cache; if so, send them the reply
 * from the cache */
int get_reply_from_cache(dw_str *query, sockaddr_all_T *client,
                         SOCKET sock, int32_t id, int resurrect,
                         int tcp_num) {
        dw_str *value = 0; /* Element in cache */
        dw_str *comp = 0; /* Compressed DNS packet */
        dw_str *packet = 0;
        socklen_t c_len = sizeof(struct sockaddr_in);
        int ret = -1;

#ifdef IPV6
        if (client->Family == AF_INET6) {
               c_len = sizeof(struct sockaddr_in6);
        }
#endif

        dwc_process(cache,query,3); /* RR rotation, TTL aging, etc. */
        value = dwh_get(cache,query,resurrect,1);
        comp = dwc_compress(query,value);
        if(comp == 0) {
                goto catch_get_reply_from_cache;
        }

        if(comp->len == 7) { /* Empty packet; workaround */
                dw_log_string("Warning: Removing empty packet from cache",11);
                dwh_zap(cache,query,0,1);
                goto catch_get_reply_from_cache;
        }

        dw_log_dwstr_str("Fetching ",query," from cache",100);
        packet = make_dns_packet(query,comp,id);
        if(packet == 0) {
                goto catch_get_reply_from_cache;
        }

        if(tcp_num == -1) {
                sendto(sock,(void *)packet->str,packet->len,0,
                       (struct sockaddr *)client, c_len);
        } else if(key_n[DWM_N_tcp_listen] == 1) {
                tcp_return_reply(tcp_num,(void *)packet->str,packet->len);
        }

        ret = 1;

catch_get_reply_from_cache:
        if(value != 0) {
                dw_destroy(value);
        }
        if(packet != 0) {
                dw_destroy(packet);
        }
        if(comp != 0) {
                dw_destroy(comp);
        }
        return ret;
}

/* Given a connection we will send on, try and send the connection on.
   If we're unable to send the connection on, see if we have an
   expired element with the data we want. */
void try_forward_local_udp_packet(SOCKET sock, int32_t local_id,
     ip_addr_T *from_ip, uint16_t from_port, unsigned char *packet,
     int len, sockaddr_all_T *client,dw_str *query, int tcp_num) {

        /* If not cached, get a reply that we will cache and send back to
         * the client */
        if(forward_local_udp_packet(sock,local_id,from_ip,from_port,
                                    packet,len,tcp_num) != -1) {
                return; /* Success! */
        }

        /* OK, at this point it failed so we'll see if we get a
         * "resurrected" cache entry */
        if(resurrections == 1 &&
           get_reply_from_cache(query,client,sock,local_id,1,tcp_num)
             == 1) {
                dw_log_string("Resurrected from cache",11);
                return; /* Resurrected entry; we're done */
        }

        if(handle_overload == 1) {
                send_server_fail(client,packet,len,sock,tcp_num);
        }
}

/* Get and process a local DNS request */
void get_local_udp_packet(SOCKET sock) {
        unsigned char packet[522];
        int len = 0;
        sockaddr_all_T client;
        socklen_t c_len = 0;
        ip_addr_T from_ip = {0,{0,0}};
        uint16_t from_port = 0;
        int32_t local_id = -1;
        dw_str *query = 0;
#ifdef VALGRIND_NOERRORS
        memset(packet,0,522);
#endif /* VALGRIND_NOERRORS */

        c_len = sizeof(client);
        make_socket_nonblock(sock); /* Linux bug workaround */
        len = recvfrom(sock,(void *)packet,520,0,(struct sockaddr *)&client,
                       &c_len);

        from_port = get_from_ip_port(&from_ip,&client);

        if(check_ip_acl(&from_ip) != 1) { /* Drop unauthorized packets */
                goto catch_get_local_udp_packet;
        }

        local_id = get_dns_qid(packet, len, 2);
        if(local_id < 0 || len < 13) { /* Invalid remote packet */
                goto catch_get_local_udp_packet;
        }

        /* See if we have something in the cache; destroyed at end of
         * function */
        query = dw_get_dname_type(packet,12,len);
        dw_log_dwstr("Got DNS query for ",query,100); /* Log it */
        if(query == 0) {
                goto catch_get_local_udp_packet;
        }
        if(get_reply_from_cache(query,&client,sock,local_id,0,-1)
                == 1) {
                goto catch_get_local_udp_packet; /* In cache; we're done */
        }

        /* Nothing in cache; lets try sending the packet upstream */
        try_forward_local_udp_packet(sock,local_id,&from_ip,from_port,
                        packet,len,&client,query,INVALID_SOCKET);

catch_get_local_udp_packet:
        if(query != 0) {
                dw_destroy(query);
                query = 0;
        }
}

/* Forward a remote reply back to the client */
void forward_remote_reply(unsigned char *packet, size_t len, remote_T *r_ip,
                int local_num) {
        sockaddr_all_T client;
        socklen_t len_inet = 0;

        if(r_ip == 0) {
                return;
        }
        if(r_ip->local[local_num]->from_socket == INVALID_SOCKET) {
                return;
        }
        memset(&client,0,sizeof(client));
        len_inet = sizeof(client);

        if (r_ip->local[local_num]->ip.len == 4) {
                client.V4.sin_family = AF_INET;
                client.V4.sin_port = htons(r_ip->local[local_num]->port);
                memcpy(&client.V4.sin_addr, r_ip->local[local_num]->ip.ip, 4);
                len_inet = sizeof(struct sockaddr_in);
#ifdef IPV6
        } else if(r_ip->local[local_num]->ip.len == 16) {
                client.V6.sin6_family = AF_INET6;
                client.V6.sin6_port = htons(r_ip->local[local_num]->port);
                memcpy(&client.V6.sin6_addr, r_ip->local[local_num]->ip.ip,
                        16);
                len_inet = sizeof(struct sockaddr_in6);
#endif
        } else {
                return;
        }
        sendto(r_ip->local[local_num]->from_socket,(void *)packet,len,0,
               (struct sockaddr *)&client,len_inet);

        /* Sometimes, especially if we use DNS-over-TCP, we will have a case
         * where some local IPs have been handled and others haven't.
         * "forward_remote_reply" finishes up a local connection to UDP and
         * is only sent once, so we close the socket to make it invalid */
        r_ip->local[local_num]->from_socket = INVALID_SOCKET;
}

/* Add a reply we have received from the remote (upstream) DNS server to
 * the cache */
int cache_dns_reply(unsigned char *packet, int count) {
        int32_t ttl = 60;
        int32_t ancount = 0;
        int is_nxdomain = 0;
        dw_str *question = 0, *answer = 0;
        dw_str *decomp = 0;
        int ret = -1;

        question = dw_get_dname_type(packet,12,count);
        dw_log_dwstr("Caching reply for ",question,100);
        if((packet[3] & 0x0f) == 3) {
                is_nxdomain = 1;
        }
        answer = dw_packet_to_cache(packet,count,is_nxdomain);
        decomp = dwc_decompress(question,answer);
        if(blacklist_dict != 0 && has_bad_ip(decomp,blacklist_dict)) {
                ret = -2; /* Tell caller we need synth "not there" */
                goto catch_cache_dns_reply;
        }
        ancount = dw_cachepacket_to_ancount(answer);
        if(ancount == 0) {
                ancount = 32; /* So we can correctly cache negative answers */
        }

        if(question == 0 || answer == 0 || ancount == -1) {
                goto catch_cache_dns_reply;
        }

        ttl = dw_get_a_dnsttl(answer,0,31536000,ancount);

        if(ttl == -1) {
                goto catch_cache_dns_reply;
        }

        /* Physically put the data in the cache */
        dwh_add(cache,question,decomp,ttl,1);
        ret = 1;

catch_cache_dns_reply:
        if(question != 0) {
                dw_destroy(question);
        }
        if(answer != 0) {
                dw_destroy(answer);
        }
        if(decomp != 0) {
                dw_destroy(decomp);
        }
        return ret;
}

/* Verify that a given DNS packet is good (The Query ID is correct, the
   query in the "question" section of the DNS header is good) */
int verify_dns_packet(int b, unsigned char *packet, int len) {
        int ret = 0;
        dw_str *question = 0;

        /* Make sure the ID we got is the same as the one we originally
         * sent them */
        if(get_dns_qid(packet,len,0) != rem[b].remote_id) {
                goto catch_verify_dns_packet;
        }

        question = dw_get_dname_type(packet,12,len);
        if(question == 0) {
                goto catch_verify_dns_packet;
        }

        /* Should we make this case-insensitive? Probably not. */
        if(dw_issame(question,rem[b].query) != 1) {
                goto catch_verify_dns_packet;
        }

        ret = 1;

catch_verify_dns_packet:
        if(question != 0) {
                dw_destroy(question);
                question = 0;
        }
        return ret;
}

/* Make the actual answer for a synthetic "not there" reply */
unsigned char *make_synth_not_there_answer(unsigned char *a, int count) {
        /* This is the answer for a "not there" reply */
        unsigned char not_there[41] =
        "\xc0\x0c" /* Name */
        "\0\x06" /* Type */
        "\0\x01" /* Class */
        "\0\0\0\0" /* TTL (don't cache) */
        "\0\x1c" /* RDLENGTH */
        "\x01\x7a\xc0\x0c" /* Origin */
        "\x01\x79\xc0\x0c" /* Email */
        "\0\0\0\x01\0\0\0\x01\0\0\0\x01\0\0\0\x01\0\0\0\x01" /* 5 numbers */;
        unsigned char *answer = 0;
        int counter = 0;

        answer = malloc(count + 43);
        /* Copy the header they sent us to our reply */
        for(counter = 0; counter < 12 && counter < count; counter++) {
                answer[counter] = a[counter];
        }

        /* Copy the question over to the reply */
        for(;counter < 520 && counter < count; counter++) {
                if(a[counter] == 0) {
                        break; /* Quick and dirty "end of name"; yes, I
                                * check in dw_get_dname_type() to make sure
                                * there is no ASCII NULL in names */
                }
                answer[counter] = a[counter];
        }
        if(count < counter + 5 || counter > 512) { /* Sanity check */
                free(answer);
                return 0;
        }

        /* Add the rest of the question */
        count = counter + 5;
        for(;counter < count; counter++) {
                answer[counter] = a[counter];
        }

        /* Add the SOA reply to the answer */
        for(counter = 0; counter < 40; counter++) {
                answer[count + counter] = not_there[counter];
        }

        /* Return the answer */
        return answer;
}

/* Make a synthetic "not there" reply */
void make_synth_not_there(int b, SOCKET sock, unsigned char *a, int count) {
        unsigned char *answer = 0;
        int local_num = 0;

        if(a == 0 || count < 12 || rem[b].local == 0) {
                return;
        }

        /* Copy original header and question in to answer */
        answer = make_synth_not_there_answer(a,count);
        if(answer == 0) {
                return;
        }

        /* Flag this as an answer */
        answer[2] |= 0x80;

        /* One "NS" record; no other records */
        answer[9] = 1;
        answer[6] = answer[7] = answer[8] = answer[10] = answer[11] = 0;

        /* Send the reply(s) */
        for(local_num = 0; local_num < rem[b].num_locals; local_num++) {
                /* Copy ID over */
                answer[0] = (rem[b].local[local_num]->local_id >> 8) & 0xff;
                answer[1] = (rem[b].local[local_num]->local_id) & 0xff;
                /* Send this reply */
                if(rem[b].local[local_num]->tcp_num == -1) {
                        forward_remote_reply(answer,count + 40, &rem[b],
                                local_num);
                } else {
                        tcp_return_reply(rem[b].local[local_num]->tcp_num,
                                (void *)answer, count);
                }
        }

        /* Reset the pending remote connection */
        closesocket(b_remote[b]);
        b_remote[b] = -1;
        reset_rem(b);

        /* Free allocated memory */
        free(answer);
}

/* Core part of code that gets and processes remote DNS requests */

int get_rem_udp_packet_core(unsigned char *a, ssize_t count,
                int b, SOCKET sock, int l) {

        int cache_dns_reply_return_value = -1;

        if(count < 12) {
                return -1; /* Not a DNS packet at all */
        }

        if((a[2] & 0x02) == 0x00) { /* If not truncated */
                fflush(stdout);
                cache_dns_reply_return_value = cache_dns_reply(a,count);
                if(cache_dns_reply_return_value == -2) { /* Make synth NX */
                        make_synth_not_there(b,sock,a,count);
                        return -1; /* Bad reply and they got a Synth NX */
                }
                if(cache_dns_reply_return_value < 0 && deliver_all == 0) {
                        return -1; /* Bad reply */
                }
        } else if(rem[b].local[l]->tcp_num != -1 &&
                        key_n[DWM_N_tcp_listen] == 1) {
                /* Send a DNS-over-TCP packet to handle a truncated reply */
                tcp_truncated_retry(rem[b].local[l]->tcp_num, rem[b].query,
                        rem[b].local[l]->local_id);
                /* Give the UDP connection more time before timeout, so
                 * we can fully process the TCP connection */
                rem[b].die = get_time() + ((int64_t)timeout_seconds << 10);
                return 2; /* Don't kill pending UDP connection */
        }

        /* Now make sure the ID is the same as the one the client
         * originally sent us */
        set_dns_qid(a,count,rem[b].local[l]->local_id);

        /* Send the answer we got to the appropriate local connection */
        if(rem[b].local[l]->tcp_num == -1 ) {
                forward_remote_reply(a,count,&rem[b], l);
        } else if(key_n[DWM_N_tcp_listen] == 1) {
                tcp_return_reply(rem[b].local[l]->tcp_num,(void *)a,count);
        }

        return 1;
}

/* Get and process a remote DNS request */
void get_remote_udp_packet(int b, SOCKET sock) {
        ssize_t count;
        unsigned char a[520];
        int l = 0, kill = 1, core_ret = 0;

        count = recv(sock,a,514,0);
        if(count < 12 || count > 512) {
                return;
        }
        a[2] |= 0x80; /* Flag this as an answer (just in case they didn't) */

        /* Make reasonably sure this is the reply to their question */
        if(verify_dns_packet(b,a,count) != 1) {
                return;
        }

        /* Having this be able to handle multiple in-flight requests
         * is hairy because we have to handle both DNS-over-UDP and
         * DNS-over-TCP */

        for(l = 0; l < rem[b].num_locals; l++) {
                core_ret = get_rem_udp_packet_core(a,count,b,sock,l);
                if(core_ret == -1) {
                        return;
                } else if(core_ret == 2) {
                        kill = 0;
                }
        }

        /* Reset this pending remote connection if needed */
        if(kill == 1) {
                closesocket(b_remote[b]);
                b_remote[b] = -1;
                reset_rem(b);
        }
}

/* Send a server failure back to the client when there is no reply from
 * the upstream server.  Input: The pending remote connection number. */
void server_fail_noreply(int a, int local_num) {
        dw_str *packet = 0;

        /* 0x8182: QR = 1; Opcode = 0; AA = 0; TC = 0; RD = 1; RA = 1;
         *         Z = 0; RCODE = "server fail" (2) */
        packet = make_dns_header(rem[a].local[local_num]->local_id,0x8182,
                        0,0,0);

        dw_log_dwstr("Sending SERVER FAIL for query ",rem[a].query,100);

        if(dw_append(rem[a].query,packet) == -1 ||
           dw_put_u16(packet,1,-1) == -1 /* QCLASS: 1 */) {
                goto catch_server_fail_noreply;
        }

        if(rem[a].local[local_num]->tcp_num == -1) {
                forward_remote_reply((unsigned char *)packet->str,
                        packet->len,&rem[a],0);
        } else if(key_n[DWM_N_tcp_listen] == 1) {
                tcp_return_reply(rem[a].local[local_num]->tcp_num,
                        (void *)packet->str,packet->len);
        }

catch_server_fail_noreply:
        if(packet != 0) {
                dw_destroy(packet);
                packet = 0;
        }
}


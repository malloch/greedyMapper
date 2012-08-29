//
// greedyMapper.c
// program for centralizing distributed libmapper network topologies
// This program is intended as a proof-of-concept to show that libmapper can be used in
// a centralized client-server-like mode if desired.
// http://www.libmapper.org
// Joseph Malloch, IDMIL 2011
//
// This software was written in the Input Devices and Music Interaction
// Laboratory at McGill University in Montreal, and is copyright those
// found in the AUTHORS file.  It is licensed under the GNU Lesser Public
// General License version 2.1 or later.  Please see COPYING for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include "mapper/mapper.h"

int done = 0;
int port = 9000;

mapper_device device;
mapper_monitor monitor;
mapper_db db;

void signalHandler(mapper_signal sig, mapper_db_signal props,
                   mapper_timetag_t *timetag, void *value)
{
    mapper_signal sig_out = props->user_data;
    msig_update(sig_out, value);
}

void linkHandler(mapper_db_link lnk, mapper_db_action_t a, void *user)
{
    if (a != MDB_NEW) return;
    int length = strlen(mdev_name(device));
    if (strncmp(lnk->src_name, mdev_name(device), length) == 0 ||
        strncmp(lnk->dest_name, mdev_name(device), length) == 0)
        return;
    mapper_monitor_link(monitor, lnk->src_name, mdev_name(device));
    mapper_monitor_link(monitor, mdev_name(device), lnk->dest_name);
}

void connectionHandler(mapper_db_connection con, mapper_db_action_t a, void *user)
{
    /* Check if this applies to me. We will check if the name string matches without its ordinal
     * in case there are multiple copies of greedyMapper running. Without this safeguard two or
     * more copies would re-route each other's mappings in an infinite loop. */
    int length = strlen("/greedyMapper.");
    if (strncmp(con->src_name, mdev_name(device), length) == 0 ||
        strncmp(con->dest_name, mdev_name(device), length) == 0) {
        // this connection involves me - don't interfere
        printf("skipping connection %s -> %s\n", con->src_name, con->dest_name);
        return;
    }
    switch (a) {
        case MDB_NEW:
        {
            printf("got new connection! %s -> %s\n", con->src_name, con->dest_name);
            char src[256], dest[256];
            mapper_signal insig, outsig;
            // check if we already have the output
            outsig = mdev_get_output_by_name(device, con->dest_name, 0);
            if (!outsig) {
                // if not, create it
                outsig = mdev_add_output(device, con->dest_name, con->dest_length, con->dest_type, 0, 0, 0);
                mdev_add_input(device, con->dest_name, con->dest_length, con->dest_type, 0,
                               (con->range.known | CONNECTION_RANGE_DEST_MIN) ? &con->range.dest_min : 0,
                               (con->range.known | CONNECTION_RANGE_DEST_MAX) ? &con->range.dest_max : 0,
                               signalHandler, outsig);
            }
            snprintf(dest, 256, "%s%s", mdev_name(device), con->dest_name);
            // check if we already have the input
            insig = mdev_get_input_by_name(device, con->src_name, 0);
            if (!insig) {
                // if not, create it
                outsig = mdev_add_output(device, con->src_name, con->src_length, con->src_type, 0,
                                         (con->range.known | CONNECTION_RANGE_SRC_MIN) ? &con->range.src_min : 0,
                                         (con->range.known | CONNECTION_RANGE_SRC_MAX) ? &con->range.src_max : 0);
                mdev_add_input(device, con->src_name, con->src_length, con->src_type,
                               0, 0, 0, signalHandler, outsig);
            }
            snprintf(src, 256, "%s%s", mdev_name(device), con->src_name);
            // create new connections
            mapper_monitor_connect(monitor, con->src_name, src, 0, 0);
            unsigned int flags = ((con->clip_min ? CONNECTION_CLIPMIN : 0) |
                                  (con->clip_max ? CONNECTION_CLIPMAX : 0) |
                                  con->range.known |
                                  (con->expression ? CONNECTION_EXPRESSION : 0) |
                                  (con->mode ? CONNECTION_MODE : 0) |
                                  (con->muted ? CONNECTION_MUTED : 0));
            mapper_monitor_connect(monitor, src, dest, con, flags);
            mapper_monitor_connect(monitor, dest, con->dest_name, 0, 0);
            mapper_monitor_disconnect(monitor, con->src_name, con->dest_name);
            break;
        }
        default:
            break;
    }
}

void createDevice()
{
    device = mdev_new("greedyMapper", port, 0);
    if (!device) {
        done = 1;
        return;
    }
    while (!mdev_ready(device)) {
        mdev_poll(device, 0);
        usleep(50 * 1000);
    }
}

void startMonitor()
{
    monitor = mapper_monitor_new();
    if (!monitor) {
        done = 1;
        return;
    }
    db = mapper_monitor_get_db(monitor);
    mapper_db_add_link_callback(db, linkHandler, 0);
    mapper_db_add_connection_callback(db, connectionHandler, 0);
}

void cleanup()
{
    printf("\nReturning connections!\n");
    mapper_db_connection_t **con = mapper_db_get_connections_by_src_dest_device_names(db,
                                                mdev_name(device), mdev_name(device));
    while (con) {
        unsigned int flags = (((*con)->clip_min ? CONNECTION_CLIPMIN : 0) |
                              ((*con)->clip_max ? CONNECTION_CLIPMAX : 0) |
                              (*con)->range.known |
                              ((*con)->expression ? CONNECTION_EXPRESSION : 0) |
                              ((*con)->mode ? CONNECTION_MODE : 0) |
                              ((*con)->muted ? CONNECTION_MUTED : 0));
        mapper_monitor_connect(monitor, strchr((*con)->src_name+1, '/'), strchr((*con)->dest_name+1, '/'), *con, flags);
        con = mapper_db_connection_next(con);
    }
    printf("\nCleaning up!\n");
    mdev_free(device);
    mapper_monitor_free(monitor);
}

void loop()
{
    createDevice();
    startMonitor();
    mapper_monitor_link(monitor, mdev_name(device), mdev_name(device));
    while (!done) {
        // poll libmapper outputs
        mapper_monitor_poll(monitor, 0);
        mdev_poll(device, 100);
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main ()
{
    signal(SIGINT, ctrlc);
    loop();

done:
    cleanup();
    return 0;
}
//
// greedyMapper.c
// program for centralizing distributed libmapper network topologies
// This program is intended as a proof-of-concept to show that libmapper can be used in
// a centralized client-server-like mode if desired
// http://www.idmil.org/software/libmapper
// Joseph Malloch, IDMIL 2010
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

typedef struct _internalMapping
{
    mapper_signal sig;
    struct _internalMapping *next;
} *internalMapping;

void signalHandler(mapper_signal sig, mapper_db_signal props,
                   mapper_timetag_t *timetag, void *value)
{
    internalMapping map = props->user_data;
    while (map) {
        msig_update(map->sig, value);
        map = map->next;
    }
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
    // check if this applies to me
    int length = strlen(mdev_name(device));
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
            char input[256], output[256];
            mapper_signal insig, outsig;
            // check if we already have the output
            outsig = mdev_get_output_by_name(device, con->dest_name, 0);
            if (!outsig) {
                // if not, create it
                outsig = mdev_add_output(device, con->dest_name, con->dest_length, con->dest_type, 0,
                                         (con->range.known | CONNECTION_RANGE_DEST_MIN) ? &con->range.dest_min : 0,
                                         (con->range.known | CONNECTION_RANGE_DEST_MAX) ? &con->range.dest_max : 0);
            }
            snprintf(output, 256, "%s%s", mdev_name(device), con->dest_name);
            // check if we already have the input
            insig = mdev_get_input_by_name(device, con->src_name, 0);
            if (insig) {
                mapper_db_signal props = msig_properties(insig);
                internalMapping map = props->user_data;
                int mappingFound = 0;
                while (map->next) {
                    if (map->sig == outsig) {
                        mappingFound = 1;
                        break;
                    }
                    map = map->next;
                }
                if (!mappingFound) {
                    internalMapping newmap = calloc(1, sizeof(internalMapping));
                    newmap->sig = outsig;
                    map->next = newmap;
                }
            }
            else {
                // if not, create it
                internalMapping newmap = calloc(1, sizeof(internalMapping));
                newmap->sig = outsig;
                mdev_add_input(device, con->src_name, con->src_length, con->src_type, 0,
                               (con->range.known | CONNECTION_RANGE_SRC_MIN) ? &con->range.src_min : 0,
                               (con->range.known | CONNECTION_RANGE_SRC_MAX) ? &con->range.src_max : 0,
                               signalHandler, newmap);
            }
            snprintf(input, 256, "%s%s", mdev_name(device), con->src_name);
            // create new connections
            mapper_monitor_connect(monitor, con->src_name, input, 0, 0);
            unsigned int flags = ((con->clip_min ? CONNECTION_CLIPMIN : 0) +
                                  (con->clip_max ? CONNECTION_CLIPMAX : 0) +
                                  (con->range.known ? CONNECTION_RANGE_KNOWN : 0) +
                                  (con->expression ? CONNECTION_EXPRESSION : 0) +
                                  (con->mode ? CONNECTION_MODE : 0) +
                                  (con->muted ? CONNECTION_MUTED : 0));
            mapper_monitor_connect(monitor, output, con->dest_name, con, flags);
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
    printf("\nCleaning up!\n");
    // TODO: need to iterate through signals here and free the internalMapping struct
    mapper_signal sig;
    mapper_db_signal props;
    internalMapping map, temp;
    int i, num = mdev_num_inputs(device);
    for (i = 0; i < num; i++) {
        sig = mdev_get_input_by_index(device, i);
        props = msig_properties(sig);
        map = props->user_data;
        while (map) {
            temp = map->next;
            free(map);
            map = temp;
        }
    }
    mdev_free(device);
    mapper_monitor_free(monitor);
}

void loop()
{
    createDevice();
    startMonitor();
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
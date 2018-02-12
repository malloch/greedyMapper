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

mapper_device dev;
mapper_database db;
int sig_counter = 0;

void mapHandler(mapper_database foo, mapper_map map, mapper_record_event e, const void *user)
{
    /* Check if this applies to me. We will check if the name string matches without its ordinal
     * in case there are multiple copies of greedyMapper running. Without this safeguard two or
     * more copies would re-route each other's maps in an infinite loop. */
    int i, length = strlen("greedyMapper.");
    mapper_slot slot;
    mapper_signal sig;
    mapper_device remote_dev;

    if (e != MAPPER_ADDED)
        return;

    for (i = 0; i < mapper_map_num_slots(map, MAPPER_LOC_ANY); i++) {
        slot = mapper_map_slot(map, MAPPER_LOC_ANY, i);
        sig = mapper_slot_signal(slot);
        remote_dev = mapper_signal_device(sig);
        if (remote_dev == dev) {
            // this map involves me - don't interfere
            printf("skipping map ");
            mapper_map_print(map);
            printf("\n");
            return;
        }
        if (strncmp(mapper_device_name(remote_dev), "greedyMapper.", length)==0) {
            // this map involves another copy of greedyMapper - don't interfere
            printf("skipping map ");
            mapper_map_print(map);
            printf("\n");
            return;
        }
    }

    printf("got new map! ");
    mapper_map_print(map);
    printf("\n");

    mapper_signal src, dst, local = 0;
    src = mapper_slot_signal(mapper_map_slot(map, MAPPER_LOC_SOURCE, 0));
    dst = mapper_slot_signal(mapper_map_slot(map, MAPPER_LOC_DESTINATION, 0));

    // check if we have already mirrored source signal
    mapper_map *maps = mapper_device_maps(dev, MAPPER_DIR_INCOMING);
    while (maps && *maps) {
        if (mapper_map_slot_by_signal(*maps, src)) {
            local = mapper_map_slot(*maps, MAPPER_LOC_DESTINATION, 0);
            break;
        }
        maps = mapper_map_query_next(maps);
    }

    if (!local) {
        char signame[16];
        snprintf(signame, 16, "signal/%d", sig_counter++);
        local = mapper_device_add_input_signal(dev, signame,
                                               mapper_signal_length(src),
                                               mapper_signal_type(src),
                                               0, 0, 0, 0, 0);
    }

    // create "bypass" map from src to local signal
    mapper_map tmp = mapper_map_new(1, &src, 1, &local);
    mapper_map_push(tmp);

    // create map from local to dst and copy all properties
    tmp = mapper_map_new(1, &local, 1, &dst);
    int num_props = mapper_map_num_properties(map);
    char type;
    const char *name;
    const void *value;
    for (i = 0; i < num_props; i++) {
        mapper_map_property_index(map, i, &name, &length, &type, &value);
        mapper_map_set_property(tmp, name, length, type, value, 1);
    }
    mapper_map_push(tmp);

    // remove original map
    mapper_map_release(map);
}

void createDevice()
{
    dev = mapper_device_new("greedyMapper", 0, 0);
    if (!dev) {
        done = 1;
        return;
    }
    while (!mapper_device_ready(dev)) {
        mapper_device_poll(dev, 50);
    }
}

void startDatabase()
{
    db = mapper_database_new(0, MAPPER_OBJ_MAPS);
    if (!db) {
        done = 1;
        return;
    }
    mapper_database_add_map_callback(db, mapHandler, 0);
}

void cleanup()
{
    // need to copy properties from outgoing maps, but need to grab source signal from incoming maps
    // solution: iterate through signals, for each, grab source, then iterate through signal's outgoing maps
    printf("\nReturning maps!\n");
    mapper_map map, *maps;
    mapper_slot slot;
    mapper_signal src, dst, *sigs = mapper_device_signals(dev, MAPPER_DIR_ANY);
    while (sigs && *sigs) {
        src = 0;
        maps = mapper_signal_maps(*sigs, MAPPER_DIR_INCOMING);
        if (maps) {
            slot = mapper_map_slot(*maps, MAPPER_LOC_SOURCE, 0);
            if (slot)
                src = mapper_slot_signal(slot);
        }
        if (src) {
            maps = mapper_signal_maps(*sigs, MAPPER_DIR_OUTGOING);
            while (maps && *maps) {
                dst = 0;
                slot = mapper_map_slot(*maps, MAPPER_LOC_DESTINATION, 0);
                if (slot)
                    dst = mapper_slot_signal(slot);
                if (dst) {
                    map = mapper_map_new(1, &src, 1, &dst);
                    int i, length, num_props = mapper_map_num_properties(*maps);
                    char type;
                    const char *name;
                    const void *value;
                    for (i = 0; i < num_props; i++) {
                        mapper_map_property_index(*maps, i, &name, &length,
                                                  &type, &value);
                        mapper_map_set_property(map, name, length, type, value, 1);
                    }
                    mapper_map_push(map);
                }
                maps = mapper_map_query_next(maps);
            }
        }
        sigs = mapper_signal_query_next(sigs);
    }
    printf("\nCleaning up!\n");
    mapper_device_free(dev);
    mapper_database_free(db);
}

void loop()
{
    createDevice();
    startDatabase();
    while (!done) {
        // poll libmapper outputs
        mapper_database_poll(db, 0);
        mapper_device_poll(dev, 100);
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

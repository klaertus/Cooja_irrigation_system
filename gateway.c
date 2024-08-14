


#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "lib/memb.h"
#include <string.h>
#include <stdlib.h> 
#include "sys/log.h" 
#include "net/linkaddr.h"
#include "protocol.h"
#include <stdio.h>
#include "net/packetbuf.h"
#include "dev/button-sensor.h"
#include "dev/serial-line.h"
#include "cpu/msp430/dev/uart0.h"


#define LOG_MODULE "Gateway"
#define LOG_LEVEL LOG_LEVEL_INFO
#define MOTE_ROLE TYPE_GATEWAY

node_t my_node;
routing_table_t my_table;
neighbors_list_t my_neighbors_list;

static struct etimer cleanup_timer;

// Callback function to be called when a packet is received
static void input_callback(const void *data, uint16_t len, const linkaddr_t *source, const linkaddr_t *destination){
    if(len < sizeof(packet_t)) return; // Invalid packet

    packet_t *packet = (packet_t *)data;
    int rssi = (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI);
    
    if (!check_packet(&my_node, &my_table, packet, len, rssi, source, destination)){
        return;
    }
    
    switch(packet->type) {

        case DISCOVERY_PACKET:
            send_discovery_response_packet(&my_node, &packet->source);
            break;

        case DISCOVERY_RESPONSE_PACKET:
            add_neighbor_to_list(&my_neighbors_list, &packet->source, rssi, extract_int_payload(packet));
            break;
        
        case CHILD_PACKET:
            send_who_is_there_packet(&my_node, &packet->source);
            break;
        
        case WHO_IS_THERE_RESPONSE_PACKET: {
            add_routing_entry(&my_table, source, &packet->source, extract_int_payload(packet));        
            break;
            }

        case KEEP_ALIVE_PACKET:
            add_routing_entry(&my_table, source, &packet->source, extract_int_payload(packet));
            if (linkaddr_cmp(source, &packet->source)){
                send_ack_keep_alive_packet(&my_node, &packet->source);
            }
            
            break;
        


        case ACK_PACKET:
        case DATA_PACKET:        
            packet_to_serial(packet);
        

    }




}



PROCESS(cleanup_process, "Cleanup Process");
PROCESS(main_process, "Main Process");


AUTOSTART_PROCESSES(&main_process);


PROCESS_THREAD(main_process, ev, data) {

    PROCESS_BEGIN();

    init_node(&my_node, &linkaddr_node_addr, MOTE_ROLE);

    nullnet_set_input_callback(input_callback);

    serial_line_init();
    uart0_set_input(serial_line_input_byte);
    process_start(&cleanup_process, NULL);

    init_network();

    while (1) {
            PROCESS_YIELD();
            if(ev==serial_line_event_message){
                char *input_str = (char *)data;
                if (data != NULL){
                    if (strcmp(input_str, "ROUTE") == 0) {
                        print_routing_table_formatted(&my_table);
                    } else{
                        packet_t *packet = parse(data);
                        if (packet->type != -1){
                            route_packet(packet, &my_table);
                        }
                        free(packet);
                    }
                }
            }
            
    }

    
    PROCESS_END();
}


PROCESS_THREAD(cleanup_process, ev, data) {


    PROCESS_BEGIN();

    etimer_set(&cleanup_timer, CLEAN_INTERVAL);

    while (1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&cleanup_timer));
        check_and_remove_expired_entries(&my_table);
        etimer_reset(&cleanup_timer);
    }

    PROCESS_END();
}

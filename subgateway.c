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
#include "dev/leds.h"
#include "sys/clock.h"

#define LOG_MODULE "Subgateway"
#define LOG_LEVEL LOG_LEVEL_INFO
#define MOTE_ROLE TYPE_SUBGATEWAY

node_t my_node;
routing_table_t my_table;
neighbors_list_t my_neighbors_list;


static struct etimer keep_alive_timer;
static struct etimer keep_alive_timeout_timer;
static struct etimer cleanup_timer;

int waiting_for_ack = 0;

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
            LOG_INFO("Received DISCOVERY_PACKET, send DISCOVERY_RESPONSE_PACKET packet\n");
            send_discovery_response_packet(&my_node, source);
            break;

        case DISCOVERY_RESPONSE_PACKET:
            LOG_INFO("Received DISCOVERY_RESPONSE_PACKET packet\n");
            add_neighbor_to_list(&my_neighbors_list, &packet->source, rssi, extract_int_payload(packet));
            break;
        
        case CHILD_PACKET:
            LOG_INFO("Received CHILD_PACKET, send WHO_IS_THERE_PACKET packet\n");
            send_who_is_there_packet(&my_node, &packet->source);
            break;
        
        case WHO_IS_THERE_PACKET:
            LOG_INFO("Received WHO_IS_THERE_PACKET, send WHO_IS_THERE_RESPONSE_PACKET packet\n");
            send_who_is_there_response_packet(&my_node, &packet->source);
            break;

        case WHO_IS_THERE_RESPONSE_PACKET:
            // source is the last node where the packet is forwrd, &packet->source is the original node where the packet has been sent
            LOG_INFO("Received WHO_IS_THERE_RESPONSE_PACKET, send packet to parent\n");
            add_routing_entry(&my_table, source, &packet->source, extract_int_payload(packet));
            send_to_parent(&my_node,packet);
            
            break;

        case KEEP_ALIVE_PACKET:
            LOG_INFO("Recevied KEEP_ALIVE_PACKET packet\n");
            add_routing_entry(&my_table, source, &packet->source, extract_int_payload(packet));
            if (linkaddr_cmp(source, &packet->source)){
                LOG_INFO("Send ACK_KEEP_ALIVE_PACKET packet to child\n");
                send_ack_keep_alive_packet(&my_node, &packet->source);
            }
            LOG_INFO("Send KEEP_ALIVE_PACKET packet to parent\n");
            send_to_parent(&my_node,packet);
            break;
        
        case ACK_KEEP_ALIVE_PACKET:
            LOG_INFO("Received ACK_KEEP_ALIVE_PACKET packet\n");
            if (linkaddr_cmp(source, &my_node.parent_address)) {
                waiting_for_ack = 0;
            }
            break;

    
        case COMMAND_PACKET:
            if (linkaddr_cmp(&packet->destination, &my_node.my_address)){
                LOG_INFO("Received COMMAND_PACKET packet, send to all my children\n");
                send_packet_to_children(packet, &my_table);

            } else {
                LOG_INFO("Received COMMAND_PACKET packet, route packet\n");
                route_packet(packet, &my_table);
            }

            
            break;
        


        case ACK_PACKET:
        case DATA_PACKET:
            
            send_to_parent(&my_node,packet);
            break;

    }
    return;



}
// Initialization function
PROCESS(keep_alive_process, "Keep Alive Process");
PROCESS(discovery_process, "Discovery Process");
PROCESS(cleanup_process, "Cleanup Process");
PROCESS(main_process, "Main Process");



AUTOSTART_PROCESSES(&main_process);

PROCESS_THREAD(main_process, ev, data) {
    
    PROCESS_BEGIN();

    init_node(&my_node, &linkaddr_node_addr, MOTE_ROLE);

    nullnet_set_input_callback(input_callback);

    process_start(&discovery_process, NULL);
    process_start(&cleanup_process, NULL);
    process_start(&keep_alive_process, NULL);

    PROCESS_END();
}



PROCESS_THREAD(keep_alive_process, ev, data) {
    PROCESS_BEGIN();
    
    etimer_set(&keep_alive_timer, KEEP_ALIVE_SEND_INTERVAL);
    etimer_set(&keep_alive_timeout_timer, KEEP_ALIVE_WAIT_FOR_ACK_TIMEOUT);

    while (1) {

        PROCESS_YIELD_UNTIL(etimer_expired(&keep_alive_timer));
        if (!linkaddr_cmp(&my_node.parent_address, &linkaddr_null)) {
            LOG_INFO("SEND KEEP_ALIVE_PACKET\n");
            send_keep_alive_packet(&my_node);
            waiting_for_ack = 1;
            etimer_restart(&keep_alive_timeout_timer);
            PROCESS_YIELD_UNTIL(etimer_expired(&keep_alive_timeout_timer));

            if (waiting_for_ack){
                linkaddr_copy(&my_node.parent_address, &linkaddr_null);
                process_start(&discovery_process, NULL);
            }

        }
        
        etimer_restart(&keep_alive_timer);
    }

    PROCESS_END();
}





PROCESS_THREAD(cleanup_process, ev, data) {
    

    PROCESS_BEGIN();

    etimer_set(&cleanup_timer, CLEAN_INTERVAL);

    while (1) {
        PROCESS_YIELD_UNTIL(etimer_expired(&cleanup_timer));

        LOG_INFO("Cleanup routing table \n");
        check_and_remove_expired_entries(&my_table);
        etimer_reset(&cleanup_timer);
    }

    PROCESS_END();
}




PROCESS_THREAD(discovery_process, ev, data) {
    static struct etimer discovery_timer;

    PROCESS_BEGIN();

    while (linkaddr_cmp(&my_node.parent_address, &linkaddr_null)){
        reset_neighbor_list(&my_neighbors_list);
        init_node(&my_node, &linkaddr_node_addr, MOTE_ROLE);

        LOG_INFO("Send DISCOVERY_PACKET packet\n");
        send_discovery_packet(&my_node);

        etimer_set(&discovery_timer, DISCOVERY_WAIT_FOR_RESPONSE_TIMEOUT);

        PROCESS_YIELD_UNTIL(etimer_expired(&discovery_timer));

        select_best_parent(&my_node, &my_neighbors_list);

    }

    LOG_INFO("Send CHILD_PACKET packet\n");
    send_child_packet(&my_node);
    


    PROCESS_END();
}
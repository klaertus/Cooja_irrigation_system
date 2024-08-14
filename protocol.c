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

#define LOG_MODULE "Network"
#define LOG_LEVEL LOG_LEVEL_INFO



// parse a address from string to linkaddr_t
void parse_address(char *addr_str, linkaddr_t *addr) {
    int i;

    for (i = 0; i < LINKADDR_SIZE; i++) {
        char byte_str[3] = { addr_str[i * 3], addr_str[i * 3 + 1], '\0' };
        addr->u8[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }
}


// parse a command payload and save information in device_type, action and additional_data
void parse_command_payload(char* payload, int* device_type, int* action, int* additional_data) {
    char* token;
    char msg[64];
    strcpy(msg, payload);
    token = strtok(msg, "|");
    if (token != NULL) *device_type = atoi(token);

    token = strtok(NULL, "|");
    if (token != NULL) *action = atoi(token);

    token = strtok(NULL, "|");
    if (token != NULL) *additional_data = atoi(token);

}


void add_suffix_to_payload(char* new_payload, const char* old_payload, const char* suffix) {
    strcpy(new_payload, old_payload);
    strcat(new_payload, suffix);
}


// Parse the received message by serial, and save a new packet
packet_t* parse(char *msg) {
    char *token;
    int packet_type = -1;
    char source[18], destination[18], payload_hex[128];
    int payload_length = 0;

    token = strtok(msg, "/");
    if (token != NULL) packet_type = atoi(token);

    token = strtok(NULL, "/");
    if (token != NULL) strcpy(source, token);

    token = strtok(NULL, "/");
    if (token != NULL) strcpy(destination, token);

    token = strtok(NULL, "/");
    if (token != NULL) payload_length = atoi(token);

    token = strtok(NULL, "/");
    if (token != NULL) strcpy(payload_hex, token);

    packet_t *packet = (packet_t *)malloc(sizeof(packet_t) + payload_length);
    if (packet == NULL) {
        return NULL;
    }

    packet->type = packet_type;

    linkaddr_t addr;
    parse_address(source, &addr);
    linkaddr_copy(&packet->source, &addr);

    parse_address(destination, &packet->destination);

    memcpy(packet->payload, payload_hex, payload_length);
    packet->payload_length = payload_length;

    return packet;
}




// check if a linkaddr_t is contained in my routing table
int is_from_my_child(routing_table_t *my_table, const linkaddr_t* source) {
    for (int i = 0; i < my_table->size; i++) {
        if (linkaddr_cmp(&my_table->entries[i].next_hop, source)) {
            return 1;  
        }
    }
    return 0;  
}


void send_broadcast(packet_t *packet) {
  nullnet_buf = (uint8_t *)packet;
  nullnet_len = sizeof(packet_t) + packet->payload_length;
  NETSTACK_NETWORK.output(NULL);

}

void send_unicast(const linkaddr_t *destination, packet_t *packet) {
  
  nullnet_buf = (uint8_t *)packet;
  nullnet_len = sizeof(packet_t) + packet->payload_length;
  //LOG_INFO("Send packet type %d, payload_length : %d, to %02x:%02x, packet destination : %02x:%02x, packet source : %02x:%02x\n", packet->type, packet->payload_length, destination->u8[0], destination->u8[1], packet->destination.u8[0], packet->destination.u8[1], packet->source.u8[0], packet->source.u8[1]);
  NETSTACK_NETWORK.output(destination);

}


// init node informations
void init_node(node_t *my_node, const linkaddr_t *my_address, int role) {
  linkaddr_copy(&my_node->my_address, my_address);
  my_node->role = role;
  linkaddr_copy(&my_node->parent_address, &linkaddr_null);
  my_node->parent_role = 0;
  my_node->parent_rssi = -128;
}


void send_discovery_packet(node_t *my_node){

    int payload_size = 0;
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t) + payload_size);
    
    packet->type = DISCOVERY_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, &linkaddr_null); 
    packet->payload_length = payload_size; 

    send_broadcast(packet);
    free(packet);
}


void send_discovery_response_packet(node_t *my_node, const linkaddr_t *destination){

    int payload_size = sizeof(int);
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t) + payload_size);
    
    packet->type = DISCOVERY_RESPONSE_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, destination); 
    memcpy(packet->payload, &my_node->role, payload_size);
    packet->payload_length = payload_size; 

    send_unicast(&packet->destination, packet);
    free(packet);

}

int extract_int_payload(packet_t *packet) {
    int payload;
    if (packet->payload_length != sizeof(int)) {
        return 0; 
    }
    memcpy(&payload, packet->payload, sizeof(int));
    return payload;
}



void select_best_parent(node_t *my_node, neighbors_list_t *neighbors_list) {
    neighbor_t *best_neighbor = NULL;
    best_neighbor->rssi = -128;

    for (int i = 0; i < neighbors_list->size; i++) {
        neighbor_t *neighbor = &neighbors_list->entries[i];
        
        if (my_node->role == TYPE_LIGHT_BULB || my_node->role == TYPE_IRRIGATION_SYSTEM || my_node->role == TYPE_LIGHT_SENSOR) {
            if (neighbor->role == TYPE_SUBGATEWAY) {
                if (neighbor->rssi > best_neighbor->rssi || best_neighbor->role != TYPE_SUBGATEWAY) {
                    best_neighbor = neighbor;
                }
            }
            else if ((neighbor->role == TYPE_LIGHT_BULB || neighbor->role == TYPE_IRRIGATION_SYSTEM || neighbor->role == TYPE_LIGHT_SENSOR) && best_neighbor->role != TYPE_SUBGATEWAY) {
                if (neighbor->rssi > best_neighbor->rssi) {
                    best_neighbor = neighbor;
                } 
            }
        } else if (my_node->role == TYPE_SUBGATEWAY) {
            if (neighbor->role == TYPE_GATEWAY) {
                if (neighbor->rssi > best_neighbor->rssi) {
                    best_neighbor = neighbor;
                } 
            } 
        } else if (my_node->role == TYPE_MOBILE ) {
            if (neighbor->role == TYPE_SUBGATEWAY) {
                if (neighbor->rssi > best_neighbor->rssi) {
                    best_neighbor = neighbor;
                } 
            }
        }
    }
    
    if (best_neighbor != NULL) {

        linkaddr_copy(&my_node->parent_address, &best_neighbor->address);
        my_node->parent_rssi = best_neighbor->rssi;
        my_node->parent_role = best_neighbor->role;
        LOG_INFO("Selected parent: %02x:%02x, Role: %d, RSSI: %d\n",  my_node->parent_address.u8[0], my_node->parent_address.u8[1], my_node->parent_role, my_node->parent_rssi);
    } 
}


void print_neighbors_list(neighbors_list_t *list) {
    printf("Neighbors List (size: %d):\n", list->size);
    for(int i = 0; i < list->size; i++) {
        const neighbor_t *neighbor = &list->entries[i];
        printf("Neighbor %d - Address: %02x:%02x, RSSI: %d, Role: %d\n", i, neighbor->address.u8[0], neighbor->address.u8[1], neighbor->rssi, neighbor->role);
    }
}


void send_child_packet(node_t *my_node){
    if (linkaddr_cmp(&my_node->parent_address, &linkaddr_null)){
        return;
    }

    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t));
    
    packet->type = CHILD_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, &my_node->parent_address); 
    packet->payload_length = 0;
    send_unicast(&packet->destination, packet);
    free(packet);

}

// send keep_alive_packet to my parent
void send_keep_alive_packet(node_t *my_node){
    if (linkaddr_cmp(&my_node->parent_address, &linkaddr_null)){
        return;
    }
    
    int payload_size = sizeof(int);
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t) + payload_size);
    
    packet->type = KEEP_ALIVE_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, &my_node->parent_address); 
    memcpy(packet->payload, &my_node->role, payload_size);
    packet->payload_length = payload_size; 

    send_unicast(&packet->destination, packet);
    free(packet);


}

void send_ack_keep_alive_packet(node_t *my_node, const linkaddr_t *destination){
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t));
    
    packet->type = ACK_KEEP_ALIVE_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, destination); 
    packet->payload_length = 0;
    send_unicast(&packet->destination, packet);
    free(packet);

}

void send_who_is_there_packet(node_t *my_node, const linkaddr_t *destination){
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t));
    
    packet->type = WHO_IS_THERE_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, destination);
    packet->payload_length = 0;

    send_unicast(&packet->destination, packet);
    free(packet);

}


void send_who_is_there_response_packet(node_t *my_node, const linkaddr_t *destination){
    int payload_size = sizeof(int);
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t) + payload_size);
    
    packet->type = WHO_IS_THERE_RESPONSE_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, destination); 
    memcpy(packet->payload, &my_node->role, payload_size);
    packet->payload_length = payload_size; 

    send_unicast(&packet->destination, packet);
    free(packet);

}



// add a new routing entries to the routing table, when a WHO_IS_THERE_RESPONSE is received
void add_routing_entry(routing_table_t *my_table, const linkaddr_t *next_hop, const linkaddr_t *destination, int role){
    if (my_table->size >= MAX_CHILDREN) {
        //printf("Routing table is full. Cannot add new entry.\n");
        return;
    }
    
    for (int i = 0; i < my_table->size; i++) {
        if (linkaddr_cmp(&my_table->entries[i].destination, destination)) {
            linkaddr_copy(&my_table->entries[i].next_hop, next_hop);
            my_table->entries[i].last_keep_alive = clock_time();
            my_table->entries[i].role = role;
            return;
        }
    }

    linkaddr_copy(&my_table->entries[my_table->size].destination, destination);
    linkaddr_copy(&my_table->entries[my_table->size].next_hop, next_hop);
    my_table->entries[my_table->size].last_keep_alive = clock_time();
    my_table->entries[my_table->size].role = role;
    my_table->size++;

}


// used periodically to remove old entries form routing table
void check_and_remove_expired_entries(routing_table_t *my_table) {
    clock_time_t current_time = clock_time();
    for (int i = 0; i < my_table->size; ) {
        if ((current_time - my_table->entries[i].last_keep_alive) > NEIGHBOR_TIMEOUT) {
            my_table->entries[i] = my_table->entries[my_table->size - 1];
            my_table->size--;
        } else {
            i++;
        }
    }
}


// add a new neigbor to the list
void add_neighbor_to_list(neighbors_list_t *my_neighbors_list, const linkaddr_t *source, int rssi, int role) {
    if (my_neighbors_list == NULL || my_neighbors_list->size >= MAX_NEIGHBORS) {
        return;
    }

    // Check if the neighbor already exists
    for (int i = 0; i < my_neighbors_list->size; i++) {
        if (memcmp(&my_neighbors_list->entries[i].address, source, sizeof(linkaddr_t)) == 0) {
            my_neighbors_list->entries[i].rssi = rssi;
            return;
        }
    }

    memcpy(&my_neighbors_list->entries[my_neighbors_list->size].address, source, sizeof(linkaddr_t));
    my_neighbors_list->entries[my_neighbors_list->size].rssi = rssi;
    my_neighbors_list->entries[my_neighbors_list->size].role = role;

    my_neighbors_list->size++;
}


// route packet to correct node
void route_packet(packet_t *packet, routing_table_t *my_table) {

    

    if (linkaddr_cmp(&packet->destination, &linkaddr_null)){

        packet_t *packet_to_send = (packet_t *)malloc(sizeof(packet_t) + packet->payload_length);

        packet_to_send->type = packet->type;
        linkaddr_copy(&packet_to_send->source, &packet->source);
        linkaddr_copy(&packet_to_send->destination, &packet->destination); 
        memcpy(packet_to_send->payload, packet->payload, packet->payload_length);
        packet_to_send->payload_length = packet->payload_length; 
        //LOG_INFO("Send packet to my children \n");
        for (int i = 0; i < my_table->size; i++) {
            
            if (linkaddr_cmp(&my_table->entries[i].destination, &my_table->entries[i].next_hop)){
                

                send_unicast(&my_table->entries[i].destination, packet_to_send);
                
            }

        }
        free(packet_to_send);


    } else {

        for (int i = 0; i < my_table->size; i++) {
            if (linkaddr_cmp(&packet->destination, &my_table->entries[i].destination)) {
                send_unicast(&my_table->entries[i].next_hop, packet);
                return;
            }
        }
        

        //LOG_ERR("No route found for destination %02x:%02x\n", packet->destination.u8[0], packet->destination.u8[1]);
    }
}

// send a packet to all my children
void send_packet_to_children(packet_t *packet, routing_table_t *my_table) {
        //LOG_INFO("Send packet to my children \n");
        for (int i = 0; i < my_table->size; i++) {
            
            if (linkaddr_cmp(&my_table->entries[i].destination, &my_table->entries[i].next_hop)){
                packet_t *packet_to_send = (packet_t *)malloc(sizeof(packet_t) + packet->payload_length);

                packet_to_send->type = packet->type;
                linkaddr_copy(&packet_to_send->source, &packet->source);
                linkaddr_copy(&packet_to_send->destination, &linkaddr_null); 
                memcpy(packet_to_send->payload, packet->payload, packet->payload_length);
                packet_to_send->payload_length = packet->payload_length; 

                send_unicast(&my_table->entries[i].destination, packet_to_send);
                free(packet_to_send);
                //LOG_INFO("Send packet to children %02x:%02x\n", my_table->entries[i].destination.u8[0], my_table->entries[i].destination.u8[1]);
            }

        }
    
}

// used to discover new parents, so needs to delete old list
void reset_neighbor_list(neighbors_list_t *my_neighbors_list) {
    if (my_neighbors_list == NULL) {
        return;
    }

    my_neighbors_list->size = 0;

    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        linkaddr_copy(&my_neighbors_list->entries[i].address, &linkaddr_null);
        my_neighbors_list->entries[i].rssi = -128;
        my_neighbors_list->entries[i].role = 0;
    }
}




bool check_packet(node_t *my_node, routing_table_t *my_table, packet_t *packet, uint16_t len, int rssi, const linkaddr_t* source, const linkaddr_t* destination) {
    //printf("RSSI: %d, Source Address: %02x:%02x, Destination Address: %02x:%02x, Packet Type: %d, Packet Source: %02x:%02x, Packet Destination: %02x:%02x\n",
    //   rssi,
    //   source->u8[0], source->u8[1],
    //   destination->u8[0], destination->u8[1],
    //   packet->type,
    //   packet->source.u8[0], packet->source.u8[1],
    //   packet->destination.u8[0], packet->destination.u8[1]);


    if (rssi < -85){
        //LOG_INFO("reject RRSI : %d\n", rssi);
        return false;
    }

    if ((packet->type > 11) || (packet->type < 0)){
        //LOG_INFO("Incorrect packet type, ignoring\n");
        return false;
    }


    switch (my_node->role) {
        case TYPE_MOBILE:
                    return true;
        
        case TYPE_IRRIGATION_SYSTEM:
        case TYPE_LIGHT_SENSOR:
        case TYPE_LIGHT_BULB:
            return true;

        case TYPE_SUBGATEWAY:
            return true;
        
        case TYPE_GATEWAY:
            
                switch (packet->type) {
                    case WHO_IS_THERE_PACKET:
                    case COMMAND_PACKET:
                    case ACK_KEEP_ALIVE_PACKET:
                        //LOG_INFO("GATEWAY received inappropriate packet type %d, ignoring\n", packet->type);
                        return false;
                    default:
                        return true;

                }
            
        
        default:
            //LOG_INFO("Unknown node role %d\n", my_node->role);
            return false;
    }
}


void init_network() {

    static uint8_t dummy_data[] = "";
    static linkaddr_t broadcast_addr;
    linkaddr_copy(&broadcast_addr, &linkaddr_null);

    LOG_INFO("Sending dummy packet to initialize network\n");
    nullnet_buf = (uint8_t *)&dummy_data;
    nullnet_len = sizeof(dummy_data);
    NETSTACK_NETWORK.output(&broadcast_addr);

    LOG_INFO("Network initialization complete\n");
}


void print_routing_table_formatted(routing_table_t* my_table){
    for (int i = 0; i < my_table->size; i++) {
        routing_entry_t *entry = &my_table->entries[i];
        printf("%02x:%02x/%02x:%02x/%d", entry->next_hop.u8[0], entry->next_hop.u8[1], entry->destination.u8[0], entry->destination.u8[1], entry->role);
        if (i < my_table->size - 1) {
                printf("|");
            }
        }
        printf("\n");
}

void packet_to_serial(packet_t* packet){
    printf("%d/%02x:%02x/%02x:%02x/%d/", 
        packet->type, 
        packet->source.u8[0], packet->source.u8[1], 
        packet->destination.u8[0], packet->destination.u8[1], 
        packet->payload_length);

        for (int i = 0; i < packet->payload_length; i++) {
                printf("%c", packet->payload[i]);
        }
        printf("\n");

}


void print_routing_table(const routing_table_t *table) {
    printf("Routing Table (Size: %d)\n", table->size);
    printf("----------------------------------------------------------\n");
    printf("| Destination | Next Hop    | Last Keep Alive   |  Role  |\n");
    printf("----------------------------------------------------------\n");

    for (int i = 0; i < table->size; i++) {
        const routing_entry_t *entry = &table->entries[i];
        printf("| %02x:%02x      | %02x:%02x      | %lu             |  %d     |\n",entry->destination.u8[0], entry->destination.u8[1], entry->next_hop.u8[0], entry->next_hop.u8[1], (unsigned long)entry->last_keep_alive, entry->role);
    }
    printf("----------------------------------------------------------\n");
}


void send_to_parent(node_t *my_node, packet_t* packet){
    if (linkaddr_cmp(&my_node->parent_address, &linkaddr_null)){
        return;
    }
    else {
        send_unicast(&my_node->parent_address, packet);
    }


}


void send_ack_packet(node_t *my_node, char* payload) {
    if (linkaddr_cmp(&my_node->parent_address, &linkaddr_null)){
        return;
    }

    int payload_size = strlen(payload);
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t) + payload_size);

    packet->type = ACK_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, &my_node->parent_address); 
    memcpy(packet->payload, payload, payload_size);
    packet->payload_length = payload_size; 

    send_unicast(&my_node->parent_address, packet);
    free(packet);


}


void send_data_packet(node_t *my_node, char* payload) {
    if (linkaddr_cmp(&my_node->parent_address, &linkaddr_null)){
        return;
    }

    int payload_size = strlen(payload);
    packet_t *packet;
    packet = (packet_t *)malloc(sizeof(packet_t) + payload_size);
    
    packet->type = DATA_PACKET;
    linkaddr_copy(&packet->source, &my_node->my_address);
    linkaddr_copy(&packet->destination, &my_node->parent_address); 
    memcpy(packet->payload, payload, payload_size);
    packet->payload_length = payload_size; 

    send_unicast(&my_node->parent_address, packet);
    free(packet);

}
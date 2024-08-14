#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include "contiki.h"
#include "net/linkaddr.h"

#define MAX_PAYLOAD_SIZE 64
#define MAX_CHILDREN 20
#define MAX_NEIGHBORS 20





#define TYPE_GATEWAY 1
#define TYPE_SUBGATEWAY 2
#define TYPE_MOBILE 3
#define TYPE_LIGHT_BULB 4
#define TYPE_LIGHT_SENSOR 5
#define TYPE_IRRIGATION_SYSTEM 6


#define DISCOVERY_PACKET 0 // broadcast to all neighbor or unicast for response to broadcast DISCOVERY_PACKET used by a node to discover her neighbors and announce her presence
#define DISCOVERY_RESPONSE_PACKET 1

#define CHILD_PACKET 2 //(child -> parent) used by a child node to announce his parent that he is his child

#define WHO_IS_THERE_PACKET 3
#define WHO_IS_THERE_RESPONSE_PACKET 4

#define KEEP_ALIVE_PACKET 6 // unicast (child -> parent) send by child to parent to keep the connection between them and keep the child in the routing table
#define ACK_KEEP_ALIVE_PACKET 7  // unicast (parent -> child) sent by parent to child to acknowledge the keep_alive

#define DATA_PACKET 8 //(sensor -> server) used to send data from sensor to server

#define COMMAND_PACKET 9 //(server -> sensor) used to send a command from the server to light, irrigation
#define ACK_PACKET 10 //(sensor -> server) used to acknowledge the command



#define KEEP_ALIVE_SEND_INTERVAL (CLOCK_SECOND * 60) // Interval to send KEEP_ALIVE_PACKET to parent
#define KEEP_ALIVE_WAIT_FOR_ACK_TIMEOUT (CLOCK_SECOND * 5) 

#define NEIGHBOR_TIMEOUT (CLOCK_SECOND * 120) // How many seconds the node wait without KEEP_ALIVE_ACK before delete his neigboor

#define CLEAN_INTERVAL (CLOCK_SECOND * 120) // Interval to clean the routing table from old entries
#define DISCOVERY_WAIT_FOR_RESPONSE_TIMEOUT (CLOCK_SECOND * 5) 



typedef struct {
    int type;
    linkaddr_t source; // original source stays the same between subgateway
    linkaddr_t destination; // 0 for broadcast, original destination stays the same between subgateway
    int payload_length;
    char payload[];  
} packet_t;

typedef struct {
    linkaddr_t my_address;
    int role;
    linkaddr_t parent_address;
    int parent_role;
    int parent_rssi;
} node_t;

typedef struct {
    linkaddr_t destination;
    linkaddr_t next_hop;
    clock_time_t last_keep_alive;
    int role;
} routing_entry_t;

typedef struct {
    routing_entry_t entries[MAX_CHILDREN];
    int size;
} routing_table_t;


typedef struct {
    linkaddr_t address;
    int rssi;
    int role;
} neighbor_t;

typedef struct {
    neighbor_t entries[MAX_NEIGHBORS];
    int size;
} neighbors_list_t;


// Useful
void parse_address(char *addr_str, linkaddr_t *addr);
packet_t* parse(char *msg);
int extract_int_payload(packet_t *packet);
void add_suffix_to_payload(char* new_payload, const char* old_payload, const char* suffix);
void parse_command_payload(char* payload, int* device_type, int* action, int* additional_data);

// init
void init_network();
void init_node(node_t *my_node, const linkaddr_t *my_address, int role);

// checkup
int is_from_my_child(routing_table_t *my_table, const linkaddr_t* source);
bool check_packet(node_t *my_node, routing_table_t *my_table, packet_t *packet, uint16_t len, int rssi, const linkaddr_t* source, const linkaddr_t* destination);


//print and log
void print_neighbors_list(neighbors_list_t *list);
void packet_to_serial(packet_t* packet);
void print_routing_table(const routing_table_t *table);
void print_routing_table_formatted(routing_table_t* my_table);

// other send
void send_broadcast(packet_t *packet);
void send_unicast(const linkaddr_t *destination, packet_t *packet);


void send_to_parent(node_t *my_node, packet_t* packet);
void send_packet_to_children(packet_t *packet, routing_table_t *my_table);

void route_packet(packet_t *packet, routing_table_t *my_table);


// discovery process
void send_discovery_packet(node_t *my_node);
void send_discovery_response_packet(node_t *my_node, const linkaddr_t *destination);

// neighbors and parent selection
void add_neighbor_to_list(neighbors_list_t *my_neighbors_list, const linkaddr_t *source, int rssi, int role);
void reset_neighbor_list(neighbors_list_t *my_neighbors_list);
void select_best_parent(node_t *my_node, neighbors_list_t *neighbors_list);

// discovers other nodes
void send_child_packet(node_t *my_node);
void send_who_is_there_packet(node_t *my_node, const linkaddr_t *destination);
void send_who_is_there_response_packet(node_t *my_node, const linkaddr_t *destination);

// routing table management
void add_routing_entry(routing_table_t *my_table, const linkaddr_t *next_hop, const linkaddr_t *destination, int role);
void check_and_remove_expired_entries(routing_table_t *my_table);

// keep alive
void send_keep_alive_packet(node_t *my_node);
void send_ack_keep_alive_packet(node_t *my_node, const linkaddr_t *destination);

// ack
void send_ack_packet(node_t *my_node, char* payload);

//data
void send_data_packet(node_t *my_node, char* payload);


#endif /* PROTOCOL_H_ */

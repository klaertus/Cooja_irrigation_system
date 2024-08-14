import socket
import argparse
import sys
import time
import schedule
import re
from collections import defaultdict
import threading


# Node types
TYPE_GATEWAY = 1
TYPE_SUBGATEWAY = 2
TYPE_MOBILE = 3
TYPE_LIGHT_BULB = 4
TYPE_LIGHT_SENSOR = 5
TYPE_IRRIGATION_SYSTEM = 6

# Packet types
DISCOVERY_PACKET = 0  # broadcast to all neighbors or unicast for response to broadcast DISCOVERY_PACKET used by a node to discover its neighbors and announce its presence
DISCOVERY_RESPONSE_PACKET = 1

CHILD_PACKET = 2  # (child -> parent) used by a child node to announce to its parent that it is their child

WHO_IS_THERE_PACKET = 3
WHO_IS_THERE_RESPONSE_PACKET = 4


KEEP_ALIVE_PACKET = 6  # unicast (child -> parent) sent by child to parent to maintain the connection and keep the child in the routing table
ACK_KEEP_ALIVE_PACKET = 7  # unicast (parent -> child) sent by parent to child to acknowledge the keep_alive

DATA_PACKET = 8  # (sensor -> server) used to send data from the sensor to the server

COMMAND_PACKET = 9  # (server -> sensor) used to send a command from the server to light, irrigation
ACK_PACKET = 10  # (sensor -> server) used to acknowledge the command


# 1 START
# 2 STOP not implemented in this project

# payload for command : DEVICE_TYPE|COMMAND|Additionnal_data
# paylod for data : DEVICE_TYPE|COMMAND|Additionnal_data|1 for received command
#DEVICE_TYPE|COMMAND|Additionnal_data|2 for completed command


def recv(sock):
    sock.settimeout(1.0)  # Timeout
    buf = b""
    try:
        while True:
            data = sock.recv(1)
            if not data or data.decode("utf-8") == "\n":
                break
            buf += data
    except socket.timeout:
        return False
    except Exception as e:
        return False
    
    return buf.decode("utf-8")

class Server:

    def __init__(self, ip, port):
        self.devices = {}
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((ip, port))
        self.sock.send(b"\n")
        self._stop_flag = False
        self.packets = []

    #send packet to the gateway
    def send(self, packet_type, destination_address, payload):

        source_address = "00:00"
        payload = payload + "\n"
        message = f"{packet_type}/{source_address}/{destination_address}/{len(payload)}/{payload}"
        print(f"Sending message: {message}")
        encoded = message.encode()

        self.sock.send(encoded)


    # decode packet received
    def decode_message(self):

        while not self._stop_flag:
            try:
                data = recv(self.sock)

                if not data:
                    continue

                parts = data.split('/')
                if len(parts) != 5:
                    continue

                packet_type = int(parts[0])
                source = parts[1]
                destination = parts[2]
                payload_length = int(parts[3])
                payload = parts[4]

                packet = {
                    'type': packet_type,
                    'source': source,
                    'destination': destination,
                    'payload_length': payload_length,
                    'payload': payload
                }
                self.packets.append(packet)
            except:
                pass
    


    # decode data payload (send by sensor to server)
    def decode_data_payload(self, payload):
        parts = payload.split('|')
        if len(parts) != 2:
            return False

        device_type = parts[0]
        data = parts[1]

        return {
            'device_type': int(device_type),
            'light_level': int(parts[1])
        }

    # decode ack payload
    def decode_ack_payload(self, payload):

        initial_command, status = payload.rsplit('|', 1)

        return {
            'command': initial_command,
            'status': int(status)
        }


    # get from received packets a specific packet
    def get_packets_type(self, packet_type, source=None, payload=None):
        packets = []
        subgateways = [details[0] for details in self.devices.values()]
        for packet in self.packets:
            #print(packet_type, source, payload)
            if packet["type"] == packet_type and (packet["source"] == source or not source or source == "00:00" or source in subgateways) and (packet["payload"] == payload or not payload):
                packets.append(packet)
                self.packets.remove(packet)
        return packets

    # Wait until all device have send ACK packet for the specific command
    def wait_ack(self, device_type, address, command, confirmation):
        
        subgateways = [details[0] for details in self.devices.values()]

        if address == "00:00":
            acks = [node for node, details in self.devices.items() if details[1] == device_type]
        # address is subgateway    
        elif any(details[0] == address for details in self.devices.values()):
            acks = [node for node, details in self.devices.items() if details[1] == device_type and details[0] == address]
        #address is simple node
        else:
            acks = [node for node, details in self.devices.items() if details[1] == device_type and node == address]
        while len(acks) > 0:
            packets = self.get_packets_type(ACK_PACKET, address, str(command) + "|" + str(confirmation))
            #print(self.packets)
            if len(packets) > 0:
                    print(f'Received ack from : {packets[0]["source"]}')
                    acks.remove(packets[0]["source"])
            time.sleep(1)
        return True


    # Send a command to start irrigation system and wait for the responses
    def start_irrigation(self, time):
        print("Start irrigation system ...\n")
        command = f"{TYPE_IRRIGATION_SYSTEM}|1|{time}"
        address = "00:00"
        self.send(COMMAND_PACKET, "00:00", command)
        self.wait_ack(TYPE_IRRIGATION_SYSTEM, address, command, 1)
        print("Irrigation system started\n")
        self.wait_ack(TYPE_IRRIGATION_SYSTEM, address, command, 2)
        print("Irrigation successfully completed\n")

    # send a command to start light bulb
    def start_light_bulb(self, address, time):
        print("Start light bulb ...\n")
        command = f"{TYPE_LIGHT_BULB}|1|{time}"
        self.send(COMMAND_PACKET, address, command)
        t = self.wait_ack(TYPE_LIGHT_BULB, address, command, 1)
        print("Light buld started\n")
        t = self.wait_ack(TYPE_LIGHT_BULB, address, command, 2)
        print("Light buld stopped\n")
        
    




    def list_devices(self):

        role_names = {TYPE_SUBGATEWAY:"TYPE_SUBGATEWAY", TYPE_MOBILE: "TYPE_MOBILE", TYPE_LIGHT_SENSOR: "TYPE_LIGHT_SENSOR", TYPE_IRRIGATION_SYSTEM: "TYPE_IRRIGATION_SYSTEM", TYPE_LIGHT_BULB: "TYPE_LIGHT_BULB"}

        subgateways = defaultdict(list)
        for device_address, (subgateway_address, role) in self.devices.items():
            if device_address != subgateway_address or role != TYPE_SUBGATEWAY:  # 2 corresponds to TYPE_SUBGATEWAY
                subgateways[subgateway_address].append((device_address, role))

        print(f"{'Subgateway Address':<20} | {'Device Address':<15} | {'Role'}")
        print("-" * 60)

        for subgateway_address, devices in subgateways.items():
            for device_address, role in devices:
                role_name = role_names.get(role, f"Unknown ({role})")
                print(f"{subgateway_address:<20} | {device_address:<15} | {role_name} ({role})")
            print("-" * 60)

    def menu(self):
        while True:
            print("\nChoose an action:")
            print("1. List greenhouses and devices addresses")
            print("2. Start irrigation system")
            print("3. Start light")
            print("4. Start automatic system (irrigatin system and light management)")
            print("5. Quit")

            choice = input("Enter your choice: ")
            
            if choice == '1':
                self.list_devices()
            elif choice == '2':
                self.start_irrigation(10)
            elif choice == '3':
                print("Specify address (00:00 for all, greenhouse address for only devices on this greenhouse or specific device address)")
                address = input("Enter your choice: ")
                print("Specify time to stay on (in sec):")
                time = input("Enter your choice: ")
                pattern_address = r'^\d{2}:\d{2}$'
                pattern_time = r'\b\d+\b'
                if re.match(pattern_address, address) and re.match(pattern_time, time):
                    self.start_light_bulb(address, time)
                    input("Press enter to continue ...")
                else:
                    print("Incorrect address format. Returning to menu\n")
            elif choice == '4':
                self.automatic_irrigation()
            elif choice == '5':
                print("Exiting...")
                self._stop_flag = True
                sys.exit()
            else:
                print("Invalid choice. Please try again.")

    def test(self):

        self.get_routing_table()        
        self.decode_thread = threading.Thread(target=self.decode_message)
        self.decode_thread.start()
        self.menu()
        #self.automatic_irrigation()



    def get_routing_table(self):
        
        try:
            self.sock.send(b'ROUTE\n')

            data = recv(self.sock)
            time.sleep(2)
            if not data:
                print("No data returned. Retry ...")
                self.get_routing_table()
            print(data)
            pattern = r'([0-9A-Fa-f]{2}:[0-9A-Fa-f]{2})/([0-9A-Fa-f]{2}:[0-9A-Fa-f]{2})/(\d+)'
            
            if re.fullmatch(f'{pattern}(\\|{pattern})*', data):                
                blocks = data.split('|')
                for block in blocks:
                    gateway_address, device_address, role = block.split('/')
                    self.devices[device_address] = [gateway_address, int(role)]
                
                print("Devices dictionary updated:")
                for device, info in self.devices.items():
                    print(f"Device: {device}, Gateway: {info[0]}, Role: {info[1]}")
                
                return self.devices
            else:
                print("Error. Retry ..")
                self.get_routing_table()
        except:
            print("Error. Retry ...")
            self.get_routing_table()
        



    def automatic_irrigation(self):
        
        update = 0

        schedule.every().day.at("05:00").do(self.start_irrigation, 10)
        while True:
            packets = self.get_packets_type(DATA_PACKET)
            for packet in packets:
                try:
                    light_data = self.decode_data_payload(packet["payload"])
                    if light_data["device_type"] == TYPE_LIGHT_SENSOR:
                        try:
                            greenhouse_address = self.devices[packet["source"]][0]
                            print(f'Light level from light sensor {packet["source"]} in greenhouse {greenhouse_address} : {light_data["light_level"]}')
                            if light_data["light_level"] < 20:
                                print(f"Light level is above 20. Starting the light in the greenhouse : {greenhouse_address}")
                                self.start_light_bulb(greenhouse_address, 10)
                        except:
                            print(f'Can\'t get the greenhouse address of the light sensor : {packet["source"]}')
                            
                except:
                    print(f'Packet incorrect. Pass')


            schedule.run_pending()
            update +=1
            time.sleep(1)
            # update routing table every min
            if update == 60:
                self._stop_flag = True
                time.sleep(2)
                self.get_routing_table()
                update = 0
                self._stop_flag = False
                self.decode_thread = threading.Thread(target=self.decode_message)
                self.decode_thread.start()
            


        







parser = argparse.ArgumentParser()
parser.add_argument("--ip", dest="ip", type=str)
parser.add_argument("--port", dest="port", type=int)
args = parser.parse_args()

print("Starting server ...")
server = Server(args.ip, args.port)
server.test()
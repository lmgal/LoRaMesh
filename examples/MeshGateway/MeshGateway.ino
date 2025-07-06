#include <SPI.h>
#include <LoRaMesh.h>

const int csPin = 7;
const int resetPin = 6;
const int irqPin = 1;

LoRaMesh mesh;

uint8_t gatewayAddress = 0xFF;

struct NodeStatus {
  uint8_t address;
  unsigned long lastSeen;
  int rssi;
};

NodeStatus nodes[10];
int nodeCount = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("LoRa Mesh Gateway");
  Serial.print("Gateway address: 0x");
  Serial.println(gatewayAddress, HEX);
  
  // Configure LoRa pins before initializing mesh
  mesh.setPins(csPin, resetPin, irqPin);
  
  if (!mesh.begin(915E6, gatewayAddress)) {
    Serial.println("Starting LoRa Mesh failed!");
    while (1);
  }
  
  Serial.println("LoRa Mesh Gateway started.");
  Serial.println("Listening for mesh traffic...");
}

void loop() {
  uint8_t buf[LORAMESH_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);
  uint8_t source, dest, id;
  
  if (mesh.recvFromAck(buf, &len, &source, &dest, &id)) {
    int rssi = LoRa.packetRssi();
    
    updateNodeStatus(source, rssi);
    
    Serial.println("=== Gateway Received ===");
    Serial.print("From: 0x");
    Serial.println(source, HEX);
    Serial.print("To: 0x");
    Serial.println(dest, HEX);
    Serial.print("ID: ");
    Serial.println(id);
    Serial.print("Message: ");
    for (int i = 0; i < len; i++) {
      Serial.print((char)buf[i]);
    }
    Serial.println();
    Serial.print("RSSI: ");
    Serial.println(rssi);
    Serial.println("====================");
    
    if (dest == gatewayAddress) {
      String response = "ACK from gateway at " + String(millis());
      mesh.sendToWait(source, (uint8_t*)response.c_str(), response.length());
    }
  }
  
  mesh.process();
  
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 20000) {
    printNetworkStatus();
    lastStatusPrint = millis();
  }
  
  if (Serial.available()) {
    handleSerialCommand();
  }
}

void updateNodeStatus(uint8_t address, int rssi) {
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].address == address) {
      nodes[i].lastSeen = millis();
      nodes[i].rssi = rssi;
      return;
    }
  }
  
  if (nodeCount < 10) {
    nodes[nodeCount].address = address;
    nodes[nodeCount].lastSeen = millis();
    nodes[nodeCount].rssi = rssi;
    nodeCount++;
  }
}

void printNetworkStatus() {
  Serial.println("\n=== Network Status ===");
  Serial.print("Active nodes: ");
  Serial.println(nodeCount);
  
  unsigned long now = millis();
  for (int i = 0; i < nodeCount; i++) {
    Serial.print("Node 0x");
    Serial.print(nodes[i].address, HEX);
    Serial.print(" - Last seen: ");
    Serial.print((now - nodes[i].lastSeen) / 1000);
    Serial.print("s ago, RSSI: ");
    Serial.println(nodes[i].rssi);
  }
  
  mesh.printRoutingTable();
  Serial.println("==================\n");
}

void handleSerialCommand() {
  String command = Serial.readStringUntil('\n');
  command.trim();
  
  if (command.startsWith("send ")) {
    String params = command.substring(5);
    int spaceIndex = params.indexOf(' ');
    if (spaceIndex > 0) {
      uint8_t dest = strtol(params.substring(0, spaceIndex).c_str(), NULL, 16);
      String message = params.substring(spaceIndex + 1);
      
      Serial.print("Sending to 0x");
      Serial.print(dest, HEX);
      Serial.print(": ");
      Serial.println(message);
      
      if (mesh.sendToWait(dest, (uint8_t*)message.c_str(), message.length())) {
        Serial.println("Message sent successfully");
      } else {
        Serial.println("Failed to send message");
      }
    }
  } else if (command == "status") {
    printNetworkStatus();
  } else if (command == "help") {
    Serial.println("Commands:");
    Serial.println("  send <hex_address> <message> - Send message to node");
    Serial.println("  status - Show network status");
    Serial.println("  help - Show this help");
  }
}
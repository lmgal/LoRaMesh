#include <SPI.h>

// Enable memory-constrained mode for minimal memory usage
#define LORAMESH_MEMORY_CONSTRAINED

#include <LoRaMesh.h>

const int csPin = 7;
const int resetPin = 6;
const int irqPin = 1;

LoRaMesh mesh;

uint8_t myAddress = 0x01;
uint8_t destinationAddress = 0x02;

unsigned long lastSendTime = 0;
int sendInterval = 15000; // Slower sending for memory-constrained mode

void setup() {
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("LoRa Mesh Node (Memory-Constrained Mode)");
  Serial.print("My address: 0x");
  Serial.println(myAddress, HEX);
  Serial.println("Memory usage: ~732 bytes (68% reduction)");
  
  // Configure LoRa pins before initializing mesh
  mesh.setPins(csPin, resetPin, irqPin);
  
  if (!mesh.begin(915E6, myAddress)) {
    Serial.println("Starting LoRa Mesh failed!");
    while (1);
  }
  
  Serial.println("LoRa Mesh init succeeded.");
}

void loop() {
  if (millis() - lastSendTime > sendInterval) {
    String message = "Hello from constrained node 0x" + String(myAddress, HEX);
    
    Serial.print("Sending to 0x");
    Serial.print(destinationAddress, HEX);
    Serial.print(": ");
    Serial.println(message);
    
    if (mesh.sendToWait(destinationAddress, (uint8_t*)message.c_str(), message.length())) {
      Serial.println("Message sent successfully");
    } else {
      Serial.println("Failed to send message");
    }
    
    lastSendTime = millis();
  }
  
  uint8_t buf[LORAMESH_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);
  uint8_t source, dest, id;
  
  if (mesh.recvFromAck(buf, &len, &source, &dest, &id)) {
    Serial.println("=== Received Message ===");
    Serial.print("From: 0x");
    Serial.println(source, HEX);
    Serial.print("Message: ");
    for (int i = 0; i < len; i++) {
      Serial.print((char)buf[i]);
    }
    Serial.println();
    Serial.println("====================");
  }
  
  mesh.process();
  
  static unsigned long lastTablePrint = 0;
  if (millis() - lastTablePrint > 60000) { // Less frequent table printing
    mesh.printRoutingTable();
    lastTablePrint = millis();
  }
}
#include <SPI.h>
#include <LoRaMesh.h>

const int csPin = 7;
const int resetPin = 6;
const int irqPin = 1;

LoRaMesh mesh;

uint8_t myAddress = 0x01;
uint8_t destinationAddress = 0x03;

unsigned long lastSendTime = 0;
int sendInterval = 10000;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("LoRa Mesh Node");
  Serial.print("My address: 0x");
  Serial.println(myAddress, HEX);
  
  // Configure LoRa pins before initializing mesh
  mesh.setPins(csPin, resetPin, irqPin);
  
  // Optional: Use custom SPI
  // mesh.setSPI(SPI1);
  // mesh.setSPIFrequency(8E6);
  
  if (!mesh.begin(915E6, myAddress)) {
    Serial.println("Starting LoRa Mesh failed!");
    while (1);
  }
  
  Serial.println("LoRa Mesh init succeeded.");
}

void loop() {
  if (millis() - lastSendTime > sendInterval) {
    String message = "Hello from node 0x" + String(myAddress, HEX) + " at " + String(millis());
    
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
    sendInterval = random(5000) + 5000;
  }
  
  uint8_t buf[LORAMESH_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);
  uint8_t source, dest, id;
  
  if (mesh.recvFromAck(buf, &len, &source, &dest, &id)) {
    Serial.println("=== Received Message ===");
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
    Serial.println(LoRa.packetRssi());
    Serial.println("====================");
  }
  
  mesh.process();
  
  static unsigned long lastTablePrint = 0;
  if (millis() - lastTablePrint > 30000) {
    mesh.printRoutingTable();
    lastTablePrint = millis();
  }
}
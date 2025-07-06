#include <SPI.h>
#include <LoRaMesh.h>

const int csPin = 7;
const int resetPin = 6;
const int irqPin = 1;

LoRaMesh mesh;

#define NODE_ADDRESS_PIN A0

uint8_t myAddress;
uint8_t targetNodes[] = {0x01, 0x02, 0x03, 0x04, 0x05};
int targetIndex = 0;

unsigned long lastSendTime = 0;
unsigned long lastBroadcastTime = 0;
int messageCount = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  
  int analogValue = analogRead(NODE_ADDRESS_PIN);
  myAddress = map(analogValue, 0, 1023, 1, 5);
  
  Serial.println("LoRa Multi-Node Mesh Demo");
  Serial.print("My address (from analog pin): 0x");
  Serial.println(myAddress, HEX);
  Serial.println("Connect different resistor values to A0 to set node addresses 1-5");
  
  // Configure LoRa pins before initializing mesh
  mesh.setPins(csPin, resetPin, irqPin);
  
  if (!mesh.begin(915E6, myAddress)) {
    Serial.println("Starting LoRa Mesh failed!");
    while (1);
  }
  
  mesh.setRetries(3);
  mesh.setRetryTimeout(500);
  
  Serial.println("LoRa Mesh started successfully");
  Serial.println("This node will send messages to other nodes in sequence");
  Serial.println("and broadcast periodic status messages");
}

void loop() {
  if (millis() - lastSendTime > 15000) {
    uint8_t targetAddress = targetNodes[targetIndex];
    
    if (targetAddress != myAddress) {
      String message = "MSG#" + String(messageCount++) + " from 0x" + 
                      String(myAddress, HEX) + " via mesh";
      
      Serial.print("\n>>> Sending unicast to 0x");
      Serial.print(targetAddress, HEX);
      Serial.print(": ");
      Serial.println(message);
      
      if (mesh.sendToWait(targetAddress, (uint8_t*)message.c_str(), message.length())) {
        Serial.println("    SUCCESS - Message delivered");
      } else {
        Serial.println("    FAILED - Could not find route");
      }
    }
    
    targetIndex = (targetIndex + 1) % 5;
    lastSendTime = millis();
  }
  
  if (millis() - lastBroadcastTime > 30000) {
    String broadcast = "Status: Node 0x" + String(myAddress, HEX) + 
                      " alive, msg count: " + String(messageCount);
    
    Serial.print("\n>>> Broadcasting: ");
    Serial.println(broadcast);
    
    mesh.sendToWait(LORAMESH_BROADCAST_ADDRESS, 
                   (uint8_t*)broadcast.c_str(), broadcast.length());
    
    lastBroadcastTime = millis();
  }
  
  uint8_t buf[LORAMESH_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);
  uint8_t source, dest, id;
  
  if (mesh.recvFromAck(buf, &len, &source, &dest, &id)) {
    Serial.println("\n<<< Received Message <<<");
    Serial.print("From: 0x");
    Serial.print(source, HEX);
    Serial.print(" To: 0x");
    Serial.println(dest, HEX);
    Serial.print("Type: ");
    if (dest == LORAMESH_BROADCAST_ADDRESS) {
      Serial.println("BROADCAST");
    } else {
      Serial.println("UNICAST");
    }
    Serial.print("Content: ");
    for (int i = 0; i < len; i++) {
      Serial.print((char)buf[i]);
    }
    Serial.println();
    Serial.print("Signal: ");
    Serial.print(LoRa.packetRssi());
    Serial.println(" dBm");
    Serial.println("<<<<<<<<<<<<<<<<<<<<");
    
    if (dest == myAddress && source != myAddress) {
      delay(random(100, 500));
      String reply = "Reply from 0x" + String(myAddress, HEX) + 
                    " to msg#" + String(id);
      mesh.sendToWait(source, (uint8_t*)reply.c_str(), reply.length());
    }
  }
  
  mesh.process();
  
  static unsigned long lastTablePrint = 0;
  if (millis() - lastTablePrint > 60000) {
    Serial.println("\n=== Current Routing Table ===");
    mesh.printRoutingTable();
    lastTablePrint = millis();
  }
}
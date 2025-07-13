#ifndef LORAMESH_H
#define LORAMESH_H

#include <Arduino.h>
#include <LoRa.h>

#define LORAMESH_MAX_MESSAGE_LEN 251
#define LORAMESH_ROUTING_TABLE_SIZE 10
#define LORAMESH_MAX_HOPS 10
#define LORAMESH_ROUTE_TIMEOUT 30000
#define LORAMESH_ROUTE_DISCOVERY_TIMEOUT 5000
#define LORAMESH_BROADCAST_ADDRESS 0xFF
#define LORAMESH_ACK_TIMEOUT 300
#define LORAMESH_MAX_ACK_RETRIES 3
#define LORAMESH_MESSAGE_BUFFER_SIZE 5
#define LORAMESH_PENDING_QUEUE_SIZE 3

enum MessageType {
    MESSAGE_TYPE_DATA = 0x00,
    MESSAGE_TYPE_ROUTE_REQUEST = 0x01,
    MESSAGE_TYPE_ROUTE_REPLY = 0x02,
    MESSAGE_TYPE_ROUTE_FAILURE = 0x03,
    MESSAGE_TYPE_ACK = 0x04
};

enum RouteState {
    ROUTE_STATE_INVALID = 0x00,
    ROUTE_STATE_DISCOVERING = 0x01,
    ROUTE_STATE_VALID = 0x02
};

struct RoutingEntry {
    uint8_t destination;
    uint8_t nextHop;
    uint8_t hopCount;
    RouteState state;
    unsigned long lastSeen;
};

struct MeshHeader {
    uint8_t destination;
    uint8_t source;
    uint8_t messageId;
    uint8_t messageType;
    uint8_t hopCount;
    uint8_t visitedNodes[LORAMESH_MAX_HOPS];
    uint8_t visitedCount;
};

class LoRaMesh {
public:
    LoRaMesh();
    
    bool begin(long frequency, uint8_t address);
    void setAddress(uint8_t address);
    uint8_t getAddress();
    
    void setSPI(SPIClass& spi);
    void setPins(int ss = LORA_DEFAULT_SS_PIN, int reset = LORA_DEFAULT_RESET_PIN, int dio0 = LORA_DEFAULT_DIO0_PIN);
    void setSPIFrequency(uint32_t frequency);
    
    bool sendToWait(uint8_t destination, const uint8_t* data, uint8_t len, uint8_t* flags = NULL);
    bool recvFromAck(uint8_t* buf, uint8_t* len, uint8_t* source = NULL, uint8_t* dest = NULL, uint8_t* id = NULL, uint8_t* flags = NULL);
    
    bool available();
    void process();
    
    RoutingEntry* getRoutingTable();
    uint8_t getRoutingTableSize();
    void printRoutingTable();
    
    void setRetries(uint8_t retries);
    void setRetryTimeout(uint16_t timeout);
    
private:
    uint8_t _address;
    uint8_t _messageId;
    uint8_t _retries;
    uint16_t _retryTimeout;
    
    RoutingEntry _routingTable[LORAMESH_ROUTING_TABLE_SIZE];
    
    // Message buffering - circular buffer for received messages
    struct MessageBuffer {
        MeshHeader header;
        uint8_t data[LORAMESH_MAX_MESSAGE_LEN];
        uint8_t dataLen;
        bool valid;
        unsigned long timestamp;
    };
    MessageBuffer _rxBuffer[LORAMESH_MESSAGE_BUFFER_SIZE];
    uint8_t _rxBufferHead;
    uint8_t _rxBufferTail;
    
    // ACK tracking
    struct AckTracker {
        uint8_t destination;
        uint8_t messageId;
        bool ackReceived;
        unsigned long timestamp;
    } _ackTracker;
    
    // Pending messages waiting for route discovery
    struct PendingMessage {
        uint8_t destination;
        uint8_t data[LORAMESH_MAX_MESSAGE_LEN];
        uint8_t dataLen;
        uint8_t messageId;
        bool valid;
        unsigned long timestamp;
    };
    PendingMessage _pendingQueue[LORAMESH_PENDING_QUEUE_SIZE];
    
    struct {
        uint8_t destination;
        unsigned long startTime;
        uint8_t messageId;
        bool active;
    } _routeDiscovery;
    
    bool sendPacket(MeshHeader& header, const uint8_t* data, uint8_t len);
    bool sendPacketWithAck(MeshHeader& header, const uint8_t* data, uint8_t len);
    bool receivePacket();
    void sendAck(uint8_t destination, uint8_t messageId);
    
    void handleDataMessage(MeshHeader& header, uint8_t* data, uint8_t len);
    void handleRouteRequest(MeshHeader& header);
    void handleRouteReply(MeshHeader& header);
    void handleRouteFailure(MeshHeader& header, uint8_t* data, uint8_t len);
    void handleAck(MeshHeader& header);
    
    void extractRoutesFromPath(MeshHeader& header, bool isRequest);
    void addToMessageBuffer(MeshHeader& header, uint8_t* data, uint8_t len);
    bool getFromMessageBuffer(uint8_t* buf, uint8_t* len, uint8_t* source, uint8_t* dest, uint8_t* id);
    void addToPendingQueue(uint8_t destination, const uint8_t* data, uint8_t len, uint8_t messageId);
    void processPendingMessages();
    
    bool startRouteDiscovery(uint8_t destination);
    void updateRoutingTable(uint8_t destination, uint8_t nextHop, uint8_t hopCount);
    RoutingEntry* findRoute(uint8_t destination);
    void clearRoute(uint8_t destination);
    void cleanupRoutingTable();
    
    bool isNodeVisited(MeshHeader& header, uint8_t node);
    void addVisitedNode(MeshHeader& header, uint8_t node);
    
    uint8_t getNextMessageId();
};

#endif
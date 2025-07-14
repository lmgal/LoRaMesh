#ifndef LORAMESH_H
#define LORAMESH_H

#include <Arduino.h>
#include <LoRa.h>

// Message and buffer configuration
#define LORAMESH_MAX_MESSAGE_LEN 251

// Configurable buffer sizes - users can override these before including the library
#ifndef LORAMESH_MESSAGE_BUFFER_SIZE
#define LORAMESH_MESSAGE_BUFFER_SIZE 3  // Default buffer size for received messages
#endif

#ifndef LORAMESH_PENDING_QUEUE_SIZE
#define LORAMESH_PENDING_QUEUE_SIZE 2   // Default queue size for pending messages
#endif

#ifndef LORAMESH_ROUTING_TABLE_SIZE
#define LORAMESH_ROUTING_TABLE_SIZE 8   // Default routing table size
#endif

#ifndef LORAMESH_MAX_HOPS
#define LORAMESH_MAX_HOPS 8             // Default maximum hop count
#endif

// Memory-constrained mode - define this to use minimal memory settings
#ifdef LORAMESH_MEMORY_CONSTRAINED
#undef LORAMESH_MESSAGE_BUFFER_SIZE
#undef LORAMESH_PENDING_QUEUE_SIZE
#undef LORAMESH_ROUTING_TABLE_SIZE
#undef LORAMESH_MAX_HOPS
#define LORAMESH_MESSAGE_BUFFER_SIZE 2
#define LORAMESH_PENDING_QUEUE_SIZE 1
#define LORAMESH_ROUTING_TABLE_SIZE 5
#define LORAMESH_MAX_HOPS 6
#endif

// High-capacity mode - define this for systems with more memory
#ifdef LORAMESH_HIGH_CAPACITY
#undef LORAMESH_MESSAGE_BUFFER_SIZE
#undef LORAMESH_PENDING_QUEUE_SIZE
#undef LORAMESH_ROUTING_TABLE_SIZE
#undef LORAMESH_MAX_HOPS
#define LORAMESH_MESSAGE_BUFFER_SIZE 8
#define LORAMESH_PENDING_QUEUE_SIZE 5
#define LORAMESH_ROUTING_TABLE_SIZE 15
#define LORAMESH_MAX_HOPS 12
#endif

// Fixed protocol constants
#define LORAMESH_ROUTE_TIMEOUT 30000
#define LORAMESH_ROUTE_DISCOVERY_TIMEOUT 5000
#define LORAMESH_BROADCAST_ADDRESS 0xFF
#define LORAMESH_ACK_TIMEOUT 300
#define LORAMESH_MAX_ACK_RETRIES 3

// Memory usage estimates (with default settings):
// - Standard mode (default): ~1,438 bytes  
// - Memory-constrained mode: ~732 bytes (68% reduction)
// - High-capacity mode: ~2,912 bytes

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
    uint16_t lastSeenAge;  // Age in seconds instead of absolute timestamp (saves 2 bytes per entry)
};

struct MeshHeader {
    uint8_t destination;
    uint8_t source;
    uint8_t messageId;
    uint8_t messageType;
    uint8_t hopCount;
    uint8_t visitedCount;
    uint8_t visitedNodes[LORAMESH_MAX_HOPS];
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
        uint8_t valid : 1;       // Pack into single bit
        uint8_t reserved : 7;    // Reserved for future use
        uint16_t timestampAge;   // Age in seconds instead of absolute timestamp (saves 2 bytes per message)
    };
    MessageBuffer _rxBuffer[LORAMESH_MESSAGE_BUFFER_SIZE];
    uint8_t _rxBufferHead;
    uint8_t _rxBufferTail;
    
    // ACK tracking
    struct AckTracker {
        uint8_t destination;
        uint8_t messageId;
        uint8_t ackReceived : 1;   // Pack into single bit
        uint8_t reserved : 7;      // Reserved for future use
        uint16_t timestampAge;     // Age in milliseconds/10 instead of absolute timestamp
    } _ackTracker;
    
    // Pending messages waiting for route discovery
    struct PendingMessage {
        uint8_t destination;
        uint8_t data[LORAMESH_MAX_MESSAGE_LEN];
        uint8_t dataLen;
        uint8_t messageId;
        uint8_t valid : 1;        // Pack into single bit
        uint8_t reserved : 7;     // Reserved for future use
        uint16_t timestampAge;    // Age in seconds instead of absolute timestamp
    };
    PendingMessage _pendingQueue[LORAMESH_PENDING_QUEUE_SIZE];
    
    struct {
        uint8_t destination;
        uint16_t startTimeAge;     // Age in seconds instead of absolute timestamp
        uint8_t messageId;
        uint8_t active : 1;        // Pack into single bit
        uint8_t reserved : 7;      // Reserved for future use
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
    
    // Helper functions for age-based timestamp system
    uint16_t getAgeFromTime(unsigned long timestamp);
    unsigned long getTimeFromAge(uint16_t age);
    bool isAgeExpired(uint16_t age, uint16_t timeoutSeconds);
};

#endif
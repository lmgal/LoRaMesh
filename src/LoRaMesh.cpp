#include "LoRaMesh.h"

LoRaMesh::LoRaMesh() {
    _address = 0x00;
    _messageId = 0;
    _retries = 3;
    _retryTimeout = 200;
    _routeDiscovery.active = 0;
    
    // Initialize message buffer
    _rxBufferHead = 0;
    _rxBufferTail = 0;
    for (int i = 0; i < LORAMESH_MESSAGE_BUFFER_SIZE; i++) {
        _rxBuffer[i].valid = 0;
    }
    
    // Initialize pending queue
    for (int i = 0; i < LORAMESH_PENDING_QUEUE_SIZE; i++) {
        _pendingQueue[i].valid = 0;
    }
    
    // Initialize ACK tracker
    _ackTracker.ackReceived = 0;
    
    for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
        _routingTable[i].state = ROUTE_STATE_INVALID;
    }
}

bool LoRaMesh::begin(long frequency, uint8_t address) {
    _address = address;
    return LoRa.begin(frequency);
}

void LoRaMesh::setAddress(uint8_t address) {
    _address = address;
}

uint8_t LoRaMesh::getAddress() {
    return _address;
}

void LoRaMesh::setSPI(SPIClass& spi) {
    LoRa.setSPI(spi);
}

void LoRaMesh::setPins(int ss, int reset, int dio0) {
    LoRa.setPins(ss, reset, dio0);
}

void LoRaMesh::setSPIFrequency(uint32_t frequency) {
    LoRa.setSPIFrequency(frequency);
}

bool LoRaMesh::sendToWait(uint8_t destination, const uint8_t* data, uint8_t len, uint8_t* flags) {
    if (len > LORAMESH_MAX_MESSAGE_LEN) {
        return false;
    }
    
    if (destination == _address) {
        return false;
    }
    
    cleanupRoutingTable();
    
    MeshHeader header;
    header.destination = destination;
    header.source = _address;
    header.messageId = getNextMessageId();
    header.messageType = MESSAGE_TYPE_DATA;
    header.hopCount = 0;
    header.visitedCount = 0;
    
    RoutingEntry* route = findRoute(destination);
    
    if (!route || route->state != ROUTE_STATE_VALID) {
        // No route - add to pending queue and start discovery
        addToPendingQueue(destination, data, len, header.messageId);
        
        if (!startRouteDiscovery(destination)) {
            return false;
        }
        
        // Wait for route discovery
        unsigned long discoveryStart = millis();
        while (millis() - discoveryStart < LORAMESH_ROUTE_DISCOVERY_TIMEOUT) {
            process();
            route = findRoute(destination);
            if (route && route->state == ROUTE_STATE_VALID) {
                // Route found - message will be sent by processPendingMessages
                return true;
            }
            
            // Check if route discovery has been cleared (failed)
            if (!_routeDiscovery.active || 
                (_routeDiscovery.destination == destination && 
                 route && route->state == ROUTE_STATE_INVALID)) {
                // Route discovery failed
                return false;
            }
            
            delay(10);
        }
        
        // Timeout - clear the active discovery
        if (_routeDiscovery.active && _routeDiscovery.destination == destination) {
            _routeDiscovery.active = 0;
        }
        
        return false;
    }
    
    // We have a route - send immediately
    return sendPacketWithAck(header, data, len);
}

bool LoRaMesh::recvFromAck(uint8_t* buf, uint8_t* len, uint8_t* source, uint8_t* dest, uint8_t* id, uint8_t* flags) {
    process();
    
    return getFromMessageBuffer(buf, len, source, dest, id);
}

bool LoRaMesh::available() {
    process();
    // Check if there are any valid data messages in the buffer
    uint8_t temp = _rxBufferTail;
    while (temp != _rxBufferHead) {
        if (_rxBuffer[temp].valid && _rxBuffer[temp].header.messageType == MESSAGE_TYPE_DATA) {
            return true;
        }
        temp = (temp + 1) % LORAMESH_MESSAGE_BUFFER_SIZE;
    }
    return false;
}

void LoRaMesh::process() {
    receivePacket();
    processPendingMessages();
}

bool LoRaMesh::sendPacket(MeshHeader& header, const uint8_t* data, uint8_t len) {
    RoutingEntry* route = NULL;
    
    if (header.destination == LORAMESH_BROADCAST_ADDRESS || 
        header.messageType == MESSAGE_TYPE_ROUTE_REQUEST) {
        header.hopCount++;
        addVisitedNode(header, _address);
    } else {
        route = findRoute(header.destination);
        if (!route || route->state != ROUTE_STATE_VALID) {
            return false;
        }
    }
    
    LoRa.beginPacket();
    
    LoRa.write(header.destination);
    LoRa.write(header.source);
    LoRa.write(header.messageId);
    LoRa.write(header.messageType);
    LoRa.write(header.hopCount);
    LoRa.write(header.visitedCount);
    
    for (uint8_t i = 0; i < header.visitedCount; i++) {
        LoRa.write(header.visitedNodes[i]);
    }
    
    if (route) {
        LoRa.write(route->nextHop);
    } else {
        LoRa.write(LORAMESH_BROADCAST_ADDRESS);
    }
    
    LoRa.write(len);
    
    for (uint8_t i = 0; i < len; i++) {
        LoRa.write(data[i]);
    }
    
    return LoRa.endPacket();
}

bool LoRaMesh::sendPacketWithAck(MeshHeader& header, const uint8_t* data, uint8_t len) {
    // Messages that don't need ACK
    if (header.destination == LORAMESH_BROADCAST_ADDRESS || 
        header.messageType == MESSAGE_TYPE_ROUTE_REQUEST ||
        header.messageType == MESSAGE_TYPE_ACK) {
        return sendPacket(header, data, len);
    }
    
    // Find the next hop
    RoutingEntry* route = findRoute(header.destination);
    if (!route || route->state != ROUTE_STATE_VALID) {
        return false;
    }
    
    uint8_t nextHop = (header.destination == route->nextHop) ? header.destination : route->nextHop;
    
    // Try sending with ACK
    for (uint8_t retry = 0; retry <= LORAMESH_MAX_ACK_RETRIES; retry++) {
        // Setup ACK tracker
        _ackTracker.destination = nextHop;
        _ackTracker.messageId = header.messageId;
        _ackTracker.ackReceived = 0;
        _ackTracker.timestampAge = 0;
        
        // Send the packet
        if (!sendPacket(header, data, len)) {
            continue;
        }
        
        // Wait for ACK
        unsigned long ackStart = millis();
        while (millis() - ackStart < LORAMESH_ACK_TIMEOUT) {
            if (receivePacket()) {
                if (_ackTracker.ackReceived) {
                    return true;
                }
            }
            delay(10);
        }
    }
    
    // Failed to get ACK - notify route failure if this was a forwarded message
    if (header.source != _address && header.messageType == MESSAGE_TYPE_DATA) {
        MeshHeader failureHeader;
        failureHeader.destination = header.source;
        failureHeader.source = _address;
        failureHeader.messageId = getNextMessageId();
        failureHeader.messageType = MESSAGE_TYPE_ROUTE_FAILURE;
        failureHeader.hopCount = 0;
        failureHeader.visitedCount = 0;
        
        uint8_t failureData[1] = {header.destination};
        sendPacket(failureHeader, failureData, 1);
    }
    
    clearRoute(header.destination);
    return false;
}

bool LoRaMesh::receivePacket() {
    int packetSize = LoRa.parsePacket();
    if (packetSize == 0) return false;
    
    if (packetSize < 8) return false;
    
    MeshHeader header;
    header.destination = LoRa.read();
    header.source = LoRa.read();
    header.messageId = LoRa.read();
    header.messageType = LoRa.read();
    header.hopCount = LoRa.read();
    header.visitedCount = LoRa.read();
    
    if (header.visitedCount > LORAMESH_MAX_HOPS) return false;
    
    for (uint8_t i = 0; i < header.visitedCount; i++) {
        if (!LoRa.available()) return false;
        header.visitedNodes[i] = LoRa.read();
    }
    
    if (!LoRa.available()) return false;
    uint8_t nextHop = LoRa.read();
    
    if (!LoRa.available()) return false;
    uint8_t dataLen = LoRa.read();
    
    if (dataLen > LORAMESH_MAX_MESSAGE_LEN) return false;
    
    uint8_t data[LORAMESH_MAX_MESSAGE_LEN];
    for (uint8_t i = 0; i < dataLen; i++) {
        if (!LoRa.available()) return false;
        data[i] = LoRa.read();
    }
    
    if (header.hopCount > LORAMESH_MAX_HOPS) return false;
    
    // Learn direct route to immediate neighbor (the actual sender)
    if (header.source != _address && nextHop != LORAMESH_BROADCAST_ADDRESS && 
        nextHop != _address) {
        // For messages from immediate neighbors, next hop is the source
        if (header.hopCount == 1 || header.source == nextHop) {
            updateRoutingTable(header.source, header.source, 1);
        }
    }
    
    switch (header.messageType) {
        case MESSAGE_TYPE_DATA:
            handleDataMessage(header, data, dataLen);
            break;
        case MESSAGE_TYPE_ROUTE_REQUEST:
            handleRouteRequest(header);
            break;
        case MESSAGE_TYPE_ROUTE_REPLY:
            handleRouteReply(header);
            break;
        case MESSAGE_TYPE_ROUTE_FAILURE:
            handleRouteFailure(header, data, dataLen);
            break;
        case MESSAGE_TYPE_ACK:
            handleAck(header);
            break;
    }
    
    return true;
}

void LoRaMesh::handleDataMessage(MeshHeader& header, uint8_t* data, uint8_t len) {
    // First, send ACK if this message is for us or we're the next hop
    if (header.destination == _address || 
        (header.destination != LORAMESH_BROADCAST_ADDRESS && header.hopCount > 0)) {
        sendAck(header.source, header.messageId);
    }
    
    if (header.destination == _address || header.destination == LORAMESH_BROADCAST_ADDRESS) {
        // Store in message buffer
        addToMessageBuffer(header, data, len);
    }
    
    if (header.destination != _address && header.destination != LORAMESH_BROADCAST_ADDRESS) {
        // Forward the message
        header.hopCount++;
        if (!sendPacketWithAck(header, data, len)) {
            // Already handled in sendPacketWithAck
        }
    }
}

void LoRaMesh::handleRouteRequest(MeshHeader& header) {
    if (isNodeVisited(header, _address)) {
        return;
    }
    
    // Learn routes from the path in the route request
    extractRoutesFromPath(header, true);
    
    if (header.destination == _address) {
        // We are the destination - send a route reply
        MeshHeader replyHeader;
        replyHeader.destination = header.source;
        replyHeader.source = _address;
        replyHeader.messageId = header.messageId;
        replyHeader.messageType = MESSAGE_TYPE_ROUTE_REPLY;
        replyHeader.hopCount = 0;
        replyHeader.visitedCount = header.visitedCount + 1; // Include ourselves
        
        // Copy visited nodes and add ourselves
        memcpy(replyHeader.visitedNodes, header.visitedNodes, header.visitedCount);
        replyHeader.visitedNodes[header.visitedCount] = _address;
        
        uint8_t emptyData[1] = {0};
        sendPacket(replyHeader, emptyData, 0);
    } else {
        // Forward the request
        header.hopCount++;
        addVisitedNode(header, _address);
        
        uint8_t emptyData[1] = {0};
        sendPacket(header, emptyData, 0);
    }
}

void LoRaMesh::handleRouteReply(MeshHeader& header) {
    // Learn routes from the path in the route reply
    extractRoutesFromPath(header, false);
    
    if (header.destination == _address) {
        // This reply is for us
        if (_routeDiscovery.active && 
            _routeDiscovery.messageId == header.messageId) {
            _routeDiscovery.active = 0;
        }
    } else {
        // Forward the reply
        RoutingEntry* route = findRoute(header.destination);
        if (route && route->state == ROUTE_STATE_VALID) {
            uint8_t emptyData[1] = {0};
            sendPacketWithAck(header, emptyData, 0);
        }
    }
}

void LoRaMesh::handleRouteFailure(MeshHeader& header, uint8_t* data, uint8_t len) {
    // Send ACK for route failure message
    sendAck(header.source, header.messageId);
    
    if (header.destination == _address && len > 0) {
        // Clear the failed route
        clearRoute(data[0]);
    } else if (header.destination != _address) {
        // Forward the route failure message
        sendPacketWithAck(header, data, len);
    }
}

bool LoRaMesh::startRouteDiscovery(uint8_t destination) {
    // Check if there's an active route discovery
    if (_routeDiscovery.active) {
        // If the current discovery has timed out, clear it
        if (isAgeExpired(_routeDiscovery.startTimeAge, LORAMESH_ROUTE_DISCOVERY_TIMEOUT / 1000)) {
            _routeDiscovery.active = 0;
            // Clear the route state if it's still discovering
            RoutingEntry* route = findRoute(_routeDiscovery.destination);
            if (route && route->state == ROUTE_STATE_DISCOVERING) {
                route->state = ROUTE_STATE_INVALID;
            }
        } else if (_routeDiscovery.destination == destination) {
            // Already discovering this destination
            return true;
        } else {
            // Different destination requested while another is active
            return false;
        }
    }
    
    MeshHeader header;
    header.destination = destination;
    header.source = _address;
    header.messageId = getNextMessageId();
    header.messageType = MESSAGE_TYPE_ROUTE_REQUEST;
    header.hopCount = 0;
    header.visitedCount = 0;
    
    _routeDiscovery.destination = destination;
    _routeDiscovery.startTimeAge = 0;
    _routeDiscovery.messageId = header.messageId;
    _routeDiscovery.active = 1;
    
    RoutingEntry* route = findRoute(destination);
    if (!route) {
        for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
            if (_routingTable[i].state == ROUTE_STATE_INVALID) {
                route = &_routingTable[i];
                break;
            }
        }
    }
    
    if (route) {
        route->destination = destination;
        route->state = ROUTE_STATE_DISCOVERING;
        route->lastSeenAge = 0;
    }
    
    uint8_t emptyData[1] = {0};
    return sendPacket(header, emptyData, 0);
}

void LoRaMesh::updateRoutingTable(uint8_t destination, uint8_t nextHop, uint8_t hopCount) {
    RoutingEntry* route = findRoute(destination);
    
    if (!route) {
        for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
            if (_routingTable[i].state == ROUTE_STATE_INVALID) {
                route = &_routingTable[i];
                break;
            }
        }
        
        if (!route) {
            uint16_t oldestAge = 0;
            int oldestIndex = 0;
            for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
                if (_routingTable[i].lastSeenAge > oldestAge) {
                    oldestAge = _routingTable[i].lastSeenAge;
                    oldestIndex = i;
                }
            }
            route = &_routingTable[oldestIndex];
        }
    }
    
    if (route) {
        route->destination = destination;
        route->nextHop = nextHop;
        route->hopCount = hopCount;
        route->state = ROUTE_STATE_VALID;
        route->lastSeenAge = 0;
    }
}

RoutingEntry* LoRaMesh::findRoute(uint8_t destination) {
    for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
        if (_routingTable[i].destination == destination && 
            _routingTable[i].state != ROUTE_STATE_INVALID) {
            return &_routingTable[i];
        }
    }
    return NULL;
}

void LoRaMesh::clearRoute(uint8_t destination) {
    RoutingEntry* route = findRoute(destination);
    if (route) {
        route->state = ROUTE_STATE_INVALID;
    }
}

void LoRaMesh::cleanupRoutingTable() {
    for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
        if (_routingTable[i].state == ROUTE_STATE_VALID &&
            isAgeExpired(_routingTable[i].lastSeenAge, LORAMESH_ROUTE_TIMEOUT / 1000)) {
            _routingTable[i].state = ROUTE_STATE_INVALID;
        }
        // Update age for all entries
        if (_routingTable[i].state != ROUTE_STATE_INVALID) {
            _routingTable[i].lastSeenAge = min(_routingTable[i].lastSeenAge + 1, 65535);
        }
    }
    
    // Also check for timed out route discovery
    if (_routeDiscovery.active && 
        isAgeExpired(_routeDiscovery.startTimeAge, LORAMESH_ROUTE_DISCOVERY_TIMEOUT / 1000)) {
        _routeDiscovery.active = 0;
        // Clear the route state if it's still discovering
        RoutingEntry* route = findRoute(_routeDiscovery.destination);
        if (route && route->state == ROUTE_STATE_DISCOVERING) {
            route->state = ROUTE_STATE_INVALID;
        }
    }
    
    // Update route discovery age
    if (_routeDiscovery.active) {
        _routeDiscovery.startTimeAge = min(_routeDiscovery.startTimeAge + 1, 65535);
    }
}

bool LoRaMesh::isNodeVisited(MeshHeader& header, uint8_t node) {
    for (uint8_t i = 0; i < header.visitedCount; i++) {
        if (header.visitedNodes[i] == node) {
            return true;
        }
    }
    return false;
}

void LoRaMesh::addVisitedNode(MeshHeader& header, uint8_t node) {
    if (header.visitedCount < LORAMESH_MAX_HOPS && !isNodeVisited(header, node)) {
        header.visitedNodes[header.visitedCount++] = node;
    }
}

uint8_t LoRaMesh::getNextMessageId() {
    return _messageId++;
}

RoutingEntry* LoRaMesh::getRoutingTable() {
    return _routingTable;
}

uint8_t LoRaMesh::getRoutingTableSize() {
    return LORAMESH_ROUTING_TABLE_SIZE;
}

void LoRaMesh::printRoutingTable() {
    Serial.println("=== Routing Table ===");
    for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
        if (_routingTable[i].state != ROUTE_STATE_INVALID) {
            Serial.print("Dest: 0x");
            Serial.print(_routingTable[i].destination, HEX);
            Serial.print(" Next: 0x");
            Serial.print(_routingTable[i].nextHop, HEX);
            Serial.print(" Hops: ");
            Serial.print(_routingTable[i].hopCount);
            Serial.print(" State: ");
            switch (_routingTable[i].state) {
                case ROUTE_STATE_DISCOVERING:
                    Serial.print("DISCOVERING");
                    break;
                case ROUTE_STATE_VALID:
                    Serial.print("VALID");
                    break;
                default:
                    Serial.print("INVALID");
            }
            Serial.println();
        }
    }
    Serial.println("==================");
}

void LoRaMesh::setRetries(uint8_t retries) {
    _retries = retries;
}

void LoRaMesh::setRetryTimeout(uint16_t timeout) {
    _retryTimeout = timeout;
}

void LoRaMesh::sendAck(uint8_t destination, uint8_t messageId) {
    MeshHeader ackHeader;
    ackHeader.destination = destination;
    ackHeader.source = _address;
    ackHeader.messageId = messageId;
    ackHeader.messageType = MESSAGE_TYPE_ACK;
    ackHeader.hopCount = 0;
    ackHeader.visitedCount = 0;
    
    uint8_t emptyData[1] = {0};
    sendPacket(ackHeader, emptyData, 0);
}

void LoRaMesh::handleAck(MeshHeader& header) {
    if (_ackTracker.destination == header.source && 
        _ackTracker.messageId == header.messageId) {
        _ackTracker.ackReceived = 1;
    }
}

void LoRaMesh::addToMessageBuffer(MeshHeader& header, uint8_t* data, uint8_t len) {
    // Add to circular buffer
    _rxBuffer[_rxBufferHead].header = header;
    _rxBuffer[_rxBufferHead].dataLen = len;
    memcpy(_rxBuffer[_rxBufferHead].data, data, len);
    _rxBuffer[_rxBufferHead].valid = 1;
    _rxBuffer[_rxBufferHead].timestampAge = 0;
    
    _rxBufferHead = (_rxBufferHead + 1) % LORAMESH_MESSAGE_BUFFER_SIZE;
    
    // If buffer is full, advance tail
    if (_rxBufferHead == _rxBufferTail) {
        _rxBufferTail = (_rxBufferTail + 1) % LORAMESH_MESSAGE_BUFFER_SIZE;
    }
}

bool LoRaMesh::getFromMessageBuffer(uint8_t* buf, uint8_t* len, uint8_t* source, uint8_t* dest, uint8_t* id) {
    // Find the oldest valid DATA message
    while (_rxBufferTail != _rxBufferHead) {
        if (_rxBuffer[_rxBufferTail].valid && 
            _rxBuffer[_rxBufferTail].header.messageType == MESSAGE_TYPE_DATA) {
            
            if (len) {
                *len = min(*len, _rxBuffer[_rxBufferTail].dataLen);
                memcpy(buf, _rxBuffer[_rxBufferTail].data, *len);
            }
            
            if (source) *source = _rxBuffer[_rxBufferTail].header.source;
            if (dest) *dest = _rxBuffer[_rxBufferTail].header.destination;
            if (id) *id = _rxBuffer[_rxBufferTail].header.messageId;
            
            _rxBuffer[_rxBufferTail].valid = 0;
            _rxBufferTail = (_rxBufferTail + 1) % LORAMESH_MESSAGE_BUFFER_SIZE;
            return true;
        }
        _rxBufferTail = (_rxBufferTail + 1) % LORAMESH_MESSAGE_BUFFER_SIZE;
    }
    return false;
}

void LoRaMesh::addToPendingQueue(uint8_t destination, const uint8_t* data, uint8_t len, uint8_t messageId) {
    for (int i = 0; i < LORAMESH_PENDING_QUEUE_SIZE; i++) {
        if (!_pendingQueue[i].valid) {
            _pendingQueue[i].destination = destination;
            _pendingQueue[i].dataLen = len;
            memcpy(_pendingQueue[i].data, data, len);
            _pendingQueue[i].messageId = messageId;
            _pendingQueue[i].valid = 1;
            _pendingQueue[i].timestampAge = 0;
            break;
        }
    }
}

void LoRaMesh::processPendingMessages() {
    
    for (int i = 0; i < LORAMESH_PENDING_QUEUE_SIZE; i++) {
        if (_pendingQueue[i].valid) {
            // Check for timeout
            if (isAgeExpired(_pendingQueue[i].timestampAge, (LORAMESH_ROUTE_DISCOVERY_TIMEOUT * 3) / 1000)) {
                // Give up after 3x discovery timeout
                _pendingQueue[i].valid = 0;
                continue;
            }
            
            // Update age
            _pendingQueue[i].timestampAge = min(_pendingQueue[i].timestampAge + 1, 65535);
            
            // Check if we now have a route
            RoutingEntry* route = findRoute(_pendingQueue[i].destination);
            if (route && route->state == ROUTE_STATE_VALID) {
                MeshHeader header;
                header.destination = _pendingQueue[i].destination;
                header.source = _address;
                header.messageId = _pendingQueue[i].messageId;
                header.messageType = MESSAGE_TYPE_DATA;
                header.hopCount = 0;
                header.visitedCount = 0;
                
                sendPacketWithAck(header, _pendingQueue[i].data, _pendingQueue[i].dataLen);
                _pendingQueue[i].valid = 0;
            } else if (!route || route->state == ROUTE_STATE_INVALID) {
                // No route or invalid route - retry discovery if not active
                if (!_routeDiscovery.active || 
                    (_routeDiscovery.destination != _pendingQueue[i].destination &&
                     isAgeExpired(_routeDiscovery.startTimeAge, LORAMESH_ROUTE_DISCOVERY_TIMEOUT / 1000))) {
                    startRouteDiscovery(_pendingQueue[i].destination);
                }
            }
        }
    }
}

void LoRaMesh::extractRoutesFromPath(MeshHeader& header, bool isRequest) {
    // For route requests: learn reverse routes (back to source)
    // For route replies: learn forward routes (to all nodes in path)
    
    if (header.visitedCount == 0) return;
    
    if (isRequest) {
        // Route request: learn routes back to source through visited nodes
        uint8_t hopCount = 1;
        
        // Find our position in the visited nodes (if we're already there)
        int ourPosition = -1;
        for (int i = 0; i < header.visitedCount; i++) {
            if (header.visitedNodes[i] == _address) {
                ourPosition = i;
                break;
            }
        }
        
        // If we're not in the list yet, we're at the end
        if (ourPosition == -1) {
            ourPosition = header.visitedCount;
        }
        
        // Learn route to source
        if (ourPosition > 0) {
            uint8_t nextHop = header.visitedNodes[ourPosition - 1];
            updateRoutingTable(header.source, nextHop, ourPosition);
        } else {
            // We're the first hop from source
            updateRoutingTable(header.source, header.source, 1);
        }
        
        // Learn routes to all intermediate nodes
        for (int i = 0; i < ourPosition; i++) {
            if (i > 0) {
                updateRoutingTable(header.visitedNodes[i], header.visitedNodes[ourPosition - 1], ourPosition - i);
            }
        }
    } else {
        // Route reply: learn routes forward through the path
        // Find our position in the path
        int ourPosition = -1;
        for (int i = 0; i < header.visitedCount; i++) {
            if (header.visitedNodes[i] == _address) {
                ourPosition = i;
                break;
            }
        }
        
        if (ourPosition >= 0) {
            // Learn routes to all nodes after us in the path
            for (int i = ourPosition + 1; i < header.visitedCount; i++) {
                uint8_t nextHop = (ourPosition + 1 < header.visitedCount) ? 
                                  header.visitedNodes[ourPosition + 1] : header.source;
                updateRoutingTable(header.visitedNodes[i], nextHop, i - ourPosition);
            }
            
            // Learn route to the reply source (original destination)
            if (ourPosition + 1 < header.visitedCount) {
                updateRoutingTable(header.source, header.visitedNodes[ourPosition + 1], 
                                 header.visitedCount - ourPosition);
            } else {
                updateRoutingTable(header.source, header.source, 1);
            }
        }
    }
}

// Helper functions for age-based timestamp system
uint16_t LoRaMesh::getAgeFromTime(unsigned long timestamp) {
    unsigned long currentTime = millis();
    if (currentTime >= timestamp) {
        return min((currentTime - timestamp) / 1000, 65535UL);
    }
    // Handle rollover case
    return min(((0xFFFFFFFF - timestamp) + currentTime) / 1000, 65535UL);
}

unsigned long LoRaMesh::getTimeFromAge(uint16_t age) {
    unsigned long currentTime = millis();
    unsigned long ageMs = (unsigned long)age * 1000;
    if (currentTime >= ageMs) {
        return currentTime - ageMs;
    }
    // Handle rollover case
    return (0xFFFFFFFF - ageMs) + currentTime;
}

bool LoRaMesh::isAgeExpired(uint16_t age, uint16_t timeoutSeconds) {
    return age >= timeoutSeconds;
}
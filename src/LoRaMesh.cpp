#include "LoRaMesh.h"

LoRaMesh::LoRaMesh() {
    _address = 0x00;
    _messageId = 0;
    _retries = 3;
    _retryTimeout = 200;
    _rxBuffer.valid = false;
    _routeDiscovery.active = false;
    
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
    
    RoutingEntry* route = findRoute(destination);
    
    if (!route || route->state != ROUTE_STATE_VALID) {
        if (!startRouteDiscovery(destination)) {
            return false;
        }
        
        unsigned long discoveryStart = millis();
        while (millis() - discoveryStart < LORAMESH_ROUTE_DISCOVERY_TIMEOUT) {
            process();
            route = findRoute(destination);
            if (route && route->state == ROUTE_STATE_VALID) {
                break;
            }
            delay(10);
        }
        
        if (!route || route->state != ROUTE_STATE_VALID) {
            return false;
        }
    }
    
    MeshHeader header;
    header.destination = destination;
    header.source = _address;
    header.messageId = getNextMessageId();
    header.messageType = MESSAGE_TYPE_DATA;
    header.hopCount = 0;
    header.visitedCount = 0;
    
    for (uint8_t retry = 0; retry < _retries; retry++) {
        if (sendPacket(header, data, len)) {
            return true;
        }
        delay(_retryTimeout);
    }
    
    clearRoute(destination);
    return false;
}

bool LoRaMesh::recvFromAck(uint8_t* buf, uint8_t* len, uint8_t* source, uint8_t* dest, uint8_t* id, uint8_t* flags) {
    process();
    
    if (!_rxBuffer.valid) {
        return false;
    }
    
    if (_rxBuffer.header.messageType != MESSAGE_TYPE_DATA) {
        _rxBuffer.valid = false;
        return false;
    }
    
    if (len) {
        *len = min(*len, _rxBuffer.dataLen);
        memcpy(buf, _rxBuffer.data, *len);
    }
    
    if (source) *source = _rxBuffer.header.source;
    if (dest) *dest = _rxBuffer.header.destination;
    if (id) *id = _rxBuffer.header.messageId;
    
    _rxBuffer.valid = false;
    return true;
}

bool LoRaMesh::available() {
    process();
    return _rxBuffer.valid && _rxBuffer.header.messageType == MESSAGE_TYPE_DATA;
}

void LoRaMesh::process() {
    if (receivePacket()) {
    }
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
    
    if (header.source != _address && header.messageType != MESSAGE_TYPE_ROUTE_REQUEST) {
        updateRoutingTable(header.source, header.source, 1);
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
    }
    
    return true;
}

void LoRaMesh::handleDataMessage(MeshHeader& header, uint8_t* data, uint8_t len) {
    if (header.destination == _address || header.destination == LORAMESH_BROADCAST_ADDRESS) {
        _rxBuffer.header = header;
        _rxBuffer.dataLen = len;
        memcpy(_rxBuffer.data, data, len);
        _rxBuffer.valid = true;
        _rxBuffer.timestamp = millis();
    }
    
    if (header.destination != _address && header.destination != LORAMESH_BROADCAST_ADDRESS) {
        RoutingEntry* route = findRoute(header.destination);
        if (route && route->state == ROUTE_STATE_VALID) {
            header.hopCount++;
            sendPacket(header, data, len);
        } else {
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
    }
}

void LoRaMesh::handleRouteRequest(MeshHeader& header) {
    if (isNodeVisited(header, _address)) {
        return;
    }
    
    if (header.destination == _address) {
        MeshHeader replyHeader;
        replyHeader.destination = header.source;
        replyHeader.source = _address;
        replyHeader.messageId = header.messageId;
        replyHeader.messageType = MESSAGE_TYPE_ROUTE_REPLY;
        replyHeader.hopCount = 0;
        replyHeader.visitedCount = header.visitedCount;
        
        memcpy(replyHeader.visitedNodes, header.visitedNodes, header.visitedCount);
        
        uint8_t emptyData[1] = {0};
        sendPacket(replyHeader, emptyData, 0);
    } else {
        header.hopCount++;
        addVisitedNode(header, _address);
        
        uint8_t emptyData[1] = {0};
        sendPacket(header, emptyData, 0);
    }
}

void LoRaMesh::handleRouteReply(MeshHeader& header) {
    if (header.destination == _address) {
        if (_routeDiscovery.active && 
            _routeDiscovery.messageId == header.messageId) {
            
            uint8_t hopsToDest = 1;
            for (uint8_t i = header.visitedCount - 1; i > 0; i--) {
                if (header.visitedNodes[i] == _address) {
                    break;
                }
                hopsToDest++;
            }
            
            updateRoutingTable(header.source, header.source, hopsToDest);
            _routeDiscovery.active = false;
        }
    } else {
        RoutingEntry* route = findRoute(header.destination);
        if (route && route->state == ROUTE_STATE_VALID) {
            uint8_t emptyData[1] = {0};
            sendPacket(header, emptyData, 0);
        }
    }
}

void LoRaMesh::handleRouteFailure(MeshHeader& header, uint8_t* data, uint8_t len) {
    if (header.destination == _address && len > 0) {
        clearRoute(data[0]);
    }
}

bool LoRaMesh::startRouteDiscovery(uint8_t destination) {
    if (_routeDiscovery.active) {
        return false;
    }
    
    MeshHeader header;
    header.destination = destination;
    header.source = _address;
    header.messageId = getNextMessageId();
    header.messageType = MESSAGE_TYPE_ROUTE_REQUEST;
    header.hopCount = 0;
    header.visitedCount = 0;
    
    _routeDiscovery.destination = destination;
    _routeDiscovery.startTime = millis();
    _routeDiscovery.messageId = header.messageId;
    _routeDiscovery.active = true;
    
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
        route->lastSeen = millis();
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
            unsigned long oldestTime = millis();
            int oldestIndex = 0;
            for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
                if (_routingTable[i].lastSeen < oldestTime) {
                    oldestTime = _routingTable[i].lastSeen;
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
        route->lastSeen = millis();
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
    unsigned long now = millis();
    for (int i = 0; i < LORAMESH_ROUTING_TABLE_SIZE; i++) {
        if (_routingTable[i].state == ROUTE_STATE_VALID &&
            (now - _routingTable[i].lastSeen) > LORAMESH_ROUTE_TIMEOUT) {
            _routingTable[i].state = ROUTE_STATE_INVALID;
        }
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
/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_proto_handler_hpp
#define vnsw_agent_proto_handler_hpp

#include "pkt_handler.h"

// Pseudo header for UDP checksum
struct PseudoUdpHdr {
    in_addr_t src;
    in_addr_t dest;
    uint8_t   res;
    uint8_t   prot;
    uint16_t  len;
    PseudoUdpHdr(in_addr_t s, in_addr_t d, uint8_t p, uint16_t l) : 
        src(s), dest(d), res(0), prot(p), len(l) { };
};

// Pseudo header for TCP checksum
struct PseudoTcpHdr {
    in_addr_t src;
    in_addr_t dest;
    uint8_t   res;
    uint8_t   prot;
    uint16_t  len;
    PseudoTcpHdr(in_addr_t s, in_addr_t d, uint16_t l) : 
        src(s), dest(d), res(0), prot(6), len(l) { };
};

// Protocol handler, to process an incoming packet
// Each protocol has a handler derived from this
class ProtoHandler {
public:
    ProtoHandler(Agent *agent, PktInfo *info, boost::asio::io_service &io);
    ProtoHandler(Agent *agent, boost::asio::io_service &io);
    virtual ~ProtoHandler();

    virtual bool Run() = 0;

    void Send(uint16_t, uint16_t, uint16_t, uint16_t, PktHandler::PktModuleName);

    void EthHdr(const unsigned char *, const unsigned char *, const uint16_t);
    void IpHdr(uint16_t, in_addr_t, in_addr_t, uint8_t);
    void UdpHdr(uint16_t, in_addr_t, uint16_t, in_addr_t, uint16_t);
    void TcpHdr(in_addr_t, uint16_t, in_addr_t, uint16_t, bool , uint32_t, uint16_t);

    uint32_t Sum(uint16_t *, std::size_t, uint32_t);
    uint16_t Csum(uint16_t *, std::size_t, uint32_t);
    uint16_t UdpCsum(in_addr_t, in_addr_t, std::size_t, udphdr *);
    uint16_t TcpCsum(in_addr_t, in_addr_t, uint16_t , tcphdr *);

    void Swap();
    void SwapL4();
    void SwapIpHdr();
    void SwapEthHdr();

    Agent *agent() { return agent_; }
    uint32_t GetVrf() { return pkt_info_->GetAgentHdr().vrf;};
    uint16_t GetIntf() { return pkt_info_->GetAgentHdr().ifindex;};
    uint16_t GetLen() { return pkt_info_->len;};
    uint32_t GetCmdParam() { return pkt_info_->GetAgentHdr().cmd_param;};

protected:
    Agent   *agent_;
    PktInfo *pkt_info_;
    boost::asio::io_service &io_;

private:
    DISALLOW_COPY_AND_ASSIGN(ProtoHandler);
};

#endif // vnsw_agent_proto_handler_hpp

//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "inet/common/packet/chunk/BitCountChunk.h"
#include "inet/common/packet/dissector/ProtocolDissector.h"
#include "inet/common/packet/dissector/ProtocolDissectorRegistry.h"

// TODO: move individual dissectors into their respective protocol folders
#include "inet/linklayer/ethernet/EtherFrame_m.h"
#include "inet/linklayer/ethernet/EtherPhyFrame_m.h"
#include "inet/linklayer/ieee80211/mac/Ieee80211Frame_m.h"
#include "inet/linklayer/ieee8022/Ieee8022Llc.h"
#include "inet/linklayer/ieee8022/Ieee8022LlcHeader_m.h"
#include "inet/linklayer/ppp/PppFrame_m.h"
#include "inet/networklayer/arp/ipv4/ArpPacket_m.h"
#include "inet/networklayer/ipv4/IcmpHeader.h"
#include "inet/networklayer/ipv4/Ipv4Header_m.h"
#include "inet/physicallayer/ieee80211/packetlevel/Ieee80211PhyHeader_m.h"
#include "inet/physicallayer/ieee80211/packetlevel/Ieee80211Tag_m.h"
#include "inet/transportlayer/tcp_common/TcpHeader.h"

namespace inet {

Register_Protocol_Dissector(nullptr, DefaultDissector);
Register_Protocol_Dissector(&Protocol::ieee80211, Ieee80211Dissector);
Register_Protocol_Dissector(&Protocol::ieee8022, Ieee802LlcDissector);
Register_Protocol_Dissector(&Protocol::tcp, TcpDissector);

void DefaultDissector::dissect(Packet *packet, ICallback& callback) const
{
    const auto& header = packet->peekHeader();
    // TODO: use signal tag as opposed to protocol tag?! e.g. IEEE 802. 11 signal -> means 802.11 PHY header
    if (const auto& ethernetPhyHeader = dynamicPtrCast<const inet::EthernetPhyHeader>(header)) {
        packet->setHeaderPopOffset(packet->getHeaderPopOffset() + ethernetPhyHeader->getChunkLength());
        callback.startProtocolDataUnit(nullptr);
        callback.visitChunk(header, nullptr);
        callback.dissectPacket(packet, &Protocol::ethernet);
        callback.endProtocolDataUnit(nullptr);
    }
    else if (const auto& pppHeader = dynamicPtrCast<const inet::PppHeader>(header)) {
        callback.dissectPacket(packet, &Protocol::ppp);
    }
    else if (const auto& ieee80211PhyHeader = dynamicPtrCast<const inet::physicallayer::Ieee80211PhyHeader>(header)) {
        callback.startProtocolDataUnit(nullptr);
        packet->setHeaderPopOffset(packet->getHeaderPopOffset() + ieee80211PhyHeader->getChunkLength());
        callback.visitChunk(header, nullptr);
        const auto& trailer = packet->peekTrailer();
        // TODO: KLUDGE: padding length
        auto ieee80211PhyPadding = dynamicPtrCast<const BitCountChunk>(trailer);
        if (ieee80211PhyPadding != nullptr)
            packet->setTrailerPopOffset(packet->getTrailerPopOffset() - ieee80211PhyPadding->getChunkLength());
        callback.dissectPacket(packet, &Protocol::ieee80211);
        if (ieee80211PhyPadding != nullptr)
            callback.visitChunk(ieee80211PhyPadding, nullptr);
        callback.endProtocolDataUnit(nullptr);
    }
    else {
        callback.startProtocolDataUnit(nullptr);
        callback.visitChunk(packet->peekData(), nullptr);
        packet->setHeaderPopOffset(packet->getTrailerPopOffset());
        callback.endProtocolDataUnit(nullptr);
    }
}

void Ieee80211Dissector::dissectPhy(Packet *packet, ICallback& callback) const
{
    const auto& header = packet->popHeader<inet::physicallayer::Ieee80211PhyHeader>();
    callback.visitChunk(header, nullptr);
    const auto& trailer = packet->peekTrailer();
    // TODO: KLUDGE: padding length
    auto ieee80211PhyPadding = dynamicPtrCast<const BitCountChunk>(trailer);
    if (ieee80211PhyPadding != nullptr)
        packet->setTrailerPopOffset(packet->getTrailerPopOffset() - ieee80211PhyPadding->getChunkLength());
    dissectMac(packet, callback);
    if (ieee80211PhyPadding != nullptr)
        callback.visitChunk(ieee80211PhyPadding, nullptr);
}

void Ieee80211Dissector::dissectMac(Packet *packet, ICallback& callback) const
{
    const auto& header = packet->popHeader<inet::ieee80211::Ieee80211MacHeader>();
    const auto& trailer = packet->popTrailer<inet::ieee80211::Ieee80211MacTrailer>();
    callback.visitChunk(header, &Protocol::ieee80211);
    // TODO: fragmentation & aggregation
    if (auto dataHeader = dynamicPtrCast<const inet::ieee80211::Ieee80211DataHeader>(header)) {
        if (dataHeader->getMoreFragments() || dataHeader->getFragmentNumber() != 0)
            callback.dissectPacket(packet, nullptr);
        else if (dataHeader->getAMsduPresent()) {
            auto originalTrailerPopOffset = packet->getTrailerPopOffset();
            int paddingLength = 0;
            while (packet->getDataLength() > B(0)) {
                packet->setHeaderPopOffset(packet->getHeaderPopOffset() + B(paddingLength == 4 ? 0 : paddingLength));
                const auto& msduSubframeHeader = packet->popHeader<ieee80211::Ieee80211MsduSubframeHeader>();
                auto msduEndOffset = packet->getHeaderPopOffset() + B(msduSubframeHeader->getLength());
                packet->setTrailerPopOffset(msduEndOffset);
                callback.dissectPacket(packet, &Protocol::ieee8022);
                paddingLength = 4 - B(msduSubframeHeader->getChunkLength() + B(msduSubframeHeader->getLength())).get() % 4;
                packet->setTrailerPopOffset(originalTrailerPopOffset);
                packet->setHeaderPopOffset(msduEndOffset);
            }
        }
        else
            callback.dissectPacket(packet, &Protocol::ieee8022);
    }
    else if (dynamicPtrCast<const inet::ieee80211::Ieee80211MgmtHeader>(header))
        dissectMgmt(packet, callback);
    // TODO: else if (dynamicPtrCast<const inet::ieee80211::Ieee80211ControlFrame>(header))
    else
        ASSERT(packet->getDataLength() == b(0));
    callback.visitChunk(trailer, &Protocol::ieee80211);
}

void Ieee80211Dissector::dissectMgmt(Packet *packet, ICallback& callback) const
{
    callback.visitChunk(packet->peekData(), &Protocol::ieee80211);
}

void Ieee80211Dissector::dissect(Packet *packet, ICallback& callback) const
{
    callback.startProtocolDataUnit(&Protocol::ieee80211);
    auto subprotocol = packet->getTag<inet::physicallayer::Ieee80211PacketSubprotocolTag>()->getSubprotocol();
    if (subprotocol == inet::physicallayer::IEEE80211_SUBPROTOCOL_PHY)
        dissectPhy(packet, callback);
    else if (subprotocol == inet::physicallayer::IEEE80211_SUBPROTOCOL_MAC)
        dissectMac(packet, callback);
    else if (subprotocol == inet::physicallayer::IEEE80211_SUBPROTOCOL_MGMT)
        dissectMgmt(packet, callback);
    else
        throw cRuntimeError("Unknown subprotocol");
    callback.endProtocolDataUnit(&Protocol::ieee80211);
}

void Ieee802LlcDissector::dissect(Packet *packet, ICallback& callback) const
{
    const auto& header = packet->popHeader<inet::Ieee8022LlcHeader>();
    callback.startProtocolDataUnit(&Protocol::ieee8022);
    callback.visitChunk(header, &Protocol::ieee8022);
    auto protocol = Ieee8022Llc::getProtocol(header);
    callback.dissectPacket(packet, protocol);
    callback.endProtocolDataUnit(&Protocol::ieee8022);
}

void TcpDissector::dissect(Packet *packet, ICallback& callback) const
{
    const auto& header = packet->popHeader<tcp::TcpHeader>();
    callback.startProtocolDataUnit(&Protocol::tcp);
    callback.visitChunk(header, &Protocol::tcp);
    if (packet->getDataLength() != b(0))
        callback.dissectPacket(packet, nullptr);
    callback.endProtocolDataUnit(&Protocol::tcp);
}

} // namespace

